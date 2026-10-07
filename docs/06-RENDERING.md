# 06. 렌더링

> **규범 문서.** RHI, 세 백엔드, 셰이더 파이프라인, 에셋, Renderer 프레임을 정합니다.
> 상태: **Phase 7A 구현** — RHI 골격(3장 중 Buffer·Texture·RenderPass Clear·Barrier·Copy·SwapChain), 4장 FrameContext·업로드 링·
> 지연 해제, D3D12 백엔드(5.1 중 committed 리소스·RTV·Debug Layer·DRED), 이미지 비교·`sbx_render_tests`(14장 중 Clear·업로드·복사·수명).
> `[계획]` Shader·Pipeline·BindGroup·Sampler(7B), Renderer·Asset·ImGui(8), Vulkan(13), Metal(14). 7A 결정: [ADR-0018](adr/0018-rhi-frame-protocol-committed-resources-wine-testing.md).
> 결정 근거: [ADR-0006](adr/0006-thin-rhi.md), [ADR-0007](adr/0007-hlsl-shader-pipeline.md), [ADR-0008](adr/0008-imgui-on-rhi.md).

---

## 1. 경계

```text
ClientWorld ─(Extraction, SandboxClient)─▶ RenderWorld ─▶ Renderer ─▶ RHI ─▶ D3D12 | Vulkan | Metal
```

```text
R1. SandboxRender 는 ECS 를 모른다. 입력은 RenderWorld 뿐이다.
R2. Renderer 상위(render/renderer, render/asset)는 RHI 타입만 안다. 그래픽 API 헤더는 render/<backend>/ 안에만.
R3. Component 에 GPU 핸들·포인터 금지. TextureHandle / MaterialHandle / MeshHandle 등 Handle 을 쓴다.
R4. Renderer 는 시뮬레이션 데이터를 바꾸지 않는다 (읽기조차 하지 않는다 — RenderWorld 만 본다).
R5. SFML, OpenGL 은 어떤 형태로도 쓰지 않는다.
```

---

## 2. 결정 표

| 항목                   | 결정                                                                                                            | 이유                                                                       |
|------------------------|-----------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------|
| SFML 제거 범위         | Graphics·Window·System·Audio·OpenGL·ImGui OpenGL3 백엔드 **전부 0**. CMake에 `find_package(SFML)`·`OpenGL` 없음 | 임시 의존은 영구 의존이 된다 (RTS core 헤더의 SFML include가 증거)         |
| Window Abstraction     | `IWindow` + 이벤트 큐. 렌더링 메서드 없음 ([07](07-PLATFORM.md))                                                | 창과 렌더러의 수명·스레드 분리                                             |
| Input Abstraction      | PlatformEvent → InputSystem → InputState → ActionMap                                                            | 시뮬레이션이 입력 장치를 모르게                                            |
| RHI Boundary           | 얇은 가상 인터페이스 + Desc 구조체 + Caps (3장)                                                                 | 백엔드 교체·추가가 상위 코드 무변경                                        |
| Render Resource Handle | 상위: `{index:32, generation:32}` 핸들 (리소스 매니저 소유). RHI: `RhiTexture` 등 백엔드 객체 핸들              | 무효 핸들 검출, native 포인터 차단                                         |
| Frame Resource 관리    | `FrameContext[N]`: 커맨드 할당자/풀, 업로드 링 구간, 임시 디스크립터, 타임스탬프, 파괴 대기열                   | 프레임 간 대기 제거                                                        |
| Frames In Flight       | **기본 2**, 설정으로 3                                                                                          | 에디터는 입력 지연이 체감된다. GPU 바운드면 3                              |
| GPU Synchronization    | 큐당 단조 증가 64비트 타임라인 펜스 하나. 프레임·업로드·파괴가 같은 값 공간                                     | 세 API 공통분모 (D3D12 Fence / Vulkan timeline semaphore / MTLSharedEvent) |
| Shader Language        | HLSL (SM 6.0 기본 부분집합) 단일 소스                                                                           | 6장                                                                        |
| Shader Compiler        | DXC → DXIL / SPIR-V, SPIRV-Cross → MSL → `metal`. **빌드 타임 오프라인**                                        | 런타임 컴파일러 의존 제거, 셰이더 오류 = 빌드 오류                         |
| Shader Reflection      | SPIR-V 기준 `.reflect.json` → 레이아웃 자동 생성 + C++ 상수 버퍼 헤더 생성                                      | 바인딩 수동 복제 금지                                                      |
| Resource Binding       | 빈도별 BindGroup 4개(0 Frame · 1 Pass · 2 Material · 3 Draw) + Push Constants ≤ 128B                            | 세 API에 무리 없이 내려가는 최소 모델                                      |
| Texture Loading        | Worker 디코드 → Render 스레드 Upload Queue → 업로드 링 → GPU                                                    | Worker는 GPU 리소스를 만들지 않는다                                        |
| Sprite Batching        | 인스턴스 쿼드, 64비트 정렬 키, Texture2DArray 페이지                                                            | 엔티티당 Draw 금지                                                         |
| GPU Instancing         | 기본 경로. `draw(4, n)` + StructuredBuffer 인스턴스                                                             | 50k 스프라이트를 수십 Draw로                                               |
| ImGui Integration      | 공식 백엔드 미사용. RHI 위 자체 렌더러 1벌 + InputState→ImGuiIO 공급                                            | Editor가 native 타입 0개. 백엔드 3벌 유지비 제거                           |
| DX12 Backend           | FL 12_0+, D3D12MA, Debug Layer / GBV / DRED, PIX 마커                                                           | 1차 플랫폼                                                                 |
| Vulkan Backend         | 1.3 core (dynamic rendering, sync2, timeline), volk, VMA                                                        | RenderPass 객체 없이 DX12·Metal과 모양 일치                                |
| Metal Backend          | Metal 3, Objective-C++ `.mm` + ARC + pimpl, macOS 13+ Apple Silicon                                             | 공개 헤더에 ObjC 타입 0개                                                  |

