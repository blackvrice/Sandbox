#pragma once
// Vulkan 백엔드. docs/06-RENDERING.md 5.2, ADR-0032.
//
//   VkInstance 1.3 (+ 표면 확장, VK_EXT_debug_utils) → PhysicalDevice (--rhi-warp 면 CPU,
//   아니면 discrete → integrated → virtual → CPU) → VkDevice (dynamicRendering · synchronization2 ·
//   timelineSemaphore · hostQueryReset) → 그래픽 큐 하나.
//   큐 펜스 = 타임라인 세마포어 (D3D12 펜스와 같은 단조 값 공간). 스왑체인은 binary 세마포어(acquire · present)를
//   큐에 맡긴다 — 다음 제출이 acquire 를 기다리고, present 는 마지막 타임라인 값을 기다려 present 세마포어를 켜는
//   빈 제출. 메모리: 리소스마다 vkAllocateMemory 하나 (D3D12 의 committed 와 같은 단계 — VMA 는 [계획]).
//   디스크립터: BindGroup = VkDescriptorSet 하나 (풀 하나에서, FREE 플래그). 세트 레이아웃 · 파이프라인 레이아웃은
//   모양(layoutSignature)으로 캐시해 디바이스 수명 동안 공유 — 레이아웃 핸들을 지워도 파이프라인 · 그룹은 그대로
//   쓴다.

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/rhi/DeferredDestruction.hpp"
#include "render/rhi/HandlePool.hpp"
#include "render/rhi/RenderDevice.hpp"
#include "render/rhi/UploadRing.hpp"
#include "render/vulkan/VulkanCommon.hpp"

namespace sbx::rhi::vk {

// 지연 해제 항목: 해제 때 할 일 하나 (vkDestroy* · vkFree*)
class Garbage {
public:
    explicit Garbage(std::function<void()> release) : m_release(std::move(release)) {}
    Garbage(Garbage&& o) noexcept : m_release(std::move(o.m_release)) { o.m_release = nullptr; }
    Garbage& operator=(Garbage&&) = delete;
    Garbage(const Garbage&) = delete;
    ~Garbage() {
        if (m_release) {
            m_release();
        }
    }

private:
    std::function<void()> m_release;
};

struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    BufferDesc desc;
    std::byte* mapped = nullptr;
    bool internal = false; // 업로드 링 · 타임스탬프 — 사용자가 파괴하지 않는다
};

struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE; // 스왑체인 이미지는 없음
    VkImageView view2D = VK_NULL_HANDLE;    // 층 0 · 모든 밉 (렌더 타깃 · Texture2D)
    VkImageView viewArray = VK_NULL_HANDLE; // 모든 층 (Texture2DArray) — 처음 쓸 때 만든다
    TextureDesc desc;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    bool external = false; // 스왑체인 이미지
    // 스왑체인 이미지가 아직 한 번도 전이되지 않았다 — RHI 는 Present 에서 시작한다고 보지만 실제 레이아웃은 UNDEFINED
    bool neverTransitioned = false;
};

struct VulkanShader {
    ShaderStage stage = ShaderStage::Vertex;
    VkShaderModule module = VK_NULL_HANDLE;
    std::string entry;
    std::string name;
    const ShaderReflection* reflection = nullptr;
};

struct VulkanSampler {
    VkSampler sampler = VK_NULL_HANDLE;
    std::string name;
};

struct VulkanBindGroupLayout {
    BindGroupLayoutDesc desc;
    std::string signature;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE; // 캐시 소유
};

struct VulkanBindGroup {
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::string layoutSignature;
    u32 descriptors = 0;
    std::string name;
};

struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE; // 캐시 소유
    std::array<std::string, kMaxBindGroups> slotSignatures{};
    std::array<bool, kMaxBindGroups> slotUsed{};
    u32 pushBytes = 0;
    u32 vertexBufferCount = 0;
    std::string name;
};

class VulkanDevice;

// 디바이스가 먼저 사라질 때 네이티브 객체(커맨드 풀 · 스왑체인 · 표면)를 먼저 놓아야 하는 자식.
// 리스트 · 스왑체인은 만들 때 attach, 소멸 때 detach 한다. 디바이스 소멸자가 releaseNative 를 부르면 그 뒤로는
// 디바이스에 손대지 않는다 (D3D12 는 COM 참조가 디바이스를 살려 두지만 Vulkan 은 순서가 규칙이다).
class DeviceChild {
public:
    virtual void releaseNative() noexcept = 0;

protected:
    ~DeviceChild() = default;
};

class VulkanQueue final : public ICommandQueue {
public:
    explicit VulkanQueue(VulkanDevice& dev) : m_dev(dev) {}
    bool init(VkQueue queue, u32 family);
    void shutdown();

    FenceValue submit(std::span<ICommandList* const> lists) override;
    [[nodiscard]] FenceValue completedValue() const override;
    [[nodiscard]] FenceValue lastSubmittedValue() const override { return m_last; }
    void wait(FenceValue value) override;
    void waitIdle() override;

