# Graphics SDK 기반 RHI 리팩토링 세분화 로드맵 (`REFACTORING_ROADMAP.md`)

본 문서는 Sandbox의 자체 그래픽 백엔드를 Graphics SDK 기반으로 전환하기 위한 `R0.1` ~ `R0.35` 버전별 개발 계획과 하위 세부 단계(`R0.x.1` ~ `R0.x.3`)를 정의합니다.

---

## 1. 상태 표기 규칙

| 표기 | 의미 |
|---|---|
| `✅ 완료` | 구현, 빌드, 단위/통합 테스트, 런타임 검증, 문서 갱신까지 모두 완료됨 |
| `🟡 진행 중` | 일부 구현 또는 하위 단계가 진행 중임 |
| `⬜ 대기` | 아직 착수하지 않은 다음 단계 |
| `❌ 실패` | 테스트 또는 빌드 실패 (수정 필요) |
| `⚠️ 환경 부족` | 코드는 준비되었으나 해당 OS/물리 GPU 하드웨어가 현재 환경에 없어 실기 검증이 보류됨 |
| `🚫 미지원` | 현 단계에서 지원하지 않기로 결정된 범위 |

---

## 2. 전체 버전 로드맵 (`R0.1` ~ `R0.35`)

| 버전 | Phase | 개발 목표 | 주요 변경 파일 | 의존 버전 | 검증 명령 | 상태 |
|---|---|---|---|---|---|---|
| `R0.1` | Phase 0 | 기존 그래픽 시스템 분석 및 호환성 매트릭스 | `docs/graphics-sdk-refactor/API_COMPATIBILITY.md`, `SDK_REQUIREMENTS.md`, `phases/PHASE0_ANALYSIS.md` | - | `python tools/check_docs.py --root .` | ✅ 완료 |
| `R0.2` | Phase 0 | 기존 렌더링 성능 기준선 측정 | `docs/graphics-sdk-refactor/BENCHMARK_BASELINE.md` | `R0.1` | `sbx_render_tests`, `sbx_bench --only render`, `SandboxClient` | ✅ 완료 |
| `R0.3` | Phase 1 | 리팩토링 Worktree 및 CMake 옵션 구축 | `cmake/SbxOptions.cmake`, `render/CMakeLists.txt`, `tools/check_includes.py` | `R0.2` | `cmake --preset windows-msvc` | ✅ 완료 |
| `R0.4` | Phase 1 | Graphics SDK CMake 패키지 연결 | `render/CMakeLists.txt` (`find_package(GraphicsSDK CONFIG REQUIRED)`) | `R0.3` | `cmake --build build/windows-msvc-gfxsdk` | ✅ 완료 |
| `R0.5` | Phase 1 | RHI Type 및 Error Mapping 구현 | `render/graphics_sdk/GraphicsSdkTypes.{hpp,cpp}`, `tests/unit/render/test_rhi_helpers.cpp` | `R0.4` | `SandboxTests --test-suite=render` | ✅ 완료 |
| `R0.6` | Phase 1 | `GraphicsSdkDevice` 및 Adapter 선택 구현 | `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}`, `GraphicsSdkFactory.cpp`, `render/rhi/RenderDeviceFactory.cpp` | `R0.5` | `sbx_render_tests --test-case="*device reports*"` | ✅ 완료 |
| `R0.7` | Phase 1 | `GraphicsSdkSwapChain` 및 Surface 연동 | `render/graphics_sdk/GraphicsSdkSwapChain.{hpp,cpp}` | `R0.6` | `SandboxTests --test-case="*verifyAdapterTypeMappings*"` | ✅ 완료 |
| `R0.8` | Phase 1 | `GraphicsSdkCommandQueue` 및 Fence 동기화 | `render/graphics_sdk/GraphicsSdkCommandQueue.{hpp,cpp}` | `R0.6` | `sbx_render_tests --rhi vulkan --debug` | ✅ 완료 |
| `R0.9` | Phase 1 | `GraphicsSdkCommandList` 및 RenderPass/Copy/Timestamp 구현 | `render/graphics_sdk/GraphicsSdkCommandList.{hpp,cpp}` | `R0.8` | `sbx_render_tests --test-case="*clear*,*timestamps*"` | ✅ 완료 |
| `R0.10` | Phase 1 | Buffer, Texture, Sampler 어댑터 및 Stage-Before-Record 구현 | `render/graphics_sdk/GraphicsSdkResource.hpp`, `GraphicsSdkDevice.cpp` | `R0.9` | `sbx_render_tests --test-case="*upload:*,*copy:*"` | ✅ 완료 |
| `R0.11` | Phase 1 | Shader, BindGroup, Pipeline 어댑터 및 오프라인 리플렉션 검증 | `render/graphics_sdk/GraphicsSdkPipeline.{hpp,cpp}` | `R0.10` | `sbx_render_tests --test-case="*validation: mismatched*"` | ✅ 완료 |
| `R0.12` | Phase 2 | Generational Handle 및 리소스 수명 관리 고도화 | `render/graphics_sdk/GraphicsSdkDevice.cpp` | `R0.11` | `sbx_render_tests --test-case="*lifetime*"` | ✅ 완료 |
| `R0.13` | Phase 2 | Frame-based Deferred Destruction 고도화 | `render/graphics_sdk/GraphicsSdkDevice.cpp`, `render/rhi/DeferredDestruction.hpp` | `R0.12` | `sbx_render_tests --rhi dx12 --debug` | ✅ 완료 |
| `R0.14` | Phase 2 | FrameSlot 및 CommandPool 리셋 안정화 | `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}` | `R0.13` | `sbx_render_tests --rhi vulkan --debug` | ✅ 완료 |
| `R0.15` | Phase 2 | 슬롯별 Upload Ring 및 Zero-copy Staging 최적화 | `render/graphics_sdk/GraphicsSdkDevice.cpp` | `R0.14` | `sbx_render_tests --test-case="*ring wrap-around*"` | ✅ 완료 |
| `R0.16` | Phase 3 | Shader Asset 및 Offline Reflection (`SDK-REQ-001`) 연동 | `render/graphics_sdk/GraphicsSdkShaders.{hpp,cpp}`, `GraphicsSdkSpirvData.hpp`, `tools/shader/gen_gfxsdk_spirv.py` | `R0.15` | `sbx_render_tests --test-case="*triangle*"` | ✅ 완료 |
| `R0.17` | Phase 3 | BindGroupLayout, 동적 Sampler(`SDK-REQ-006`), PushConstant(`SDK-REQ-005`) | `render/graphics_sdk/GraphicsSdkDevice.cpp`, `GraphicsSdkCommandList.cpp` | `R0.16` | `sbx_render_tests --test-case="*texture*,*push*"` | ✅ 완료 |
| `R0.18` | Phase 3 | Pipeline State (`SDK-REQ-003` Topology, `SDK-REQ-004` Cull) 및 Cache 연동 | `render/graphics_sdk/GraphicsSdkDevice.cpp`, `GraphicsSdkCommandList.cpp` | `R0.17` | `sbx_render_tests --test-case="*culling*"` | ✅ 완료 |
| `R0.19` | Phase 4 | `Renderer` 및 FrameGraph/RenderPass 전환 (버퍼 상태 사전 전이 포함) | `render/graphics_sdk/GraphicsSdkCommandList.cpp` | `R0.18` | `sbx_render_tests` | ✅ 완료 |
| `R0.20` | Phase 4 | `SpriteBatcher` 및 Instanced Rendering (`SDK-REQ-002` `R32Uint`) 전환 | `render/graphics_sdk/GraphicsSdkTypes.cpp`, `GraphicsSdkShaders.cpp` | `R0.19` | `sbx_render_tests --test-case="*sprite*,*batch*"` | ✅ 완료 |
| `R0.21` | Phase 4 | `TerrainPass` (`SDK-REQ-002` `R16Uint`) 및 월드 렌더링 전환 | `render/graphics_sdk/GraphicsSdkTypes.cpp`, `GraphicsSdkCommandList.cpp` | `R0.20` | `sbx_render_tests --test-case="*terrain*,*gpu timings*"` | ✅ 완료 |
| `R0.22` | Phase 4 | `DebugDraw` 및 Overlay(`GridPass`, `LinePass`) 렌더링 전환 (`SDK-REQ-009` 보정) | `render/graphics_sdk/GraphicsSdkDevice.cpp`, `GraphicsSdkCommandList.cpp` | `R0.21` | `sbx_render_tests --test-case="*overlay*"` | ✅ 완료 |
| `R0.23` | Phase 5 | `ImGuiRenderer` 전환 | `render/graphics_sdk/GraphicsSdkShaders.cpp`, `tests/render/CMakeLists.txt` | `R0.22` | `sbx_render_tests --test-case="*imgui*"` | ✅ 완료 |
| `R0.24` | Phase 5 | `SandboxClient` / Studio 스왑체인 통합 및 정적 Upload 버퍼 동기화 검증 | `render/graphics_sdk/GraphicsSdkDevice.cpp` | `R0.23` | `SandboxClient --world ecosystem_small --frames 300` | ✅ 완료 |
| `R0.25` | Phase 6 | Multithreaded Command Recording 및 핫 패스 힙 할당 제거 | `render/graphics_sdk/GraphicsSdkCommandList.cpp`, `GraphicsSdkDevice.cpp`, `tests/render/test_rhi_basic.cpp` | `R0.24` | `sbx_render_tests --test-case="*multithread*"` | ✅ 완료 |
| `R0.26` | Phase 6 | 다중 Queue (`Graphics`/`Copy`/`Compute`) 및 `CommandPool` 분리 | `render/rhi/RhiTypes.hpp`, `render/graphics_sdk/GraphicsSdkCommandQueue.{hpp,cpp}`, `GraphicsSdkDevice.{hpp,cpp}` | `R0.25` | `sbx_render_tests --test-case="*queues*"` | ✅ 완료 |
| `R0.27` | Phase 6 | UploadRing 4KB 패딩 병합 및 1% Low / 0.1% Low FPS 분포 계측 | `render/graphics_sdk/GraphicsSdkDevice.cpp`, `apps/client/Application.{hpp,cpp}`, `apps/client/main.cpp` | `R0.26` | `SandboxClient --world ecosystem_10k` | ✅ 완료 |
| `R0.28` | Phase 7 | 멀티 플랫폼 Surface 매핑(Win32/Xcb/Cocoa/Xlib/Wayland) 및 Tearing PresentMode | `render/graphics_sdk/GraphicsSdkTypes.cpp`, `GraphicsSdkSwapChain.cpp`, `PLATFORM_SUPPORT.md` | `R0.27` | `SandboxTests --test-suite=render` | ✅ 완료 |
| `R0.29` | Phase 7 | GPU Timestamp · 메모리 예산(`queryMemoryBudget`) · `checkStatus()` 통합 | `render/graphics_sdk/GraphicsSdkDevice.{hpp,cpp}` | `R0.28` | `sbx_render_tests --test-case="*gpu timings*"` | ✅ 완료 |
| `R0.30` | Phase 7 | Device Lost 감지 및 SwapChain Resize/최소화(0×0)/VSync 토글 안정화 | `render/graphics_sdk/GraphicsSdkSwapChain.{hpp,cpp}`, `tests/render/test_rhi_basic.cpp` | `R0.29` | `sbx_render_tests --test-case="*swapchain*"` | ✅ 완료 |
| `R0.31` | Phase 8 | Graphics UI 연동 호스트 브리지(`GraphicsSdkUiBridge`) 구축 | `render/graphics_sdk/GraphicsSdkUiBridge.{hpp,cpp}`, `GraphicsSdkCommandList.{hpp,cpp}` | `R0.30` | `sbx_render_tests --test-case="*ui_bridge*"` | ✅ 완료 |
| `R0.32` | Phase 8 | World Pass → Native Graphics UI → Dear ImGui 단일 커맨드 리스트 공존 합성 검증 | `tests/render/test_imgui.cpp`, `tests/render/CMakeLists.txt` | `R0.31` | `sbx_render_tests --test-case="*ui_bridge*"` | ✅ 완료 |
| `R0.33` | Phase 9 | 전체 100개 CTest 최종 재검증 및 전체 시나리오 최종 벤치마크 확정 | `docs/graphics-sdk-refactor/BENCHMARK_COMPARISON.md`, `TEST_REPORT.md` | `R0.32` | `ctest -C RelWithDebInfo -j 4` (99/99 PASS) | ✅ 완료 |
| `R0.34` | Phase 9 | `SBX_USE_GRAPHICS_SDK` 기본값 전환 검토 및 병합 계획(`MERGE_PLAN.md`) 확정 | `docs/graphics-sdk-refactor/MERGE_PLAN.md`, `KNOWN_ISSUES.md` | `R0.33` | Gate 2 달성 확인 + `origin/master` 동기화 점검 | ✅ 완료 |
| `R0.35` | Phase 10 | Legacy 백엔드(`render/dx12`, `vulkan`, `metal`) 정리 및 최종 병합 | `render/CMakeLists.txt`, `docs/` | `R0.34` | **사용자 최종 승인 후 진행** | ⏸️ 승인 대기 |

