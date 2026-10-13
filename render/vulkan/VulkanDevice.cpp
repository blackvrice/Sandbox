// Vulkan 인스턴스 · 디바이스 · 큐 · 리소스 · 프레임. docs/06-RENDERING.md 4 · 5.2장, ADR-0032.
#include "render/vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

#include "foundation/log/Log.hpp"
#include "render/rhi/Backends.hpp"

namespace sbx::rhi::vk {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT type,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    static_cast<VulkanDevice*>(user)->onDebugMessage(
        severity, type, data != nullptr && data->pMessage != nullptr ? data->pMessage : "");
    return VK_FALSE;
}

bool hasExtension(std::span<const VkExtensionProperties> list, const char* name) {
    return std::ranges::any_of(list,
                               [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

} // namespace

// ---------------------------------------------------------------------------
// 큐 (타임라인 세마포어)
// ---------------------------------------------------------------------------

bool VulkanQueue::init(VkQueue queue, u32 family) {
    m_queue = queue;
    m_family = family;
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = 0;
    VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    ci.pNext = &type;
    if (const VkResult r = vkCreateSemaphore(m_dev.device(), &ci, nullptr, &m_timeline); r != VK_SUCCESS) {
        m_dev.reportFailure("타임라인 세마포어", r);
        return false;
    }
    m_dev.setName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<u64>(m_timeline), "graphics timeline");
    return true;
}

void VulkanQueue::shutdown() {
    if (m_timeline != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_dev.device(), m_timeline, nullptr);
        m_timeline = VK_NULL_HANDLE;
    }
}