---

## 3. RHI

### 3.1 핸들과 Desc

```cpp
namespace sbx::rhi {
template<class Tag> struct Handle { std::uint32_t index = ~0u, generation = 0; bool valid() const; };
using RhiBuffer = Handle<struct BufferTag>;   using RhiTexture = Handle<struct TextureTag>;
using RhiSampler = Handle<struct SamplerTag>; using RhiShader = Handle<struct ShaderTag>;
using RhiPipeline = Handle<struct PipelineTag>;
using RhiBindGroupLayout = Handle<struct BGLTag>; using RhiBindGroup = Handle<struct BGTag>;

struct BufferDesc  { std::uint64_t size; BufferUsage usage; MemoryType memory; std::string_view debugName; };
struct TextureDesc { Extent3D extent; Format format; std::uint32_t mips = 1, layers = 1;
                     TextureUsage usage; std::string_view debugName; };
}
```

`MemoryType`: `GpuOnly`(DEFAULT / DEVICE_LOCAL / private), `Upload`(UPLOAD / HOST_VISIBLE|COHERENT / shared), `Readback`.

### 3.2 인터페이스

```cpp
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual const DeviceCaps& caps() const = 0;
    virtual RhiBuffer   createBuffer(const BufferDesc&) = 0;
    virtual RhiTexture  createTexture(const TextureDesc&) = 0;
    virtual RhiSampler  createSampler(const SamplerDesc&) = 0;
    virtual RhiShader   createShader(const ShaderBlob&) = 0;
    virtual RhiPipeline createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual RhiPipeline createComputePipeline(const ComputePipelineDesc&) = 0;
    virtual RhiBindGroupLayout createBindGroupLayout(const BindGroupLayoutDesc&) = 0;
    virtual RhiBindGroup createBindGroup(const BindGroupDesc&) = 0;
    virtual void* mapUploadBuffer(RhiBuffer) = 0;             // Upload 메모리만, 영구 매핑
    virtual void destroy(RhiHandleAny) = 0;                    // 지연 해제 (4.3)
    virtual ICommandQueue& queue(QueueType) = 0;
    virtual std::unique_ptr<ISwapChain> createSwapChain(const NativeWindowHandle&, const SwapChainDesc&) = 0;
    virtual std::unique_ptr<ICommandList> createCommandList(QueueType) = 0;
};

class ICommandList {
public:
    virtual ~ICommandList() = default;
    virtual void begin() = 0;  virtual void end() = 0;
    virtual void barrier(std::span<const ResourceBarrier>) = 0;
    virtual void beginRenderPass(const RenderPassDesc&) = 0;  virtual void endRenderPass() = 0;
    virtual void setPipeline(RhiPipeline) = 0;
    virtual void setBindGroup(std::uint32_t slot, RhiBindGroup) = 0;
    virtual void pushConstants(std::span<const std::byte>) = 0;
    virtual void setVertexBuffer(std::uint32_t slot, RhiBuffer, std::uint64_t offset) = 0;
    virtual void setIndexBuffer(RhiBuffer, std::uint64_t offset, IndexFormat) = 0;
    virtual void setViewport(const Viewport&) = 0;  virtual void setScissor(const Rect2D&) = 0;
    virtual void draw(std::uint32_t vtx, std::uint32_t inst, std::uint32_t firstVtx, std::uint32_t firstInst) = 0;
    virtual void drawIndexed(std::uint32_t idx, std::uint32_t inst, std::uint32_t firstIdx,
                             std::int32_t vtxOff, std::uint32_t firstInst) = 0;
    virtual void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void copyBuffer(const BufferCopy&) = 0;
    virtual void copyBufferToTexture(const BufferTextureCopy&) = 0;
    virtual void beginDebugLabel(std::string_view) = 0;  virtual void endDebugLabel() = 0;
    virtual void writeTimestamp(QueryHandle) = 0;
};

class ICommandQueue {
public:
    virtual FenceValue submit(std::span<ICommandList* const>) = 0;   // 제출 후 signal 된 값
    virtual FenceValue completedValue() const = 0;
    virtual void waitIdle() = 0;
    virtual void wait(FenceValue) = 0;
};

class ISwapChain {
public:
    virtual AcquireResult acquire() = 0;     // { RhiTexture backbuffer, bool outOfDate }
    virtual void present() = 0;
    virtual void resize(Extent2D) = 0;
    virtual Format format() const = 0;
};
```