---

## 3. 완료된 버전(`R0.1` ~ `R0.34`)의 하위 세부 단계 체크리스트

### R0.1 — 기존 그래픽 시스템 분석 및 호환성 매트릭스
- [x] `R0.1.1`: `render/rhi/`, `render/dx12/`, `render/vulkan/`, `render/metal/`, `render/renderer/`, `render/imgui/` 전수 분석
- [x] `R0.1.2`: `GraphicsSDK::Core` 13개 공개 헤더 및 `examples/sandbox_adapter/` 대조 분석
- [x] `R0.1.3`: `API_COMPATIBILITY.md` 및 `SDK_REQUIREMENTS.md` (`SDK-REQ-001` ~ `008`) 작성

### R0.2 — 기존 렌더링 성능 기준선 측정
- [x] `R0.2.1`: `sbx_render_tests` D3D12 WARP, D3D12(GTX 1660 SUPER), Vulkan(GTX 1660 SUPER) 31/31 통과 및 GPU 타이밍 실측
- [x] `R0.2.2`: `sbx_bench --only render` 10k/50k 스프라이트 배치 CPU 시간 실측
- [x] `R0.2.3`: `SandboxClient` 1920×1080 `--vsync off` `ecosystem_small` / `ecosystem_10k` (D3D12 & Vulkan) FPS 및 CPU 타이밍 실측

