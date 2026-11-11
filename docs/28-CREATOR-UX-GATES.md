# 28. 제작자 흐름 출시 기준 (Creator UX Gates) · 사용성 시험 계획

> **계획 · 규범 문서** (2026-11-03, master a1307fc). 알파 목표: **처음 쓰는 제작자가 Sandbox Studio 에서 CLI 없이 간단한 2D 게임을 만들고 ·
> 편집하고 · 스크립트를 쓰고 · 테스트하고 · 빌드해, 그 결과를 다른 PC 에서 실행한다.** 보안 · 안정성 기준은 [24](24-SECURITY-GATES.md) (이
> 문서는 사용 흐름의 기준), 기능 설계는 [25](25-STUDIO-ARCHITECTURE.md) · [26](26-ASSET-PIPELINE.md) · [27](27-SCRIPT-IDE.md), 작업은
> [23](23-ROADMAP.md). **이 문서의 시험 결과 칸은 비어 있다 — 실제 외부 사용자 시험을 하지 않았다.**

> **2026-11-03 로드맵 재구성 ([ADR-0096](adr/0096-roadmap-versioning-small-milestones.md)):** 이 문서의 버전 표기 ("[계획 v0.x]" · "v0.5 M6" …) 는 옛 줄이다 —
> 목표 버전 · 상태의 정본은 [roadmap/task-registry](roadmap/task-registry.md), 읽는 법은 [versioning 4장](roadmap/versioning.md). 태스크 ID 는 그대로. 2차 감사의 새 설계 (NG) 는 [audit/SECOND-PASS-GAP-ANALYSIS](audit/SECOND-PASS-GAP-ANALYSIS.md) 4장.

---

## 1. 흐름 단계별 기준 (G1 ~ G9) — 지금 CLI · 외부 도구가 필요한가

| G | 단계 | 통과 기준 (자동 시험 또는 기록된 수동 QA) | 지금 (2026-11-03) | 필요한 작업 | 버전 |
|---|------|-------------------------------------------|-------------------|-------------|------|
| G1 | 프로젝트 생성 | Studio 홈 → 템플릿 → 만들기 → 편집 화면, 1 분 안 · 손으로 파일을 만들지 않는다 | **CLI 필요** (sbx new — core::createProject 는 있다) | M6-T1 | v0.5 |
| G2 | 에셋 가져오기 | PNG 를 끌어다 놓아 Prefab 에 연결, 다시 열어도 연결 유지 | **불가** — 프로젝트 에셋 폴더 · 가져오기 · 안정 id 없음, 에셋은 전역 루트 하나 | M6-T9 | v0.5 |
| G3 | 월드 편집 | 배치 · 이동 · 지우기 · Inspector · 되돌리기 · 저장 | 가능 (10 — 툴 · Inspector · Undo · Ctrl+S) | — (Prefab 오버라이드 · 그룹은 v0.6) | v0.5 ✅ |
| G4 | 스크립트 작성 | 편집기 안에서 쓰기 · 오류 줄 · 적용, `ctx.` 자동 완성 | 쓰기 · 오류 줄 · 적용 가능, 자동 완성 없음 (프로토타입만) | M6-T8 · M6-T4 | v0.5 |
| G5 | 게임 UI 제작 | HUD 문서를 만들고 값에 묶고 버튼 → 신호 | **불가** — 게임 UI 런타임 없음 (M3 계획) | M3-T1 ~ T5 (v0.5 는 JSON 편집 + 미리보기, 시각 디자이너 v0.6) | v0.5 · v0.6 |
| G6 | 테스트 플레이 | ▶ → 플레이 → ■ → 편집 상태 그대로, 멀티 = 서버 + 2 클라이언트 단추 | 단독 테스트 플레이 가능 (ADR-0053). 멀티 = "테스트" 창 · 서버 + N 프로세스 ✅ v0.8 ST-06-9 (ADR-0115) | TestService | v0.5 (단독) · v0.6 (멀티) |
| G7 | 디버깅 · 성능 | 오류의 파일 · 줄로 이동, 시스템 프로파일 | 트랩 = 파일:줄:열 (v0.9 M6-T4 줄 표 — 편집기 줄 이동은 M6-T8), 시스템 · 함수 프로파일 있음 (v0.9 M6-T4) | M6-T4 (줄 표 · 함수 프로파일) · 재생 디버거 v0.6 | v0.5 · v0.6 |
| G8 | 빌드 · 패키징 | [빌드] → .sbpkg + 누락 검사 — 에셋 포함 | **CLI 필요** (sbx build), 에셋 미포함 (26 0장) | M6-T3 · M6-T9 | v0.5 |
| G9 | 다른 PC 실행 | B 가 빈 캐시에서 패키지를 열어 같은 게임 (C1) | 패키지 열기는 가능 (--world), 에셋 · 진단 화면 부족 | M7-T3 (기존) + G8 | v0.5 |