모든 백엔드 클래스는 `final`. 가상 호출 비용은 배치 단위 호출 수(프레임당 수천 이하)에서 무시 가능합니다.

**Phase 7A 구현 (`render/rhi/RenderDevice.hpp`)** — 위 초안과 다른 점 (ADR-0018):

```text
IRenderDevice  + beginFrame() / endFrame() / framesInFlight() / frameIndex() / frameNumber() / waitIdle()
               + map(RhiBuffer)            Upload · Readback 영구 매핑 (mapUploadBuffer 대신)
               + allocateUpload(size, align) → UploadAllocation{buffer, offset, cpu}   업로드 링 구간
               + destroy(RhiBuffer) · destroy(RhiTexture) 오버로드, alive(h), textureDesc(h), stats() → DeviceStats
               createSwapChain → Expected<unique_ptr<ISwapChain>>
               [7B] createSampler · createShader · createGraphics/ComputePipeline · createBindGroupLayout · createBindGroup
ICommandList   7A: begin end barrier beginRenderPass endRenderPass setViewport setScissor copyBuffer
                   copyBufferToTexture + copyTextureToBuffer (기준 이미지 읽기) beginDebugLabel endDebugLabel
               [7B] setPipeline setBindGroup pushConstants setVertexBuffer setIndexBuffer draw drawIndexed dispatch
               [8]  writeTimestamp
ICommandQueue  + lastSubmittedValue()
ISwapChain     acquire() → {backbuffer, skip(최소화)}, present() → bool, resize(w, h), setVsync/vsync, format, extent
ResourceBarrier  7A 는 텍스처 전이만 ({texture, before, after}). 버퍼는 메모리 종류가 상태를 정한다
RenderPass     beginRenderPass 가 색 첨부를 묶고 LoadOp::Clear 면 지우며, 뷰포트·시저를 첫 첨부 크기로 맞춘다
DeviceDesc     backend · debugLayer · gpuValidation · warp · framesInFlight(2|3) · uploadRingBytes(32 MB) ·
               allowFeatureLevel11 (시험 전용 — Wine/vkd3d)
DeviceCaps     + adapterName · softwareAdapter · featureLevel · dedicatedVideoMemory · tearing ·
               textureCopyRowAlignment(D3D12 256) · textureCopyOffsetAlignment(D3D12 512)
createRenderDevice(desc)  이 빌드의 백엔드. 없는 OS(지금 Linux·macOS)는 Unsupported (render/stub)
```
Windows에서 DX12와 Vulkan을 함께 빌드할 수 있게(`SBX_ENABLE_VULKAN_ON_WINDOWS`) 가상 인터페이스를 유지합니다.

### 3.3 Capability / Extension

```cpp
struct DeviceCaps {
    BackendType backend;                                  // D3D12 / Vulkan / Metal
    bool timestampQueries, compute, bindless, meshShaders, rayTracing;
    bool bcCompression, astcCompression;
    std::uint32_t maxTextureSize, maxTextureArrayLayers, maxPushConstantBytes, constantBufferAlignment;
};
template<class Ext> Ext* queryExtension(IRenderDevice&);  // IBindlessExtension, ID3D12NativeAccess …
```

**최소 공통분모 방지:** 한 백엔드에만 있는 기능도 Caps로 노출하고 렌더 패스가 분기할 수 있습니다.
같은 결과를 내는 대체 경로는 **콘텐츠가 그 기능에 의존할 때만** 의무입니다.

### 3.4 Binding 모델

HLSL 규칙: `register(<b|t|s|u>N, spaceG)` 에서 **G = BindGroup 번호(0~3)**, push constant는 `[[vk::push_constant]]` + DX12 root constant(`b0, space7` 예약).

