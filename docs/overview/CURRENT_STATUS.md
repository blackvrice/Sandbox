# 현황 대시보드 — 지금 어디까지 왔나

> **보는 문서.** 소스를 읽지 않고도 "어느 버전인가 · 무엇이 되는가 · 무엇이 남았나" 를 답한다. 숫자 표 (1장) 는
> `tools/gen_roadmap.py` 가 [태스크 등록부](../roadmap/task-registry.md) 에서 집계한다. 나머지는 아래 "마지막 검증일" 에 소스 · 시험 · 문서를
> 대조해 손으로 적었다 — 낡았다고 의심되면 등록부와 [DEVELOPMENT_LOG](../../DEVELOPMENT_LOG.md) 가 이긴다.

| 항목 | 내용 |
|---|---|
| 문서 목적 | 프로젝트 현황 한 장 — 버전 · 모듈 · 막힌 것 · 다음 할 일 |
| 문서 상태 | 확정 (1장은 자동 생성) |
| 마지막 검증일 | 2026-11-12 (master `1cce764` 위 — 검증한 것 · 못 한 것은 5장) |
| 다음 문서 | [전체 로드맵](../roadmap/MASTER_ROADMAP.md) → [버전별 체크리스트](../roadmap/versions/) → [기능 매트릭스](FEATURE_MATRIX.md) → [모듈 안내](MODULES.md) |

## 목차

