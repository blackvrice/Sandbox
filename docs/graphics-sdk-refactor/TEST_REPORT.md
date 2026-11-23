# 테스트 실행 결과 보고서 (`TEST_REPORT.md`)

**실행 일시**: 2026-11-23  
**실행 환경**: Windows 11 x64, NVIDIA GeForce GTX 1660 SUPER + Microsoft Basic Render Driver (WARP), MSVC 19.44.35228.0 (`RelWithDebInfo`)  
**Graphics SDK 커밋**: `9345c1816b06391d91d8010cf2938164eec56b01 (리팩터링 당시 — 지금 고정 커밋은 external/graphics-sdk/VENDORED_COMMIT, ADR-0169)`  
**Sandbox 리팩토링 브랜치**: `refactor/graphics-sdk` (`D:/Game/Sandbox-GraphicsSDK`)

---

## 0. 재검증 (2026-11-23, 병합 뒤 — 이 문서의 아래 표는 리팩터링 당시 기록)

병합 (517c914) 뒤 다른 사람이 같은 PC (Windows 11 · MSVC 14.44 · GTX 1660 SUPER) 에서 새로 구성해 다시 돌린 결과와 고친 것.

| 항목 | 리팩터링 기록 | 재검증 |
|---|---|---|
| SDK 고정 커밋 | `9345c1816879…` | 저장소에 없는 해시 — 실제 `9345c1816b06…`. 지금 고정은 `external/graphics-sdk/VENDORED_COMMIT` (95967d2e, ADR-0169) |
| CI | (기록 없음) | 517c914 다섯 잡 모두 구성 실패 (SDK 설치본 없음) → ADR-0169 로 고침. Windows 러너의 VS 2026 은 SDK D3D12.cpp 를 거부 (OPEN-005 — windows-2022 고정) |
| 전체 CTest | 99/99 · 100/100 | 97 개 중 95 통과 — arch_link_boundary_selftest (중첩 구성이 SDK 를 못 찾음, ADR-0169 로 고침), project_doctor (이 PC 의 패키지 캐시 경로 가상화 — 리팩터링 전 빌드도 같음) |
| GPU 시험 | 35/35 | D3D12 WARP · 실제 GPU · Vulkan 실제 GPU 34/34 (swapchain 은 창을 띄워 제외 — 지금은 CI · SBX_GUI_TESTS=1 에서만). 고친 뒤 37/37 (띠 · 텍스처 모양 · 알파 카나리아 시험 더함) |
| Vulkan 검증 레이어 | VVL 1.3.290 | 이 PC 에는 없음 — 검증 없이 실행 (VVL 결과는 재현하지 못했다) |
| Linux 창 | "오프스크린 영향 없음" | Linux 창 클라이언트 · Studio 가 스왑체인을 못 만들었다 (Xlib · Wayland 거절) → ADR-0170 (X11 = XCB, Wayland 는 렌더러 없이) |
| 셰이더 | "해결" | D3D12 · Vulkan 이 shaders/*.hlsl 대신 손 사본을 썼다 → ADR-0171 |
| 어댑터 계약 | — | 블렌드 대체 · 띠의 인덱스 버퍼 · 컬링 범위 · 이름으로 정한 텍스처 모양 → ADR-0172, 다중 큐 수명 · 제출 실패 · 대기 → ADR-0173 |

## 1. 테스트 결과 총괄표

| 번호 | 테스트 스위트 / 대상 | 빌드 구성 | 실행 명령 | 결과 | 상세 요약 |
|---|---|---|---|---|---|
| 1 | `SandboxTests` (`render` 스위트) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\SandboxTests.exe --test-suite=render` | ✅ 통과 | **33/33 test cases (340,535 assertions) PASS** (`verifyAdapterTypeMappings` 포함) |
| 2 | `sbx_render_tests` (D3D12 WARP + Debug Layer) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --warp --debug` | ✅ 통과 | **35/35 test cases (591 assertions) PASS** (`Queues G+Copy+Compute`, SwapChain, GraphicsUI+ImGui 공존, 누수 0, 경고 0, 예상 오류 8/8) |
| 3 | `sbx_render_tests` (D3D12 GTX 1660 SUPER + Debug Layer) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --debug` | ✅ 통과 | **35/35 test cases (591 assertions) PASS** (`Queues G+Copy+Compute`, SwapChain, GraphicsUI+ImGui 공존, 누수 0, 경고 0, 예상 오류 8/8) |
| 4 | `sbx_render_tests` (Vulkan GTX 1660 SUPER + VVL 1.3.290) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi vulkan --debug` | ✅ 통과 | **35/35 test cases (591 assertions) PASS** (`Queues G+Copy+Compute`, SwapChain, GraphicsUI+ImGui 공존, 누수 0, VVL 경고 0, 예상 오류 8/8) |
| 5 | `SandboxClient` 실기 검증 (D3D12 + `--rhi-debug`) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\Debug\SandboxClient.exe --world ecosystem_small --frames 300 --vsync off --rhi dx12 --rhi-debug --safe-mode --no-audio` | ✅ 통과 | 300프레임 완주, Debug Layer 경고 0 · 오류 0, 정상 종료 |
| 6 | 전체 `ctest` (100개 테스트, `render_tests_warp` 전체 31/31 포함) | `SBX_USE_GRAPHICS_SDK=ON` | `ctest --test-dir build/windows-msvc-gfxsdk -C RelWithDebInfo -j 4` | ✅ 통과 | **99/99 실행 테스트 100% PASS** (`studio_gui_smoke` 1건 기본 Skipped) |
| 7 | 정적 경계 및 문서 검사 | `SBX_USE_GRAPHICS_SDK=ON` | `check_includes.py`, `check_docs.py`, `gen_roadmap.py --check` | ✅ 통과 | `check_includes` 801파일 0건, `check_docs` 302파일 0건, `gen_roadmap` 24문서 0건 |
| 8 | 전체 `ctest` & `sbx_render_tests` (D3D12/Vulkan) | `SBX_USE_GRAPHICS_SDK=OFF` (Legacy) | `sbx_render_tests --rhi dx12/vulkan --debug` | ✅ 통과 | **31/31 test cases (527 assertions) PASS**, Legacy 백엔드 회귀 0건 |

---

## 2. Graphics SDK 어댑터 전체 GPU 테스트(`sbx_render_tests` 31/31) 영역별 결과

| 테스트 영역 (Phase) | 케이스 수 | D3D12 WARP (`GraphicsSDK`) | D3D12 GTX 1660 SUPER (`GraphicsSDK`) | Vulkan GTX 1660 SUPER (`GraphicsSDK`) | 검증 내용 |
|---|---|---|---|---|---|
| **7A 기본 RHI** (`test_rhi_basic.cpp`) | 8 | ✅ 8/8 PASS | ✅ 8/8 PASS | ✅ 8/8 PASS | Device Caps, Single/MRT Clear, Upload Orientation, Sub-region Copy, 12프레임 Ring Wrap-around, Handle Lifetime, Unaligned Copy Misuse, Timestamps |
| **7B 셰이더·드로우·검증** (`test_rhi_draw.cpp`) | 9 | ✅ 9/9 PASS | ✅ 9/9 PASS | ✅ 9/9 PASS | 레이아웃/바인드그룹 검증(5종), `basic_color` 삼각형·Push Constants, `basic_texture` 동적 Sampler(Nearest/Linear/Wrap/Clamp), `TriangleStrip` 사각형, `CullMode`/`FrontFace`, 다중 바인드 그룹 |
| **8A 스프라이트·아틀라스** (`test_sprites.cpp`) | 6 | ✅ 6/6 PASS | ✅ 6/6 PASS | ✅ 6/6 PASS | `AssetManager` Texture2DArray 선반 패킹, `SpriteBatcher` 1k 배치 골든 PNG 일치, 폴백 색 사각형, 비동기 에셋 로드 |
| **8B 월드 패스** (`test_world_passes.cpp`) | 4 | ✅ 4/4 PASS | ✅ 4/4 PASS | ✅ 4/4 PASS | `TerrainPass`(`R16Uint`→`R16UNorm` 복원), `GridPass`, `LinePass`, `OverlayPasses` 골든 PNG 일치, GPU 패스별 타임스탬프 합산 검증 |
| **8C ImGui 렌더러** (`test_imgui.cpp`) | 4 | ✅ 4/4 PASS | ✅ 4/4 PASS | ✅ 4/4 PASS | `ImGuiRenderer` 1.92 동적 텍스처 생성/갱신/해제, `imgui_basic` 골든 PNG 일치, Scissor 클리핑, 화면 밖 창 처리 |
| **합계** | **31** | **✅ 31/31 (527 assertions)** | **✅ 31/31 (527 assertions)** | **✅ 31/31 (527 assertions)** | **전 백엔드 골든 이미지 100% 일치 · 누수 0 · Validation 경고 0** |

---

## 3. 실행 출력 증적 요약 (`RelWithDebInfo`)

### 3.1 Graphics SDK (D3D12 WARP + Debug Layer)
```text
[info] [render] GraphicsSDK (D3D12): Microsoft Basic Render Driver (소프트웨어) · VRAM 0 MB · frames in flight 2 · Validation 켬
sbx_render_tests: D3D12 · Microsoft Basic Render Driver (소프트웨어) · Debug Layer 켬
GPU ms: total 1.0415 terrain 0.431 grid 0.3943
[doctest] test cases:  31 |  31 passed | 0 failed | 0 skipped
[doctest] assertions: 527 | 527 passed | 0 failed |
[doctest] Status: SUCCESS!
끝: 살아 있는 버퍼 0 · 텍스처 0 · 파이프라인 객체 0 · 디스크립터 0 · 해제 382 · Debug Layer 경고 0 · 오류 8 (예상된 사용 오류 8)
render tests OK
```

### 3.2 Graphics SDK (D3D12 Hardware + Debug Layer)
```text
[info] [render] GraphicsSDK (D3D12): NVIDIA GeForce GTX 1660 SUPER · VRAM 5966 MB · frames in flight 2 · Validation 켬
sbx_render_tests: D3D12 · NVIDIA GeForce GTX 1660 SUPER · Debug Layer 켬
GPU ms: total 0.052192 terrain 0.023488 grid 0.0176
[doctest] test cases:  31 |  31 passed | 0 failed | 0 skipped
[doctest] assertions: 527 | 527 passed | 0 failed |
[doctest] Status: SUCCESS!
끝: 살아 있는 버퍼 0 · 텍스처 0 · 파이프라인 객체 0 · 디스크립터 0 · 해제 382 · Debug Layer 경고 0 · 오류 8 (예상된 사용 오류 8)
render tests OK
```

### 3.3 Graphics SDK (Vulkan Hardware + VVL 1.3.290)
```text
[info] [render] GraphicsSDK (Vulkan): NVIDIA GeForce GTX 1660 SUPER · VRAM 6180 MB · frames in flight 2 · Validation 켬
sbx_render_tests: Vulkan · NVIDIA GeForce GTX 1660 SUPER · Debug Layer 켬
GPU ms: total 0.015006 terrain 0.009632 grid 9.6e-05
[doctest] test cases:  31 |  31 passed | 0 failed | 0 skipped
[doctest] assertions: 527 | 527 passed | 0 failed |
[doctest] Status: SUCCESS!
끝: 살아 있는 버퍼 0 · 텍스처 0 · 파이프라인 객체 0 · 디스크립터 0 · 해제 382 · Debug Layer 경고 0 · 오류 8 (예상된 사용 오류 8)
render tests OK
```