bool VulkanQueue::submitBatch(std::span<const VkCommandBufferSubmitInfo> buffers, VkSemaphore binarySignal,
                              bool waitLast) {
    std::vector<VkSemaphoreSubmitInfo> waits = std::move(m_pendingWaits);
    m_pendingWaits.clear();
    if (waitLast && m_last > 0) {
        VkSemaphoreSubmitInfo w{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        w.semaphore = m_timeline;
        w.value = m_last;
        w.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waits.push_back(w);
    }
    std::array<VkSemaphoreSubmitInfo, 2> signals{};
    u32 signalCount = 0;
    if (binarySignal != VK_NULL_HANDLE) {
        signals[signalCount] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signals[signalCount].semaphore = binarySignal;
        signals[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ++signalCount;
    } else {
        signals[signalCount] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signals[signalCount].semaphore = m_timeline;
        signals[signalCount].value = m_last + 1;
        signals[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ++signalCount;
    }
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = static_cast<u32>(waits.size());
    si.pWaitSemaphoreInfos = waits.data();
    si.commandBufferInfoCount = static_cast<u32>(buffers.size());
    si.pCommandBufferInfos = buffers.data();
    si.signalSemaphoreInfoCount = signalCount;
    si.pSignalSemaphoreInfos = signals.data();
    if (const VkResult r = vkQueueSubmit2(m_queue, 1, &si, VK_NULL_HANDLE); r != VK_SUCCESS) {
        m_dev.reportFailure("vkQueueSubmit2", r);
        return false;
    }
    return true;
}

FenceValue VulkanQueue::submit(std::span<ICommandList* const> lists) {
    std::vector<VkCommandBufferSubmitInfo> buffers;
    buffers.reserve(lists.size());
    for (ICommandList* l : lists) {
        if (l == nullptr) {
            continue;
        }
        VkCommandBufferSubmitInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        b.commandBuffer = nativeList(*l);
        if (b.commandBuffer != VK_NULL_HANDLE) {
            buffers.push_back(b);
        }
    }
    // 제출이 실패해도 값은 올린다 — 기다리는 쪽이 영원히 걸리지 않게 아래 wait 가 시간 초과로 끝난다
    (void)submitBatch(buffers, VK_NULL_HANDLE, false);
    ++m_last;
    for (ICommandList* l : lists) {
        if (l != nullptr) {
            noteSubmitted(*l, m_last);
        }
    }
    return m_last;
}

FenceValue VulkanQueue::completedValue() const {
    if (m_timeline == VK_NULL_HANDLE) {
        return 0;
    }
    u64 v = 0;
    if (vkGetSemaphoreCounterValue(m_dev.device(), m_timeline, &v) != VK_SUCCESS) {
        return 0;
    }
    return v;
}

void VulkanQueue::wait(FenceValue value) {
    if (m_timeline == VK_NULL_HANDLE || completedValue() >= value) {
        return;
    }
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &m_timeline;
    wi.pValues = &value;
    // GPU 가 멈추면 무한정 기다리지 않는다: 5초마다 확인, 30초면 포기 (D3D12 백엔드와 같다)
    for (int seconds = 5;; seconds += 5) {
        const VkResult r = vkWaitSemaphores(m_dev.device(), &wi, 5'000'000'000ull);
        if (r == VK_SUCCESS) {
            return;
        }
        if (r != VK_TIMEOUT) {
            m_dev.reportFailure("vkWaitSemaphores", r);
            return;
        }
        log::warn("render", "GPU 타임라인 {} 를 {}초째 기다립니다 (완료 {})", value, seconds, completedValue());
        if (seconds >= 30) {
            log::error("render", "GPU 가 30초 넘게 응답하지 않아 기다림을 포기합니다");
            return;
        }
    }
}

void VulkanQueue::waitIdle() {
    (void)submitBatch({}, VK_NULL_HANDLE, false);
    ++m_last;
    wait(m_last);
}

void VulkanQueue::addWait(VkSemaphore semaphore, VkPipelineStageFlags2 stage) {
    VkSemaphoreSubmitInfo w{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    w.semaphore = semaphore;
    w.stageMask = stage;
    m_pendingWaits.push_back(w);
}

bool VulkanQueue::signalAfterLast(VkSemaphore semaphore) {
    return submitBatch({}, semaphore, true);
}

// ---------------------------------------------------------------------------
// 디바이스 생성
// ---------------------------------------------------------------------------

VulkanDevice::VulkanDevice(const DeviceDesc& desc) : m_desc(desc) {
    m_framesInFlight = desc.framesInFlight < 2 ? 2 : (desc.framesInFlight > 3 ? 3 : desc.framesInFlight);
    m_frameFences.assign(m_framesInFlight, 0);
}

Expected<std::unique_ptr<IRenderDevice>> VulkanDevice::create(const DeviceDesc& desc) {
    auto dev = std::make_unique<VulkanDevice>(desc);
    if (auto r = dev->init(); !r) {
        return std::unexpected(r.error());
    }
    return std::unique_ptr<IRenderDevice>(std::move(dev));
}

Expected<void> VulkanDevice::createInstance() {
    // 로더는 실행 중에 연다 (volk) — 없으면 Vulkan 만 못 쓰고 프로그램은 뜬다
    static const VkResult loader = volkInitialize();
    if (loader != VK_SUCCESS) {
        return makeError(ErrorCode::Unsupported,
                         "Vulkan 로더를 찾지 못했습니다 (Windows: vulkan-1.dll — GPU 드라이버, Linux: libvulkan.so.1 — "
                         "배포판의 libvulkan1 · mesa-vulkan-drivers)");
    }
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
        return makeError(ErrorCode::Unsupported, "Vulkan 1.3 로더가 필요합니다 (드라이버 · 로더를 갱신하십시오)");
    }

    u32 n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());

    std::vector<const char*> enabledExts = surfaceExtensions(exts);
    m_surfaces = !enabledExts.empty();
    m_debugUtils = hasExtension(exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (m_debugUtils) {
        enabledExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    std::vector<const char*> enabledLayers;
    const bool wantValidation = m_desc.debugLayer || m_desc.gpuValidation;
    if (wantValidation) {
        const bool present = std::ranges::any_of(
            layers, [](const VkLayerProperties& l) { return std::strcmp(l.layerName, kValidationLayer) == 0; });
        if (present) {
            enabledLayers.push_back(kValidationLayer);
            m_validation = true;
        } else {
            log::warn("render",
                      "Vulkan Validation Layer({})가 없습니다 — Vulkan SDK 또는 배포판의 "
                      "vulkan-validationlayers 를 설치하십시오. 검증 없이 계속합니다",
                      kValidationLayer);
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Sandbox";
    app.pEngineName = "Sandbox";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<u32>(enabledExts.size());
    ci.ppEnabledExtensionNames = enabledExts.data();
    ci.enabledLayerCount = static_cast<u32>(enabledLayers.size());
    ci.ppEnabledLayerNames = enabledLayers.data();
    // --rhi-gbv: 동기화 검증까지 (D3D12 의 GPU-Based Validation 자리 — 06 13장 --rhi-sync-validation)
    const VkValidationFeatureEnableEXT syncValidation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    if (m_validation && m_desc.gpuValidation) {
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = &syncValidation;
        ci.pNext = &features;
    }
    if (const VkResult r = vkCreateInstance(&ci, nullptr, &m_instance); r != VK_SUCCESS) {
        return makeError(ErrorCode::Unsupported, "vkCreateInstance 실패: " + resultText(r));
    }
    volkLoadInstanceOnly(m_instance);

    if (m_validation && m_debugUtils) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = debugCallback;
        mi.pUserData = this;
        if (vkCreateDebugUtilsMessengerEXT(m_instance, &mi, nullptr, &m_messenger) != VK_SUCCESS) {
            m_messenger = VK_NULL_HANDLE;
        }
    }
    return {};
}

Expected<void> VulkanDevice::pickPhysicalDevice() {
    u32 n = 0;
    vkEnumeratePhysicalDevices(m_instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devices(n);
    vkEnumeratePhysicalDevices(m_instance, &n, devices.data());
    int bestScore = -1;
    std::string rejected;
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);
        const auto reject = [&](std::string_view why) {
            rejected += std::format("{}{} ({})", rejected.empty() ? "" : ", ", props.deviceName, why);
        };
        if (props.apiVersion < VK_API_VERSION_1_3) {
            reject("Vulkan 1.3 아님");
            continue;
        }
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        f12.pNext = &f13;
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(pd, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2 || !f12.timelineSemaphore || !f12.hostQueryReset) {
            reject("필수 기능 없음");
            continue;
        }
        u32 qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> families(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, families.data());
        const auto it = std::ranges::find_if(
            families, [](const VkQueueFamilyProperties& q) { return (q.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0; });
        if (it == families.end()) {
            reject("그래픽 큐 없음");
            continue;
        }
        // --rhi-warp 면 CPU(소프트웨어 — lavapipe · SwiftShader) 를 먼저 (D3D12 WARP 자리). 아니면 하드웨어 먼저
        int score = 0;
        switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            score = 4;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            score = 3;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            score = 2;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            score = m_desc.warp ? 10 : 1;
            break;
        default:
            score = 0;
            break;
        }
        if (score > bestScore) {
            bestScore = score;
            m_physical = pd;
            m_props = props;
            m_queueFamily = static_cast<u32>(it - families.begin());
            m_fillModeNonSolid = f2.features.fillModeNonSolid != VK_FALSE;
        }
    }
    if (m_physical == VK_NULL_HANDLE) {
        return makeError(ErrorCode::Unsupported,
                         std::format("Vulkan 1.3 (dynamicRendering · synchronization2 · timelineSemaphore · "
                                     "hostQueryReset) 디바이스가 없습니다{}{}",
                                     rejected.empty() ? "" : " — ", rejected));
    }
    if (m_desc.warp && m_props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
        log::warn("render", "--rhi-warp: CPU Vulkan 디바이스(lavapipe 등)가 없어 {} 를 씁니다", m_props.deviceName);
    }
    vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memProps);
    return {};
}

Expected<void> VulkanDevice::createLogicalDevice() {
    u32 n = 0;
    vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &n, exts.data());
    std::vector<const char*> enabled;
    // 스왑체인은 표면이 있는 인스턴스에서만 (Linux 13A 는 표면 없음 — 헤드리스)
    if (m_surfaces && hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    const f32 priority = 1.f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = m_queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.timelineSemaphore = VK_TRUE;
    f12.hostQueryReset = VK_TRUE;
    f12.pNext = &f13;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.features.fillModeNonSolid = m_fillModeNonSolid ? VK_TRUE : VK_FALSE;
    f2.pNext = &f12;

    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &f2;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = static_cast<u32>(enabled.size());
    ci.ppEnabledExtensionNames = enabled.data();
    if (const VkResult r = vkCreateDevice(m_physical, &ci, nullptr, &m_device); r != VK_SUCCESS) {
        return makeError(ErrorCode::Unsupported, "vkCreateDevice 실패: " + resultText(r));
    }
    volkLoadDevice(m_device);
    VkQueue q = VK_NULL_HANDLE;
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &q);
    if (!m_queue.init(q, m_queueFamily)) {
        return makeError(ErrorCode::Unsupported, "Vulkan 큐를 만들 수 없습니다");
    }
    setName(VK_OBJECT_TYPE_DEVICE, reinterpret_cast<u64>(m_device), "Sandbox Vulkan device");
    return {};
}

Expected<void> VulkanDevice::init() {
    if (auto r = createInstance(); !r) {
        return r;
    }
    if (auto r = pickPhysicalDevice(); !r) {
        return r;
    }
    if (auto r = createLogicalDevice(); !r) {
        return r;
    }

    const VkPhysicalDeviceLimits& lim = m_props.limits;
    m_caps.backend = BackendType::Vulkan;
    m_caps.adapterName = m_props.deviceName;
    m_caps.softwareAdapter = m_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
    m_caps.featureLevel = m_props.apiVersion; // Vulkan: VK_MAKE_API_VERSION 값
    for (u32 i = 0; i < m_memProps.memoryHeapCount; ++i) {
        if ((m_memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            m_caps.dedicatedVideoMemory += m_memProps.memoryHeaps[i].size;
        }
    }
    m_caps.compute = true;
    m_caps.maxTextureSize = lim.maxImageDimension2D;
    m_caps.maxPushConstantBytes = std::min<u32>(kMaxPushConstantBytes, lim.maxPushConstantsSize);
    // 상수 버퍼 오프셋 · 복사 배치는 D3D12 와 같은 값 이상으로 — 상위 코드가 백엔드마다 다른 배치를 만들지 않게.
    // Vulkan 의 실제 요구(minUniformBufferOffsetAlignment · optimalBufferCopy*)는 모두 이 값 이하다
    m_caps.constantBufferAlignment = static_cast<u32>(std::max<VkDeviceSize>(256, lim.minUniformBufferOffsetAlignment));
    m_caps.textureCopyRowAlignment =
        static_cast<u32>(std::max<VkDeviceSize>(256, lim.optimalBufferCopyRowPitchAlignment));
    m_caps.textureCopyOffsetAlignment =
        static_cast<u32>(std::max<VkDeviceSize>(512, lim.optimalBufferCopyOffsetAlignment));
    m_caps.tearing = false; // 스왑체인 present mode 로 정한다 (IMMEDIATE · MAILBOX)

    // 디스크립터 풀 하나 — BindGroup 마다 세트 하나 (FREE 플래그: 그룹을 지우면 세트를 돌려준다)
    {
        const std::array<VkDescriptorPoolSize, 5> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192},
                                                         {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8192},
                                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8192},
                                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
                                                         {VK_DESCRIPTOR_TYPE_SAMPLER, 2048}}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets = 8192;
        pi.poolSizeCount = static_cast<u32>(sizes.size());
        pi.pPoolSizes = sizes.data();
        if (const VkResult r = vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool); r != VK_SUCCESS) {
            return makeError(ErrorCode::Unsupported, "디스크립터 풀: " + resultText(r));
        }
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        if (const VkResult r = vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_emptySetLayout);
            r != VK_SUCCESS) {
            return makeError(ErrorCode::Unsupported, "빈 세트 레이아웃: " + resultText(r));
        }
    }

    // 업로드 링 (06 4.2)
    m_ring = UploadRing(m_desc.uploadRingBytes);
    m_ringBuffer =
        createBuffer({m_desc.uploadRingBytes,
                      BufferUsage::CopySrc | BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Constant,
                      MemoryType::Upload, "upload ring"});
    if (VulkanBuffer* rb = m_buffers.get(m_ringBuffer); rb != nullptr) {
        rb->internal = true;
        m_ringCpu = rb->mapped;
    } else {
        return makeError(ErrorCode::Unsupported, "업로드 링 버퍼를 만들 수 없습니다");
    }

    // 8B 타임스탬프: 프레임 슬롯마다 kMaxTimestampsPerFrame 칸. 슬롯은 beginFrame 이 호스트에서 비운다 (hostQueryReset)
    {
        u32 qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> families(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical, &qn, families.data());
        const bool supported = families[m_queueFamily].timestampValidBits > 0 && lim.timestampPeriod > 0.f;
        m_tsResolved.assign(m_framesInFlight, {});
        if (supported) {
            VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qi.queryCount = m_framesInFlight * kMaxTimestampsPerFrame;
            if (vkCreateQueryPool(m_device, &qi, nullptr, &m_tsPool) == VK_SUCCESS) {
                vkResetQueryPool(m_device, m_tsPool, 0, qi.queryCount);
                m_tsReadback = createBuffer({u64{qi.queryCount} * sizeof(u64), BufferUsage::CopyDst,
                                             MemoryType::Readback, "timestamp readback"});
                if (VulkanBuffer* b = m_buffers.get(m_tsReadback); b != nullptr) {
                    b->internal = true;
                    m_timestamps.frequency =
                        static_cast<u64>(std::llround(1e9 / static_cast<f64>(lim.timestampPeriod)));
                }
            }
        }
        m_caps.timestampQueries = m_tsPool != VK_NULL_HANDLE && m_tsReadback.valid() && m_timestamps.frequency != 0;
        if (!m_caps.timestampQueries) {
            log::warn("render", "GPU 타임스탬프를 쓸 수 없습니다 — 패스별 GPU 시간을 재지 않는다");
        }
    }

    log::info("render", "Vulkan {}.{}.{}: {}{} · VRAM {} MB · frames in flight {} · Validation {}{}",
              VK_API_VERSION_MAJOR(m_props.apiVersion), VK_API_VERSION_MINOR(m_props.apiVersion),
              VK_API_VERSION_PATCH(m_props.apiVersion), m_caps.adapterName,
              m_caps.softwareAdapter ? " (소프트웨어)" : "", m_caps.dedicatedVideoMemory >> 20, m_framesInFlight,
              m_validation ? "켬" : "끔", m_validation && m_desc.gpuValidation ? " + 동기화 검증" : "");
    return {};
}

