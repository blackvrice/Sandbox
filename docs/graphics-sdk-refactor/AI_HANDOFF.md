# AI 인수인계 문서 (`AI_HANDOFF.md`)

이 문서는 다음 AI 에이전트 또는 개발자가 **5분 안에** Graphics SDK 기반 RHI 리팩토링의 현재 상태, 작업 공간, 완료된 범위, 그리고 바로 이어서 진행할 다음 작업을 파악할 수 있도록 작성되었습니다.

---

## 1. 작업 환경 및 브랜치 정보

| 구분 | 경로 | 브랜치 / 커밋 | 주의사항 |
|---|---|---|---|
| **리팩토링 전용 Worktree (작업 위치)** | `D:/Game/Sandbox-GraphicsSDK` | `refactor/graphics-sdk` | **모든 코드 수정·빌드·커밋은 반드시 여기서 수행** |
| **원본 Sandbox (건드리지 말 것)** | `D:/Game/Sandbox` | `master` (`189ac37`) | 다른 AI/개발자의 기능 개발 공간 — 수정 금지 |
| **Graphics SDK 소스** | `D:/source/GraphicsSDK` | `main` (`9345c1816879c63f8c26f13fc876382b0c2ed054`) | 루트에 미커밋 `tests/VulkanAllocationBenchmarkTests.cpp` 존재 — 건드리지 말 것 |
| **Graphics SDK 설치 프리픽스** | `D:/Game/Sandbox-GraphicsSDK/build/gfx-sdk-install` | Multi-Config (`Debug`/`RelWithDebInfo`/`Release`) | `CMAKE_PREFIX_PATH`로 연결됨 |
| **Legacy 빌드 디렉터리** | `D:/Game/Sandbox-GraphicsSDK/build/windows-msvc` | `SBX_USE_GRAPHICS_SDK=OFF` | 기준선 및 회귀 검증용 |
| **Graphics SDK 빌드 디렉터리** | `D:/Game/Sandbox-GraphicsSDK/build/windows-msvc-gfxsdk` | `SBX_USE_GRAPHICS_SDK=ON` | 어댑터 개발 및 검증용 |

---

## 2. 현재까지 완료된 작업 (`R0.1` ~ `R0.27`, Phase 0 ~ Phase 6 완료 — 진행률 77.1%)

1. **Phase 0 (`R0.1` ~ `R0.2`) 완료**:
   - Sandbox RHI 및 3대 백엔드(`dx12`, `vulkan`, `metal`), 렌더러 패스 전수 분석 및 `GraphicsSDK::Core` 호환성 매트릭스([API_COMPATIBILITY.md](API_COMPATIBILITY.md)), SDK 확장 요구사항([SDK_REQUIREMENTS.md](SDK_REQUIREMENTS.md)) 수립.
   - Windows 11 + NVIDIA GeForce GTX 1660 SUPER + WARP 환경에서 Legacy D3D12 / Vulkan 기준선 실측 완료([BENCHMARK_BASELINE.md](BENCHMARK_BASELINE.md)).
2. **Phase 1 & Phase 2 (`R0.3` ~ `R0.15`) 완료**:
   - `SBX_USE_GRAPHICS_SDK` 옵션(기본값 `OFF`) 및 `find_package(GraphicsSDK CONFIG REQUIRED)` 연동(`SandboxRender` `PRIVATE` 링크).
   - `render/graphics_sdk/` 핵심 어댑터 구현(`GraphicsSdkTypes`, `GraphicsSdkResource`, `GraphicsSdkPipeline`, `GraphicsSdkCommandQueue`, `GraphicsSdkSwapChain`, `GraphicsSdkDevice`, `GraphicsSdkCommandList`, `GraphicsSdkFactory`).
   - Stage-Before-Record 지연 재생, 슬롯별 SDK Upload 링 버퍼, `FrameSlot`별 `CommandPool`/`CommandList`, 세대형 핸들(`HandlePool`) 및 지연 해제(`DeferredDestructionQueue`), Timestamp Scope 변환, 오프라인 리플렉션 검증 완료.
3. **Phase 3 (`R0.16` ~ `R0.18`) 완료**:
   - `tools/shader/gen_gfxsdk_spirv.py` + `render/graphics_sdk/GraphicsSdkSpirvData.hpp` + `GraphicsSdkShaders.{hpp,cpp}`로 7종 내장 셰이더(14개 스테이지) D3D12(SM5.1 캐시) · Vulkan(단일 셋 SPIR-V 1.3) · Metal(MSL) 변환 완료 (`SDK-REQ-001` 해소).
   - `Format::R16Uint` → `R16UNorm`, 정점 `Format::R32Uint` → `VertexFormat::Float` (`asuint()` 복원) (`SDK-REQ-002` 해소).
   - 동적 `RhiSampler` `(filter, address)` 조합별 PSO 변형 캐시(`resolvePipelineVariant`) 및 슬롯별 전용 `pushBuffer`(256 KB) (`SDK-REQ-005`, `006` 해소).
   - 공용 6-인덱스 쿼드 스트립 버퍼(`m_quadStripIndexBuffer`) + 동적 스트립→리스트 변환, 2D CPU 와인딩 컬링(`tryDrawWithCpuCulling`) (`SDK-REQ-003`, `004` 해소).
