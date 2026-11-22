# 리팩토링 버전 이력 (`VERSION_HISTORY.md`)

본 문서는 Graphics SDK 기반 RHI 리팩토링에서 완료된 각 버전(`R0.1` ~ `R0.15`)의 완료 일자, 변경 내역 및 검증 증적을 기록합니다.

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