### R0.3 ~ R0.4 — 리팩토링 Worktree, CMake 옵션 및 패키지 연결
- [x] `R0.3.1`: `D:/Game/Sandbox-GraphicsSDK` Worktree (`refactor/graphics-sdk` 브랜치) 분리 구축
- [x] `R0.3.2`: `cmake/SbxOptions.cmake`에 `SBX_USE_GRAPHICS_SDK` (기본값 `OFF`) 추가 및 `tools/check_includes.py` 경계 규칙 등록
- [x] `R0.4.1`: `render/CMakeLists.txt`에서 `find_package(GraphicsSDK CONFIG REQUIRED)` 및 `SandboxRender` `PRIVATE` 링크 구성

### R0.5 ~ R0.11 — Phase 1 Graphics SDK 어댑터 구현
- [x] `R0.5.1`: `GraphicsSdkTypes.{hpp,cpp}` 포맷/상태/서피스/오류 매핑 및 `verifyAdapterTypeMappings()` 단위 테스트 통과
- [x] `R0.6.1`: `GraphicsSdkDevice` 및 `GraphicsSdkFactory` 구현 (`BackendType::Auto`, 하드웨어/WARP 선택, Validation 폴백)
- [x] `R0.7.1`: `GraphicsSdkSwapChain` 구현 (Win32/Cocoa/X11 Surface 매핑, 외부 백버퍼 등록/회수, Resize)
- [x] `R0.8.1`: `GraphicsSdkCommandQueue` 구현 (타임라인 펜스 제출, 유한 타임아웃 분할 대기, Readback 자동 동기화)
- [x] `R0.9.1`: `GraphicsSdkCommandList` 구현 (Stage-Before-Record 지연 재생, RenderPass Clear/MRT, Copy, Timestamp Scope 변환)
- [x] `R0.10.1`: `GraphicsSdkBuffer`, `GraphicsSdkTexture`, `GraphicsSdkSampler` 및 256B/512B 복사 정렬 검증 구현
- [x] `R0.11.1`: `GraphicsSdkShader`, `GraphicsSdkBindGroupLayout`, `GraphicsSdkBindGroup`, `GraphicsSdkPipeline` 및 오프라인 리플렉션 검증 구현

