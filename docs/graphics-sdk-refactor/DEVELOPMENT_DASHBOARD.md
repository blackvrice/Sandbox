# Graphics SDK 리팩토링 개발 대시보드 (`DEVELOPMENT_DASHBOARD.md`)

**최종 업데이트**: 2026-11-23  
**현재 리팩토링 브랜치**: `refactor/graphics-sdk` (`D:/Game/Sandbox-GraphicsSDK`)  
**고정 Graphics SDK 버전**: `9345c1816879c63f8c26f13fc876382b0c2ed054` (`D:/source/GraphicsSDK`)  
**현재 완료 버전**: `R0.1` ~ `R0.24` (Phase 0 ~ Phase 5 완료 — 전체 31/31 GPU 테스트 및 `SandboxClient` 구동 완료)  
**다음 착수 버전**: `R0.25` (Phase 6 — 멀티스레드 커맨드 기록, 다중 Queue 및 GPU 메모리 최적화)

---

## 1. 전체 진행률 요약

| Phase | 범위 (`R` 버전) | 목표 | 진행률 | 상태 |
|---|---|---|---|---|
| **Phase 0** | `R0.1` ~ `R0.2` | 기존 그래픽 시스템 분석, 호환성 매트릭스 및 성능 기준선 확보 | 100% (2/2) | ✅ 완료 |
| **Phase 1** | `R0.3` ~ `R0.11` | Graphics SDK CMake 연동 및 `render/graphics_sdk/` 어댑터 전체 구현 | 100% (9/9) | ✅ 완료 |
| **Phase 2** | `R0.12` ~ `R0.15` | 세대형 핸들, 지연 해제, FrameSlot CommandPool, 슬롯별 UploadRing 구현 | 100% (4/4) | ✅ 완료 |
| **Phase 3** | `R0.16` ~ `R0.18` | 셰이더 변환(`DXBC`/`SPIR-V 1.3`), 동적 샘플러 변형 PSO, PushConstant 전용 슬롯, 토폴로지/컬링 | 100% (3/3) | ✅ 완료 |
| **Phase 4** | `R0.19` ~ `R0.22` | `Renderer`, `SpriteBatcher`, `TerrainPass`, `GridPass`, `LinePass` Graphics SDK 구동 | 100% (4/4) | ✅ 완료 |
| **Phase 5** | `R0.23` ~ `R0.24` | `ImGuiRenderer` 및 `SandboxClient` / Studio 스왑체인 통합 검증 | 100% (2/2) | ✅ 완료 |
| **Phase 6** | `R0.25` ~ `R0.27` | 멀티스레드 커맨드 기록, 다중 Queue, GPU 메모리 최적화 | 0% (0/3) | ⬜ 대기 |
| **Phase 7** | `R0.28` ~ `R0.30` | 멀티 플랫폼(Windows/Linux/macOS), GPU 프로파일러, Device Lost 복구 | 0% (0/3) | ⬜ 대기 |
| **Phase 8** | `R0.31` ~ `R0.32` | Graphics UI 프레임워크 연동 및 ImGui 공존 | 0% (0/2) | ⬜ 대기 |
| **Phase 9** | `R0.33` ~ `R0.34` | 전체 시나리오 벤치마크 비교 및 기본 백엔드(`ON`) 전환 검토 | 0% (0/2) | ⬜ 대기 |
| **Phase 10** | `R0.35` | Legacy 백엔드 정리 및 최종 통합 | 0% (0/1) | ⬜ 대기 |
| **합계** | `R0.1` ~ `R0.35` | 총 35개 리팩토링 마일스톤 | **68.6% (24/35)** | 🟡 진행 중 |

---

## 2. 백엔드 및 플랫폼 검증 현황

| 플랫폼 / 백엔드 | Legacy (`SBX_USE_GRAPHICS_SDK=OFF`) | Graphics SDK Adapter (`SBX_USE_GRAPHICS_SDK=ON`) | 비고 |
|---|---|---|---|
| **Windows D3D12 (WARP)** | ✅ 완료 (31/31 PASS) | ✅ 완료 (**31/31 PASS**, 527/527 assertions, CTest 전체 통과) | Debug Layer 경고 0 · 누수 0 |
| **Windows D3D12 (GTX 1660 SUPER)** | ✅ 완료 (31/31 PASS, RelWithDebInfo 2843.8 / 604.3 FPS) | ✅ 완료 (**31/31 PASS**, `SandboxClient` Debug 729.0 FPS / 0.58 ms) | Debug Layer 경고 0 · 오류 0 · 누수 0 |
| **Windows Vulkan (GTX 1660 SUPER)** | ✅ 완료 (31/31 PASS, RelWithDebInfo 3631.9 / 623.7 FPS) | ✅ 완료 (**31/31 PASS**, `SandboxClient` Debug 681.7 FPS / 0.65 ms) | 누수 0 · 오류 0 |
| **Linux Vulkan (Lavapipe / GPU)** | ⚠️ 환경 부족 (CI 검증됨) | ⚠️ 환경 부족 (어댑터 코드 준비 완료, Linux 실행 환경 필요) | `SDK-REQ-008` (Xlib/Wayland) 식별됨 |
| **macOS Metal (Apple Silicon)** | ⚠️ 환경 부족 (CI 검증됨) | ⚠️ 환경 부족 (어댑터 `Cocoa` → `MetalLayer` 매핑 완료, macOS 실기 필요) | Phase 7에서 실기 검증 |

---

## 3. 아키텍처 및 품질 게이트 현황

| 검사 항목 | 실행 명령 | Legacy 빌드 | Graphics SDK 빌드 |
|---|---|---|---|
| 헤더 경계 검사 | `python tools/check_includes.py --root .` | ✅ 통과 | ✅ 통과 (`render/graphics_sdk/` 네이티브 헤더 0건) |
| 링크 경계 검사 | `ctest -R arch_link_boundary_selftest` | ✅ 통과 (13 타깃) | ✅ 통과 (`SandboxServer`에 GraphicsSDK 미링크 확인) |
| 문서 일관성 검사 | `python tools/check_docs.py --root .` | ✅ 통과 | ✅ 통과 |
| 로드맵 생성 검사 | `python tools/gen_roadmap.py --root . --check` | ✅ 통과 | ✅ 통과 |
| 시뮬레이션 결정론 | `ctest -L det` (16개 골든 해시) | ✅ 16/16 통과 | ✅ 16/16 통과 (`kSimVersion` 변경 없음) |