    // 스왑체인: 다음 제출이 이 binary 세마포어를 기다린다 (acquire)
    void addWait(VkSemaphore semaphore, VkPipelineStageFlags2 stage);
    // 지금까지 제출한 일이 끝나면 binary 세마포어를 켠다 (present 직전 — 명령 없는 제출). 실패면 false
    bool signalAfterLast(VkSemaphore semaphore);

    [[nodiscard]] VkQueue native() const noexcept { return m_queue; }
    [[nodiscard]] u32 family() const noexcept { return m_family; }

private:
    bool submitBatch(std::span<const VkCommandBufferSubmitInfo> buffers, VkSemaphore binarySignal, bool waitLast);

    VulkanDevice& m_dev;
    VkQueue m_queue = VK_NULL_HANDLE;
    u32 m_family = 0;
    VkSemaphore m_timeline = VK_NULL_HANDLE;
    FenceValue m_last = 0;
    std::vector<VkSemaphoreSubmitInfo> m_pendingWaits;
};

class VulkanDevice final : public IRenderDevice {
public:
    static Expected<std::unique_ptr<IRenderDevice>> create(const DeviceDesc& desc);
    explicit VulkanDevice(const DeviceDesc& desc);
    ~VulkanDevice() override;
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] const DeviceCaps& caps() const override { return m_caps; }
    [[nodiscard]] DeviceStats stats() const override;

    [[nodiscard]] RhiBuffer createBuffer(const BufferDesc& desc) override;
    [[nodiscard]] RhiTexture createTexture(const TextureDesc& desc) override;
    void destroy(RhiBuffer buffer) override;
    void destroy(RhiTexture texture) override;
    [[nodiscard]] bool alive(RhiBuffer buffer) const override { return m_buffers.get(buffer) != nullptr; }
    [[nodiscard]] bool alive(RhiTexture texture) const override { return m_textures.get(texture) != nullptr; }
    [[nodiscard]] const TextureDesc* textureDesc(RhiTexture texture) const override;

    [[nodiscard]] RhiShader createShader(const ShaderDesc& desc) override;
    [[nodiscard]] RhiSampler createSampler(const SamplerDesc& desc) override;
    [[nodiscard]] RhiBindGroupLayout createBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    [[nodiscard]] RhiBindGroup createBindGroup(const BindGroupDesc& desc) override;
    [[nodiscard]] RhiPipeline createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    void destroy(RhiShader shader) override;
    void destroy(RhiSampler sampler) override;
    void destroy(RhiBindGroupLayout layout) override;
    void destroy(RhiBindGroup group) override;
    void destroy(RhiPipeline pipeline) override;
    [[nodiscard]] bool alive(RhiPipeline pipeline) const override { return m_pipelines.get(pipeline) != nullptr; }
    [[nodiscard]] bool alive(RhiBindGroup group) const override { return m_groups.get(group) != nullptr; }

    [[nodiscard]] std::byte* map(RhiBuffer buffer) override;
    [[nodiscard]] UploadAllocation allocateUpload(u64 size, u64 alignment) override;

    [[nodiscard]] ICommandQueue& queue(QueueType type) override;
    [[nodiscard]] std::unique_ptr<ICommandList> createCommandList(QueueType type) override;
    [[nodiscard]] Expected<std::unique_ptr<ISwapChain>> createSwapChain(const platform::NativeWindowHandle& window,
                                                                        const SwapChainDesc& desc) override;

    void beginFrame() override;
    void endFrame() override;
    [[nodiscard]] u32 framesInFlight() const override { return m_framesInFlight; }
    [[nodiscard]] u32 frameIndex() const override { return m_frameIndex; }
    [[nodiscard]] u64 frameNumber() const override { return m_frameNumber; }
    [[nodiscard]] const TimestampReadback& completedTimestamps() const override { return m_timestamps; }
    void waitIdle() override;

    // ---- 백엔드 내부 (커맨드 리스트 · 스왑체인) ----
    [[nodiscard]] VkInstance instance() const noexcept { return m_instance; }
    [[nodiscard]] VkPhysicalDevice physical() const noexcept { return m_physical; }
    [[nodiscard]] VkDevice device() const noexcept { return m_device; }
    [[nodiscard]] VulkanQueue& graphicsQueue() noexcept { return m_queue; }
    [[nodiscard]] bool debugUtils() const noexcept { return m_debugUtils; }
    [[nodiscard]] VulkanBuffer* buffer(RhiBuffer h) noexcept { return m_buffers.get(h); }
    [[nodiscard]] VulkanTexture* texture(RhiTexture h) noexcept { return m_textures.get(h); }
    [[nodiscard]] VulkanPipeline* pipeline(RhiPipeline h) noexcept { return m_pipelines.get(h); }
    [[nodiscard]] VulkanBindGroup* bindGroup(RhiBindGroup h) noexcept { return m_groups.get(h); }
    [[nodiscard]] VkQueryPool timestampPool() const noexcept { return m_tsPool; }
    [[nodiscard]] VkBuffer timestampReadback() noexcept;
    void noteTimestampsResolved(u32 count) noexcept;
    RhiTexture registerSwapchainImage(VkImage image, const TextureDesc& desc);
    void releaseSwapchainImageNow(RhiTexture texture); // GPU 가 쉬는 중에만 (스왑체인 재생성 · 소멸)
    void setName(VkObjectType type, u64 handle, std::string_view name);
    void reportFailure(std::string_view what, VkResult r);
    void countValidationError() noexcept { ++m_debugErrors; }
    void onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
                        const char* text);
    // Texture2DArray 뷰 (처음 쓸 때 만든다)
    [[nodiscard]] VkImageView arrayView(VulkanTexture& t);
    void attach(DeviceChild* child);
    void detach(DeviceChild* child) noexcept;