| RHI             | D3D12                             | Vulkan                  | Metal                                |
|-----------------|-----------------------------------|-------------------------|--------------------------------------|
| BindGroupLayout | Root Signature의 descriptor table | `VkDescriptorSetLayout` | Argument Buffer 레이아웃 (Tier 2)    |
| BindGroup       | shader-visible heap 연속 구간     | `VkDescriptorSet`       | argument `MTLBuffer` + `useResource` |
| Push Constants  | Root Constants                    | Push Constants          | `set{Vertex,Fragment}Bytes`          |
| 동적 상수       | Root CBV (GPU VA)                 | Dynamic UBO offset      | `setBuffer:offset:`                  |

### 3.5 Resource Barrier

RHI 상태: `Undefined, RenderTarget, DepthWrite, DepthRead, ShaderRead, UnorderedAccess, CopySrc, CopyDst, Present`.

```text
D3D12   Legacy ResourceBarrier (Enhanced Barriers 는 Caps 로 후속)
Vulkan  vkCmdPipelineBarrier2 (상태 → stage/access/layout 테이블)
Metal   대부분 no-op (자동 hazard tracking). untracked 리소스만 MTLFence
초기에는 Pass 가 배리어를 손으로 선언한다. 자동 추적은 RenderGraph 도입 시 (11장).
```

---

## 4. 프레임 리소스 · 동기화 · 수명

### 4.1 FrameContext

```cpp
struct FrameContext {                        // 백엔드별 구현, 개념은 공통
    CommandAllocatorOrPool  commands;
    FenceValue              submittedValue = 0;
    UploadRingSlice         upload;            // 동적 버텍스·인스턴스·상수
    TransientDescriptorSlice descriptors;
    QueryRange              timestamps;
};
```

```text
BeginFrame(i): queue.wait(frames[i].submittedValue) → reset(commands, upload, descriptors)
EndFrame(i):   frames[i].submittedValue = queue.submit(...) ; i = (i + 1) % N
```

### 4.2 업로드 링

```text
Upload 메모리 하나(기본 32 MB), 영구 매핑. 프레임마다 [head, tail) 구간을 쓴다.
할당 = 정렬 후 bump. 부족하면 그 프레임 업로드를 다음 프레임으로 미루고 메트릭 경고.
텍스처 업로드도 같은 링을 쓰되 프레임 예산(기본 8 MB)을 둔다.
```

**Phase 7A 구현:** `render/rhi/UploadRing`(구간 관리 — 정렬, 끝에 안 맞으면 앞으로 감기, 감을 때 버린 꼬리도 사용량, 프레임
펜스로 반납)과 D3D12 의 Upload 버퍼 하나(영구 매핑). 가득 차면 무효 할당 + `stats.uploadRingDeferred` 증가 — 이월은 호출자.
프레임 예산(텍스처 8 MB)은 Phase 8.

### 4.3 GPU Resource Lifetime

```text
destroy(handle)
  → 핸들 슬롯 즉시 무효화 (generation++). 이후 사용은 Debug 단언
  → 실제 객체는 (lastSubmittedFenceValue) 와 함께 DeferredDestructionQueue 로
  → BeginFrame: completedValue() ≥ 기록값 인 항목만 실제 해제
  → 종료: waitIdle 후 전부 해제, 타입별 잔존 수 로그 (Debug 에서 0 이 아니면 실패)
```

| 규칙                                                                                       | 이유                           |
|--------------------------------------------------------------------------------------------|--------------------------------|
| 상위 계층은 `destroy`만 부르고 시점을 모른다                                               | 펜스 지식을 RHI 안에 가둔다    |
| 생성·파괴는 Render 스레드만                                                                | 수명 장부를 한 스레드가 가진다 |
| 소유 관용구: D3D12 `Com<T>`(render/dx12, ComPtr 대신), Vulkan VMA + 핸들, Metal ARC `id<>` |                                |

---

## 5. 백엔드

### 5.1 DirectX 12 (Windows)

```text
HWND → IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE) (CI: WARP)
     → D3D12CreateDevice(FL 12_0) → DIRECT 큐 (후속 COPY 큐)
     → IDXGISwapChain4 (FLIP_DISCARD, 버퍼 = framesInFlight + 1, ALLOW_TEARING 지원 시)
```

| 구성            | 결정                                                                                                                              |
|-----------------|-----------------------------------------------------------------------------------------------------------------------------------|
| Command List    | `ID3D12GraphicsCommandList7` 가능 시, 아니면 4                                                                                    |
| Descriptor Heap | CPU 힙(RTV/DSV/스테이징) + shader-visible CBV/SRV/UAV 1개(1,000,000) + Sampler(2048). shader-visible은 장기 영역 + 프레임 링 영역 |
| Root Signature  | 리플렉션에서 생성, 해시 공유                                                                                                      |
| PSO             | Desc 해시 캐시. `ID3D12PipelineLibrary` 디스크 캐시는 후속                                                                        |
| Memory          | D3D12MA                                                                                                                           |
| 디버그          | `--rhi-debug` Debug Layer, `--rhi-gbv` GPU-Based Validation, DRED(Debug 기본), WinPixEventRuntime 마커, `--rhi-warp`              |
| Agility SDK     | 초기 미사용. Enhanced Barriers 등 필요 시 ADR                                                                                     |