1. [요약](#1-요약)
2. [상태 표시의 뜻](#2-상태-표시의-뜻)
3. [모듈별 현황](#3-모듈별-현황)
4. [막힌 것 · 다음 할 일](#4-막힌-것--다음-할-일)
5. [검증 기록](#5-검증-기록)

## 1. 요약

<!-- GENERATED:BEGIN — tools/gen_roadmap.py 가 쓴다. 이 구간을 손으로 고치지 않는다 (정본: task-registry · releases) -->

| 항목 | 현재 상태 (등록부 · CMake · MANUAL-QA 에서 집계) |
|---|---|
| 제품 버전 (CMake `project VERSION`) | 0.7.0 |
| 최근 완료 (출시) 버전 | [v0.7](../roadmap/versions/v0.7.md) — 게임 UI Runtime · 다국어 기반 |
| 태스크는 끝났고 출시 판정을 기다리는 버전 | [v0.8](../roadmap/versions/v0.8.md) (16 / 17 Done, 수동 QA 1 / 13) · [v0.9](../roadmap/versions/v0.9.md) (5 / 5 Done, 수동 QA 0 / 5) · [v0.10](../roadmap/versions/v0.10.md) (6 / 8 Done, 수동 QA 0 / 3) · [v0.11](../roadmap/versions/v0.11.md) (6 / 7 Done, 수동 QA 0 / 2) · [v0.12](../roadmap/versions/v0.12.md) (10 / 10 Done, 수동 QA —) |
| 현재 개발 버전 | [v0.13](../roadmap/versions/v0.13.md) — Closed Creator Alpha (5 / 12, 42 %) |
| 다음 목표 버전 | [v0.14](../roadmap/versions/v0.14.md) — 계정 · 레지스트리 · 서명 |
| 전체 진행 (v0.5 ~ v1.0 태스크) | 91 / 181 Done — 50 % |
| Closed Creator Alpha (v0.13) 까지 남은 태스크 | 11 |
| v1.0 (GA) 까지 남은 태스크 | 90 |
| 수동 QA (v0.7 이후 버전 줄) | 8 / 45 확인 |

<!-- GENERATED:END -->

| 항목 | 현재 상태 (2026-11-12 조사) |
|---|---|
| 전체 개발 상태 | 엔진 · 스크립트 · 패키지 · 게임 UI 런타임은 자동 시험으로 검증됨. Studio (v0.8) · Script IDE (v0.9) 는 코드 · 자동 시험이 끝났고 **창에서 사람이 확인하는 QA 가 남아** 출시 (0.8.0 · 0.9.0) 를 못 했다. v0.10 은 첫 태스크만 끝났다 |
| 완료된 주요 기능 | ECS · 30 TPS 결정론 시뮬레이션 · 세이브 · 리플레이 · 서버 권위 네트워크 (LAN) · 월드 에디터 · 세 렌더 백엔드 · 게임 프로젝트 · .sbpkg · SandboxScript (컴파일러 · VM · 표준 라이브러리) · 패키지 관리자 · 충돌 보고 · 프로젝트 에셋 · 게임 UI 런타임 · 다국어 |
| 진행 중인 기능 | Studio 기본 환경 (창 QA 대기) · Script IDE (창 QA 대기) · Creator Foundation v0.10 (RT-06-1 ✅, 나머지 7 태스크 미착수) |
| 주요 미구현 기능 | Prefab 오버라이드 · 애니메이션 · 계정 · 레지스트리 · 보안 전송 · 예측 · 호스팅 · 결제 — [기능 매트릭스](FEATURE_MATRIX.md) |
| 현재 차단 요인 | ① v0.8 · v0.9 창 QA (사람이 창을 봐야 한다) ② v0.1 의 Linux · macOS 실기 검증 · 50k 성능 판정 ③ 트랙 C 전체가 저장소 경계 결정 D-06 (PS-12-1) 을 기다림 — 4장 |
| 다음 마일스톤 | Closed Creator Alpha (v0.13) — 남은 태스크 수는 위 표, 목록은 [MASTER_ROADMAP](../roadmap/MASTER_ROADMAP.md) 3장 |

## 2. 상태 표시의 뜻

모든 보는 문서 (이 문서 · [기능 매트릭스](FEATURE_MATRIX.md) · [로드맵](../roadmap/MASTER_ROADMAP.md) · 버전 문서) 가 같은 표시를 쓴다.
정의의 정본은 [문서 가이드](../development/DOCUMENTATION_GUIDE.md) 2장.

| 표시 | 뜻 | 태스크 등록부의 상태 |
|---|---|---|
| ✅ 완료 | 완료 기준을 채웠고 자동 시험 (또는 기록된 수동 QA) 이 있다 | Done |
| 🟡 진행 중 | 일부 구현 · 완료 기준 미충족 (버전은 "태스크는 끝났지만 출시 판정 전" 도 여기) | Planned · Proposed 인데 세부 체크리스트에 `[x]` 가 있음 |
| ⬜ 미착수 | 구현이 확인되지 않음 | Planned · Proposed |
| 🔴 차단 | 문제 · 선행 작업 · 정의 없음 때문에 진행할 수 없음 | Missing, 또는 선행이 끝나지 않음 (버전 문서의 "차단 여부" 칸) |
| ⚠️ 확인 필요 | 실제 상태를 충분히 검증하지 못함 (창 · 실기 · 측정 뒤 판정) | Review |

**코드가 있다는 것만으로 ✅ 를 붙이지 않는다.** 설계 · 구현 · 시험을 따로 적는다 — 구현 칸의 ✅ 는 소스가 있고 타깃에 묶여 있다는 뜻,
시험 칸의 ✅ 는 등록된 CTest 가 그 기능을 돈다는 뜻, "실기" 는 사람이 창 · GPU 로 본 기록 ([MANUAL-QA](../qa/MANUAL-QA.md)) 이다.

## 3. 모듈별 현황

모듈마다의 역할 · 구조 · 제약은 [모듈 안내](MODULES.md), 기능 단위는 [기능 매트릭스](FEATURE_MATRIX.md).

| 모듈 | 설계 | 구현 | 자동 시험 | 실기 (창 · GPU) | 현재 상태 | 상세 |
|---|---|---|---|---|---|---|
| ECS | ✅ [02](../02-ECS.md) | ✅ `core/ecs` | ✅ `unit_ecs` · `property_ecs` | 해당 없음 | ✅ 완료 | [모듈](MODULES.md#ecs) |
| 시뮬레이션 · 결정론 | ✅ [03](../03-SIMULATION.md) · [04](../04-DETERMINISM.md) | ✅ `core/simulation` · `core/systems` · `core/command` · `core/replay` | ✅ `det_*` (골든 · 리플레이 · 세이브 왕복) | 해당 없음 | ✅ 완료 — 교차 툴체인 재생 일치는 v0.22 (RH-22-7) | [모듈](MODULES.md#시뮬레이션--결정론) |
| 월드 · 세이브 | ✅ [05](../05-WORLD.md) · [09](../09-SERIALIZATION.md) | ✅ `core/world` · `core/persist` | ✅ `det_world_save_load` · `unit_persist` | 해당 없음 | ✅ 완료 | [모듈](MODULES.md#월드--세이브) |
| 콘텐츠 · 게임 규칙 | ✅ [11](../11-CONTENT-SCHEMA.md) | ✅ `core/content` · `core/trigger` · `core/game` | ✅ `content_eco_validate` · `sample_playthrough` | 해당 없음 | ✅ 완료 | [모듈](MODULES.md#콘텐츠--게임-규칙) |
| 물리 · 충돌 | 🟡 [03](../03-SIMULATION.md) 2장 · [22](../22-RUNTIME-V2.md) 5장 (다음 단계는 설계만) | 🟡 원 분리 충돌 · 이동 · raycast · overlap 질의만 | ✅ (있는 범위) | 해당 없음 | 🟡 진행 중 — layer · mask · Enter/Exit 이벤트 · PhysicsCommand 는 v0.11 | [v0.11](../roadmap/versions/v0.11.md) |
| Script 컴파일러 · VM | ✅ [19](../19-SANDBOXSCRIPT.md) | ✅ `script/lang` · `script/vm` · `script/api` | ✅ `unit_script` (퍼즈 · 보호 시험 포함) | 해당 없음 | ✅ 완료 | [모듈](MODULES.md#sandboxscript) |
| Script Runtime (Host API · 시스템 · 콜백) | ✅ 19 · [22](../22-RUNTIME-V2.md) | 🟡 `core/script` — server onStart · onPrePhysics · onTick · onEvent · onStop · onMigrate, client onStart · onEvent · onAction · onUpdate · onLateUpdate · onStop, 호출 토큰 | ✅ (있는 범위) | 해당 없음 | ✅ v0.11 의 Runtime 몫 — onPrePhysics · 물리 명령 · 충돌 · 트리거 이벤트 · onRender (RT-06-2 ~ 5 · 9), 다음은 예측 (v0.17) | [v0.11](../roadmap/versions/v0.11.md) |
| 네트워크 · 복제 | ✅ [08](../08-NETWORK.md) | ✅ `network/` (ENet · 차분 스냅숏 · Interest · 역할) | ✅ `net_convergence` · `unit_network` | ⚠️ LAN 만 | ✅ 완료 (LAN 한정) — 암호화 · 인증 없음, 보안 전송은 v0.16 | [모듈](MODULES.md#네트워크) |
| 렌더링 | ✅ [06](../06-RENDERING.md) | ✅ `render/` (RHI · D3D12 · Vulkan · Metal) | ✅ `render_tests_warp` · `_lavapipe` · `_metal` | ⚠️ Windows 일부만, Linux · macOS 실기 없음 | ⚠️ 확인 필요 — 자동 시험은 통과, 실기 매트릭스는 RH-23-7 | [모듈](MODULES.md#렌더링) |
| 플랫폼 (창 · 입력) | ✅ [07](../07-PLATFORM.md) | ✅ `platform/` (Win32 · X11 · Wayland · Cocoa) | ✅ `unit_platform` · `platform_x11` · `platform_wayland` · `platform_cocoa` | ⚠️ macOS · Linux 실기 없음 | ⚠️ 확인 필요 | [모듈](MODULES.md#플랫폼) |
| 오디오 | ✅ 07 7장 · ADR-0095 · 0153 | 🟡 `platform/audio` — 믹서 · waveOut · AudioQueue · ALSA (dlopen) · Null, WAV, 팩 sounds.json | ✅ test_audio_output | 해당 없음 | 🟡 구현 — 세 OS 출력 수동 QA 남음 (NG-17) | [v0.11](../roadmap/versions/v0.11.md) |
| 게임 UI 런타임 · 다국어 | ✅ [20](../20-CREATOR-ALPHA.md) 2 · 6장 | ✅ `ui/` (SandboxGameUI) | ✅ `unit_ui` | 🟡 7 / 19 줄 | ✅ 완료 (0.7.0) | [모듈](MODULES.md#게임-ui-런타임) |
| 월드 에디터 | ✅ [10](../10-EDITOR.md) | ✅ `editor/` (툴 5 · Inspector · 목록 · Undo · 콘텐츠 창) | ✅ `unit_editor` · `det_edit_session` | ⚠️ Phase 12 줄 미확인 | ✅ 완료 (자동) — 회전 · 크기 전용 툴은 없다 | [모듈](MODULES.md#에디터--studio) |
| Studio 셸 · 서비스 | ✅ [25](../25-STUDIO-ARCHITECTURE.md) · [29](../29-STUDIO-UI.md) | ✅ `editor/services` · `apps/client/studio` | ✅ `studio_*` 7 개 (`studio_gui_smoke` 는 창이 없으면 SKIP) | 🟡 1 / 13 줄 | 🟡 진행 중 — 창 QA (UI-4) 뒤 0.8.0 | [v0.8](../roadmap/versions/v0.8.md) |
| Script IDE | ✅ [27](../27-SCRIPT-IDE.md) | ✅ `script/lang/LanguageService` · `editor/ui/ScriptEditor` · `ScriptDebugPanel` | ✅ `unit_script` · `unit_editor` | ⬜ 0 / 5 줄 | 🟡 진행 중 — 창 QA 뒤 0.9.0 | [v0.9](../roadmap/versions/v0.9.md) |
| 에셋 파이프라인 | ✅ [26](../26-ASSET-PIPELINE.md) | 🟡 `core/asset` (.meta 안정 id · 그룹) · `render/asset` (아틀라스 · 비동기 로드) | ✅ `project_assets` · `project_own_png` | 해당 없음 | ✅ "에셋" 창 (ST-06-3 — ADR-0146), 파일 감시 [계획] | [v0.10](../roadmap/versions/v0.10.md) |
| 프로젝트 · 패키지 · 패키지 관리자 | ✅ [18](../18-PLATFORM.md) 5장 · 09 9장 | ✅ `core/project` · `core/package` · `tools/sbx` | ✅ `project_*` · `tech_alpha_e2e` | 해당 없음 | ✅ 완료 — 서명 · 레지스트리는 v0.14 | [모듈](MODULES.md#프로젝트--패키지) |
| 진단 · 충돌 보고 | ✅ 20 7장 | ✅ `foundation/diag` · `platform/crash` | ✅ `crash_report_*` · `project_doctor` | 해당 없음 | ✅ 완료 | [모듈](MODULES.md#프로젝트--패키지) |
| Player 모드 | ✅ ADR-0121 | ✅ `apps/client/player` | ✅ `player_mode` | ⬜ 0 / 1 줄 | 🟡 진행 중 — v0.8 창 QA 에 포함 | [v0.8](../roadmap/versions/v0.8.md) |
| 플랫폼 서비스 (계정 · 레지스트리 · 방 · 호스팅 · 결제) | 🟡 초안만 ([platform/](../platform/START_HERE.md) — 동결) | ⬜ 없음 | ⬜ | ⬜ | 🔴 차단 — 저장소 경계 D-06 결정 (PS-12-1) 먼저 | [v0.14](../roadmap/versions/v0.14.md) |

## 4. 막힌 것 · 다음 할 일

### 4.1 차단 요인

| # | 무엇이 | 왜 막혔나 | 푸는 방법 | 관련 |
|---|---|---|---|---|
| B1 | v0.8 · v0.9 출시 (0.8.0 · 0.9.0) | 창에서 사람이 봐야 하는 QA 줄이 남았다 (v0.8 12 줄 · v0.9 5 줄 — 헤드리스 시험으로 대신하지 않는다) | [MANUAL-QA](../qa/MANUAL-QA.md) 의 v0.8 · v0.9 줄을 창으로 확인하고 [QA-EVIDENCE](../qa/QA-EVIDENCE.md) 에 기록 → UI-4 Done → 릴리스 노트 | [v0.8](../roadmap/versions/v0.8.md) · [v0.9](../roadmap/versions/v0.9.md) |
| B2 | v0.1 의 완료 선언 | Linux · macOS 실기 (창 · GPU) 장비가 없다, 50k 성능 판정을 사용자 PC 에서 재지 않았다 | [18](../18-PLATFORM.md) 1.1 의 명령 · MANUAL-QA Phase 13 · 14 줄. 늦어도 RH-23-7 (v0.23) | [16](../16-ROADMAP.md) |
| B3 | 트랙 C (v0.14 ~ v0.21 의 서비스 태스크) | 서비스 저장소와 엔진 저장소의 경계 (D-06) 가 정해지지 않았다 | PS-12-1 (ADR) | [v0.14](../roadmap/versions/v0.14.md) |
| B4 | RH-23-6 운영 런북 · RH-23-8 개인정보 정책 | 내용이 정의되지 않았다 (등록부 Missing) — GA 차단 조건 | D-06 뒤 카드 작성 | [v0.23](../roadmap/versions/v0.23.md) |
| B5 | Closed Creator Alpha 판정 | 외부 제작자 사용성 시험을 한 번도 하지 않았다 ([28](../28-CREATOR-UX-GATES.md) 의 결과 칸이 비어 있다) | ST-06-12 (v0.13) | [v0.13](../roadmap/versions/v0.13.md) |

### 4.2 다음 할 일 (순서)

1. v0.10 트랙 A — RT-06-10 타이머 · defineState ✅ (ADR-0138 · 0139) → RT-06-7 client 수명주기 · onEvent ✅ (ADR-0140 · 0141) → RT-06-8 onAction · 입력 Context ✅ (ADR-0142 · 0143) — v0.10 트랙 A 끝. 트랙 B: M6-T2 템플릿 셋 ✅ (minigame · board · survival — ADR-0144) → ST-06-3 Asset Browser ✅ (ADR-0146) · ST-06-4 Game UI Designer (모델 · 창 ✅ — ADR-0145, 수동 QA 남음) → NG-18 흐름 CTest ✅ (ng18_board_flow · sbx ui — ADR-0147) — v0.10 자동화 작업 끝.
2. v0.10 출시 판정 대기 — 사람의 창 확인 셋 (MANUAL-QA: ST-06-4 UI 디자이너 · ST-06-3 에셋 창 · NG-18 같은 흐름) → 릴리스 노트 · 제품 판
   (v0.8 · v0.9 와 같이 수동 QA 뒤). 세부 칸은 [v0.10 체크리스트](../roadmap/versions/v0.10.md).
3. v0.11 — RT-06-2 onPrePhysics · RT-06-3 물리 명령 · RT-06-4 트리거 · RT-06-5 충돌 · 트리거 이벤트 · RT-06-9 onRender ✅ (ADR-0148 ~ 0152),
   NG-17 오디오 출력 (구현 ✅ — ADR-0153, 세 OS 소리 수동 QA 남음) · NG-19 통합 시험 ✅ (samples/orbs · sample_orbs — ADR-0154) — v0.11 자동화 작업 끝.
   출시 판정은 수동 QA (NG-17 소리 · 화면의 그리기) 뒤 — v0.8 ~ v0.10 과 같이.
4. 병행 (사람) — B1 의 창 QA 로 0.8.0 · 0.9.0 을 낸다.

## 5. 검증 기록

실행한 것만 통과로 적는다 ([release-gates](../roadmap/release-gates.md) 머리말의 규칙).

| 무엇 | 결과 | 날짜 · 명령 |
|---|---|---|
| 문서 일관성 (링크 · ADR · 태스크 ID · 배정) | 통과 — 문제 0 | 2026-11-12 `python tools/check_docs.py --root .` |
| 로드맵 보기 문서 = 등록부 | 통과 | 2026-11-12 `python tools/gen_roadmap.py --root . --check` |
| 전체 CTest (Windows MSVC Debug) | 90 개 중 89 통과 · 실패 0 · `studio_gui_smoke` SKIP (창 없음) | 2026-11-16 `ctest --preset windows-msvc-debug` |
| CI 다섯 잡 | `d2d646a` 통과 (run 43191209923). `1cce764` 은 조사 시점에 실행 중 | 2026-11-12 `gh run list` |
| 호환 번호 (코드 값) | `kProtocolVersion` 18 · `.sbc` 12 · `kSimVersion` 4 · 패키지 3 · 매니페스트 3 · 세이브 1 / 1 · 리플레이 1 · Host API 1 | 2026-11-12 소스 grep — 표는 [versioning](../roadmap/versioning.md) 5장 |

검증하지 못한 것:

| 무엇 | 이유 |
|---|---|
| 창 · GPU 로 보는 기능 (Studio · 스크립트 편집기 · 디버거 · Player · 렌더 화면) | 이 조사는 헤드리스 — 창을 띄워 보지 않았다. MANUAL-QA 의 `[ ]` 줄이 그대로 남아 있다 |
| Linux · macOS 실기 | 장비 없음 — CI 는 헤드리스 빌드 · 시험이다 |
| 기능마다의 동작 | 등록부의 완료 기준이 가리키는 시험이 등록돼 있고 전체 CTest 가 통과하는지까지 확인했다. 기능을 하나씩 손으로 써 보지는 않았다 |
| GitHub Issue · PR | 저장소에 Issue · PR 기록이 없다 (작업 이력은 DEVELOPMENT_LOG · 커밋) |