VulkanDevice::~VulkanDevice() {
    if (m_device == VK_NULL_HANDLE) {
        if (m_messenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
        }
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
        }
        return;
    }
    waitIdle();
    if (!m_children.empty()) {
        log::warn("render", "커맨드 리스트 · 스왑체인 {} 개가 디바이스보다 오래 산다 — 먼저 파괴하십시오",
                  m_children.size());
        for (DeviceChild* c : std::exchange(m_children, {})) {
            c->releaseNative();
        }
    }
    usize leakedBuffers = 0;
    usize leakedTextures = 0;
    m_buffers.forEach([&](RhiBuffer, VulkanBuffer& b) {
        if (!b.internal) {
            ++leakedBuffers;
            log::warn("render", "해제되지 않은 버퍼 '{}' ({} 바이트)", b.desc.debugName, b.desc.size);
        }
        vkDestroyBuffer(m_device, b.buffer, nullptr);
        vkFreeMemory(m_device, b.memory, nullptr);
    });
    m_textures.forEach([&](RhiTexture, VulkanTexture& t) {
        if (t.external) {
            log::warn("render", "스왑체인이 디바이스보다 오래 산다 — 스왑체인을 먼저 파괴하십시오");
            return;
        }
        ++leakedTextures;
        log::warn("render", "해제되지 않은 텍스처 '{}' ({}×{})", t.desc.debugName, t.desc.width, t.desc.height);
        vkDestroyImageView(m_device, t.view2D, nullptr);
        vkDestroyImageView(m_device, t.viewArray, nullptr);
        vkDestroyImage(m_device, t.image, nullptr);
        vkFreeMemory(m_device, t.memory, nullptr);
    });
    const usize leakedOther =
        m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
    m_pipelines.forEach([&](RhiPipeline, VulkanPipeline& p) {
        log::warn("render", "해제되지 않은 파이프라인 '{}'", p.name);
        vkDestroyPipeline(m_device, p.pipeline, nullptr);
    });
    m_groups.forEach(
        [&](RhiBindGroup, VulkanBindGroup& g) { log::warn("render", "해제되지 않은 바인드 그룹 '{}'", g.name); });
    m_shaders.forEach([&](RhiShader, VulkanShader& s) { vkDestroyShaderModule(m_device, s.module, nullptr); });
    m_samplers.forEach([&](RhiSampler, VulkanSampler& s) { vkDestroySampler(m_device, s.sampler, nullptr); });
    if (leakedOther != 0) {
        log::warn("render", "종료 시 남은 셰이더·샘플러·레이아웃·바인드 그룹·파이프라인 {}", leakedOther);
    }
    if (leakedBuffers + leakedTextures != 0) {
        log::warn("render", "종료 시 남은 GPU 리소스: 버퍼 {} · 텍스처 {} (destroy 를 부르지 않았다)", leakedBuffers,
                  leakedTextures);
    }
    m_garbage.clear(); // 남은 지연 해제 (GPU 는 쉬고 있다)
    for (auto& [key, layout] : m_pipelineLayouts) {
        vkDestroyPipelineLayout(m_device, layout, nullptr);
    }
    for (auto& [key, layout] : m_setLayouts) {
        vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
    }
    vkDestroyDescriptorSetLayout(m_device, m_emptySetLayout, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); // 남은 세트도 함께
    if (m_tsPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_tsPool, nullptr);
    }
    m_queue.shutdown();
    vkDestroyDevice(m_device, nullptr);
    if (m_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    }
    vkDestroyInstance(m_instance, nullptr);
}

