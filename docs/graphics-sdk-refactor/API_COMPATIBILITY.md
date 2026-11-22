# Sandbox RHI ↔ Graphics SDK API 호환성 매트릭스

본 문서는 Sandbox RHI(`render/rhi/`)의 모든 공개 인터페이스, 열거형, 리소스 기술자 및 렌더러 사용 API를 Graphics SDK(`GraphicsSDK::Core`, `include/graphics/*.hpp`, commit `9345c181`)의 공개 API와 1:1 대조한 호환성 매트릭스입니다.

---

## 1. 호환 상태 분류 기준

| 호환 상태 | 의미 |
|---|---|
| **직접 호환** | Graphics SDK 공개 API와 의미·시그니처가 1:1로 직접 대응됨 |
| **어댑터로 호환** | `render/graphics_sdk/` 어댑터 계층의 변환·래핑으로 호출자 수정 없이 100% 동작함 |
| **호출 방식 변경 필요** | 기존 Sandbox의 호출 순서(예: 기록 후 CPU 쓰기)를 어댑터의 지연 재생 또는 호출부 정렬로 변환해야 함 |
| **Graphics SDK 기능 확장 필요** | Graphics SDK 공개 API 또는 백엔드 구현에 기능 추가가 필요함 ([SDK_REQUIREMENTS.md](SDK_REQUIREMENTS.md) 참조) |
| **플랫폼별 미지원** | 특정 OS/윈도우 시스템 백엔드에서만 아직 지원되지 않음 |
| **통합 검증 필요** | API는 존재하나 상위 렌더 패스 전체와 결합한 실기 검증이 남아 있음 |

---

## 2. 핵심 인터페이스 호환성 매트릭스

