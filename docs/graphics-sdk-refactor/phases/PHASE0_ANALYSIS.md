# Phase 0 — 기존 Sandbox 그래픽 시스템 정밀 분석 보고서 (`PHASE0_ANALYSIS.md`)

본 문서는 리팩토링 착수 전 Sandbox의 자체 그래픽 시스템(`render/rhi/`, `render/dx12/`, `render/vulkan/`, `render/metal/`, `render/renderer/`, `render/imgui/`)을 전수 조사하고 `GraphicsSDK::Core`(commit `9345c181`)와의 구조적 차이를 분석한 보고서입니다.

---

## 1. 기존 Sandbox 그래픽 아키텍처 요약

1. **공개 RHI 인터페이스 (`render/rhi/`)**:
   - `IRenderDevice`, `ICommandList`, `ICommandQueue`, `ISwapChain` 4개 순수 가상 인터페이스와 `Handle<Tag>` 세대형 핸들(`RhiBuffer`, `RhiTexture`, `RhiSampler`, `RhiShader`, `RhiBindGroupLayout`, `RhiBindGroup`, `RhiPipeline`)로 구성됩니다.
   - `RenderDevice.hpp`는 네이티브 그래픽 API 헤더를 일절 노출하지 않습니다 (`tools/check_includes.py`가 강제).
2. **백엔드별 중복 구현 규모**:
   - `render/dx12/`: `Dx12Device`, `Dx12CommandList`, `Dx12SwapChain`, `Dx12Pipeline`, `Dx12DescriptorHeap`, `Dx12DebugLayer`
   - `render/vulkan/`: `VkDevice`, `VkCommandList`, `VkSwapChain`, `VkPipeline`, `VkDescriptors`, `VkDebug`
   - `render/metal/`: `MtlDevice`, `MtlCommandList`, `MtlSwapChain`, `MtlPipeline` (Objective-C++ `.mm`)
   - 세 백엔드가 각각 독립적으로 `HandlePool`, `DeferredDestructionQueue`, `UploadRing`, `FrameSlot`, `TimestampReadback`을 반복 구현하고 있습니다.
3. **공통 헬퍼 (`render/rhi/`)**:
   - `HandlePool<Tag, T>`: `index` + `generation` 기반 O(1) 슬롯 풀.
   - `DeferredDestructionQueue<T>`: `(fenceValue, object)` FIFO 큐로 `completedValue()`가 지난 리소스만 소멸.
   - `UploadRing`: 프레임별 `(fence, head)` 구간을 기록하고 완료된 펜스까지 `tail`을 전진시키는 선형/순환 링 오프셋 계산기.
   - `PipelineValidation`: 오프라인 리플렉션(`ShaderReflection`)과 `GraphicsPipelineDesc` / `BindGroupLayoutDesc` / `BindGroupDesc`의 정합성을 백엔드 독립적으로 검사.

---

## 2. 렌더러 및 패스별 RHI 사용 특성

| 컴포넌트 | 주요 RHI 기능 및 포맷 | 토폴로지 / 셰이더 특성 |
|---|---|---|
| `Renderer` | `SwapChain`, `UploadRing`, `TimestampReadback`, `TextureAtlas`(2DArray `RGBA8Unorm`) | 풀스크린/쿼드 `TriangleStrip` |
| `SpriteBatcher` | 인스턴스 정점 버퍼(`UploadRing`), `Format::RGBA32Float` + `Format::R32Uint`(`page`, `flags`) | `TriangleStrip` (4 정점 × N 인스턴스), `sprite.hlsl` |
| `TerrainPass` | 타일맵 텍스처(`Format::R16Uint`), 팔레트 상수 버퍼 | 풀스크린 `TriangleStrip` (3 정점/4 정점), `terrain.hlsl` |
| `GridPass` | 카메라 상수 버퍼 + Push Constants | 풀스크린 `TriangleStrip`, `grid.hlsl` |
| `LinePass` (`DebugDraw`) | 선분 인스턴스 버퍼(`UploadRing`) | `TriangleStrip` (4 정점 × N 선분), `lines.hlsl` |
| `ImGuiRenderer` | 동적 정점/인덱스 업로드 버퍼(`map()`), 폰트 텍스처(`RGBA8Unorm`), 시저 클리핑 | `TriangleList` (`IndexFormat::Uint16`), `imgui.hlsl` |

---

## 3. Graphics SDK와의 핵심 계약 차이 및 해결 설계

1. **CPU 버퍼 쓰기 시점 (`Stage-Before-Record`)**:
   - Sandbox는 `allocateUpload()` / `map()`이 돌려준 포인터에 커맨드 기록 전후 언제든 메모리를 씁니다.
   - Graphics SDK는 커맨드 기록 시 버퍼의 `observedVersion`을 고정하므로, 어댑터에서 `RecordedOp` 지연 재생 + `flushPendingUploads()`로 제출 직전에 일괄 `gfx::Buffer::write()`를 수행하도록 설계했습니다.
2. **멀티 프레임 UploadRing과 버퍼 단위 In-Flight 펜스**:
   - Graphics SDK Vulkan 백엔드는 버퍼 단위로 `lastUse` 펜스를 추적하므로, `FrameSlot`마다 별도의 `gfx::Buffer` 업로드 링 인스턴스를 배치하여 프레임 간 간섭을 제거했습니다.
3. **셰이더 리플렉션 및 파이프라인 상태 격차**:
   - `SDK-REQ-001`(오프라인 리플렉션 / 커스텀 엔트리포인트 / 다중 Descriptor Set), `SDK-REQ-002`(`R16Uint`, `R32Uint`), `SDK-REQ-003`(`TriangleStrip`), `SDK-REQ-004`(`CullMode`), `SDK-REQ-005`(Push Constants), `SDK-REQ-006`(동적 Sampler) 6가지 핵심 요구사항을 도출하여 [../SDK_REQUIREMENTS.md](../SDK_REQUIREMENTS.md)에 명세했습니다.

---

## 4. 관련 문서
- [../API_COMPATIBILITY.md](../API_COMPATIBILITY.md)
- [../SDK_REQUIREMENTS.md](../SDK_REQUIREMENTS.md)
- [../BENCHMARK_BASELINE.md](../BENCHMARK_BASELINE.md)
