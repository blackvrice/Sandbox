# 병렬 개발 동기화 및 병합 계획 (`MERGE_PLAN.md`)

본 문서는 Sandbox의 기존 기능 개발(`master` 브랜치)을 방해하지 않으면서 리팩토링 브랜치(`refactor/graphics-sdk`, Worktree `D:/Game/Sandbox-GraphicsSDK`)를 유지·동기화하고 최종 병합하기 위한 규칙과 절차를 정의합니다.

---

## 1. 작업 공간 격리 원칙

| 작업 공간 | 로컬 경로 | 브랜치 | 빌드 디렉터리 | 변경 정책 |
|---|---|---|---|---|
| **Sandbox (원본)** | `D:/Game/Sandbox` | `master` | `D:/Game/Sandbox/build/` | **절대 수정 금지** (다른 AI/개발자의 기능 개발 보존) |
| **Sandbox-GraphicsSDK** | `D:/Game/Sandbox-GraphicsSDK` | `refactor/graphics-sdk` | `D:/Game/Sandbox-GraphicsSDK/build/` | 리팩토링 전용 작업 공간 |
| **GraphicsSDK** | `D:/source/GraphicsSDK` | `main` (`9345c181`) | `D:/Game/Sandbox-GraphicsSDK/build/gfx-sdk-build-mc` | out-of-tree 빌드/설치 사용, 기존 미커밋 파일 보존 |
| **GraphicsUI** | `D:/source/GraphicsUI` | `main` | - | 읽기 전용 조사 (Phase 8에서 연동) |

---

## 2. 비침투적 공존 설계 (`SBX_USE_GRAPHICS_SDK`)

1. **기본값 `OFF` 유지 (Phase 1 ~ Phase 8)**:
   - `cmake/SbxOptions.cmake`의 `SBX_USE_GRAPHICS_SDK`는 기본값이 `OFF`입니다.
   - `SBX_USE_GRAPHICS_SDK=OFF`로 빌드할 경우 `render/graphics_sdk/`는 컴파일조차 되지 않으며, 기존 `render/dx12/`, `render/vulkan/`, `render/metal/` 코드가 100% 그대로 실행됩니다.
2. **변경 파일 범위 최소화**:
   - 기존 파일 수정은 `cmake/SbxOptions.cmake`, `render/CMakeLists.txt`, `render/rhi/Backends.hpp`, `render/rhi/RenderDeviceFactory.cpp`, `tests/render/CMakeLists.txt`, `tests/unit/render/test_rhi_helpers.cpp`, `tools/check_includes.py`, `docs/17-SOURCE-MAP.md`, `DEVELOPMENT_LOG.md`로 한정되어 있어 `core/`, `script/`, `network/`, `ui/`, `editor/`, `apps/`에서의 병합 충돌 가능성이 사실상 `0`입니다.

---

## 3. 주기적 `origin/master` 동기화 절차

1. `git fetch origin`으로 `origin/master`의 최신 커밋을 확인합니다.
2. 무단 `git rebase`나 `git push --force`를 사용하지 않고, 필요 시 `git merge origin/master`로 최신 기능을 가져옵니다.
3. 충돌이 발생할 경우 **기존 Sandbox의 신규 기능 보존을 최우선**으로 해결하며, `SBX_USE_GRAPHICS_SDK=OFF`와 `ON` 양쪽 빌드에서 모두 `ctest`와 `sbx_render_tests`를 통과시킨 뒤 커밋합니다.

---

## 4. 단계별 최종 병합 게이트 (Phase 9 ~ Phase 10)

| 단계 | 승인 조건 | 조치 |
|---|---|---|
| **Gate 1 (어댑터 공존 병합 가능)** | `SBX_USE_GRAPHICS_SDK=OFF`에서 기존 31/31 GPU 테스트 + 100/100 CTest 통과, `SBX_USE_GRAPHICS_SDK=ON`에서 Phase 1·2 코어 RHI 10/10 통과 | **✅ 현재 달성 완료** (`refactor/graphics-sdk` 브랜치) |
| **Gate 2 (전체 렌더 패스 동등성 달성)** | Phase 3~5 완료 (`SDK-REQ-001`~`006` 해소 후 `SBX_USE_GRAPHICS_SDK=ON`에서 `sbx_render_tests` 31/31 전체 골든 PNG 통과 + Studio 멀티 윈도우 통과) | Phase 5(`R0.24`) 목표 |
| **Gate 3 (`SBX_USE_GRAPHICS_SDK=ON` 기본값 전환)** | Phase 6~9 완료 (Windows D3D12/Vulkan, Linux Vulkan, macOS Metal 검증 + `ecosystem_10k` 성능 기준선 이상 달성 + 사용자 승인) | Phase 9(`R0.34`) 목표 |
| **Gate 4 (Legacy 백엔드 제거 및 `master` 최종 병합)** | 기본값 `ON` 안정화 완료 + **사용자의 명시적 최종 승인** | Phase 10(`R0.35`)에서 `render/dx12`, `vulkan`, `metal` 제거 |