| Sandbox 기능 / API | SDK 대응 API | 호환 상태 | 추가 작업 / 어댑터 처리 | 테스트 상태 |
|---|---|---|---|---|
| `createRenderDevice(DeviceDesc)` | `gfx::enumerateAdapters`, `gfx::createDevice(gfx::DeviceDesc)` | **어댑터로 호환** | `BackendType::Auto` 해석, `warp` → `softwareAdapter`, Validation 레이어 미설치 시 경고 후 폴백 | ✅ 완료 (`sbx_render_tests`) |
| `IRenderDevice::caps()` | `gfx::Device::caps()`, `gfx::AdapterInfo` | **어댑터로 호환** | `adapterName`, `softwareAdapter`, `dedicatedVideoMemory`, `timestampQueries`, 정렬 상수 매핑 | ✅ 완료 (`test_rhi_basic`) |
| `IRenderDevice::stats()` | HandlePool 카운터 + `gfx::Device::validationErrorCount()` | **어댑터로 호환** | `liveBuffers`, `liveTextures`, `livePipelines`, `descriptorsUsed`, `pendingDestructions`, `debugErrors` 집계 | ✅ 완료 (`test_rhi_basic`) |
| `IRenderDevice::beginFrame()` / `endFrame()` | `gfx::CommandPool::reset()`, `gfx::CommandQueue::wait()` | **어댑터로 호환** | `FrameSlot` 회전(`framesInFlight = 2\|3`), 슬롯 펜스 대기, 타임스탬프 수거, `UploadRing::retire`, `DeferredDestructionQueue::collect` | ✅ 완료 (D3D12 WARP / D3D12 / Vulkan) |
| `IRenderDevice::waitIdle()` | `gfx::CommandQueue::waitIdle()` + 슬롯 펜스 확정 은퇴 | **어댑터로 호환** | 모든 슬롯의 타임스탬프 수거, `CommandPool::reset()`, 지연 해제 큐 즉시 비움(`m_garbage.clear()`) | ✅ 완료 (`test_rhi_basic`) |
| `IRenderDevice::createBuffer(BufferDesc)` | `gfx::Device::createBuffer(gfx::BufferDesc)` | **어댑터로 호환** | 크기 0 및 `Storage` + 비-`GpuOnly` 사전 검증, `MemoryType`(`GpuOnly`/`Upload`/`Readback`) 매핑 | ✅ 완료 (`test_rhi_basic`) |
| `IRenderDevice::map(RhiBuffer)` | `GraphicsSdkBuffer::cpuShadow` + `gfx::Buffer::write()` / `read()` | **호출 방식 변경 필요** | Stage-Before-Record: `map()`은 `cpuShadow` 포인터를 반환하고, `submit()` 직전에 `Buffer::write()`로 일괄 flush; Readback은 펜스 완료 시 `Buffer::read()` 동기화 | ✅ 완료 (`test_rhi_basic`) |
| `IRenderDevice::allocateUpload(size, align)` | `UploadRing::allocate` + 슬롯별 `gfx::Buffer` (`Upload`) | **호출 방식 변경 필요** | 멀티 프레임 래핑 시 Vulkan `Upload buffer busy` 방지를 위해 `FrameSlot`별 SDK Upload 버퍼 + 공유 CPU 섀도 링으로 구현 | ✅ 완료 (`test_rhi_basic` ring wrap-around) |
| `IRenderDevice::createTexture(TextureDesc)` | `gfx::Device::createTexture(gfx::TextureDesc)` | **어댑터로 호환** | 크기 0 / `maxTextureSize` 상한 검증, 2D 및 2DArray(`layers > 1`), `mipLevels`, RenderTarget/DepthStencil/Storage 플래그 매핑 | ✅ 완료 (`test_rhi_basic`) |
| `IRenderDevice::createSampler(SamplerDesc)` | `gfx::StaticSamplerDesc` (`Filter`, `AddressMode`, `CompareOp`) | **Graphics SDK 기능 확장 필요** | 어댑터가 `GraphicsSdkSampler` 핸들을 관리하며 정적 샘플러 매핑을 준비했으나, 동적 `BindGroup` 샘플러 바인딩은 `SDK-REQ-006` 필요 | 🟡 진행 중 (`SDK-REQ-006`) |
| `IRenderDevice::createShader(ShaderDesc)` | `gfx::ShaderStageDesc` (`ShaderFormat::DXIL`/`SPIRV`/`MSL`) | **Graphics SDK 기능 확장 필요** | 어댑터는 셰이더 바이트코드와 오프라인 `ShaderReflection`을 보관. 단, SDK의 D3D12 DXIL 런타임 리플렉션 및 Vulkan SPIR-V 엔트리포인트(`"main"`, set 0) 제약 해소는 `SDK-REQ-001` 필요 | 🟡 진행 중 (`SDK-REQ-001`) |
| `IRenderDevice::createBindGroupLayout` | `gfx::BindingLayoutDesc` (`ConstantBuffer`/`SampledTexture`/`StorageBuffer`/`StorageTexture`) | **어댑터로 호환** | 중복 바인딩 검증(`PipelineValidation`) 및 `(group, binding)` → 평탄화된 SDK binding index 매핑 테이블 생성 | ✅ 완료 (`test_rhi_draw` validation) |
| `IRenderDevice::createBindGroup` | `gfx::ResourceBindings` (`ConstantBufferBinding`, `TextureBinding`, `StorageBufferBinding`) | **어댑터로 호환** | 레이아웃 항목 수·타입·핸들 생존 검증(`validateBindGroupDesc`) 및 디스크립터 사용량(`descriptorsUsed`) 추적 | ✅ 완료 (`test_rhi_draw` validation) |
| `IRenderDevice::createGraphicsPipeline` | `gfx::Device::createCachedGraphicsPipeline(gfx::GraphicsPipelineDesc)` | **Graphics SDK 기능 확장 필요** | 오프라인 리플렉션 대조 검증(`validatePipelineAgainstReflection`)은 100% 통과. 실제 PSO 생성은 `SDK-REQ-001`~`005` 해소 후 Phase 3에서 활성화 | 🟡 진행 중 (`SDK-REQ-001`~`005`) |
| `IRenderDevice::destroy(Handle)` / `alive(Handle)` | `HandlePool::take` + `DeferredDestructionQueue` | **어댑터로 호환** | 파괴 즉시 핸들 세대 무효화(`alive == false`), GPU 완료 후 리소스 해제 | ✅ 완료 (`test_rhi_basic`) |
| `ICommandList::begin()` / `end()` | `gfx::CommandList::begin()` / `end()` (지연 재생) | **어댑터로 호환** | `FrameSlot`의 `gfx::CommandPool`에서 할당된 커맨드 리스트에 `submit()` 시점에 일괄 기록 | ✅ 완료 (`test_rhi_basic`) |
| `ICommandList::barrier(span<ResourceBarrier>)` | `gfx::CommandList::transition(TexturePtr, gfx::ResourceState)` | **어댑터로 호환** | 텍스처의 `trackedState`를 갱신하며, 아직 미초기화(`Undefined`)인 텍스처에 대한 불필요한 초기 전환은 생략하고 SDK 자동 상태 추적과 정합 유지 | ✅ 완료 (`test_rhi_basic`) |
| `ICommandList::beginRenderPass` / `endRenderPass` | `gfx::CommandList::beginRenderPass(gfx::RenderPassDesc)` / `endRenderPass()` | **어댑터로 호환** | 단일 및 MRT(`kMaxColorAttachments = 4`), Depth/Stencil 첨부, `LoadOp`/`StoreOp`, Clear 색상 및 뷰포트/시저 기본값 설정 | ✅ 완료 (`test_rhi_basic` clear & MRT) |
| `ICommandList::setViewport` / `setScissor` | `gfx::CommandList::setViewport` / `setScissor` | **직접 호환** | `Viewport` 및 `ScissorRect` 1:1 전달 | ✅ 완료 |
| `ICommandList::setPipeline` / `setBindGroup` | `gfx::CommandList::setPipeline` / `bindResources` | **어댑터로 호환** | 파이프라인의 `bindingMap`을 통해 4개 슬롯의 바인드 그룹 항목을 `gfx::ResourceBindings`로 조립 | 🟡 통합 검증 필요 (Phase 3) |
| `ICommandList::pushConstants` | 업로드 링 256B 정렬 상수 버퍼 슬롯 폴백 / 네이티브 Push Constant | **Graphics SDK 기능 확장 필요** | 어댑터에 업로드 링 상수 버퍼 폴백 경로가 구현되어 있으나, 기존 셰이더 바이너리(`register(b0, space7)` / `[[vk::push_constant]]`) 직접 실행을 위해 `SDK-REQ-005` 필요 | 🟡 진행 중 (`SDK-REQ-005`) |
| `ICommandList::setVertexBuffer` / `setIndexBuffer` | `gfx::CommandList::bindVertexBuffer` / `bindIndexBuffer` | **직접 호환** | `GpuOnly` 버퍼일 때 `VertexBuffer` / `IndexBuffer` 상태 전환 자동 삽입 | 🟡 통합 검증 필요 (Phase 3) |
| `ICommandList::draw` / `drawIndexed` | `gfx::CommandList::draw` / `drawIndexed` | **직접 호환** | `vertexCount`, `instanceCount`, `firstVertex`, `firstInstance`, `vertexOffset` 1:1 대응 | 🟡 통합 검증 필요 (Phase 3) |
| `ICommandList::copyBuffer` | `gfx::CommandList::copyBufferRegion` | **직접 호환** | 임의 오프셋/크기 부분 복사, `GpuOnly` 버퍼의 `CopySource`/`CopyDestination` 전환 및 Readback 완료 동기화 등록 | ✅ 완료 (`test_rhi_basic`) |
| `ICommandList::copyBufferToTexture` / `copyTextureToBuffer` | `gfx::CommandList::copyBufferToTextureRegion` / `copyTextureToBufferRegion` | **어댑터로 호환** | 256B `rowPitch` 및 512B `bufferOffset` 정렬 사전 검증(위반 시 크래시 없이 `debugErrors` 보고), 전체 및 부분 2D 영역(`x, y, width, height, mip, layer`) 복사 | ✅ 완료 (`test_rhi_basic` quadrant, sub-region, misuse) |
| `ICommandList::beginDebugEvent` / `endDebugEvent` | `gfx::CommandList::debugMarker` | **어댑터로 호환** | 디버그 마커 기록 (`VK_EXT_debug_utils` 미지원 시 안전하게 무시) | ✅ 완료 |
| `ICommandList::writeTimestamp` / `resolveTimestamps` | `gfx::CommandList::beginTimestampScope` / `endTimestampScope` / `resolveTimestampScopes` / `timestampScopes()` | **어댑터로 호환** | 인덱스 타임스탬프를 구간 스코프로 변환하고, 슬롯 재사용(`beginFrame`) 시 ns 단위 누적 틱(`TimestampReadback`)으로 복원; 렌더 패스 내부 `resolveTimestamps` 호출 및 범위 초과 인덱스 검증 포함 | ✅ 완료 (`test_rhi_basic` timestamps 8B) |
| `ICommandQueue::submit` / `wait` / `waitIdle` / `completedValue` | `gfx::CommandQueue::submit` / `wait` / `waitIdle` / `completedValue` | **어댑터로 호환** | 빈 제출 시에도 단조 증가 타임라인 펜스 보장, 5000ms 유한 타임아웃 분할 대기 및 Readback 버퍼 자동 동기화 | ✅ 완료 (`test_rhi_basic`) |
| `ISwapChain::acquireNextTexture` / `present` / `resize` / `setVsync` | `gfx::Device::createSwapChain`, `gfx::SwapChain::acquireNextImage`, `currentBackbuffer`, `present`, `resize` | **어댑터로 호환** | `gfx::SwapChain::currentBackbuffer()`를 외부(`external = true`) `RhiTexture` 핸들로 등록하고 `present()`/`resize()` 전에 즉시 회수 | ✅ 완료 (`SandboxTests`, `SandboxClient`) |