// ---------------------------------------------------------------------------
// 진단
// ---------------------------------------------------------------------------

void VulkanDevice::onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
                                  const char* text) {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        ++m_debugErrors;
        log::error("vulkan", "{}", text);
    } else if ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0) {
        ++m_debugWarnings;
        log::warn("vulkan", "{}", text);
    } else {
        // 성능 안내 · 로더 일반 메시지는 세지 않는다 (D3D12 의 정보성 메시지와 같은 취급)
        log::debug("vulkan", "{}", text);
    }
}

void VulkanDevice::setName(VkObjectType type, u64 handle, std::string_view name) {
    if (!m_debugUtils || handle == 0 || name.empty() || m_device == VK_NULL_HANDLE) {
        return;
    }
    const std::string n(name);
    VkDebugUtilsObjectNameInfoEXT ni{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    ni.objectType = type;
    ni.objectHandle = handle;
    ni.pObjectName = n.c_str();
    vkSetDebugUtilsObjectNameEXT(m_device, &ni);
}

void VulkanDevice::reportFailure(std::string_view what, VkResult r) {
    ++m_debugErrors;
    log::error("render", "{} 실패: {}", what, resultText(r));
    if (r == VK_ERROR_DEVICE_LOST && !m_lostReported) {
        m_lostReported = true;
        log::error("render",
                   "Vulkan 디바이스를 잃었습니다 (드라이버 재시작 · 잘못된 명령) — --rhi-debug 로 다시 실행해 "
                   "Validation 메시지를 확인하십시오");
    }
}

void VulkanDevice::validationError(std::string_view what) {
    ++m_debugErrors;
    log::error("render", "{}", what);
}

DeviceStats VulkanDevice::stats() const {
    DeviceStats s;
    m_buffers.forEach([&](RhiBuffer, const VulkanBuffer& b) { s.liveBuffers += b.internal ? 0 : 1; });
    m_textures.forEach([&](RhiTexture, const VulkanTexture& t) { s.liveTextures += t.external ? 0 : 1; });
    s.livePipelines = m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
    s.descriptorsUsed = m_descriptorsUsed;
    s.pendingDestructions = m_garbage.size();
    s.releasedObjects = m_released;
    s.debugWarnings = m_debugWarnings;
    s.debugErrors = m_debugErrors;
    s.uploadRingUsed = m_ring.used();
    s.uploadRingDeferred = m_ringDeferred;
    s.frameNumber = m_frameNumber;
    s.debugLayerActive = m_validation;
    return s;
}

// ---------------------------------------------------------------------------
// 메모리 · 버퍼 · 텍스처
// ---------------------------------------------------------------------------

u32 VulkanDevice::findMemoryType(u32 typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const {
    for (const VkMemoryPropertyFlags want : {required | preferred, required}) {
        for (u32 i = 0; i < m_memProps.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) != 0 && (m_memProps.memoryTypes[i].propertyFlags & want) == want) {
                return i;
            }
        }
    }
    return ~0u;
}