### R0.12 ~ R0.15 — Phase 2 리소스 수명 및 프레임 컨텍스트 구현 (Phase 1과 통합 완료)
- [x] `R0.12.1`: `HandlePool` 기반 세대형 핸들 및 즉시 무효화(`alive == false`) 검증 통과
- [x] `R0.13.1`: `DeferredDestructionQueue` 기반 프레임 지연 해제 및 `waitIdle()` 시 확정 회수(`pendingDestructions == 0`) 검증 통과
- [x] `R0.14.1`: `FrameSlot`별 `gfx::CommandPool` 및 `CommandList` 재사용, Vulkan 드라이버 펜스 레이스 방어 완료
- [x] `R0.15.1`: `FrameSlot`별 SDK Upload 버퍼 + 공유 `UploadRing` 오프셋 관리로 12프레임 링 감기(wrap-around) 테스트 D3D12/Vulkan 모두 통과

### R0.16 ~ R0.18 — Phase 3 셰이더, 샘플러, Push Constant, 토폴로지/컬링 어댑터 구현
- [x] `R0.16.1`: `GraphicsSdkShaders.{hpp,cpp}` (D3D12 `gfx::compileHlsl` SM5.1 DXBC 캐시 + Vulkan SPIR-V 1.3 내장 데이터) 및 `tools/shader/gen_gfxsdk_spirv.py` 작성
- [x] `R0.17.1`: 동적 `RhiSampler` 조합별 `gfx::PipelinePtr` 변형 캐시(`resolvePipelineVariant`) 및 프레임별 Push Constant 전용 슬롯 버퍼(`FrameSlot::pushBuffer`) 구현
- [x] `R0.18.1`: `TriangleStrip` → 공용/동적 인덱스 `TriangleList` 자동 변환 및 `CullMode::Back`/`Front` CPU 삼각형 와인딩 필터링 구현