**v0.5 알파 판정 = G1 ~ G4 · G6 (단독) · G8 · G9 가 통과, G5 는 JSON + 미리보기 수준, G7 은 줄 표 수준.** 시각 UI 디자이너 · 멀티 테스트 ·
디버거는 v0.6 (Closed Creator Alpha — 23).

## 2. 사용성 시험 계획 (외부 제작자)

```text
대상       처음 Sandbox 를 쓰는 제작자 5 ~ 8 명 (프로그래밍 경험 없음 2 이상 · TS/JS 경험 2 이상), 각자의 Windows PC (사양 기록)
과제       "오목 프로젝트" — ① 새 프로젝트 (보드게임 템플릿) ② 돌 이미지 2 장 가져오기 · Prefab 연결 ③ 판 배치 ④ 스크립트로 5 목 판정
           (onEvent action — M2-T5 (2)) ⑤ HUD (차례 · 승자) ⑥ 테스트 플레이 ⑦ 빌드 ⑧ 다른 PC 에서 실행 (진행자가 옮김)
진행       화면 녹화 · 소리 내어 생각하기, 진행자는 막힌 지 3 분 뒤에만 힌트 (힌트 횟수 기록), 과제마다 제한 시간 (① 5 분 · ④ 30 분 …)
지표       완료율 (과제별) · 소요 시간 · 오류 횟수 (진단 · 충돌 · 거절) · 도움말 · 힌트 의존 (횟수) · 혼란 지점 (녹화 표시) · 프로젝트 복구 성공
           (진행자가 과제 ⑥ 중 강제 종료 → 다음 시작의 복구) · 만족도 (SUS 10 문항) · 성능 (Studio 프레임 · 빌드 시간 — 자동 기록)
합격 (v0.5 알파 · 내부 목표)  ① ~ ⑦ 완료율 ≥ 80 %, CLI 사용 0, 충돌 0, 복구 성공 100 %, SUS ≥ 68 — 목표치이고 결과가 아니다
```

### 2.1 평가 양식 (참가자마다 한 장)

| 칸 | 값 |
|----|----|
| 참가자 id · 경험 · PC 사양 · Sandbox 판 (커밋) | |
| 과제 ① ~ ⑧ — 완료 (예 · 아니오 · 힌트로) · 시간 · 오류 · 힌트 수 | |
| 막힌 지점 (화면 · 시각 · 설명) | |
| 충돌 · 거절 · 진단 문장 (그대로 옮김) | |
| 복구 시험 결과 | |
| SUS 점수 · 자유 의견 | |

결과 칸은 실제 시험 뒤에만 채운다 (ALPHA-READINESS 에 요약).

## 3. 자동 시험으로 바꿀 수 있는 것

```text
G1  CTest studio_new_project ✅ (v0.8 M6-T1, ADR-0111) — 헤드리스 Studio 명령 (CommandService 의 studio.new-project — --new-project) → 프로젝트 →
    InWorld → sbx check 오류 0 · 경고 0 · 최근 목록 (studio.json), 같은 폴더 · 모르는 템플릿은 실패. 손으로 하는 홈 (폴더 글자 · 1 분 안) 은 MANUAL-QA
G2  unit — 가져오기 → .meta → 이름 바꾸기 → 참조 그대로 ✅ M6-T9 (test_asset_database · test_pack_materials · project_assets — CLI).
    Studio 화면 (끌어다 놓기 · 이름 바꾸기 UI) 은 [계획 v0.10 ST-06-3]
G8  CTest studio_build ✅ (v0.8 M6-T3, ADR-0112) — 명령 studio.build (--run-command · 일꾼) → 패키지 · A8 경고 → 빈 캐시에서 sbx check →
    누락 에셋 (A6) 은 열지 않음 · 패키지 없음. 열린 뒤 생긴 누락의 [빌드] 거절은 test_build_service
G9  M7-T3 A → B E2E (기존 계획)
복구 CTest studio_recovery ✅ (v0.8 M6-T7, ADR-0113) — 자동 저장 → 충돌 (--abort-after-autosaves) → 다시 시작 → 복구 제안 (--recover) →
    worlds/recovered-1 · 같은 tick · 해시 (불러오기가 확인) → 깨끗이 끝난 뒤에는 제안 없음
G6 멀티 CTest studio_test_session ✅ (v0.8 ST-06-9, ADR-0115) — 명령 studio.test-start → 서버 + 2 헤드리스 클라이언트 (흉내) → 들어옴 ·
    InWorld · 깨끗한 끊김 · 모두 종료 코드 0. 창 클라이언트 둘의 손 확인은 MANUAL-QA
레이아웃 CTest studio_layout ✅ (v0.8 M6-T6, ADR-0114) — 저장한 배치로 시작 → 같은 자리로 다시 저장 · 기억 → 기본 배치로 → 깨진 ini 옮김 →
    안전 모드는 쓰지 않음 (창을 실제로 옮기는 확인은 MANUAL-QA)
```