VkDeviceMemory VulkanDevice::allocate(const VkMemoryRequirements& req, MemoryType type, std::string_view name) {
    VkMemoryPropertyFlags required = 0;
    VkMemoryPropertyFlags preferred = 0;
    switch (type) {
    case MemoryType::GpuOnly:
        preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case MemoryType::Upload:
        required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case MemoryType::Readback:
        required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }
    const u32 index = findMemoryType(req.memoryTypeBits, required, preferred);
    if (index == ~0u) {
        validationError(std::format("'{}': 맞는 메모리 종류가 없다", name));
        return VK_NULL_HANDLE;
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = index;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (const VkResult r = vkAllocateMemory(m_device, &ai, nullptr, &mem); r != VK_SUCCESS) {
        reportFailure(std::format("vkAllocateMemory '{}' ({} 바이트)", name, req.size), r);
        return VK_NULL_HANDLE;
    }
    return mem;
}

RhiBuffer VulkanDevice::createBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        validationError(std::format("createBuffer '{}': 크기 0", desc.debugName));
        return {};
    }
    if (desc.memory != MemoryType::GpuOnly && hasFlag(desc.usage, BufferUsage::Storage)) {
        validationError(std::format("createBuffer '{}': Storage 는 GpuOnly 메모리만", desc.debugName));
        return {};
    }
    // 복사 방향은 언제나 허용 (D3D12 는 버퍼 용도를 묻지 않는다 — 상위 코드가 백엔드마다 용도를 맞추지 않게)
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(desc.usage, BufferUsage::Vertex)) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (hasFlag(desc.usage, BufferUsage::Index)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (hasFlag(desc.usage, BufferUsage::Constant)) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    // StructuredBuffer(읽기)는 D3D12 SRV 처럼 어떤 버퍼든 묶을 수 있어야 한다 (Vulkan: STORAGE_BUFFER 디스크립터)
    usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = desc.size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VulkanBuffer b;
    b.desc = desc;
    if (const VkResult r = vkCreateBuffer(m_device, &bi, nullptr, &b.buffer); r != VK_SUCCESS) {
        reportFailure(std::format("createBuffer '{}' ({} 바이트)", desc.debugName, desc.size), r);
        return {};
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(m_device, b.buffer, &req);
    b.memory = allocate(req, desc.memory, desc.debugName);
    if (b.memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, b.buffer, nullptr);
        return {};
    }
    vkBindBufferMemory(m_device, b.buffer, b.memory, 0);
    if (desc.memory != MemoryType::GpuOnly) {
        void* p = nullptr;
        if (const VkResult r = vkMapMemory(m_device, b.memory, 0, VK_WHOLE_SIZE, 0, &p); r != VK_SUCCESS) {
            reportFailure("vkMapMemory", r);
            vkDestroyBuffer(m_device, b.buffer, nullptr);
            vkFreeMemory(m_device, b.memory, nullptr);
            return {};
        }
        b.mapped = static_cast<std::byte*>(p);
    }
    setName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<u64>(b.buffer), desc.debugName);
    return m_buffers.insert(std::move(b));
}

