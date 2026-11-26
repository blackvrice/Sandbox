# UI-R10 — 기존 UI 제거 · 최종 전환 [차단]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R10-01 ~ 05. **모든 필수 기능의 동등성 · 안정성이 확인된 뒤에만.**

선행 (전부): FEATURE_MATRIX 2장의 26 화면 GraphicsUI 동등 · 같은 자동 시험 통과 · QA_CHECKLIST 전부 · GUI-UP-01 (과도기 경로가
ImGui 에 기댄다 — 이것 없이 ImGui 를 지울 수 없다) · GraphicsUI 제품 gate (P9-06 · P10-03 ~ 07).

순서: R10-01 ImGui Studio UI → R10-02 과도기 브리지 · render/imgui → R10-03 sbx_imgui · CMake → R10-04 설정 마이그레이션 (imgui ini ·
studio.json) → R10-05 전체 회귀 · 사용자 시나리오 · 배포 빌드. GraphicsUI 판이 기존 기능을 대체하지 못한 상태에서 기존 구현을 지우지 않는다.