**Phase 7A 구현 (`render/dx12/`)** — 위 표와 다른 점:

```text
메모리        CreateCommittedResource (D3D12MA 는 Phase 8 — ADR-0018). 텍스처는 COMMON 으로 만든다 (Undefined = COMMON)
디스크립터    RTV CPU 힙 1024 + 자리 목록. shader-visible CBV/SRV/UAV · Sampler 힙은 7B
커맨드 리스트 ID3D12GraphicsCommandList (기본 인터페이스 — 7A 기능에 충분, MinGW 헤더와도 맞는다). 슬롯마다 할당자 하나
어댑터        --rhi-warp 면 WARP. 아니면 고성능 순서의 하드웨어 → 소프트웨어 → WARP. 최소 FL 12_0 (allowFeatureLevel11 시험용 11_0)
Debug Layer   ID3D12InfoQueue 를 프레임마다 비워 경고·오류를 센다(정보성 메시지는 저장 안 함). 없으면 "그래픽 도구" 안내 후 계속
              성능 안내만인 CLEAR(RENDERTARGET|DEPTHSTENCIL)VIEW_MISMATCHINGCLEARVALUE 는 저장하지 않는다 (백버퍼는 최적 값이 없다)
DRED          Debug 빌드와 --rhi-debug 에서 자동 브레드크럼 · 페이지 폴트. 디바이스 제거는 원인을 한 번 로그
PIX 마커      BeginEvent(metadata 0, UTF-16) — WinPixEventRuntime 없이
스왑체인      FLIP_DISCARD, BGRA8Unorm, 버퍼 framesInFlight + 1, ALLOW_TEARING(지원 시, VSync 끔), DXGI_MWA_NO_ALT_ENTER
펜스 대기     5초마다 디바이스 제거 확인, 30초면 포기 (멈춘 GPU 에서 테스트가 영원히 걸리지 않게)
```

### 5.2 Vulkan (Linux, Windows 옵션)

```text
X11(Display*, Window) | Wayland(wl_display*, wl_surface*)
 → VkInstance 1.3 (+ surface 확장, Debug: VK_EXT_debug_utils) → VkSurfaceKHR
 → PhysicalDevice (discrete 우선, 1.3 + 필수 기능 + graphics/present 큐)
 → VkDevice (dynamicRendering, synchronization2, timelineSemaphore, (caps) descriptorIndexing)
 → VkSwapchainKHR (FIFO 기본, MAILBOX 선택) → 프레임별 Command Pool
```

| 구성        | 결정                                                                      |
|-------------|---------------------------------------------------------------------------|
| 로더        | volk                                                                      |
| 메모리      | VMA                                                                       |
| 동기화      | 큐 타임라인 세마포어 + 스왑체인 이미지별 binary 세마포어(acquire/present) |
| Render Pass | `vkCmdBeginRendering`                                                     |
| Descriptor  | 장기 풀 + 프레임별 리셋 풀                                                |
| 캐시        | `VkPipelineCache` 디스크 저장                                             |
| 재생성      | `OUT_OF_DATE`/`SUBOPTIMAL` → idle 후 재생성                               |
| 디버그      | Debug 빌드 Validation Layer 기본, `--rhi-sync-validation`                 |

### 5.3 Metal (macOS)

```text
CocoaWindow(.mm): NSWindow + layer-backed NSView + CAMetalLayer
 → NativeWindowHandle{Cocoa, window = CAMetalLayer*}
 → MTLCreateSystemDefaultDevice → MTLCommandQueue
 → 프레임: MTLCommandBuffer → MTLRenderCommandEncoder → presentDrawable → commit
 → MTLSharedEvent (타임라인)
```

```text
render/metal/MetalDevice.hpp      class MetalRenderDevice final : public rhi::IRenderDevice { struct Impl; std::unique_ptr<Impl> m; };
render/metal/MetalDevice.mm       Impl: id<MTLDevice>, id<MTLCommandQueue> (ARC)
render/metal/MetalSwapChain.mm    CAMetalLayer, nextDrawable
render/metal/MetalPipeline.mm     MTLRenderPipelineState, metallib 로드
render/metal/MetalBuffer.mm       MTLBuffer (shared / private)
규칙: .hpp 에 Objective-C 타입·#import 금지. ObjC 는 .mm 안에서만.
디버그: Metal API Validation, Shader Validation, MTLCaptureManager 프로그램 캡처(--rhi-capture)
```

