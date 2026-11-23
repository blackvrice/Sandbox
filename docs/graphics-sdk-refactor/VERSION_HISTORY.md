# 리팩토링 버전 이력 (`VERSION_HISTORY.md`)

본 문서는 Graphics SDK 기반 RHI 리팩토링에서 완료된 각 버전(`R0.1` ~ `R0.24`)의 완료 일자, 변경 내역 및 검증 증적을 기록합니다.

---

## 완료된 버전 목록

### `R0.1` — 기존 그래픽 시스템 분석 및 호환성 매트릭스 (2026-11-22)
- **상태**: ✅ 완료
- **변경 요약**:
  - Sandbox `render/rhi/`, `render/dx12/`, `render/vulkan/`, `render/metal/`, `render/renderer/`, `render/imgui/` 전체 인터페이스 및 수명 규칙 분석
  - `GraphicsSDK::Core` 공개 헤더 13종 및 `examples/sandbox_adapter/` 대조 분석
  - `API_COMPATIBILITY.md` 및 `SDK_REQUIREMENTS.md`(`SDK-REQ-001`~`008`) 작성

### `R0.2` — 기존 렌더링 성능 기준선 측정 (2026-11-22)
- **상태**: ✅ 완료
- **변경 요약**:
  - Windows 11 + NVIDIA GeForce GTX 1660 SUPER + WARP 환경에서 Legacy D3D12 / Vulkan 기준선 실측
  - `sbx_render_tests` 31/31 통과(D3D12 WARP, D3D12 HW, Vulkan HW), `sbx_bench --only render`(10k: 0.235 ms, 50k: 1.218 ms), `SandboxClient` 1920×1080 `--vsync off` (`ecosystem_small`: D3D12 2843.8 FPS / Vulkan 3631.9 FPS, `ecosystem_10k`: D3D12 604.3 FPS / Vulkan 623.7 FPS) 측정값을 `BENCHMARK_BASELINE.md`에 기록

### `R0.3` ~ `R0.4` — 리팩토링 Worktree, CMake 옵션 및 패키지 연결 (2026-11-22)
- **상태**: ✅ 완료
- **변경 파일**: `cmake/SbxOptions.cmake`, `render/CMakeLists.txt`, `tools/check_includes.py`
- **변경 요약**:
  - `SBX_USE_GRAPHICS_SDK` 옵션(기본값 `OFF`) 추가
  - `SBX_USE_GRAPHICS_SDK=ON`일 때 `find_package(GraphicsSDK CONFIG REQUIRED)` 및 `SandboxRender` `PRIVATE` 링크 구성
  - `tools/check_includes.py`에 `("render/graphics_sdk/", set())` 경계 규칙 등록

### `R0.5` — RHI Type 및 Error Mapping 구현 (2026-11-22)
- **상태**: ✅ 완료
- **변경 파일**: `render/graphics_sdk/GraphicsSdkTypes.{hpp,cpp}`, `tests/unit/render/test_rhi_helpers.cpp`
- **변경 요약**:
  - `BackendType`, `Format`, `MemoryType`, `BufferUsage`, `TextureUsage`, `ResourceState`, `LoadOp`, `StoreOp`, `Filter`, `AddressMode`, `CompareOp`, `NativeWindowHandle`, `gfx::ErrorCode` ↔ `sbx::Error` 1:1 매핑 구현 및 `verifyAdapterTypeMappings()` 단위 테스트 통과

### `R0.6` ~ `R0.11` — Phase 1 Graphics SDK 어댑터 구현 (2026-11-22)
- **상태**: ✅ 완료
- **변경 파일**:
  - `render/graphics_sdk/GraphicsSdkResource.hpp`
  - `render/graphics_sdk/GraphicsSdkPipeline.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkCommandQueue.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkSwapChain.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkCommandList.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkFactory.cpp`
  - `render/rhi/Backends.hpp`, `render/rhi/RenderDeviceFactory.cpp`
- **변경 요약**:
  - `IRenderDevice`, `ICommandList`, `ICommandQueue`, `ISwapChain` 어댑터 전체 구현
  - Stage-Before-Record 업로드 동기화 및 Timestamp Scope 변환 구현
  - `PipelineValidation` 오프라인 리플렉션 대조 검증 연결

### `R0.12` ~ `R0.15` — Phase 2 리소스 수명, FrameSlot CommandPool, 슬롯별 UploadRing 구현 (2026-11-22)
- **상태**: ✅ 완료
- **변경 파일**: `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}`, `render/graphics_sdk/GraphicsSdkCommandQueue.cpp`, `render/graphics_sdk/GraphicsSdkCommandList.cpp`
- **변경 요약**:
  - `HandlePool` 세대형 핸들 및 `DeferredDestructionQueue` 지연 해제 통합
  - `FrameSlot`별 `gfx::CommandPool` 및 `gfx::CommandList` 소유권 분리(커맨드 리스트 파괴 후 dangling 포인터 방지)
  - `FrameSlot`별 SDK Upload 링 버퍼 분리로 Vulkan 12프레임 링 감기(wrap-around) `Upload buffer busy` 제거
  - `GraphicsSdkCommandQueue::wait()`의 5000ms 유한 타임아웃 분할 대기 및 드라이버 펜스 레이스 방어

