# 기능 매트릭스 — 무엇이 되고 무엇이 안 되는가

> **보는 문서.** 주요 기능마다 "지금 쓸 수 있는가 · 어느 버전에서 되는가 · 근거가 무엇인가" 를 한 줄로 적는다.
> 태스크의 상태 · 목표 버전은 [태스크 등록부](../roadmap/task-registry.md) 가 정본이고, 이 표는 그것을 기능 관점으로 묶은 것이다.
> 설계 상세는 "관련 문서" 칸의 규범 문서에 있다 — 여기서 다시 설명하지 않는다.

| 항목 | 내용 |
|---|---|
| 문서 목적 | 구현된 기능과 예정 기능을 가른다 |
| 문서 상태 | 확정 |
| 마지막 검증일 | 2026-11-12 (master `1cce764` 위 — 등록부 · 소스 경로 · CTest 이름 대조. 창 확인은 하지 않았다) |
| 갱신 시점 | 태스크가 Done 이 되어 어떤 줄의 분류가 바뀔 때 ([문서 가이드](../development/DOCUMENTATION_GUIDE.md) 3장) |

## 목차

1. [읽는 법](#1-읽는-법)
2. [Studio (제작 도구)](#2-studio-제작-도구)
3. [Runtime / Engine](#3-runtime--engine)
4. [제작 · 배포 흐름](#4-제작--배포-흐름)
5. [플랫폼 서비스](#5-플랫폼-서비스)
6. [분류별 요약](#6-분류별-요약)

## 1. 읽는 법

| 분류 | 뜻 |
|---|---|
| ✅ 사용 가능 | 코드가 제품 타깃에 있고 등록된 자동 시험이 돈다. **"창" 칸이 ⬜ 면 사람이 화면으로 확인한 기록은 아직 없다** |
| 🟡 일부 | 일부만 된다 — 되는 것과 빠진 것을 적는다 |
| 🧪 시험 전용 | 시험 · 헤드리스 경로로만 있고 사용자가 쓰는 길이 없다 |
| 📐 설계만 | 설계 문서 · 태스크 카드는 있고 코드는 없다 (등록부 Planned · Proposed) |
| ❓ 계획 불명확 | 코드도 없고 등록부에 그 일을 맡은 태스크도 찾지 못했다 |
| 범위 외 | 지금 로드맵 (v1.0 까지) 의 범위가 아니다 |

- **목표 버전** — 그 기능이 (남은 부분까지) 되는 버전. 버전은 식별자다: v0.10 은 v0.9 다음이다.
- **창** — [MANUAL-QA](../qa/MANUAL-QA.md) 에 그 기능의 줄이 있고 확인됐는가 (✅ 확인 · 🟡 일부 · ⬜ 안 함 · — 줄 없음 / 화면 없음).
- **검증 근거** — CTest 이름 또는 시험 파일 (`tests/unit/…`). 소스 위치는 [17-SOURCE-MAP](../17-SOURCE-MAP.md).

## 2. Studio (제작 도구)

| ID | 기능 | 목표 버전 | 현재 상태 | 창 | 검증 근거 | 관련 문서 |
|---|---|---|---|---|---|---|
| F-101 | 프로젝트 생성 · 열기 · 최근 목록 (Studio 홈) | v0.8 | ✅ 사용 가능 | ⬜ | CTest `studio_new_project` · `test_project_service` · `test_studio_home_data` | [25](../25-STUDIO-ARCHITECTURE.md) · [29](../29-STUDIO-UI.md) · M6-T1 · UI-2 |
| F-102 | 월드 (Scene) 저장 · 불러오기 — 월드 = 세이브 폴더, Ctrl+S | v0.1 | ✅ 사용 가능 | ⬜ | CTest `det_world_save_load` · `server_project_saves_root` · `test_world_ops` | [09](../09-SERIALIZATION.md) 3장 · [10](../10-EDITOR.md) |
| F-103 | 한 프로젝트 안에서 새 월드 (Scene) 를 더 만드는 화면 | — | ❓ 계획 불명확 | — | 새 프로젝트 · `sbx new --from` 은 시작 월드 하나를 만든다. Studio 에서 월드를 추가하는 화면 · 태스크는 찾지 못했다 | [18](../18-PLATFORM.md) 5.2 |
| F-104 | Scene Hierarchy — "목록" 패널 (태그 · Prefab · 이름 거르기) | v0.1 | ✅ 사용 가능 | ⬜ | `test_editor` | 10 2장 |
| F-105 | Hierarchy 검색 · 편집기 그룹 · 부모-자식 | v0.12 | 📐 설계만 | — | — (ST-06-6) | 25 7장 |
| F-106 | Object 생성 · 삭제 (Place · Erase 툴) | v0.1 | ✅ 사용 가능 | ⬜ | `test_editor` · CTest `det_edit_session` | 10 3장 |
| F-107 | Prefab 정의 편집 ("콘텐츠" 창 — JSON, 오버레이 적용 / 팩 파일 저장) | v0.1 · v0.9 | 🟡 일부 | ⬜ | `test_editor` · `test_document_service` — 폼 없이 JSON 을 고친다 | 10 8장 · [11](../11-CONTENT-SCHEMA.md) |
| F-108 | Prefab 인스턴스 오버라이드 · 변형 | v0.12 | ✅ 구현 (변형 inherits · 차이 기반 오버라이드 · EditContent.propagate — ADR-0158) | ⬜ | test_prefab_variants · test_editor_session (ST-06-5) | 25 7장 |
| F-109 | Prefab 배치 (팔레트에서 골라 클릭 · 끌기) | v0.1 | ✅ 사용 가능 | ⬜ | `test_editor` · `test_pack_groups` | 10 3장 |
| F-110 | 프로젝트 PNG 가져오기 (.meta 안정 id) | v0.6 | 🟡 일부 — CLI (`sbx asset import`) 로만 | — | CTest `project_assets` · `project_own_png` · `test_asset_database` | [26](../26-ASSET-PIPELINE.md) · M6-T9 |
| F-111 | Asset Browser (썸네일 · 끌어다 놓기 · 참조 찾기 · 안전 지우기 · 다시 가져오기) | v0.10 | ✅ "에셋" 창 (목록 · 썸네일 · 가져오기 · 참조 · 옮기기 · 다시 읽기 · 안전 지우기 · 놓기 → UI 디자이너) — 폴더 나무 · 파일 감시 [계획] | `test_asset_browser` · `test_sprites` hot reload | — | 26 · 25 5장 · ADR-0146 |
| F-112 | Object 선택 · 이동 (Select · Move 툴) | v0.1 | ✅ 사용 가능 | ⬜ | `test_editor` | 10 3장 |
| F-113 | Object 회전 · 크기 조절 (전용 툴 · 기즈모) | — | ❓ 계획 불명확 | — | 툴은 Select · Move · Place · TerrainBrush · Erase 다섯 (`editor/Editor.hpp`). 회전은 Inspector 에서 `core.transform.rotation` 값으로만 고친다. Transform 에 크기 필드가 없다. 등록부에 이 일을 맡은 태스크가 없다 | [감사](../audit/DOCS-AUDIT-2026-11-12.md) G-09 |
| F-114 | Component 추가 · 제거 · 속성 편집 | v0.1 | ✅ 사용 가능 | ⬜ | `test_editor` (ChangeComponent · AddComponent · RemoveComponent) | 10 4장 |
| F-115 | Inspector (리플렉션 필드 → 자동 폼, 스크립트 컴포넌트 포함) | v0.1 · v0.3 | ✅ 사용 가능 | ⬜ | `test_editor` · `test_components` | 10 4장 · [02](../02-ECS.md) |
| F-116 | Undo / Redo (월드 명령 + 문서 편집 한 줄) | v0.1 · v0.8 | ✅ 사용 가능 | ⬜ | `test_undo` · `test_editor_undo_line` | 10 6장 · ST-06-1 |
| F-117 | Script IDE — 편집기 · 자동 완성 · hover · 밑줄 · 정의 · 참조 · 이름 바꾸기 · 빠른 수정 | v0.9 | ✅ 사용 가능 | ⬜ | `test_language_service` · `test_language_index` · `test_script_editor` · `test_script_navigation` | [27](../27-SCRIPT-IDE.md) · M6-T8 · ST-06-8 |
| F-118 | 스크립트 재생 디버거 (중단점 · 단계 · 지역 · 감시 — 기록한 리플레이 위에서) | v0.9 | ✅ 사용 가능 | ⬜ | `test_script_debugger` · `test_script_debug_session` · `test_script_debug_service` | 27 6장 · ST-06-7 |
| F-119 | Play / Test Mode (편집 월드를 맡기고 복사본으로 플레이) | v0.2 | ✅ 사용 가능 | ⬜ | `unit_network` · `unit_client` (ADR-0053) | 10 · [08](../08-NETWORK.md) |
| F-120 | 테스트 세션 — 서버 + N 클라이언트 · 지연 · 손실 모의 | v0.8 | ✅ 사용 가능 | ⬜ | CTest `studio_test_session` · `test_test_service` | 25 3장 · ST-06-9 |
| F-121 | 편집 데이터 자동 저장 · 크래시 복구 | v0.8 | ✅ 사용 가능 | ⬜ | CTest `studio_recovery` · `test_autosave_service` | 25 7장 · M6-T7 |
| F-122 | 빌드 화면 (검사 · 패키지 · 누락 보고 · Export Profile) | v0.8 | ✅ 사용 가능 | ⬜ | CTest `studio_build` · `test_build_service` | 25 5장 · M6-T3 |
| F-123 | 레이아웃 저장 · 명령 팔레트 · 단축키 표 · 테마 | v0.8 | ✅ 사용 가능 | 🟡 (1366×768 셸만) | CTest `studio_layout` · `test_layout_service` · `test_command_palette` | 29 · M6-T6 · ST-06-2 · UI-1 |
| F-124 | Game UI Designer (HUD · 버튼 · 메뉴를 화면에서 편집) | v0.10 | 🟡 구현 — "UI 디자이너" 창 (계층 · 라이브러리 · 미리보기 · 끌기 · 노드 JSON · 저장), 수동 QA 남음 | `test_ui_design_service` · `test_ui_designer_panel` · CTest `ng18_board_flow` (sbx ui) | — (ST-06-4 수동 QA) | 25 8장 · ADR-0145 · 0147 |
| F-125 | Visual Trigger 편집 — 폼 | v0.2 | ✅ 사용 가능 | ⬜ | `test_trigger_form` | 10 8장 · 11 10장 |
| F-126 | 트리거 노드 그래프 캔버스 · 콘텐츠 데이터 폼 | v0.12 | 📐 설계만 | — | — (ST-06-10) | 26 6장 |
| F-127 | 지형 칠하기 (TerrainBrush) | v0.1 | ✅ 사용 가능 | ⬜ | `test_editor` | 10 3장 · [05](../05-WORLD.md) |
| F-128 | 2D 타일맵 · 타일셋 | v0.12 | ✅ 지형 머티리얼 + 그림 (시트의 칸) — 칠하기 · 세이브 · 해시 그대로, 장식 층 · 자동 타일 [계획] | `test_tilemap` | — | 05 3.2 · ADR-0156 |
| F-129 | Scene / Game 뷰 분리 · 떼어 낸 창 | v0.12 | 📐 설계만 (P3 — 시작 때 필요성 판정) | — | — (ST-06-11) | ADR-0123 |
| F-130 | 리플레이 뷰어 (열기 · 재생 · 멈춤 · seek · 기록) | v0.8 | ✅ 사용 가능 | ⬜ | CTest `client_replay` · `replay_record` | 09 4장 · M5-T1 ~ T3 |
| F-131 | 여러 사람이 같은 월드를 함께 편집 (역할 · 권한) | v0.1 | ✅ 사용 가능 (LAN) | ⬜ | `test_editor_roles` · CTest `det_edit_session` | 10 7장 |
| F-132 | 템플릿 3종 · 튜토리얼 | v0.10 · v0.13 | 🟡 일부 — 게임 템플릿 틀 (`sbx new --template` · Studio 고르기 — ADR-0144) · 템플릿 셋 minigame · board (틱택토) · survival (모닥불) ✅ (M6-T2), 튜토리얼 (M6-T5) 남음, `sbx new --from <시나리오>` · [표준 라이브러리 안내](../tutorials/stdlib.md) | `test_project_template` · CTest `project_templates` · `sample_tictactoe` · `sample_campfire` · `project_new_from_scenario` | 20 10장 · ADR-0144 |
| F-133 | Editor Plugin API | v0.21 | 📐 설계만 | — | — (NG-10) | 25 9장 |

## 3. Runtime / Engine

| ID | 기능 | 목표 버전 | 현재 상태 | 검증 근거 | 관련 문서 |
|---|---|---|---|---|---|
| F-201 | ECS · Component 관리 (Sparse Set · 리플렉션 · 스크립트 동적 컴포넌트) | v0.1 · v0.3 | ✅ 사용 가능 | CTest `unit_ecs` · `property_ecs` | [02](../02-ECS.md) |
| F-202 | System 실행 · 스케줄링 (30 TPS 고정 틱 · 단계 파이프라인) | v0.1 | ✅ 사용 가능 | CTest `det_*` | [03](../03-SIMULATION.md) 2장 |
| F-203 | 단계 안 병렬 · 스크립트 Worker · sleep | v0.22 (측정 뒤) | ⚠️ 확인 필요 — 측정이 예산 초과를 보일 때만 시작, 아니면 v1.x | — (RT-09-3 ~ 5 Review) | [22](../22-RUNTIME-V2.md) |
| F-204 | 결정론 (WorldHash · 골든 · 리플레이 · 세이브 왕복) | v0.1 | ✅ 사용 가능 | CTest `det_*_golden` · `det_replay_*` | [04](../04-DETERMINISM.md) |
| F-205 | 교차 툴체인 결정론 (세 툴체인이 한 리플레이를 같은 해시로) | v0.22 | 📐 설계만 | — (RH-22-7). 지금 골든은 툴체인마다 따로다 (ADR-0012) | [stabilization](../roadmap/stabilization.md) |
| F-206 | Script Runtime — 컴파일러 · 검증기 · VM (연료 · 메모리 예산) · 표준 라이브러리 | v0.3 · v0.5 | ✅ 사용 가능 | CTest `unit_script` · `sample_stdlib_tour` | [19](../19-SANDBOXSCRIPT.md) |
| F-207 | 이벤트 · 콜백 — server onStart · onTick · onEvent · onStop · onMigrate, client onUpdate | v0.3 ~ v0.6 | ✅ 사용 가능 | `test_script_callbacks` · `test_client_scripts` | 19 17 ~ 21장 |
| F-208 | Capability 실행 검사 (호출 토큰) | v0.10 | ✅ 사용 가능 | `test_script_capability` | ADR-0137 · RT-06-1 |
| F-209 | 타이머 · defineState · client 수명주기 · onAction · 입력 Context | v0.10 | ✅ — 타이머 (ctx.after · every · cancelTimer) · 월드 상태 (defineState · ctx.state · setState) · client 수명주기 (onStart · onLateUpdate · onStop) · client onEvent (복제 이벤트 — 프로토콜 19) · client onAction · 입력 Context (메뉴가 게임 키를 먹는다) ✅ | `test_script_timers` · `test_script_state` · `test_client_lifecycle` · `test_client_events` · `test_client_actions` · `test_play_keys` | ADR-0138 · 0139 · 0140 · 0141 · 0142 · 0143 · 22 3 · 6 · 9장 |
| F-210 | 렌더링 — 자체 RHI, D3D12 · Vulkan · Metal, 스프라이트 · 지형 · ImGui | v0.1 | ✅ 사용 가능 (자동) · ⚠️ 실기는 Windows 일부만 | CTest `render_tests_warp` · `render_tests_lavapipe` · `render_tests_metal` | [06](../06-RENDERING.md) |
| F-211 | 외부 Graphics SDK 연동 | — | ❓ 계획 불명확 | 렌더는 자체 RHI 가 세 그래픽 API 를 직접 쓴다. 다른 그래픽 SDK 와 잇는 계획 · 태스크는 문서에서 찾지 못했다 | 06 · ADR-0006 |
| F-212 | onRender · Draw API (스크립트가 그리기 명령을 낸다) | v0.11 | ✅ client onRender — drawColor · drawLine · drawCircle · drawRect · drawText (월드 좌표, 프레임 1,024) | `test_client_render` (RT-06-9) | 22 · ADR-0152 |
| F-213 | 입력 — 키 · 마우스 · ActionMap · 팩 게임 키 · 키 다시 묶기 · 게임패드 (게임 UI) | v0.1 ~ v0.7 | ✅ 사용 가능 | CTest `unit_platform` · `test_key_bindings` · `test_gamepad` | [07](../07-PLATFORM.md) |
| F-214 | 고급 입력 — Hold · Double Tap · Combo · 게임패드 바인딩 | v0.12 | ✅ 구현 (팩 input.json triggers · Pad.* — ADR-0160) | test_input_triggers (RT-06-13) | 22 6장 |
| F-215 | 물리 · 충돌 — 이동 · 원 분리 충돌 · raycast · overlap 질의 | v0.1 · v0.5 | 🟡 일부 | CTest `det_*` · `test_script_engine_queries` | 03 2장 |
| F-216 | 물리 단계 콜백 · PhysicsCommand · layer · mask · 충돌 · 트리거 Enter / Exit | v0.11 | ✅ onPrePhysics (8b) · 물리 명령 · 트리거 · 충돌 · 트리거 Enter / Exit (16a) | `test_script_prephysics` · `test_physics_commands` · `test_collision_trigger` · `test_contact_events` (RT-06-2 ~ 5) | 22 5장 · ADR-0148 ~ 0151 |
| F-217 | ShapeCast · Substep · CCD · 재질 | v0.12 · v0.18 | 🟡 ShapeCast ✅ (ctx.shapeCast — v0.12 RT-06-6), Substep · CCD · 재질 [계획 v0.18] | `test_script_engine_queries` | 22 5장 · ADR-0155 |
| F-218 | 리소스 관리 — PNG 디코드 · 아틀라스 · 비동기 로드 · 캐시 | v0.1 · v0.6 | 🟡 일부 — 해제 · Hot Reload · 스트리밍 · 메모리 예산 없음 | CTest `render_tests_*` (`test_sprites`) | 06 7장 · 26 |
| F-219 | 에셋 스트리밍 · 메모리 예산 | v0.18 | 📐 설계만 | — (RT-08-4) | 22 11장 |
| F-220 | 네트워크 · 동기화 — 서버 권위 · 차분 스냅숏 · Interest · 역할 · 재접속 | v0.1 | ✅ 사용 가능 (LAN · 신뢰 환경 한정) | CTest `net_convergence` · `net_server_probe_smoke` · `unit_network` | [08](../08-NETWORK.md) |
| F-221 | 보안 전송 (암호화 · 인증) · 방 · JoinTicket | v0.16 | 📐 설계만 — 이것 없이 인터넷 방을 열지 않는다 | — (PS-14-1 · PS-14-2) | [24](../24-SECURITY-GATES.md) N2 |
| F-222 | 클라이언트 예측 · 고정 Tick · InputFrame | v0.17 | 📐 설계만 | — (RT-07-1 ~ RT-07-7) | 22 · ADR-0087 |
| F-223 | 지연 · 손실 모의 전송 (`SimulatedTransport`) | v0.8 | ✅ 사용 가능 — 테스트 세션에서 고른다 (그 전에는 🧪 시험 전용이었다) | CTest `studio_test_session` | 08 · ADR-0115 |
| F-224 | 세이브 · 버전 마이그레이션 (엔진 스키마 · 스크립트 컴포넌트 · 게임 버전 onMigrate) | v0.1 ~ v0.6 | ✅ 사용 가능 | `test_save_migration` · `unit_persist` | 09 6장 · NG-04 |
| F-225 | 게임 UI 런타임 (선언 HUD · 메뉴 · 바인딩 · 버튼 → 서버 신호 · 입력 컨트롤) | v0.7 | ✅ 사용 가능 | CTest `unit_ui` · `test_game_ui_layer` | [20](../20-CREATOR-ALPHA.md) 2장 |
| F-226 | 다국어 (문자열 표 · 로케일 · 폰트 대체 · UI 배율) | v0.7 | ✅ 사용 가능 | `test_ui_strings` · `test_font_fallback` | 20 6장 |
| F-227 | 오디오 — 소리 이벤트 (client) | v0.5 | ✅ v0.11 부터 팩 sounds.json 의 소리를 재생 (헤드리스는 Null) | `test_client_scripts` | 07 7장 · ADR-0095 |
| F-228 | 오디오 출력 백엔드 · 믹서 · 2D 위치 | v0.11 · v0.12 | 🟡 구현 — 믹서 · waveOut · AudioQueue · ALSA · WAV · sounds.json (세 OS 수동 QA 남음), 위치 소리 · 반복 · 멈춤 · 버스 볼륨 (RT-06-15 ✅) | `test_audio_output` | ADR-0095 · 0153 · 0159 |
| F-229 | 2D 애니메이션 클립 · 상태 기계 | v0.12 | ✅ 구현 (표시 쪽 — 렌더 틱 · ADR-0157) | test_animation · test_client_scripts (RT-06-14) | 22 |
| F-230 | 창 없는 실행 (`HeadlessWindow` · `--ticks N --exit`) | v0.1 | 🧪 시험 전용 (서버는 원래 창이 없다 — 클라이언트의 헤드리스는 시험 · CI 용) | CTest `client_headless_smoke` · `studio_flow_e2e` | 07 · [13](../13-TESTING.md) |
| F-231 | 3D | v1.x | 범위 외 (결정 D-16 뒤) | — | 22 · releases v1.x |
| F-232 | 성능 — 50k 개체 30 TPS | v0.1 | ⚠️ 확인 필요 — 2 코어 측정은 예산 밖, 판정 측정 대기 | `sbx_bench` 기록 ([14](../14-PERFORMANCE.md) 7.12) | 14 |

## 4. 제작 · 배포 흐름

| ID | 기능 | 목표 버전 | 현재 상태 | 검증 근거 | 관련 문서 |
|---|---|---|---|---|---|
| F-301 | 게임 프로젝트 (매니페스트 · 검사) · `sbx new` · `check` | v0.2 | ✅ 사용 가능 | CTest `project_new_empty` · `project_minimal_validate` · `project_bad_manifest_rejected` | [18](../18-PLATFORM.md) 5장 |
| F-302 | GameMode · 점수 · 팀 · 플레이어 행동 · Rule · Behavior | v0.2 | ✅ 사용 가능 | CTest `project_game_start` · `sample_playthrough` | 11 · 03 |
| F-303 | 게임 패키징 (.sbpkg 형식 3 — 에셋 · digest · 빌드 스냅숏) | v0.2 · v0.6 | ✅ 사용 가능 | CTest `project_package` · `tech_alpha_e2e` · `test_game_package` | 09 9장 |
| F-304 | 게임 실행 — Player 모드 (패키지 열기 · 최근 게임 · 오류 화면) | v0.8 | ✅ 사용 가능 (창 ⬜) | CTest `player_mode` | ADR-0121 · NG-03 |
| F-305 | Export Profile (대상 OS · 백엔드) | v0.6 | 🟡 일부 — Windows 만 검증, 다른 대상은 경고 | `test_export_profile` | NG-08 |
| F-306 | 패키지 관리자 (의존 해석 · 잠금 · 설치 · Git 소스 · 팩 사이 import) | v0.4 | ✅ 사용 가능 | CTest `project_install` · `sample_stash` | 18 6.4 |
| F-307 | 충돌 보고 · `sbx doctor` · 안전 모드 · 새 버전 알림 | v0.5 | ✅ 사용 가능 | CTest `crash_report_server` · `crash_report_client` · `project_doctor` · `client_safe_mode` | 20 7장 |
| F-308 | 배포 묶음 (`sbx_release`) | v0.5 | ✅ 사용 가능 | CTest `release_bundle` | [15](../15-BUILD.md) |
| F-309 | 다른 PC 에서 Studio 로 만든 게임 실행 (A → B, 자기 에셋 포함) | v0.13 | 🟡 일부 — CLI · 같은 PC 의 다른 폴더 · 빈 캐시까지 (`tech_alpha_e2e` · `project_own_png`). 두 번째 PC 는 해 보지 않았다 | — (M7-T3) | [28](../28-CREATOR-UX-GATES.md) G9 |
| F-310 | 패키지 서명 · 자동 업데이트 · 되돌리기 | v0.14 · v0.23 | 📐 설계만 | — (PS-12-3 · PS-20-1) | 24 P3 · O2 |

## 5. 플랫폼 서비스

코드가 없다. 서비스 저장소와 엔진 저장소의 경계 (D-06 — PS-12-1) 가 먼저다.

| ID | 기능 | 목표 버전 | 현재 상태 | 관련 문서 |
|---|---|---|---|---|
| F-401 | 계정 · 인증 · 패키지 레지스트리 · 검색 · 신고 | v0.14 | 📐 설계만 (초안) | [v0.14](../roadmap/versions/v0.14.md) |
| F-402 | Studio 에서 게시 · Player 설치 · 업데이트 · 출시 채널 · 롤백 | v0.15 | 📐 설계만 | [v0.15](../roadmap/versions/v0.15.md) |
| F-403 | 방 · 자체 서버 등록 · 무료 세션 · 친구 · 관전 | v0.16 | 📐 설계만 | [v0.16](../roadmap/versions/v0.16.md) |
| F-404 | 상시 호스팅 · NetworkModule | v0.18 | 📐 설계만 | [v0.18](../roadmap/versions/v0.18.md) |
| F-405 | 결제 (테스트 모드) · 에셋 마켓 · 정산 · 분석 · 등급전 | v0.19 · v0.20 | 📐 설계만 | [v0.19](../roadmap/versions/v0.19.md) · [v0.20](../roadmap/versions/v0.20.md) |
| F-406 | 팀 ACL · CI/CD · 문서 동시 편집 · `sbx lsp` | v0.21 | 📐 설계만 | [v0.21](../roadmap/versions/v0.21.md) |
| F-407 | 운영 런북 · 개인정보 정책 | v0.23 | 🔴 차단 — 내용이 정의되지 않았다 (등록부 Missing) | [v0.23](../roadmap/versions/v0.23.md) |

## 6. 분류별 요약

| 분류 | 기능 |
|---|---|
| 실제 사용 가능 | 월드 편집 (배치 · 이동 · 지우기 · 지형 · Inspector · Undo) · 저장 · Studio 홈 · 빌드 화면 · 자동 저장 · 테스트 세션 · Script IDE · 디버거 · 리플레이 · 게임 UI 런타임 · 다국어 · 스크립트 · 패키지 · 패키지 관리자 · LAN 멀티플레이 · 충돌 보고 |
| 일부만 구현 | Prefab 편집 (JSON) · 에셋 가져오기 (CLI) · 물리 · 리소스 관리 · Export Profile · 템플릿 · A → B 실행 |
| 시험 · 시뮬레이션 전용 | 소리 이벤트 (Null 백엔드) · 클라이언트 헤드리스 실행 |
| 설계만 | Asset Browser · UI Designer · 타이머 · client 수명주기 · 물리 단계 · 충돌 이벤트 · onRender · 오디오 출력 · 플랫폼 서비스 전부 |
| 계획 불명확 | 프로젝트 안 월드 추가 화면 (F-103) · 회전 · 크기 툴 (F-113) · 외부 Graphics SDK 연동 (F-211) |
| 범위 외 | 3D (F-231) |

"사용 가능" 중 Studio 쪽은 대부분 **창 확인이 남아 있다** — 자동 시험은 로직과 헤드리스 흐름을 보고, 화면이 실제로 그렇게 보이는지는
[MANUAL-QA](../qa/MANUAL-QA.md) 가 본다.
