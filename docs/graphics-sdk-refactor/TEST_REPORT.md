# 테스트 실행 결과 보고서 (`TEST_REPORT.md`)

**실행 일시**: 2026-11-22  
**실행 환경**: Windows 11 x64, NVIDIA GeForce GTX 1660 SUPER + Microsoft Basic Render Driver (WARP), MSVC 19.44.35228.0 (`RelWithDebInfo`)  
**Graphics SDK 커밋**: `9345c1816879c63f8c26f13fc876382b0c2ed054`  
**Sandbox 리팩토링 브랜치**: `refactor/graphics-sdk` (`D:/Game/Sandbox-GraphicsSDK`)

---

## 1. 테스트 결과 총괄표

| 번호 | 테스트 스위트 / 대상 | 빌드 구성 | 실행 명령 | 결과 | 상세 요약 |
|---|---|---|---|---|---|
| 1 | `SandboxTests` (`render` 스위트) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\SandboxTests.exe --test-suite=render` | ✅ 통과 | **33/33 test cases (340,535 assertions) PASS** (`verifyAdapterTypeMappings` 포함) |
| 2 | `sbx_render_tests` (D3D12 WARP + Debug Layer) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --warp --debug --test-case=...` | ✅ 통과 | **10/10 test cases (101 assertions) PASS**, 누수 0, Debug Layer 경고 0, 예상 오류 8/8 |
| 3 | `sbx_render_tests` (D3D12 GTX 1660 SUPER + Debug Layer) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi dx12 --debug --test-case=...` | ✅ 통과 | **10/10 test cases (101 assertions) PASS**, 누수 0, Debug Layer 경고 0, 예상 오류 8/8 |
| 4 | `sbx_render_tests` (Vulkan GTX 1660 SUPER + VVL 1.3.290) | `SBX_USE_GRAPHICS_SDK=ON` | `.\build\windows-msvc-gfxsdk\bin\RelWithDebInfo\sbx_render_tests.exe --rhi vulkan --debug --test-case=...` | ✅ 통과 | **10/10 test cases (101 assertions) PASS**, 누수 0, VVL 경고/오류 0, 예상 오류 8/8 |
| 5 | 전체 `ctest` (100개 테스트) | `SBX_USE_GRAPHICS_SDK=ON` | `ctest --test-dir build/windows-msvc-gfxsdk -C RelWithDebInfo -j 4` | ✅ 통과 | **99/99 실행 테스트 100% PASS** (`studio_gui_smoke` 1건 기본 Skipped) |
| 6 | 전체 `ctest` & `sbx_render_tests` (D3D12/Vulkan) | `SBX_USE_GRAPHICS_SDK=OFF` (Legacy) | `sbx_render_tests --rhi dx12/vulkan --debug` | ✅ 통과 | **31/31 test cases (527 assertions) PASS**, Legacy 백엔드 회귀 0건 |

---

## 2. Graphics SDK 어댑터 GPU 테스트(`sbx_render_tests`) 상세 항목별 결과

| 테스트 케이스 | D3D12 WARP (`GraphicsSDK`) | D3D12 GTX 1660 SUPER (`GraphicsSDK`) | Vulkan GTX 1660 SUPER (`GraphicsSDK`) | 검증 내용 |
|---|---|---|---|---|
| `device reports caps of the selected adapter` | ✅ PASS | ✅ PASS | ✅ PASS | 어댑터 이름, 소프트웨어 플래그, VRAM, 256B/512B 정렬 상수 |
| `clear: single render target and MRT match reference images` | ✅ PASS | ✅ PASS | ✅ PASS | 단일 RT 및 MRT(2개 컬러 타깃) Clear + Readback + 골든 PNG 일치 |
| `upload: top-left pixel of CPU image lands at top-left of texture` | ✅ PASS | ✅ PASS | ✅ PASS | 4사분면 RGBA 업로드 방향(좌상단 원점) 및 골든 PNG 일치 |
| `copy: arbitrary 2D sub-regions to and from textures` | ✅ PASS | ✅ PASS | ✅ PASS | 비원점 `(x, y)` 부분 텍스처 업로드 및 부분 텍스처 → 버퍼 Readback |
| `copy: upload ring → GPU buffer → readback, across frames with ring wrap-around` | ✅ PASS | ✅ PASS | ✅ PASS | 64KB 링에 12프레임 × 20KB(240KB) 연속 기록·래핑·왕복 복사 무결성(`bad == 0`) 및 지연 해제 회수 |
| `lifetime: destroy invalidates the handle now and releases the object after the GPU is done` | ✅ PASS | ✅ PASS | ✅ PASS | `destroy()` 직후 `alive() == false`, 프레임 슬롯 회전 후 `releasedObjects` 증가 |
| `misuse is reported, not crashed: unaligned copy pitch` | ✅ PASS | ✅ PASS | ✅ PASS | 256B 미정렬 `rowPitch` 전달 시 크래시 없이 `debugErrors` 1 증가 |
| `timestamps: written around GPU work come back after the frame slot is reused, in order (8B)` | ✅ PASS | ✅ PASS | ✅ PASS | 패스 안팎 3개 타임스탬프 단조 증가(`ticks[0] <= ticks[1] <= ticks[2]`), `millis(0, 2)` 환산, 패스 내 resolve 오류 감지 |
| `validation: mismatched layouts and bind groups are rejected with a reason` | ✅ PASS | ✅ PASS | ✅ PASS | 누락된 레이아웃, 잘못된 바인딩 타입, 누락된 정점 속성, 빈 바인드 그룹, 중복 바인딩 5종 거부 |

---

## 3. 실행 출력 증적 요약

### 3.1 Graphics SDK (D3D12 Hardware)
```text
[info] [render] GraphicsSDK (D3D12): NVIDIA GeForce GTX 1660 SUPER · VRAM 5966 MB · frames in flight 2 · Validation 켬
sbx_render_tests: D3D12 · NVIDIA GeForce GTX 1660 SUPER · Debug Layer 켬
[doctest] test cases:  10 |  10 passed | 0 failed | 21 skipped
[doctest] assertions: 101 | 101 passed | 0 failed |
[doctest] Status: SUCCESS!
끝: 살아 있는 버퍼 0 · 텍스처 0 · 파이프라인 객체 0 · 디스크립터 0 · 해제 18 · Debug Layer 경고 0 · 오류 8 (예상된 사용 오류 8)
render tests OK
```

### 3.2 Graphics SDK (Vulkan Hardware + VVL 1.3.290)
```text
[info] [render] GraphicsSDK (Vulkan): NVIDIA GeForce GTX 1660 SUPER · VRAM 6180 MB · frames in flight 2 · Validation 켬
sbx_render_tests: Vulkan · NVIDIA GeForce GTX 1660 SUPER · Debug Layer 켬
[doctest] test cases:  10 |  10 passed | 0 failed | 21 skipped
[doctest] assertions: 101 | 101 passed | 0 failed |
[doctest] Status: SUCCESS!
끝: 살아 있는 버퍼 0 · 텍스처 0 · 파이프라인 객체 0 · 디스크립터 0 · 해제 18 · Debug Layer 경고 0 · 오류 8 (예상된 사용 오류 8)
render tests OK
```