### R0.19 ~ R0.24 — Phase 4~5 전체 렌더 패스, ImGui 및 SandboxClient 통합 검증
- [x] `R0.19.1`: `prepareAndReplay()` 렌더 패스 진입 전 Vertex/Index/ConstantBuffer 상태 자동 전이 및 Vulkan 부분 업로드 1회 `fullSlice` 0-초기화 구현
- [x] `R0.20.1`: `Format::R32Uint` → `gfx::VertexFormat::Float` + `asuint()` 및 `sprite atlas` `Texture2DArray` 매핑으로 `SpriteBatcher` 전체 테스트(`sprite`, `batch_1k`, `assets` 등) 통과
- [x] `R0.21.1`: `Format::R16Uint` → `gfx::Format::R16UNorm` + `(uint)(raw * 65535.0 + 0.5)`로 `TerrainPass` 및 `gpu timings` 통과
- [x] `R0.22.1`: `GridPass`, `LinePass` (`overlay` 기준 이미지) 및 `SDK-REQ-009` 불투명 타깃 Readback 알파 불변식 보정 완료
- [x] `R0.23.1`: `ImGuiRenderer` (`imgui_basic` 기준 이미지 및 1.92 동적 폰트 갱신/클리핑/해제) 통과 및 `tests/render/CMakeLists.txt` 전체 31개 GPU 테스트 활성화
- [x] `R0.24.1`: `SandboxClient --world ecosystem_small` D3D12 (`--rhi-debug` 경고 0 · 오류 0) 및 Vulkan 실기 구동 검증, 정적 Upload 버퍼 dirty-check 동기화 완료

