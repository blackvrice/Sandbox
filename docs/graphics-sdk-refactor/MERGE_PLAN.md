# 병렬 개발 동기화 및 병합 계획 (`MERGE_PLAN.md`)

본 문서는 Sandbox의 기존 기능 개발(`master` 브랜치)을 방해하지 않으면서 리팩토링 브랜치(`refactor/graphics-sdk`, Worktree `D:/Game/Sandbox-GraphicsSDK`)를 유지·동기화하고 최종 병합하기 위한 규칙, 전환 검토 결과 및 승인 절차를 정의합니다.

---

## 1. 작업 공간 격리 원칙

| 작업 공간 | 로컬 경로 | 브랜치 | 빌드 디렉터리 | 변경 정책 |
|---|---|---|---|---|
| **Sandbox (원본)** | `D:/Game/Sandbox` | `master` | `D:/Game/Sandbox/build/` | **절대 수정 금지** (다른 AI/개발자의 기능 개발 보존) |
| **Sandbox-GraphicsSDK** | `D:/Game/Sandbox-GraphicsSDK` | `refactor/graphics-sdk` | `D:/Game/Sandbox-GraphicsSDK/build/` | 리팩토링 전용 작업 공간 |
| **GraphicsSDK** | `D:/source/GraphicsSDK` | `main` (`9345c181`) | `D:/Game/Sandbox-GraphicsSDK/build/gfx-sdk-build-mc` | out-of-tree 빌드/설치 사용, 기존 미커밋 파일 보존 |
| **GraphicsUI** | `D:/source/GraphicsUI` | `main` | - | 읽기 전용 조사 완료 (Phase 8 브리지 구축) |

---

## 2. 비침투적 공존 설계 (`SBX_USE_GRAPHICS_SDK`) 및 기본값 전환 검토 (`R0.34`)

### 2.1 현재 상태 및 기본값 유지 권고
- `cmake/SbxOptions.cmake`의 `SBX_USE_GRAPHICS_SDK` 기본값은 현재 **`OFF`**를 유지합니다.
- **전환 검토 결론 (`R0.34`)**:
  - **Windows (D3D12 & Vulkan)**: 기능 100% 동등(`sbx_render_tests` 35/35 PASS, 전체 CTest 99/99 PASS, 결정론 16/16 일치, 리소스 누수 0, Validation 경고 0), 다중 Queue(`G+Copy+Compute`), 멀티스레드 기록, Tearing PresentMode(1% Low / 0.1% Low FPS Legacy 초과), GraphicsUI+ImGui 공존 브리지까지 **즉시 `ON` 전환 가능한 수준**에 도달했습니다.
  - **Linux / macOS**: `OPEN-001`(Linux Xlib/Wayland `SDK-REQ-008`)과 `OPEN-003`(Linux/macOS 실기 CI 검증 미수행)이 남아 있고, 외부 `GraphicsSDK` 패키지가 사전 설치(`CMAKE_PREFIX_PATH`)되어야 하므로, **기본값은 `OFF`로 두고 `refactor/graphics-sdk` 브랜치를 병합(Gate 2)한 뒤**, CI에 `GraphicsSDK` 아티팩트/FetchContent 연동 및 Linux/macOS 실기 검증이 완료되는 시점에 기본값을 `ON`으로 전환(Gate 3)하는 단계적 전략이 가장 안전합니다.

### 2.2 `origin/master` 동기화 확인 (2026-11-23)
- `git fetch origin && git log HEAD..origin/master` 확인 결과 `origin/master`에 신규 커밋 0건 — `refactor/graphics-sdk`는 충돌 없이 즉시 Fast-Forward 또는 Merge 가능한 상태입니다.

---

## 3. 단계별 최종 병합 게이트 (Phase 9 ~ Phase 10)

| 단계 | 승인 조건 | 현재 상태 |
|---|---|---|
| **Gate 1 (어댑터 코어 공존)** | `SBX_USE_GRAPHICS_SDK=OFF` 회귀 0 + `ON` 코어 RHI 10/10 통과 | **✅ 완료** |
| **Gate 2 (전체 기능·성능·공존 완성)** | `SBX_USE_GRAPHICS_SDK=ON`에서 `sbx_render_tests` **35/35 PASS**, 전체 CTest **99/99 PASS**, 다중 Queue/MT/SwapChain/GraphicsUI+ImGui 검증, `OFF` 빌드 34/34 PASS | **✅ 완료** (Windows D3D12/Vulkan) |
| **Gate 3 (`SBX_USE_GRAPHICS_SDK=ON` 기본값 전환)** | Linux(Xlib/Wayland `SDK-REQ-008` 포함) & macOS 실기 CI 검증 + `GraphicsSDK` CI 패키지 공급 구성 + 사용자 승인 | 🟡 검토 완료 (기본값 `OFF` 유지 권고) |
| **Gate 4 (Legacy 백엔드 제거 및 `master` 최종 병합)** | Gate 3 안정화 완료 + **사용자의 명시적 최종 승인** | ⏸️ **사용자 승인 대기 (`R0.35`)** |