4. **Phase 4 & Phase 5 (`R0.19` ~ `R0.24`) 완료**:
   - 렌더 패스 진입 전 Vertex/Index/Constant 버퍼 상태 사전 전이 스캔, 1-layer `"sprite atlas"` `Texture2DArray` 생성, Vulkan 부분 업로드 전 1회 `fullSlice` 제로 초기화, 불투명 RT Readback 알파 불변량 보정(`SDK-REQ-009`), 정적 매핑 Upload 버퍼 `lastUploadedShadow` 변경 감지 구현.
   - `tests/render/CMakeLists.txt`에서 `SBX_RENDER_TEST_FILTER` 완전 제거.
5. **검증 완료 상태**:
   - `SandboxTests --test-suite=render`: **33/33 PASS (340,535 assertions)**
   - `sbx_render_tests` (**전체 31/31 케이스, 527 assertions**):
     - D3D12 WARP (`--rhi dx12 --warp --debug`): **31/31 PASS**, 누수 0, Debug Layer 경고 0, 예상 오류 8/8
     - D3D12 Hardware (`--rhi dx12 --debug`): **31/31 PASS**, 누수 0, Debug Layer 경고 0, 예상 오류 8/8
     - Vulkan Hardware (`--rhi vulkan --debug` + VVL 1.3.290): **31/31 PASS**, 누수 0, VVL 경고 0, 예상 오류 8/8
   - `SandboxClient --world ecosystem_small --rhi dx12 --rhi-debug`: Debug Layer 경고 0 · 오류 0 정상 실행.
   - 전체 `ctest` (`build/windows-msvc-gfxsdk`, `RelWithDebInfo`): **99/99 실행 테스트 100% PASS** (`studio_gui_smoke` 1건 기본 Skipped).
   - `check_includes.py`, `check_docs.py`, `gen_roadmap.py --check` 모두 **0 errors** 통과.

---

## 3. 바로 이어서 진행할 다음 작업 (`R0.28` — Phase 7 착수)

다음 작업자는 **Phase 7 (`R0.28` ~ `R0.30`: 멀티 플랫폼, 프로파일러, Device Lost/Resize 안정화)**부터 시작하면 됩니다:

1. **`R0.28` — 멀티 플랫폼 검증 (Windows / Linux / macOS)**:
   - Windows D3D12/Vulkan 33/33 검증 완료. Linux X11/Wayland(`SDK-REQ-008`) 및 macOS Metal(`MetalLayer`) 어댑터 컴파일/경계 점검 및 CI 매트릭스 정합성 확인.
2. **`R0.29` — GPU Timestamp 및 CPU/GPU 프로파일러 통합**:
   - `GraphicsSdkDevice::sdkDevice().profilingSnapshot()` / `resourceStatistics()`와 Sandbox 프로파일러 연동 확인.
3. **`R0.30` — Device Lost 및 Resize / Recovery 안정화**:
   - `GraphicsSdkSwapChain::resize()` 최소화(0×0) 및 반복 리사이즈 안정화, `gfx::Device::checkStatus()`/`recover()` 오류 전파 검증.

---

## 4. 자주 쓰는 빌드 및 검증 명령

```powershell
# 1. Graphics SDK 어댑터 빌드 (D:/Game/Sandbox-GraphicsSDK 에서 실행)
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build D:/Game/Sandbox-GraphicsSDK/build/windows-msvc-gfxsdk --config RelWithDebInfo"

# 2. 전체 GPU RHI 테스트 31/31 (D3D12 WARP / D3D12 HW / Vulkan HW)
.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --warp --debug
.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --debug
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi vulkan --debug

# 3. 전체 CTest (UTF-8 코드페이지 활성화 필수)
cmd /c "chcp 65001 >nul && ctest --test-dir D:/Game/Sandbox-GraphicsSDK/build/windows-msvc-gfxsdk -C RelWithDebInfo -j 4 --output-on-failure"

# 4. 정적 경계 및 문서 검사
python tools/check_includes.py --root .
python tools/check_docs.py --root .
python tools/gen_roadmap.py --root . --check
```