---

## 6. 셰이더 파이프라인

### 6.1 경로

```text
shaders/<name>.hlsl
  Windows : dxc -T vs_6_0|ps_6_0|cs_6_0 -E <entry> -Zpc -Qstrip_reflect → .dxil
  Vulkan  : dxc -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout     → .spv
  Metal   : .spv → spirv-cross --msl --msl-version 30000                 → .metal → xcrun metal → .metallib
  공통    : .spv → SPIRV-Cross 리플렉션                                   → .reflect.json
```

```cmake
sbx_add_shader(TARGET SandboxRender SOURCE shaders/sprite.hlsl
               STAGES vs:VSMain ps:PSMain
               OUTPUT_HEADER render/generated/SpriteShader.hpp)   # 상수 버퍼 구조체 + static_assert
```

| 결정           | 내용                                                                                                                                                           |
|----------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 도구 버전 고정 | DXC·SPIRV-Cross 버전을 `cmake/SbxDependencies.cmake`에 고정 (2026-10 기준 DXC 최신은 2026년 9월 릴리스 v1.9.2609). 업그레이드는 단독 커밋 + 기준 이미지 재확인 |
| 산출물 위치    | `build/<preset>/shaders/<backend>/` — 저장소에 커밋하지 않음                                                                                                   |
| 런타임         | 패키지된 바이트코드만 로드. 에디터 옵션 `--shader-hot-reload`만 런타임 DXC 호출                                                                                |
| 기능 제한      | SM 6.0 + 위 바인딩 규칙. 웨이브 intrinsic·16비트 타입은 Caps 분기 + 셰이더 변형                                                                                |
| 대안           | Slang: Phase 13 이전 스파이크 후 ADR. Microsoft의 DirectX SPIR-V 채택(SM7 계획)이 실현되면 산출물 단일화 검토                                                  |

### 6.2 HLSL 작성 규칙

```text
- 파일 하나 = 관련 엔트리 포인트 묶음. 공통은 shaders/common/*.hlsli
- 행렬은 column_major, mul(M, v). 상수 버퍼는 16바이트 정렬을 직접 맞춘다 (생성 헤더의 static_assert 가 검사)
- 좌표 규약은 엔진 규약(12장)으로만 작성. #ifdef VULKAN 같은 백엔드 분기 금지
- 텍스처 샘플링 좌표는 좌상단 원점
```

### 6.3 리플렉션

```jsonc
{ "stages": ["vs","ps"],
  "bindGroups": [ { "group": 0, "bindings": [ { "name": "FrameConstants", "type": "ConstantBuffer", "binding": 0, "size": 96 } ] },
                  { "group": 2, "bindings": [ { "name": "SpriteTexture", "type": "Texture2DArray", "binding": 0 },
                                              { "name": "SpriteSampler", "type": "Sampler", "binding": 1 } ] },
                  { "group": 3, "bindings": [ { "name": "Instances", "type": "StructuredBuffer", "binding": 0, "stride": 48 } ] } ],
  "pushConstants": { "size": 16 } }
```

용도: 파이프라인 레이아웃 자동 생성 · C++ 상수 버퍼 헤더 생성 · 머티리얼 에디터 파라미터 목록.

---

## 7. 에셋 파이프라인

### 7.1 Content vs Asset

| 구분    | 로더                     | 서버 필요 | 예                                           | contentHash 포함 |
|---------|--------------------------|-----------|----------------------------------------------|------------------|
| Content | `ContentDatabase` (Core) | ✓         | Prefab, Rule, Behavior, TerrainMaterial      | ✓                |
| Asset   | `AssetManager` (Render)  | ✗         | Texture, Mesh, Shader, Material, Font, Sound | ✗                |

### 7.2 AssetManager

```text
AssetId          경로 정규화(소문자, '/', assets/ 상대) 후 FNV-1a64. 후속: .meta GUID
AssetHandle<T>   { index, generation }
상태             Unloaded → Queued → Decoding(Worker) → Uploading(Render) → Ready | Failed
참조             refcount. 0 → 지연 해제 (4.3)
핫 리로드         에디터 모드 파일 감시 → 같은 핸들로 교체
Placeholder      Ready 전: 1×1 마젠타/흰색 텍스처, 기본 머티리얼
```

### 7.3 Texture Loading

```text
Worker   파일 읽기 + stb_image 디코드 → ImageAsset{RGBA8, w, h}  (밉은 후속: 빌드 타임 생성)
Render   UploadQueue.pop (프레임 예산 8 MB) → createTexture → 업로드 링 복사
         → copyBufferToTexture → barrier(CopyDst→ShaderRead) → 펜스 도달 시 Ready, CPU 픽셀 해제
```