RhiTexture VulkanDevice::createTexture(const TextureDesc& desc) {
    const VkFormat fmt = toVk(desc.format);
    if (fmt == VK_FORMAT_UNDEFINED || desc.width == 0 || desc.height == 0 || desc.mips == 0 || desc.layers == 0) {
        validationError(std::format("createTexture '{}': 잘못된 Desc ({}×{}, {})", desc.debugName, desc.width,
                                    desc.height, formatInfo(desc.format).name));
        return {};
    }
    const bool depth = formatInfo(desc.format).depth;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(desc.usage, TextureUsage::Sampled)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (hasFlag(desc.usage, TextureUsage::RenderTarget)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (hasFlag(desc.usage, TextureUsage::DepthStencil)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (hasFlag(desc.usage, TextureUsage::Storage)) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {desc.width, desc.height, 1};
    ii.mipLevels = desc.mips;
    ii.arrayLayers = desc.layers;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VulkanTexture t;
    t.desc = desc;
    t.format = fmt;
    t.aspect = depth ? (formatInfo(desc.format).stencil ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                        : VK_IMAGE_ASPECT_DEPTH_BIT)
                     : VK_IMAGE_ASPECT_COLOR_BIT;
    if (const VkResult r = vkCreateImage(m_device, &ii, nullptr, &t.image); r != VK_SUCCESS) {
        reportFailure(std::format("createTexture '{}' ({}×{})", desc.debugName, desc.width, desc.height), r);
        return {};
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(m_device, t.image, &req);
    t.memory = allocate(req, MemoryType::GpuOnly, desc.debugName);
    if (t.memory == VK_NULL_HANDLE) {
        vkDestroyImage(m_device, t.image, nullptr);
        return {};
    }
    vkBindImageMemory(m_device, t.image, t.memory, 0);
    // 뷰는 셰이더 · 첨부로 쓸 때만 (복사 전용 텍스처의 뷰는 Vulkan 이 허용하지 않는다)
    constexpr VkImageUsageFlags kViewUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {t.aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT, 0, desc.mips, 0, 1};
    if (const VkResult r =
            (usage & kViewUsage) != 0 ? vkCreateImageView(m_device, &vi, nullptr, &t.view2D) : VK_SUCCESS;
        r != VK_SUCCESS) {
        reportFailure("vkCreateImageView", r);
        vkDestroyImage(m_device, t.image, nullptr);
        vkFreeMemory(m_device, t.memory, nullptr);
        return {};
    }
    setName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(t.image), desc.debugName);
    return m_textures.insert(std::move(t));
}

VkImageView VulkanDevice::arrayView(VulkanTexture& t) {
    if (t.viewArray == VK_NULL_HANDLE) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = t.format;
        vi.subresourceRange = {t.aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT, 0, t.desc.mips, 0, t.desc.layers};
        if (const VkResult r = vkCreateImageView(m_device, &vi, nullptr, &t.viewArray); r != VK_SUCCESS) {
            reportFailure("vkCreateImageView (배열)", r);
        }
    }
    return t.viewArray;
}

void VulkanDevice::destroy(RhiBuffer buffer) {
    if (buffer == m_ringBuffer || buffer == m_tsReadback) {
        validationError("업로드 링 · 타임스탬프 버퍼는 파괴할 수 없습니다");
        return;
    }
    if (auto b = m_buffers.take(buffer)) {
        // 아직 제출하지 않은 기록이 쓸 수 있다 → 다음 제출이 끝난 뒤 해제
        m_garbage.push(Garbage([dev = m_device, buf = b->buffer, mem = b->memory] {
                           vkDestroyBuffer(dev, buf, nullptr);
                           vkFreeMemory(dev, mem, nullptr);
                       }),
                       nextFence());
    }
}

void VulkanDevice::destroy(RhiTexture texture) {
    VulkanTexture* t = m_textures.get(texture);
    if (t == nullptr) {
        return;
    }
    if (t->external) {
        validationError("스왑체인 백버퍼는 destroy 할 수 없습니다");
        return;
    }
    auto taken = m_textures.take(texture);
    m_garbage.push(
        Garbage([dev = m_device, img = taken->image, mem = taken->memory, v2 = taken->view2D, va = taken->viewArray] {
            vkDestroyImageView(dev, v2, nullptr);
            vkDestroyImageView(dev, va, nullptr);
            vkDestroyImage(dev, img, nullptr);
            vkFreeMemory(dev, mem, nullptr);
        }),
        nextFence());
}

const TextureDesc* VulkanDevice::textureDesc(RhiTexture texture) const {
    const VulkanTexture* t = m_textures.get(texture);
    return t != nullptr ? &t->desc : nullptr;
}

std::byte* VulkanDevice::map(RhiBuffer buffer) {
    VulkanBuffer* b = m_buffers.get(buffer);
    return b != nullptr ? b->mapped : nullptr;
}

UploadAllocation VulkanDevice::allocateUpload(u64 size, u64 alignment) {
    const auto offset = m_ring.allocate(size, alignment);
    if (!offset) {
        ++m_ringDeferred;
        return {};
    }
    return {m_ringBuffer, *offset, m_ringCpu + *offset, size};
}

ICommandQueue& VulkanDevice::queue(QueueType /*type*/) {
    return m_queue;
}

std::unique_ptr<ICommandList> VulkanDevice::createCommandList(QueueType /*type*/) {
    return makeCommandList(*this);
}

Expected<std::unique_ptr<ISwapChain>> VulkanDevice::createSwapChain(const platform::NativeWindowHandle& window,
                                                                    const SwapChainDesc& desc) {
    return makeSwapChain(*this, window, desc);
}

RhiTexture VulkanDevice::registerSwapchainImage(VkImage image, const TextureDesc& desc) {
    VulkanTexture t;
    t.image = image;
    t.desc = desc;
    t.format = toVk(desc.format);
    t.external = true;
    t.neverTransitioned = true;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = t.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (const VkResult r = vkCreateImageView(m_device, &vi, nullptr, &t.view2D); r != VK_SUCCESS) {
        reportFailure("스왑체인 이미지 뷰", r);
        return {};
    }
    setName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(image), desc.debugName);
    return m_textures.insert(std::move(t));
}