### R0.25 ~ R0.27 — Phase 6 멀티스레드 기록, 다중 Queue, CPU/Upload 최적화 및 프레임 분포 계측
- [x] `R0.25.1`: `GraphicsSdkCommandList` 풀 재사용(`m_recordedPasses`, `m_recordedBindings`, `m_passBuffersScratch`, `m_lastBufferStates`), 파이프라인 지연 발행, `m_recordMutex` 스레드 안전화 및 4스레드 병렬 기록 배치 제출 GPU 테스트 통과
- [x] `R0.26.1`: `QueueType::Copy`/`Compute` 확장, `GraphicsSdkCommandQueue` 전용 큐 바인딩, `FrameSlot`별 `copyPool`/`computePool` 분리 및 Copy Queue 전송 GPU 테스트(D3D12 WARP/HW, Vulkan HW) 통과
- [x] `R0.27.1`: `allocateUpload()` 4KB 패딩 병합으로 프레임당 `Buffer::write()` 호출 최소화(Vulkan `ecosystem_10k` +25.6% FPS 향상) 및 `Application` 1% Low / 0.1% Low FPS 분포 계측 추가

### R0.28 ~ R0.30 — Phase 7 멀티 플랫폼 매핑, 프로파일링/메모리 예산, SwapChain/Device Lost 안정화
- [x] `R0.28.1`: `verifyAdapterTypeMappings()`에 6종 Surface(Win32/Xcb/Cocoa/Xlib/Wayland/None) · 3종 QueueType 검증 확장 및 `PresentMode::Tearing` 자동 선택 연동 (D3D12 1% Low 173.6 FPS, Vulkan 0.1% Low 430.3 FPS 달성)
- [x] `R0.29.1`: `GraphicsSdkDevice`에 `queryLocalMemoryUsageBytes()` (`queryMemoryBudget`), `checkDeviceStatus()` (`checkStatus`), `markDeviceLost()` 통합
- [x] `R0.30.1`: `GraphicsSdkSwapChain` 최소화(`0×0`) 건너뛰기, Vulkan 표면 크기 정합(`surfaceMetrics`), `InvalidState` 1회 `syncExtent()` 자동 복구, 런타임 `setVsync()` 재생성 및 34/34 GPU 테스트(D3D12 WARP/HW, Vulkan HW+VVL, Legacy D3D12) 통과

### R0.31 ~ R0.32 — Phase 8 Graphics UI 연동 브리지 및 ImGui 공존 구조
- [x] `R0.31.1`: `GraphicsSdkUiBridge.{hpp,cpp}` (`queryGraphicsSdkDevice`, `resolveGraphicsSdkTarget`, `recordNativeUiPass`) 및 `GraphicsSdkCommandList::enqueueNativeCallback` 구현 (순환 의존성 0, `Stage-Before-Record` 순서 보장)
- [x] `R0.32.1`: `tests/render/test_imgui.cpp`에 단일 커맨드 리스트 · 단일 타깃 위 `World Pass → Native GraphicsSDK UI Pass → Dear ImGui` 공존 합성 픽셀 검증 테스트 추가 → D3D12 WARP/HW, Vulkan HW+VVL 모두 **35/35 PASS (591 assertions)** 통과

### R0.33 ~ R0.34 — Phase 9 전체 최종 검증 및 기본 백엔드 전환 검토
- [x] `R0.33.1`: `build/windows-msvc-gfxsdk` (`RelWithDebInfo`) 전체 100개 CTest 최종 재검증 (**99/99 실행 테스트 100% PASS**, `render_tests_warp` 35/35 · 결정론 16/16 · 경계 3/3 포함) 및 최종 벤치마크 비교표 확정
- [x] `R0.34.1`: `SBX_USE_GRAPHICS_SDK` 기본값 전환 검토 및 `MERGE_PLAN.md` / `KNOWN_ISSUES.md` 확정 (Gate 2 완료, Linux/macOS CI 실기 검증 전까지 기본값 `OFF` 유지 권고, `origin/master` 충돌 0 확인)