### `R0.16` ~ `R0.18` — Phase 3 셰이더 컴파일, 동적 샘플러 캐시, Push Constants 및 토폴로지/컬링 전환 (2026-11-23)
- **상태**: ✅ 완료
- **변경 파일**:
  - `tools/shader/gen_gfxsdk_spirv.py`
  - `render/graphics_sdk/GraphicsSdkSpirvData.hpp`
  - `render/graphics_sdk/GraphicsSdkShaders.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkTypes.cpp`
  - `render/graphics_sdk/GraphicsSdkPipeline.hpp`
  - `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkCommandList.cpp`
  - `render/CMakeLists.txt`
- **변경 요약**:
  - 7종 내장 셰이더(14개 스테이지)를 D3D12(SM5.1 `gfx::compileHlsl()` 캐시), Vulkan(단일 셋 SPIR-V 1.3 + `OpTypeImage` Depth 정규화), Metal(MSL)로 변환하는 `GraphicsSdkShaders` 구현 (`SDK-REQ-001` 어댑터 해소)
  - `Format::R16Uint` → `gfx::Format::R16UNorm` 및 정점 `Format::R32Uint` → `gfx::VertexFormat::Float`(`asuint()` 복원) 매핑 (`SDK-REQ-002` 어댑터 해소)
  - 바인딩된 `RhiSampler` `(filter, address)` 조합별 PSO 변형 캐시(`resolvePipelineVariant`) 및 슬롯별 전용 `pushBuffer`(256 KB) 도입 (`SDK-REQ-005`, `006` 어댑터 해소)
  - 공용 6-인덱스 쿼드 스트립 버퍼(`m_quadStripIndexBuffer`) + 동적 스트립→리스트 변환, 2D CPU 와인딩 컬링(`tryDrawWithCpuCulling`) 구현 (`SDK-REQ-003`, `004` 어댑터 해소)

### `R0.19` ~ `R0.24` — Phase 4~5 상위 렌더러 패스, ImGui 및 실기 클라이언트 검증 (2026-11-23)
- **상태**: ✅ 완료
- **변경 파일**:
  - `render/graphics_sdk/GraphicsSdkResource.hpp`
  - `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkCommandList.cpp`
  - `tests/render/CMakeLists.txt`
- **변경 요약**:
  - `prepareAndReplay()`의 렌더 패스 진입 전 Vertex/Index/Constant 버퍼 상태 사전 전이 스캔 구현
  - 1-layer 스프라이트 아틀라스(`"sprite atlas"`)의 `TextureDimension::Texture2DArray` 생성 및 Vulkan 부분 업로드 전 1회 `fullSlice` 제로 초기화 구현
  - GraphicsSDK D3D12/Vulkan의 `DestBlendAlpha = ZERO` 불일치(`SDK-REQ-009`)에 대한 불투명 RT Readback 알파 불변량 보정(`applyOpaqueAlphaFixup`) 구현
  - 정적 매핑 Upload 버퍼의 `lastUploadedShadow` 변경 감지 도입으로 `SandboxClient`의 `Cannot overwrite a buffer in flight` 제거
  - `tests/render/CMakeLists.txt`에서 `SBX_RENDER_TEST_FILTER` 완전 제거 → D3D12 WARP, D3D12 HW, Vulkan HW(+VVL) 모두 **31/31 PASS (527 assertions)**, `SandboxClient` Debug Layer 0경고/0오류 검증 완료

### `R0.25` ~ `R0.27` — Phase 6 멀티스레드 기록, 다중 Queue, 핫 패스 CPU/Upload 최적화 및 1%/0.1% Low 계측 (2026-11-23)
- **상태**: ✅ 완료
- **변경 파일**:
  - `render/rhi/RhiTypes.hpp`
  - `render/graphics_sdk/GraphicsSdkTypes.cpp`
  - `render/graphics_sdk/GraphicsSdkCommandQueue.{hpp,cpp}`
  - `render/graphics_sdk/GraphicsSdkCommandList.cpp`
  - `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}`
  - `tests/render/test_rhi_basic.cpp`
  - `apps/client/Application.{hpp,cpp}`, `apps/client/main.cpp`
- **변경 요약**:
  - `GraphicsSdkCommandList`의 `m_recordedPasses`/`m_recordedBindings` 풀 재사용, `m_passBuffersScratch`/`m_lastBufferStates` 재사용, 파이프라인 지연 발행, 중복 버퍼 `transition()` 제거, 정적 타임스탬프 이름 테이블, `m_recordMutex` 스레드 안전화 구현
  - `QueueType::Copy`/`Compute` 확장, `GraphicsSdkCommandQueue` 전용 큐 바인딩 + 16개 인라인 스택 배치 제출, `FrameSlot`별 `copyPool`/`computePool` 분리 구현
  - `allocateUpload()` 4KB 패딩 병합으로 프레임당 `Buffer::write()` 호출 최소화(D3D12 `small` +14.4% FPS, Vulkan `10k` +25.6% FPS)
  - `Application::totalTimings()`에 O(N) `nth_element` 기반 1% Low / 0.1% Low FPS 분포 계측 추가
  - 4스레드 병렬 커맨드 기록 배치 제출 및 Copy Queue 전송 GPU 테스트 2종 추가 → Legacy & GraphicsSDK (D3D12 WARP / D3D12 HW / Vulkan HW+VVL) 모두 **33/33 PASS (559 assertions)** 통과