---

## 3. 포맷 및 상태 열거형 매핑표

### 3.1 `sbx::rhi::Format` ↔ `gfx::Format` / `gfx::VertexFormat`

| Sandbox `Format` | 텍스처 (`gfx::Format`) | 정점 속성 (`gfx::VertexFormat`) | 호환 상태 | 비고 |
|---|---|---|---|---|
| `Format::Unknown` | 미지원 (`InvalidArgument`) | 미지원 (`InvalidArgument`) | **직접 호환** | 유효성 검사용 |
| `Format::RGBA8Unorm` | `gfx::Format::RGBA8UNorm` | `gfx::VertexFormat::UByte4Normalized` | **직접 호환** | 기본 렌더 타깃·텍스처·ImGui 색상 정점 |
| `Format::RGBA8UnormSrgb` | `gfx::Format::RGBA8UNormSrgb` | 미지원 | **직접 호환** | sRGB 텍스처 |
| `Format::BGRA8Unorm` | `gfx::Format::BGRA8UNorm` | 미지원 | **직접 호환** | 기본 스왑체인 백버퍼 |
| `Format::BGRA8UnormSrgb` | `gfx::Format::BGRA8UNormSrgb` | 미지원 | **직접 호환** | sRGB 스왑체인 |
| `Format::R8Unorm` | `gfx::Format::R8UNorm` | 미지원 | **직접 호환** | 단일 채널 마스크/폰트 |
| `Format::R16Uint` | 미지원 | 미지원 | **Graphics SDK 기능 확장 필요** | `TerrainPass` 타일맵 `Texture2D<uint>` (`SDK-REQ-002`) |
| `Format::R32Float` | `gfx::Format::R32Float` | `gfx::VertexFormat::Float` | **직접 호환** | 단정밀도 실수 |
| `Format::RG32Float` | `gfx::Format::RG32Float` | `gfx::VertexFormat::Float2` | **직접 호환** | 2D 위치/UV 정점 |
| `Format::RGB32Float` | 미지원 | `gfx::VertexFormat::Float3` | **직접 호환** | 3D 정점 속성 전용 |
| `Format::RGBA32Float` | `gfx::Format::RGBA32Float` | `gfx::VertexFormat::Float4` | **직접 호환** | HDR 타깃 및 `ColorVertex` / `SpriteInstanceGpu` |
| `Format::R32Uint` | 미지원 | 미지원 | **Graphics SDK 기능 확장 필요** | `SpriteInstanceGpu` `page` · `flags` 정점 속성 (`SDK-REQ-002`) |
| `Format::D32Float` | `gfx::Format::D32Float` | 미지원 | **직접 호환** | 기본 깊이 버퍼 |
| `Format::D24UnormS8Uint` | `gfx::Format::D24UNormS8UInt` | 미지원 | **직접 호환** | 깊이+스텐실 버퍼 |