### 7.4 아틀라스 / 배열

스프라이트는 2048² 페이지로 패킹(빌드 단계 도구 `sbx_atlas`, 초기에는 로드 시 패킹) → `Texture2DArray`.
인스턴스에 (page, uvRect). 같은 배열·같은 파이프라인이면 한 번의 Draw.

---

## 8. Renderer

### 8.1 RenderWorld

```cpp
struct SpriteInstance {            // 48 bytes, GPU StructuredBuffer 레이아웃과 동일
    Vec2  position;   Vec2 size;   // 월드 단위
    float rotation;   std::uint32_t page;
    Vec4  uvRect;                  // (u0, v0, u1, v1)
    std::uint32_t colorRGBA8;  std::uint32_t flags;   // flipX, 선택 강조 등
};
struct RenderWorld {
    Camera2D camera;
    std::vector<SpriteDraw> sprites;      // { SpriteInstance, MaterialHandle, layer, depth }
    std::vector<TerrainChunkDraw> terrain;// { ChunkCoord, revision, MeshHandle }
    DebugDrawList debug;                  // 선·원·사각형·텍스트
    OverlayInputs overlay;                // 그리드, 선택 박스, Gizmo
};
```

`RenderWorld`는 프레임마다 다시 채웁니다. 지속 상태(청크 메시, 텍스처)는 리소스 매니저에 있습니다.

### 8.2 정렬 키 (64비트)

```text
[ pass:4 | layer:8 | pipeline:12 | material(texture page 포함):20 | depth:20 ]
```

### 8.3 Pass

| Pass            | 내용                                           |
|-----------------|------------------------------------------------|
| TerrainPass     | 보이는 청크 메시 (revision 바뀐 청크만 재빌드) |
| WorldSpritePass | 인스턴스 배치                                  |
| GridPass        | 에디터 타일·청크 격자                          |
| SelectionPass   | 선택 외곽선, 박스 선택                         |
| DebugPass       | DebugDraw (경로, 센서 반경, 청크 경계)         |
| UIPass          | ImGui                                          |

### 8.4 프레임

```text
BeginFrame → AcquireSwapchainImage → (RenderWorld 수신) → ProcessUploadQueue → UploadDynamicData
→ Cull(청크 → 스프라이트) → Sort → Batch → TerrainPass → WorldSpritePass → GridPass → SelectionPass
→ DebugPass → UIPass → Submit → Present → EndFrame(타임스탬프 수거, 메트릭)
```

### 8.5 성능 원칙

```text
P1 엔티티당 Draw 금지   P2 아틀라스/배열로 배치 분할 최소화   P3 동적 데이터는 업로드 링에 연속 기록
P4 컬링 2단(청크 → 스프라이트), 후속 GPU 컬링   P5 지형 청크 메시 캐시
목표(설계값): 50k 가시 스프라이트에서 Draw ≤ 64, CPU 렌더 ≤ 4 ms, 인스턴스 업로드 ≈ 2.4 MB/frame
```

### 8.6 첫 Renderer 순서 (Phase 7~8)

```text
Window → Clear → Triangle → Texture → Sprite → Camera → Batch → ImGui
각 단계 = 커밋 1개 + 기준 이미지 테스트 1개.
```

---

## 9. 데이터 흐름

```text
[Server] SimulationWorld ─Replicated─▶ Snapshot ─(Network)─▶ [Client] ClientWorld
  ─▶ InterpolationSystem (SnapshotBuffer → InterpolatedTransform)
  ─▶ ExtractionSystem (view<InterpolatedTransform, render.sprite, core.tags>, 읽기 전용)
  ─▶ RenderWorld ─▶ Cull ─▶ Sort ─▶ Batch ─▶ Passes ─▶ RHI ─▶ Backend ─▶ Present
```

Render 스레드 분리 시 RenderWorld를 이중 버퍼로 두고 포인터만 교환합니다. Dedicated Server에는 이 흐름 전체가 없습니다.

---

## 10. ImGui 통합

```text
SandboxEditor  : ImGui:: 호출만 (패널 코드). 백엔드·native 타입 모름
SandboxRender  : ImGuiRenderer — ImDrawData → 업로드 링(정점/인덱스) → 파이프라인 1개 → scissor 별 drawIndexed
                 폰트 아틀라스 = 일반 Texture 에셋, ImTextureID = TextureHandle 값
SandboxClient  : InputState → ImGuiIO (마우스, 키, 텍스트, 휠, 포커스), DisplaySize/FramebufferScale
```