void VulkanDevice::attach(DeviceChild* child) {
    m_children.push_back(child);
}

void VulkanDevice::detach(DeviceChild* child) noexcept {
    std::erase(m_children, child);
}

void VulkanDevice::releaseSwapchainImageNow(RhiTexture texture) {
    if (auto t = m_textures.take(texture)) {
        vkDestroyImageView(m_device, t->view2D, nullptr);
    }
}

// ---------------------------------------------------------------------------
// 프레임
// ---------------------------------------------------------------------------

void VulkanDevice::beginFrame() {
    ++m_frameNumber;
    m_frameIndex = static_cast<u32>(m_frameNumber % m_framesInFlight);
    // 이 슬롯을 마지막으로 쓴 프레임이 끝나기를 기다린다 (보통 이미 끝나 있다)
    m_queue.wait(m_frameFences[m_frameIndex]);
    const FenceValue done = m_queue.completedValue();
    m_ring.retire(done);
    m_released += m_garbage.collect(done);
    // 이 슬롯의 타임스탬프는 GPU 가 끝냈다 — 옮긴 뒤 칸을 비운다 (다음 기록 전에 reset 이 필요하다)
    if (m_tsPool != VK_NULL_HANDLE) {
        if (ResolvedTimestamps& r = m_tsResolved[m_frameIndex]; r.count != 0) {
            if (const VulkanBuffer* b = m_buffers.get(m_tsReadback); b != nullptr && b->mapped != nullptr) {
                const usize first = static_cast<usize>(m_frameIndex) * kMaxTimestampsPerFrame;
                std::memcpy(m_timestamps.ticks.data(), b->mapped + first * sizeof(u64), r.count * sizeof(u64));
                m_timestamps.count = r.count;
                m_timestamps.frameNumber = r.frameNumber;
            }
            r = {};
        }
        vkResetQueryPool(m_device, m_tsPool, m_frameIndex * kMaxTimestampsPerFrame, kMaxTimestampsPerFrame);
    }
}

VkBuffer VulkanDevice::timestampReadback() noexcept {
    VulkanBuffer* b = m_buffers.get(m_tsReadback);
    return b != nullptr ? b->buffer : VK_NULL_HANDLE;
}

void VulkanDevice::noteTimestampsResolved(u32 count) noexcept {
    m_tsResolved[m_frameIndex] = {m_frameNumber, count};
}

void VulkanDevice::endFrame() {
    const FenceValue last = m_queue.lastSubmittedValue();
    m_frameFences[m_frameIndex] = last;
    m_ring.endFrame(last);
}

void VulkanDevice::waitIdle() {
    m_queue.waitIdle();
    const FenceValue done = m_queue.completedValue();
    m_ring.retire(done);
    m_released += m_garbage.collect(done);
}

Expected<std::unique_ptr<IRenderDevice>> createDevice(const DeviceDesc& desc) {
    return VulkanDevice::create(desc);
}

} // namespace sbx::rhi::vk
