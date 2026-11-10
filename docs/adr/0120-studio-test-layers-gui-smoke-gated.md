# ADR-0120. Studio 시험 층 — 층 2 흐름 E2E 하나, 층 3 GUI smoke 는 Windows WARP · CI 에서만 (그 밖은 SKIPPED), 기준마다 증거 층

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [28-CREATOR-UX-GATES](../28-CREATOR-UX-GATES.md) 3.1 (층 1 ~ 4), [29-STUDIO-UI](../29-STUDIO-UI.md) 7장 (흐름), [ADR-0118](0118-studio-shell-dockspace-design-system.md),
  [qa/QA-EVIDENCE](../qa/QA-EVIDENCE.md), task-registry NG-07

## 맥락

층 1 (서비스 단위) · 층 2 의 조각 (studio_new_project · studio_build · studio_recovery …) 은 있었지만 "새 프로젝트 → 가져오기 → 빌드 → 플레이" 를
한 줄로 지나는 시험이 없었다. 층 3 (실제 창 · 렌더러로 모든 창 그리기) 은 없었다. 실제 창은 사람이 쓰는 PC 에서 포커스를 빼앗는다 (이번 세션에서
사용자의 전체 화면 게임 위로 창을 띄울 뻔했다). GitHub 의 Windows 러너에는 GPU 가 없지만 D3D12 WARP 가 있다.

## 결정

```text
1. 층 2: CTest studio_flow_e2e — --new-project (홈 명령) → sbx asset import → --run-command studio.build → 빈 캐시에서 --world <패키지>
2. 층 3: CTest studio_gui_smoke (WIN32 만) — --rhi-warp 1366×768 로 홈 90 프레임 · 워크스페이스 180 프레임 (studio.json developerPanels
   true — 모든 창), 종료 0 · [error] 없음 · 렌더러가 그렸다 · 끝날 때 배치에 목록 · 인스펙터 · 콘솔 · 통계. CI (CI=true) 또는 SBX_GUI_TESTS=1
   일 때만 창을 띄우고, 아니면 · 창 · 디바이스를 만들 수 없으면 "건너뜀" — SKIP_REGULAR_EXPRESSION 으로 SKIPPED (통과로 적지 않는다)
3. 층 4: qa/QA-EVIDENCE 양식 (명령 · OS · GPU · 해상도 · 배율 · 결과 · 커밋) — MANUAL-QA 의 [x] 는 이 양식 한 줄을 남긴다
4. 28 의 기준 G1 ~ G9 마다 증거 층 표 (3.1). 층 3 · 4 만 있는 기준은 Closed Creator Alpha 진입 때 수동 기록이 있어야 한다 (28 그대로)
```

## 근거

```text
- 한 줄 E2E: 조각 시험이 각각 통과해도 사이의 넘김 (최근 목록 · 패키지 경로 · 캐시) 은 따로 깨질 수 있다
- WARP: GPU 없는 CI 에서 실제 렌더 백엔드 · 스왑체인 · ImGui 렌더 길을 지난다 (화면 모양은 보지 않는다 — 그건 층 4)
- 게이트: 개발자 PC 의 기본 ctest 가 창을 띄우지 않는다. 건너뜀은 SKIPPED 로 보여 통과로 착각하지 않는다
```

## 결과

- 얻는 것: 흐름 A · D 의 한 줄 시험, CI 의 실제 창 smoke (Windows), 기준마다 증거 층, QA 기록 양식.
- 포기하는 것: Linux · macOS 의 GUI smoke (디스플레이 · 디바이스 — Linux 는 xvfb + lavapipe 로 [계획]), 화면 비교 (골든 그림) — tests/render 의
  렌더 골든과 별개로 Studio 화면 골든은 [계획].
- 위험: Windows 러너가 창을 만들지 못하면 SKIPPED 로 남는다 — CI 로그에서 SKIPPED 가 계속이면 다시 본다.

## 재검토 조건

Linux xvfb · lavapipe smoke, Studio 화면 골든 (ImGui 그리기 목록 비교), 외부 사용성 시험 (ST-06-12 · v0.13).