private:
    Expected<void> init();
    Expected<void> createInstance();
    Expected<void> pickPhysicalDevice();
    Expected<void> createLogicalDevice();
    [[nodiscard]] u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags required,
                                     VkMemoryPropertyFlags preferred) const;
    VkDeviceMemory allocate(const VkMemoryRequirements& req, MemoryType type, std::string_view name);
    VkDescriptorSetLayout setLayoutFor(const BindGroupLayoutDesc& desc, const std::string& signature);
    VkPipelineLayout pipelineLayoutFor(const std::array<const VulkanBindGroupLayout*, kMaxBindGroups>& layouts,
                                       u32 pushBytes);
    void validationError(std::string_view what);
    FenceValue nextFence() const noexcept { return m_queue.lastSubmittedValue() + 1; }

    DeviceDesc m_desc;
    DeviceCaps m_caps;
    u32 m_framesInFlight = 2;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_props{};
    VkPhysicalDeviceMemoryProperties m_memProps{};
    VkDevice m_device = VK_NULL_HANDLE;
    u32 m_queueFamily = 0;
    VulkanQueue m_queue{*this};
    bool m_debugUtils = false;
    bool m_surfaces = false; // 인스턴스에 표면 확장을 켰다 → 디바이스에 VK_KHR_swapchain
    bool m_validation = false;
    bool m_fillModeNonSolid = false;

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_emptySetLayout = VK_NULL_HANDLE; // 파이프라인 레이아웃의 빈 슬롯
    std::unordered_map<std::string, VkDescriptorSetLayout> m_setLayouts;
    std::unordered_map<std::string, VkPipelineLayout> m_pipelineLayouts;

    HandlePool<BufferTag, VulkanBuffer> m_buffers;
    HandlePool<TextureTag, VulkanTexture> m_textures;
    HandlePool<ShaderTag, VulkanShader> m_shaders;
    HandlePool<SamplerTag, VulkanSampler> m_samplers;
    HandlePool<BindGroupLayoutTag, VulkanBindGroupLayout> m_layouts;
    HandlePool<BindGroupTag, VulkanBindGroup> m_groups;
    HandlePool<PipelineTag, VulkanPipeline> m_pipelines;
    DeferredDestructionQueue<Garbage> m_garbage;
    u64 m_descriptorsUsed = 0;

    RhiBuffer m_ringBuffer;
    std::byte* m_ringCpu = nullptr;
    UploadRing m_ring{0};

    // 8B 타임스탬프: 프레임 슬롯 s 의 칸은 풀 [s × kMaxTimestampsPerFrame, …), 읽기 버퍼도 같은 배치
    VkQueryPool m_tsPool = VK_NULL_HANDLE;
    RhiBuffer m_tsReadback;
    struct ResolvedTimestamps {
        u64 frameNumber = 0;
        u32 count = 0;
    };
    std::vector<ResolvedTimestamps> m_tsResolved;
    TimestampReadback m_timestamps;

    std::vector<FenceValue> m_frameFences;
    u32 m_frameIndex = 0;
    u64 m_frameNumber = 0;

    std::vector<DeviceChild*> m_children;

    u64 m_released = 0;
    u64 m_debugWarnings = 0;
    u64 m_debugErrors = 0;
    u64 m_ringDeferred = 0;
    bool m_lostReported = false;
};

// VulkanCommandList.cpp
[[nodiscard]] std::unique_ptr<ICommandList> makeCommandList(VulkanDevice& dev);
[[nodiscard]] VkCommandBuffer nativeList(ICommandList& list) noexcept; // 기록이 끝난(end) 리스트
void noteSubmitted(ICommandList& list, FenceValue value) noexcept;     // 큐가 제출 뒤 알린다 (풀 재사용 시점)
// VulkanSwapChain.cpp
[[nodiscard]] Expected<std::unique_ptr<ISwapChain>>
makeSwapChain(VulkanDevice& dev, const platform::NativeWindowHandle& window, const SwapChainDesc& desc);
// 인스턴스에 켤 표면 확장 (이 빌드 · 이 로더가 아는 것)
[[nodiscard]] std::vector<const char*> surfaceExtensions(std::span<const VkExtensionProperties> available);

} // namespace sbx::rhi::vk