```text
I1. ImGui 가 WantCaptureMouse/Keyboard 이면 그 프레임 게임 입력 차단
I2. docking 브랜치 사용. 멀티 뷰포트(창 밖으로 떼기)는 초기 미지원
I3. 폴백: 문제가 생기면 공식 imgui_impl_dx12 를 render/dx12 내부에서 래핑 (ADR-0008 대안)
```

---

## 11. Render Graph 도입 기준

```text
초기: 명시적 Pass 목록, Pass 가 배리어 선언.
둘 이상 충족 시 ADR 로 검토:
  - 오프스크린 패스 6개 이상
  - 같은 텍스처를 3개 이상 패스가 읽고 쓰며 배리어 버그 2회 이상
  - transient 타겟 메모리 앨리어싱이 예산에 필요
대비: Pass 인터페이스를 처음부터 setup(입출력 선언) / execute(기록) 두 단계로 만든다.
```

---

## 12. 좌표 규약

| 항목               | 엔진 규약                       | D3D12                   | Vulkan                                                 | Metal                        |
|--------------------|---------------------------------|-------------------------|--------------------------------------------------------|------------------------------|
| NDC Y              | 위가 +1                         | 동일                    | 아래가 +1 → **음수 높이 viewport**로 뒤집음 (1.1 core) | 동일                         |
| Depth              | [0, 1]                          | 동일                    | 동일                                                   | 동일                         |
| 텍스처 원점        | 좌상단                          | 동일                    | 동일                                                   | 동일                         |
| Front Face         | CCW = 앞면, PipelineDesc에 명시 | `FrontCounterClockwise` | Y 뒤집힘을 고려해 백엔드가 매핑                        | `MTLWindingCounterClockwise` |
| 상수 버퍼 레이아웃 | DXIL 패킹                       | 기준                    | `-fvk-use-dx-layout`로 일치                            | SPIRV-Cross가 오프셋 유지    |

```text
규칙: 게임 로직·Extraction·셰이더 소스에 백엔드 분기 금지. 차이는 백엔드의 viewport/파이프라인 생성 코드 한 곳에서만.
검증: "좌상단 빨강, 우상단 초록, 좌하단 파랑" 기준 이미지 + 컬링 방향 테스트를 백엔드마다.
```

---

## 13. 디버그 옵션 (SandboxClient)

| 옵션                        | 효과                                                      |
|-----------------------------|-----------------------------------------------------------|
| `--rhi=dx12\|vulkan\|metal` | 백엔드 선택 (빌드에 포함된 것만)                          |
| `--rhi-debug`               | Debug Layer / Validation Layer / Metal API Validation     |
| `--rhi-gbv`                 | D3D12 GPU-Based Validation                                |
| `--rhi-sync-validation`     | Vulkan synchronization validation                         |
| `--rhi-warp`                | D3D12 WARP 소프트웨어 디바이스                            |
| `--rhi-fl11`                | D3D12 FL 11_0 어댑터도 허용 (오래된 GPU·Wine 시험, 7A)    |
| `--no-render`               | 렌더러 없이 창만 (7A)                                     |
| `--rhi-capture=N`           | N번째 프레임 프로그램 캡처 (PIX / Xcode)                  |
| `--frames-in-flight 2\|3`   | 7A 구현                                                   |
| `--vsync on\|off`           | 7A 구현. 렌더러가 있으면 페이싱도 VSync (--fps 가 이긴다) |

---

## 14. 필수 테스트 (`sbx_render_tests`)

```text
- 기준 이미지: Clear, Triangle, Texture, Sprite, Batch(1k), 좌표 규약, ImGui 기본 창 — 백엔드별, 허용 오차 비교
- 수명: destroy 직후 핸들 무효, N 프레임 후 실제 해제, 종료 시 잔존 0
- 업로드 링: 경계 넘김, 예산 초과 이월
- 리플렉션 헤더: static_assert 가 깨지면 빌드 실패 (테스트 대신 컴파일이 검사)
CI: Windows WARP, Linux lavapipe, macOS Apple Silicon 러너
```

Phase 7A 구현 (`tests/render/`): clear(64×48) · 두 색 첨부 · upload_quadrants(버퍼 → 텍스처, 좌상단 원점·행 순서 — 손실 없음) ·
region_copy(오프셋 부분 복사) · 버퍼 왕복 + 업로드 링 감기(12프레임) · 수명(기록 중 파괴 → 지연 해제) · 사용 오류 보고.
끝에 누수 0, Debug Layer 경고 0, 오류 = 예상한 사용 오류 수. CTest `render_tests_warp` (Windows). 클라우드에서는 MinGW +
Wine(vkd3d) + lavapipe 로 같은 실행 파일을 돌린다 (`tools/wine/`, ADR-0018). 순수 로직(핸들 풀·지연 해제·업로드 링·이미지 비교)은
SandboxTests 의 `render` 스위트 (모든 OS).
