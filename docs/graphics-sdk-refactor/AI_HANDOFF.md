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

## 2. 현재까지 완료된 작업 (`R0.1` ~ `R0.15`, Phase 0 ~ Phase 2 완료)

1. **Phase 0 (`R0.1` ~ `R0.2`) 완료**:
   - Sandbox RHI 및 3대 백엔드(`dx12`, `vulkan`, `metal`), 렌더러 패스(`Renderer`, `SpriteBatcher`, `TerrainPass`, `GridPass`, `LinePass`, `ImGuiRenderer`) 전수 분석.
   - `GraphicsSDK::Core` 공개 API와의 1:1 호환성 매트릭스([API_COMPATIBILITY.md](API_COMPATIBILITY.md)) 및 8개 SDK 확장 요구사항([SDK_REQUIREMENTS.md](SDK_REQUIREMENTS.md)) 수립.
   - Windows 11 + NVIDIA GeForce GTX 1660 SUPER + WARP 환경에서 Legacy D3D12 / Vulkan 성능 기준선 실측 완료([BENCHMARK_BASELINE.md](BENCHMARK_BASELINE.md)).
2. **Phase 1 & Phase 2 (`R0.3` ~ `R0.15`) 완료**:
   - `SBX_USE_GRAPHICS_SDK` 옵션(기본값 `OFF`) 및 `find_package(GraphicsSDK CONFIG REQUIRED)` 연동(`SandboxRender` `PRIVATE` 링크).
   - `render/graphics_sdk/` 어댑터 전체 구현:
     - `GraphicsSdkTypes.{hpp,cpp}`
     - `GraphicsSdkResource.hpp`
     - `GraphicsSdkPipeline.{hpp,cpp}`
     - `GraphicsSdkCommandQueue.{hpp,cpp}`
     - `GraphicsSdkSwapChain.{hpp,cpp}`
     - `GraphicsSdkDevice.{hpp,cpp}`
     - `GraphicsSdkCommandList.{hpp,cpp}`
     - `GraphicsSdkFactory.cpp`
   - **Stage-Before-Record 업로드 동기화**, **슬롯별 SDK Upload 링 버퍼**, **세대형 핸들(`HandlePool`) 및 지연 해제(`DeferredDestructionQueue`)**, **Timestamp Scope 변환**, **오프라인 리플렉션 파이프라인 검증** 완료.
3. **검증 완료 상태**:
   - `SandboxTests --test-suite=render`: **33/33 PASS**
   - `sbx_render_tests` (Phase 1·2 코어 RHI 10개 케이스):
     - D3D12 WARP (`--rhi dx12 --warp --debug`): **10/10 PASS**, 누수 0, Debug Layer 경고 0
     - D3D12 Hardware (`--rhi dx12 --debug`): **10/10 PASS**, 누수 0, Debug Layer 경고 0
     - Vulkan Hardware (`--rhi vulkan --debug` + VVL 1.3.290): **10/10 PASS**, 누수 0, VVL 경고 0
   - 전체 `ctest` (`build/windows-msvc-gfxsdk`): **99/99 실행 테스트 100% PASS** (`studio_gui_smoke` 1건 기본 Skipped)
   - `check_includes.py`, `check_docs.py`, `gen_roadmap.py --check` 모두 **0 errors** 통과.

---

## 3. 바로 이어서 진행할 다음 작업 (`R0.16` — Phase 3 착수)

다음 작업자는 **Phase 3 (`R0.16` ~ `R0.18`: 셰이더, 바인드 그룹 및 파이프라인 전환)**부터 시작하면 됩니다:

1. **[SDK_REQUIREMENTS.md](SDK_REQUIREMENTS.md)의 P0/P1 항목 해소**:
   - `SDK-REQ-001`: GraphicsSDK가 Sandbox의 오프라인 리플렉션(`ShaderReflection`) 또는 커스텀 엔트리포인트(`"VSMain"`/`"PSMain"`)와 다중 디스크립터 셋(`space0..space3`)을 수용하도록 확장 (`dxcompiler.dll` 런타임 의존 없이 D3D12 Root Signature 생성, Vulkan SPIR-V 다중 set 지원).
   - `SDK-REQ-002`: `gfx::Format::R16UInt` 및 `gfx::VertexFormat::UInt`(`R32Uint`) 추가.
   - `SDK-REQ-003` & `004`: `gfx::GraphicsPipelineDesc`에 `PrimitiveTopology`(`TriangleStrip`, `LineList` 등), `CullMode`, `FrontFace`, `wireframe` 추가.
   - `SDK-REQ-005` & `006`: Push Constants 및 동적 Sampler 바인딩 연결.
2. **`tests/render/CMakeLists.txt`의 `SBX_RENDER_TEST_FILTER` 해제**:
   - Phase 3 완료 시 `test_rhi_draw.cpp`의 8개 드로우 테스트를 추가로 활성화하고, Phase 4~5 완료 시 필터를 완전히 제거하여 31/31 전체 GPU 골든 이미지 테스트를 통과시킵니다.

---

## 4. 자주 쓰는 빌드 및 검증 명령

```powershell
# 1. Graphics SDK 어댑터 빌드 (D:/Game/Sandbox-GraphicsSDK 에서 실행)
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 && cmake --build D:/Game/Sandbox-GraphicsSDK/build/windows-msvc-gfxsdk --config RelWithDebInfo"

# 2. GPU RHI 테스트 (D3D12 WARP / D3D12 HW / Vulkan HW)
.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --warp --debug --test-case="*device reports*,*clear*,*upload:*,*copy:*,*lifetime: destroy invalidates*,*misuse*,*timestamps*,*validation: mismatched*"
.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --debug --test-case="*device reports*,*clear*,*upload:*,*copy:*,*lifetime: destroy invalidates*,*misuse*,*timestamps*,*validation: mismatched*"
$env:VK_LAYER_PATH = "D:\source\GraphicsSDK\out\vulkan-validation-runtime\build\Vulkan-ValidationLayers\layers\Release"
.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi vulkan --debug --test-case="*device reports*,*clear*,*upload:*,*copy:*,*lifetime: destroy invalidates*,*misuse*,*timestamps*,*validation: mismatched*"

# 3. 전체 CTest (UTF-8 코드페이지 활성화 필수)
cmd /c "chcp 65001 >nul && ctest --test-dir D:/Game/Sandbox-GraphicsSDK/build/windows-msvc-gfxsdk -C RelWithDebInfo -j 4 --output-on-failure"

# 4. 정적 경계 및 문서 검사
python tools/check_includes.py --root .
python tools/check_docs.py --root .
python tools/gen_roadmap.py --root . --check
```