### 3.2 `sbx::platform::NativeWindowHandle` ↔ `gfx::NativeSurfaceDesc`

| 플랫폼 / 윈도우 백엔드 | Sandbox `NativeWindowHandle::Backend` | SDK `gfx::SurfaceKind` | 호환 상태 | 비고 |
|---|---|---|---|---|
| Windows Win32 | `Backend::Win32` (`hwnd`, `hinstance`) | `gfx::SurfaceKind::Win32` (`hwnd`, `hinstance`) | **직접 호환** | Windows D3D12 / Vulkan 완벽 동작 |
| macOS Cocoa / Metal | `Backend::Cocoa` (`metalLayer`) | `gfx::SurfaceKind::MetalLayer` (`layer`) | **직접 호환** | `CAMetalLayer*` 전달 |
| Linux X11 (Xlib) | `Backend::Xlib` (`xDisplay`, `xWindow`) | `gfx::SurfaceKind::Xcb` (`connection`, `window`) | **플랫폼별 미지원** | Sandbox Linux 창은 Xlib(`Display*`) 또는 Wayland를 사용하나 SDK는 XCB만 지원 (`SDK-REQ-008`) |
| Linux Wayland | `Backend::Wayland` (`wlDisplay`, `wlSurface`) | 미지원 | **플랫폼별 미지원** | `SDK-REQ-008` |
| Headless | `Backend::Headless` | 스왑체인 불필요 (오프스크린 RHI) | **직접 호환** | `sbx_render_tests` 및 헤드리스 검증에서 사용 |