### 3.1 Studio 시험 층 (NG-07 — 2차 감사 4.7) ✅ v0.8 (ADR-0120)

```text
층 1  Editor Services 단위 시험 (ImGui 없음) — 명령 · Undo · 문서 · 빌드 · 테스트 실행 (ST-06-1 이 가능하게 한다)
층 2  헤드리스 Studio 흐름 — 서비스만으로 "새 프로젝트 → 에셋 가져오기 → 빌드 → --play" (위 G1 · G2 · G8 의 CTest 가 이 층)
층 3  GUI smoke — 창을 열고 N 프레임 · 패널마다 그리기 오류 없음. CI 에 GPU · 디스플레이가 없으면 건너뛰고 "건너뜀" 으로 적는다 (통과로 적지 않는다)
층 4  실기 QA (16 형식 — 명령 · OS · GPU · 결과 · 커밋) · 외부 사용성 시험 (2장, ST-06-12)
규칙  G1 ~ G9 마다 증거 층을 적는다 — 층 3 · 4 만 있는 기준은 Closed Creator Alpha 진입 (roadmap/release-gates 3장) 때 수동 기록이 있어야 한다
지금  층 2 = CTest studio_flow_e2e (새 프로젝트 → sbx asset import → studio.build → 빈 캐시에서 패키지 플레이) 와 기준별 CTest.
      층 3 = CTest studio_gui_smoke (Windows · D3D12 WARP · 1366×768 — 홈 · 모든 창 워크스페이스 · 배치 저장). CI 또는 SBX_GUI_TESTS=1 일 때만
      창을 띄우고, 아니면 SKIPPED (통과로 적지 않는다). CI windows-msvc 에서 실제로 창을 띄워 통과 — 첫 기록 2026-11-11 (실행
      42940155797, 커밋 274f7e6: Debug 6.1 s · RelWithDebInfo 4.8 s — CI 로그). 층 4 = qa/QA-EVIDENCE 양식
```

| 기준 | 층 1 (단위) | 층 2 (헤드리스 흐름) | 층 3 (GUI smoke) | 층 4 (실기 — QA-EVIDENCE) |
|------|-------------|----------------------|------------------|---------------------------|
| G1 프로젝트 생성 | test_project_service · test_studio_home_data | studio_new_project · studio_flow_e2e | studio_gui_smoke (홈) | MANUAL-QA UI-2 [ ] |
| G2 에셋 | test_asset_database | studio_flow_e2e (CLI 가져오기) | — | 끌어다 놓기 화면 없음 (v0.10) |
| G3 월드 편집 | test_editor · test_undo (setField) | test_editor_session | studio_gui_smoke (워크스페이스) | MANUAL-QA UI-1 [x] 부분 |
| G4 스크립트 | test_script_editor | — | studio_gui_smoke (콘텐츠 창) | MANUAL-QA [ ] |
| G5 게임 UI | test_game_ui_layer | — | — | MANUAL-QA [ ] |
| G6 테스트 플레이 | test_test_service | test_world_panel · studio_test_session | — | MANUAL-QA ST-06-9 [ ] |
| G7 디버깅 | test_console_panel · test_vm (줄 표 · 호출 경로) · test_diagnostic_help (고치는 법 · 문구 골든) | — | studio_gui_smoke (콘솔 · 통계) | MANUAL-QA UI-3 [ ] |
| G8 빌드 | test_build_service | studio_build · studio_flow_e2e | — | MANUAL-QA M6-T3 [ ] |
| G9 다른 PC | — | studio_flow_e2e (빈 캐시 패키지 플레이) | — | M7-T3 [계획] |

### 3.2 G10 Studio 접근성 (2차 감사 SG-08) [계획 v0.13 전]

```text
G10  키보드만으로 G1 ~ G3 (홈 · 가져오기 · 배치) 를 끝낼 수 있다 · 글자 크기 배율 · 고대비 테마 — 게임 UI 접근성 (20 5장) 과 별개로 도구 자체
```
