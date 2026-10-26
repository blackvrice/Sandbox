# 18. 플랫폼 계획 (v0.1 ~ v1.0)

> **계획 문서** (5.2 의 매니페스트 계약은 규범). Simulation Maker 를 게임 제작 · 배포 플랫폼으로 넓히는 버전 계획과 지금 상태.
> 결정: [ADR-0039](adr/0039-platform-scope-version-line-after-phase-15.md). 요구사항의 출처: [플랫폼 설계 초안](platform/START_HERE.md)
> (2026-10-13, 동결 — 거기의 "현행 구현" 서술은 이 문서 2장이 고친다). 기준: 2026-10-21 (v0.2 종료 — 0.2.0).

---

## 0. 지금

```text
버전     v0.1 Engine Foundation ◐ (구현 끝, 실기 검증 대기)   v0.2 Game Creator ✅ 0.2.0 (2026-10-21 — 릴리스 노트 releases/v0.2.0)
         v0.3 SandboxScript ◐ (진입 결정 D-01 · D-04 ✅ — ADR-0056 · 0057, 마일스톤 6.2)
다음     v0.3 M2-T2 — Host 바인딩 core/script (표 항목마다 구현 등록 + 계약 검사, 호출 단위 버퍼), sandbox.d.ts 생성기 · 골든
작업 단위 "v0.N Mk-Tn" (ADR-0039 2). 태스크마다 빌드 · 전체 CTest 통과 커밋, 미래 버전 기능은 미리 만들지 않는다
```

## 1. 버전 줄

| 버전 | 목표                   | 제품 검증 (초안 04_RELEASE_MATRIX)         | 상태                                            |
|------|------------------------|--------------------------------------------|-------------------------------------------------|
| v0.1 | Engine Foundation      | ECS · 서버 · 에디터 회귀 없는 기준선       | ◐ = Phase 0~15. 남은 것은 실기 검증 (1.1)       |
| v0.2 | Game Creator           | 비개발자가 미니게임을 만들어 끝까지 플레이 | ✅ 0.2.0 (5장 · [노트](releases/v0.2.0.md))       |
| v0.3 | SandboxScript VM / SDK | 사용자 Component / System · 격리           | ◐ 진입 결정 ✅ (6.2)                            |
| v0.4 | Package Manager        | 남의 Feature 설치 · 잠금 파일 재현         | [계획]                                          |
| v0.5 | Creator Alpha          | A 가 만든 게임을 B 가 다른 환경에서 실행   | [계획]                                          |
| v0.6 | Sandbox Hub            | 공개 → 기여 PR → 릴리스 → 설치             | [계획] — 저장소 경계 D-06 먼저                  |
| v0.7 | Multiplayer Platform   | 인증된 방 · 자체 서버 · 무료 세션          | [계획] — 보안 전송 ADR 먼저 (R6)                |
| v0.8 | Marketplace & Hosting  | 결제 · 정산 · 상시 서버                    | [계획] — 실제 사용자 노출은 보안 · 운영 조건 뒤 |
| v0.9 | Business & Stability   | 팀 리뷰 → 자동 빌드 → 승인 → 배포          | [계획]                                          |
| v1.0 | Sandbox Platform GA    | 사용자 · 운영 · 보안 · 호환성 기준         | [계획]                                          |

### 1.1 v0.1 의 열린 항목 (v0.2 를 막지 않는다)

```text
- 사용자 PC: Phase 6~8 Windows 실기 QA (Win32 · D3D12 · 렌더러 · ImGui — 지금까지 Wine · WARP 로만), Phase 13 Linux 실기 QA,
  Phase 15 50k 판정 (sbx_bench --only sim.ecosystem --threads <코어 수 − 1> → 평균 < 10 ms · p99 < 25 ms)
- CI: GitHub Actions 첫 실행 결과 (Linux · Windows · macOS 잡) — 이 개발 환경은 Actions 를 읽을 수 없다
- macOS: Phase 14 (CI macOS 잡 · MANUAL-QA 14B · 14C)
- 초안 v0.1 요구 4 "LAN · 신뢰 환경 제약을 UI 에 노출": 서버 도움말 · 08 · 16 R6 에 있다. 클라이언트 접속 화면 문구는 v0.7 전에
```

## 2. 현황 (초안 대비, 2026-10-21 소스 · 테스트 기준 — v0.2 종료)

**Implemented** = 소스와 자동 시험이 있다, **Partial** = 바탕은 있고 플랫폼 요구에 확장이 필요하다, **Planned** = 새로 만든다.
초안(2026-10-13)이 "초기 Vulkan"으로 적은 것은 그 뒤 13A~13C · 14 · 15 로 바뀌었다.

| 영역                    | 상태        | 근거 (소스 · 시험)                                                                                                          |
|-------------------------|-------------|-----------------------------------------------------------------------------------------------------------------------------|
| ECS · 리플렉션          | Implemented | core/ecs (Sparse-Set, stableId, ComponentCatalog 명시 등록 ADR-0011), unit_ecs · property_ecs                               |
| 시뮬레이션 · 결정론     | Implemented | 30 TPS SimulationWorld · SystemScheduler · SimCommand · ECB, WorldHash · Replay, det_* 16 개 (골든 · D1~D5)                 |
| 월드 · 세이브           | Implemented | core/world, core/persist (world.json · entities.jsonl · chunks, 마이그레이션, ADR-0013), unit_persist · det_world_save_load |
| 콘텐츠 팩               | Implemented | core/content (pack.json id · version · requires, V1~V7, contentHash, 오버레이 ADR-0030), content_eco_validate               |
| 네트워크 · 복제         | Implemented | network/ (ENet · ServerHost · 차분 스냅숏 · Interest · 역할 · 속도 제한), net_convergence · unit_network — LAN 한정 (R6)    |
| 멀티플레이 에디터       | Implemented | editor/ (ICommandSink · 툴 · 인스펙터 · Undo · 역할 · 콘텐츠 창 · 월드 저장/불러오기 · 리플레이), unit_editor · unit_client |
| 렌더 · 플랫폼           | Implemented | DX12 · Vulkan (lavapipe 기준 이미지) · Metal (macOS 검증 대기), Win32 · X11 · Wayland · Cocoa(미검증)                       |
| 성능                    | Partial     | 50k 2코어 29.6 ms — 판정 대기 (14 7.12)                                                                                     |
| GameProject · Manifest  | Implemented | M1 ✅ — 검사 · 열기 · 게임 정체성 · sbx new/check · 에디터 저장 = 프로젝트 worlds/ (ADR-0040~0044)                          |
| World / Map             | Implemented | 세이브 폴더 형식이 곧 월드 (ADR-0040) — 지형 · Prefab 인스턴스 · 오버레이                                                   |
| SaveGame                | Implemented | 세이브 = 월드 상태 전체 + world.json "game" 결속 (M5-T2 — 다른 게임 거절 · 다른 버전 알림). 버전 간 마이그레이션은 v0.4      |
| GamePackage (.sbpkg)    | Implemented | M5-T2 ✅ — sbx build · check, --world <패키지>, 파일마다 SHA-256 · digest, 세이브 결속 (ADR-0054). 압축 v0.4 · 서명 v0.6 |
| 게임 라이프사이클       | Implemented | M2 ✅ — GameMode · game.session (17b) · 시작 · 중단 · 재시작 (자동), "게임" 패널 (ADR-0045 · 0046)                          |
| 플레이어 행동           | Partial     | 콘텐츠 PlayerAction ✅ (M3-T1 — 점에 쓰는 행동, "행동" 패널, ADR-0047). 쿨다운 · 비용 · 유닛 지시는 v0.3 · v0.5           |
| Feature Bundle          | Implemented | life.health · energy · Spawn(Prefab 생성 · Reproduce · 스폰 지점) · Score(팀 · 공용) · Win/Lose(끝 조건) · Camera(처음 자리) · Input(팩 게임 키) · 데모 모드 eco.protect (M3 ✅) · Timer (event.timer · cond.time, M4) |
| Visual Trigger          | Implemented | M4 ✅ — triggers/*.json 노드 21 종 · V10 · 단계 17c · "콘텐츠" 창 폼 (ADR-0050~0052). 노드 그래프 캔버스는 v0.3 · v0.5      |
| Edit ↔ Test Play        | Implemented | M5-T1 ✅ — 편집 월드를 서버 메모리에 맡기고 복사본으로 플레이, 멈추면 원본 그대로 (ADR-0053). 여러 사람의 테스트 플레이는 v0.7 |
| SandboxScript · VM      | Planned     | v0.3. 지금 표현력 확장은 Rule op (03 6.4 — ADR-0039 가 언어를 TypeScript 부분집합으로 정함)                                 |
| 사용자 Component 스키마 | Planned     | v0.3 (D-03). 콘텐츠 로더는 등록되지 않은 컴포넌트를 거절한다 (그대로 둔다)                                                  |
| 패키지 · SemVer         | Partial     | foundation/version/SemVer (v0.2 M1-T1 — 버전 · 범위 비교만). 의존성 해석 · 잠금 파일은 v0.4                                 |
| Hub · Cloud · 결제      | Planned     | v0.6~v0.9. 이 저장소 밖의 서비스가 필요하다 (D-06)                                                                          |

## 3. 모듈 계획 (C++ 의존 방향은 01 2장 그대로)

```text
재사용 (고치지 않고 쓴다)
  core/content ContentLoader · ContentIssue    프로젝트 팩 검사 · 문제 보고 형식 (V1~V7 + P1~P5)
  core/persist saveWorld · loadWorld           월드 = 세이브 폴더 (ADR-0040)
  core/scenarios WorldSource                   서버 · 클라이언트가 월드를 여는 한 곳 (M1-T2 로 프로젝트도 — ADR-0041)
  network ServerHost · LocalServerHost         같은 GamePackage 를 헤드리스 서버와 로컬 플레이에서 (v0.2 요구 6)
  editor ICommandSink · Undo · 역할            Game Creator 편집도 SimCommand (00 2장)
새로 (v0.2)
  foundation/version   SemVer · 버전 범위                         의존 없음 (v0.4 패키지 관리자가 넓힌다)
  core/project         GameProject 매니페스트 · 검사 · 열기 (M1-T1 · T2)   Core 안 — 서버가 링크한다, 그래픽 없음
  core/game [계획 M2]   GameSession 상태 · 규칙 컴포넌트                Simulation — 04 를 따른다
v0.3 (ADR-0056 9 — 초안의 세 타깃을 둘로)
  script/               SandboxScript — lang (렉서 · 파서 · 검사기 · 코드 생성 ✅ M1) · api (Host 표 ✅ 첫 항목 · Math 표) ·
                        vm (바이트코드 · 검증기 ✅ M1-T3, 인터프리터 ✅ M2-T1)
                        의존 Foundation 만, ECS 를 모른다. 서버도 링크한다 (로드 때 컴파일)
  core/script [계획 M2]  Host 바인딩 · 동적 컴포넌트 · ScriptSystemAdapter (SandboxCore 가 SandboxScript 를 링크)
  → 경계 검사(sbx_check_link_boundaries · check_includes.py)에 규칙을 더하는 커밋과 함께 (M1-T1)
```

## 4. 버전 · 호환성 규칙

```text
제품 버전      CMake project VERSION (지금 0.2.0 — v0.2 완료, 2026-10-21) — 버전이 끝날 때 올린다 (v0.3 완료 = 0.3.0).
               버전마다 릴리스 노트 docs/releases/v<제품 버전>.md (이력 — 바꾸지 않는다)
엔진 범위      매니페스트 "engine" — 이 엔진이 범위 밖이면 프로젝트 검사 오류 P3
SemVer         2.0.0 엄격 (세 숫자, 앞자리 0 금지, 프리릴리스 · 빌드). 범위 문법 (v0.2): >= > <= < = ^ 와 부분 버전 (npm 과 같은 뜻),
               공백 = AND. "||" · "~" · "*" · 하이픈 범위는 Unsupported (v0.4 D-05 에서). 프리릴리스는 우선순위 그대로 비교
그 밖의 축       kSimVersion · 세이브 worldVersion/schemaVersion · 프로토콜 버전 · 매니페스트 schemaVersion — 서로 독립 (ADR-0039 6)
```

## 5. v0.2 Game Creator

초안 roadmap/v0_2: 프로젝트 · 월드 · 세이브 · 패키지 개념, 라이프사이클과 서버 전환, 기본 Feature, Visual Trigger 최소 노드,
Edit ↔ TestPlay 분리, 한 패키지를 헤드리스 서버와 로컬 플레이에서. 동적 사용자 컴포넌트는 v0.3.

### 5.1 마일스톤 · 태스크

```text
M1 GameProject
  T1 ✅ 매니페스트 sandbox.project.json (schemaVersion 1) · SemVer · 검사 P1~P5 · sbx_sim_check --validate-project (ADR-0040)
  T2 ✅ 프로젝트 열기: openProject (검사와 같은 함수, 오류면 시작 안 함) · WorldSource 가 프로젝트 폴더를 안다 →
        SandboxServer --world <프로젝트> · SandboxClient --world <프로젝트> (팩 = <프로젝트>/content, 월드 = entryWorld) (ADR-0041)
  T3 ✅ 핸드셰이크에 게임 정체성: Welcome · Reject 에 GameIdentity (프로토콜 8), 클라이언트 표시 · 팩을 못 읽으면
        --content <프로젝트>/content 안내, 매니페스트 팩 ≤ 16 (ADR-0042)
  T4 ✅ 프로젝트 만들기: createProject (빈 월드 · 시나리오 템플릿 — 팩 복사 + 첫 틱의 월드, 임시 폴더에서 검사 후 rename) ·
        도구 sbx new · check · templates (검사를 sbx_sim_check 에서 옮김) (ADR-0043)
  T5 ✅ 에디터 저장 위치: 프로젝트 서버 · 싱글플레이의 세이브 폴더 = <프로젝트>/worlds, Ctrl+S 처음 이름 = 시작 월드 (ADR-0044)
  → M1 완료 (2026-10-20): 만들기 (sbx new) → 검사 (sbx check) → 열기 (--world) → 편집 · 저장 (Ctrl+S) → 서버로 실행
M2 GameMode · 라이프사이클 (서버 권위 — ADR-0045: 모드는 콘텐츠, 진행 상태는 시뮬레이션 안)
  T1 ✅ GameMode 데이터: 팩의 modes/*.json (players · teams · start · end{time · count} · restart, V8), 매니페스트
        schemaVersion 2 의 gameMode (1 도 읽는다, P5 = 콘텐츠에 없음) (11 9장)
  T2 ✅ game.session 컴포넌트 (월드에 하나 — Persistent · Replicated, 엔진 관리) · 단계 17b GameRules: Starting 카운트다운
        → Running → 끝 조건 → Finished, Lobby · Finished = 편집 단계. 세션 없는 월드는 그대로 (골든 동일, 03 2장)
  T3 ✅ 프로젝트를 열면 세션 Lobby, 명령 GameControl (start · abort — admin, players.min), WorldOp Restart = 시작 월드를
        다시 열기 (+ restart auto), 프로토콜 9, 서버 --start-game, 클라이언트 "게임" 패널 (ADR-0046)
  → M2 완료 (2026-10-20): 모드 고르기 (매니페스트) → 열기 (Lobby) → 시작 → 끝 조건 → 결과 → (자동) 재시작
M3 Feature Bundle (Native)
  T1 ✅ PlayerAction: 팩의 actions.json (V9) · 명령 PlayerAction{action, position} (태그 30, 프로토콜 10, 역할 player,
        세션이 있으면 Running 에서만) — 대상은 시뮬레이션이 위치에서 고른다, 클라이언트 "행동" 패널 (ADR-0047)
  T2 ✅ 점수 · 팀: game.score (세션 엔티티, 공용 + 팀 8) · 효과 score.add (Running 만) · 끝 조건 score · 팀 소속 = 팀 태그
        (teams[].tag), 스폰 지점 game.spawn_point (시작 때 saveId 순으로 Prefab), "게임" 패널 점수 줄 (ADR-0048)
  T3 ✅ 게임 키 = 팩 presentation/input.json 의 play.action.<id> (누르면 커서 자리에 행동, play.* 만, --input 이 마지막),
        카메라 처음 자리 game.camera_start {height} (월드마다 · Home), 데모 모드 eco.protect "토끼 지키기" (eco 팩 행동
        feed · trap · 키 Q · E) · sbx new --mode (ADR-0049)
  → M3 완료 (2026-10-20): sbx new --from ecosystem_small --mode eco.protect → 클라이언트 → 시작 → Q · E 로 플레이 → 승패
M4 Visual Trigger
  T1 ✅ 트리거 스키마: 팩의 triggers/*.json — 이벤트 하나에서 시작하는 나무 (노드 · links · then/else), 노드 종류 표
        core/trigger/TriggerSchema (21 종 · 타입 · 범위 · 대상 규칙), compileGraph → 실행 표현 (펼친 명령열), V10 (모양 · 깊이 16 ·
        노드 64 · 개수 256 · limits.perTick) · 재진입 고리 경고 (ADR-0050)
  T2 ✅ 실행: 단계 17c Triggers (세션 Running 만) — 틱 처음 스냅숏으로 이벤트, 같은 틱 이어짐 (destroy → death · spawn →
        area_enter · score.add → score), 예산 perTick · 틱 1024 실행 · 65536 줄, 효과는 PlayerAction 과 공용 applyEffectNow,
        eco 트리거 eco.rabbit_lost (ADR-0051)
  T3 ✅ 에디터 폼: "콘텐츠" 창의 트리거 — 표 기반 노드 · 연결 양식 (editor/ui/TriggerForm) · JSON 탭 · 실행 표현 줄,
        편집 = EditContent{trigger} (오버레이 종류 Trigger, 프로토콜 11, 세이브 overlay/triggers/) (ADR-0052)
  → M4 완료 (2026-10-20): 트리거 만들기 (폼) → 검사 (V10) → 적용 (게임 중에도) → 단계 17c 실행 → 세이브
M5 Edit ↔ Test Play · GamePackage
  T1 ✅ Test Play: 서버가 편집 월드 runner 를 메모리에 그대로 맡기고 복사본 (임시 세이브 → 열기) 으로 바꾼다 — 게임이면
        시작 · 일시정지면 재개, 멈추면 원본 그대로 돌아온다. 그 동안 저장만 (불러오기 · 재생 · 재시작 · 기록 · 자동 재시작 거절),
        WorldOp TestPlay · TestStop · 프로토콜 12, "월드" 패널 단추 (ADR-0053)
  T2 ✅ GamePackage: .sbpkg = 머리 + 색인(JSON) + 파일 (자체 컨테이너 — 압축 · 서명 없음), 파일마다 SHA-256 · digest (같은
        프로젝트 = 같은 바이트), 매니페스트 · 읽은 팩 · 시작 월드만, sbx build · check, --world <패키지> (검증 → 캐시에 풀기),
        세이브 결속 world.json "game" {gameId, version, package} — 다른 게임 거절 · 다른 버전 알림 (ADR-0054, D-02 결정)
  → M5 완료 (2026-10-20): 편집 → 테스트 플레이 → 되돌리기 → sbx build → 패키지로 서버 · 싱글플레이 → 세이브가 게임에 묶인다
M6 E2E · 회귀
  T1 ✅ 데모 미니게임 samples/rabbit_guard (sbx new --mode eco.protect 그대로 + README) · 자동 플레이 스크립트 playthrough.json
        (core/scenarios/PlayScript — 서버 러너의 시나리오, 단계마다 명령) · SandboxServer · SandboxClient --script → 같은 "게임 결과:"
        줄 (CTest sample_playthrough), expect (해시는 툴체인별), --exit 는 게임이 끝나도 끝낸다 (ADR-0055)
  T2 ✅ 버전 종료: 요구사항 done / deferred / removed (5.3 · 릴리스 노트), 제품 버전 0.2.0, sample_playthrough 가 패키지로도
        두 호스트를 비교한다 (요구 6)
  → M6 완료 · v0.2 완료 (2026-10-21): 0.2.0
```

v0.2 완료 기준 (초안 5장 → 자동 검증): 템플릿만으로 미니게임을 만들어 저장 · 실행 · 종료 (M6-T1 시나리오), 종료 조건 ·
Lobby · Restart 의 서버 권위 상태 시험 (M2 · M3), Edit / Play 분리 · Undo / Redo · 패키지 검증 시험 (M5). 기존 det_* · 골든 회귀 없음.

### 5.3 요구사항 검토 (v0.2 M6-T2 — 판정과 근거는 [릴리스 노트](releases/v0.2.0.md) 4 · 5장)

```text
done      요구 1~6 (프로젝트 · 월드 · 세이브 · 패키지, 라이프사이클, Feature Bundle, Visual Trigger 최소 노드 + 폼, Edit ↔ TestPlay,
          한 패키지 = 헤드리스 서버 · 로컬 플레이), 완료 기준 1~3 (미니게임), DoD 1 · 2 · 4
deferred  팀 대전 템플릿 (v0.5), 노드 그래프 캔버스 (v0.3 · v0.5), 패키지 압축 · 의존 (v0.4) · 서명 (v0.6) · 원격 받기 (v0.7),
          게임 버전 간 세이브 마이그레이션 (v0.4), 준비 · 팀 고르기 · 여러 사람의 테스트 플레이 (v0.7), 봇 자동 플레이 (v0.9) …
removed   맵 파일 worlds/main.world (→ 세이브 폴더, ADR-0040), Loading · Stopped 게임 상태 (→ 서버, ADR-0045), 초안 매니페스트 예시의
          scriptApi · dependencies · permissions · network 키 (→ 필요한 버전이 schemaVersion 을 올려), zip 패키지 (→ 자체 컨테이너, ADR-0054)
골든      kSimVersion 4 그대로 — v0.2 의 기능은 세션 · 트리거가 있는 월드에서만 돈다 (det_* · 리플레이 해시 그대로)
```

### 5.2 GameProject 매니페스트 (규범 — v0.2 M1-T1, ADR-0040)

```text
MyGame/
  sandbox.project.json   매니페스트
  content/<팩 폴더>/      콘텐츠 팩 (11-CONTENT-SCHEMA 그대로, pack.json 의 id 로 찾는다)
  worlds/<이름>/          월드 = 세이브 폴더 형식 (09 6장 — world.json · entities.jsonl · chunks/)
```

| 키              | 필수 | 형식                                                                                                                 |
|-----------------|------|----------------------------------------------------------------------------------------------------------------------|
| `schemaVersion` | 예   | 2 (1 도 읽는다 — 2 의 키는 쓸 수 없다). 그 밖이면 P3 하나만 내고 멈춘다 (나머지 키의 뜻이 다를 수 있다)              |
| `gameId`        | 예   | 소문자 · 숫자 · `_` 마디 둘 이상을 `.` 로 (`studio.arena`), ≤ 64 바이트 (P2)                                         |
| `name`          | 예   | 1~64 글자, 제어 문자 없음 (P1)                                                                                       |
| `version`       | 예   | SemVer 2.0.0 (P3)                                                                                                    |
| `engine`        | 예   | 버전 범위 (4장) — 이 엔진이 범위 밖이면 P3                                                                           |
| `description`   | 아니 | ≤ 1024 글자 (P1)                                                                                                     |
| `content`       | 아니 | `{"packs": [팩 id …]}` — `<프로젝트>/content` 에서 찾는다. 없거나 비면 내장 콘텐츠 (중복 · `core` · 16 개 초과는 P2) |
| `entryWorld`    | 예   | 프로젝트 기준 상대 경로, `/` 구분, 마디 `[A-Za-z0-9_.-]`, `.` · `..` · Windows 예약 이름 금지 (P4)                   |
| `gameMode`      | 아니 | schemaVersion 2. 콘텐츠의 GameMode id (11 9장) — 형식 P2, 프로젝트 콘텐츠에 없으면 P5. 없으면 끝 없는 샌드박스       |

모르는 키는 P1 오류 (pack.json 과 같은 엄격함 — 새 키는 schemaVersion 을 올려 더한다). 문제는 `ContentIssue` 로 모두 모은다
(`sandbox.project.json:/gameId: 오류 P2 — …`). 정규형 쓰기(키 순서 고정 · 들여쓰기 2 · 끝 줄바꿈)는 다시 읽으면 같은 값이다.

프로젝트 검사 (`project::validateProject`, `sbx check <폴더>` — 오류면 종료 코드 1, T1 의 sbx_sim_check --validate-project 를 옮김):

```text
1 매니페스트 (P1~P4 형식)   2 엔진 범위 (P3)   3 팩 — ContentLoader V1~V7 (file 은 "content/<팩 폴더>/…"), content/ 없음 P5
4 시작 월드 — 폴더 · world.json 요약 (P4), 월드가 쓰는 팩이 매니페스트에 없음 (P5 오류), contentHash 가 다름 (P5 경고),
  persist::loadWorld 로 실제로 연다 (실패 P4 오류, 로드 경고 P4 경고)
```

## 6. v0.3 ~ v1.0 (요약 — 들어갈 때 이 장을 마일스톤으로 쪼갠다)

```text
v0.3 SandboxScript   D-01 언어 부분집합(숫자 i32/f32/f64 · 예외 · GC 없음 · import) → D-04 Host API 단일 정의(IDL) → D-03 동적 풀
                     → 바이트코드 · 검증기 · VM(fuel · 메모리 · 스택) → ScriptSystemAdapter(Stage 고정) → .d.ts SDK → Mana 예제
                     · 파서/타입 검사기를 직접 쓸지 TS 컴파일러(Node, 빌드 체인만)를 쓸지는 D-01 에서 (게임 실행 파일에 JS 런타임 없음)
v0.4 Package Manager  매니페스트 dependencies · 잠금 파일 · 로컬/Git 소스 · 충돌 · 순환 · 다이제스트, sbx CLI (init/add/remove/update/build/test)
v0.5 Creator Alpha    템플릿 3종 · 패키지 빌드 · 설치 UX · 오류 보고 · A 제작 → B 실행 E2E
v0.6 Hub              계정 · 레지스트리 · 불변 릴리스 · GitHub 연동 (D-06 저장소 경계)
v0.7 Multiplayer      보안 전송 (D-07) · Directory · Room · JoinTicket · 자체 서버 등록 · 무료 세션 (D-08)
v0.8 Marketplace       Entitlement · 결제 대행 · 정산 장부 · 상시 호스팅 (D-09) — 테스트 모드로만
v0.9 Business          팀 ACL · CI/CD · 승인 · 감사 (D-10)
v1.0 GA                호환성 표 · 마이그레이션 가이드 · 보안 감사 · 데모 3종
```

### 6.1 열린 결정 (초안 04 3장 + 이 저장소)

| 번호 | 결정                                                              | 기한         | 상태                                       |
|------|-------------------------------------------------------------------|--------------|--------------------------------------------|
| D-01 | SandboxScript 숫자 타입 · 예외 · GC · import 부분집합             | v0.3 진입 전 | 결정 — 자체 컴파일러 · number(f64) · i32 · 예외 · GC · 전역 상태 없음 (ADR-0056) |
| D-02 | GamePackage 직렬화 · 서명 · 압축                                  | v0.2 종료 전 | 결정 — 자체 컨테이너 · SHA-256 · 압축 · 서명 없음 (ADR-0054) |
| D-03 | 사용자 Component 스키마의 동적 풀                                 | v0.3 초기    | 열림 — v0.3 M3-T1 의 ADR                   |
| D-04 | Script SDK / Host API 단일 정의 (IDL)                             | v0.3 초기    | 결정 — C++ 표 하나 → 검사 · 로드 · 바인딩 · d.ts (ADR-0057) |
| D-05 | SemVer 충돌 해결 · 범위 문법 확장 · 유료 패키지 EULA              | v0.4 초기    | 일부 — v0.2 범위 문법 (4장)                |
| D-06 | GitHub 연동 범위 · 서비스(Hub · Cloud)를 둘 저장소                | v0.6 초기    | 열림                                       |
| D-07 | 공개 네트워크 보안 전송 · NAT                                     | v0.7 진입 전 | 열림 (R6)                                  |
| D-08 | 무료 세션 자원 · 릴레이 비용 한도                                 | v0.7 초기    | 열림                                       |
| D-09 | 판매 국가 · 수수료 · 환불 · 정산 사업자                           | v0.8 진입 전 | 열림                                       |
| D-10 | Business 좌석 · CI 할당 · SLA                                     | v0.9 초기    | 열림                                       |
| D-11 | 플레이어 행동 모델 (유닛 명령 · 직접 조작, 지연 체감 — 예측 없음) | v0.2 M3 전   | 결정 — 콘텐츠 PlayerAction 부터 (ADR-0045) |

### 6.2 v0.3 SandboxScript — 마일스톤 · 태스크 (ADR-0056 언어 · ADR-0057 Host API)

초안 roadmap/v0_3: 언어 부분집합 규범 동결, defineComponent · defineSystem · .d.ts SDK, 동적 Component 스키마 · 풀 (Inspector ·
세이브 · 복제), IServerContext · IClientContext · Host 함수 표 · 권한, 라이프사이클 콜백, ScriptSystemAdapter → SystemScheduler
(명령은 ECB · 승인된 API), 연료 · 메모리 · 스택 · 질의 · 이벤트 제한 · 불법 바이트코드 거부, IDE · 시험 · 프로파일 기본.

```text
M1 언어 앞단 (실행 없음)
  T1 ✅ 규범 문서 19-SANDBOXSCRIPT (낱말 · 문법 · 부분집합 S3xx · 진단), 모듈 script/ = SandboxScript (Foundation 만 — 링크 ·
        include 경계 검사), 렉서 · 파서 → AST (ASI = JS 규칙, 회복, 깊이 · 오류 상한), 진단 "파일:줄:열: 오류 Sxxx — …"
  T2 ✅ 타입 검사 · 이름 해석 (script/lang/Checker → Program — 마디마다 타입 · 가리키는 것 · 상수값): import 그래프 (같은 팩 ·
        "sandbox", 없는 파일 · 순환 S416), 최상위 = 기본 타입 상수식, number · i32 · enum 규칙과 상수 접기, null 좁히기 (대입 없는
        이름만), 흐름 (모든 길 return · switch 떨어짐), 반환 타입 추론, 금지 이름. Host 표 script/api 의 첫 항목 ctx.tick ·
        ctx.log + 지문 (컴포넌트 읽기 · 질의는 컴포넌트 타입과 함께 M3 로)
  T3 ✅ 코드 생성 (검사한 Program = 타입 있는 IR → bc::Module) · .sbc 형식 1 (Host 표 지문) · 검증기 (선형 — 타입 · 번호 ·
        끝 명령 · import 서명), 비트 뒤집기 · 잘림 · 일부러 틀린 모듈 시험 (ADR-0058)
  → M1 완료 (2026-10-21): 소스 → 파싱 → 검사 → 바이트코드 → 검증 (실행은 M2)
M2 VM · Host API
  T1 ✅ 인터프리터 (script/vm/Interpreter): 레지스터 프레임 · 호출마다 새 아레나 · 예산 (연료 · 깊이 · 칸 · 아레나 · 배열 ·
        문자열) · 트랩 10 종, JS 수 규칙 (script/vm/Numbers — ToInt32 · Number#toString · round · pow · min/max · UTF-16 길이,
        검사기 접기와 공용), IHost (가짜 Host 로 시험)
  T2 [계획] 바인딩 core/script: 표 항목마다 구현 등록 + 계약 검사, 호출 단위 버퍼 (read-your-writes · 트랩이면 버림),
            sandbox.d.ts 생성기 (sbx sdk) · sdk/sandbox.d.ts 골든 시험
M3 사용자 Component (D-03)
  T1 [계획] ADR (동적 풀): defineComponent → 스키마 (필드 종류 i32 · f32 · f64 · bool · vec2 · entityRef · enum, 기본값 · 범위 ·
            플래그) → 카탈로그에 동적 등록 (ADR-0011 에 더한다), 콘텐츠 검사 순서 (스크립트 컴포넌트 먼저 — Prefab 이 쓴다)
  T2 [계획] 세이브 · WorldHash · 복제 · Inspector 연결, 스키마 version 과 필드 마이그레이션 (더하기 · 기본값)
M4 Script System
  T1 [계획] defineSystem · ScriptSystemAdapter · 단계 ADR (고정 단계 하나, 순서 = 팩 순서 · 시스템 id), onTick (intervalTicks),
            스크립트 없는 월드 = 골든 그대로
  T2 [계획] onStart · onStop · onEvent (구독 — 죽음 · 생성 · 영역 · 점수, 트리거 이벤트와 공용), 재진입 · 폭주 제한
  T3 [계획] 예산 (연료 · 메모리 · 질의 · 이벤트 · HostCall — 호출 · 틱) · 트랩 정책 (시스템 끄기 기준) · 시스템별 통계 (메트릭 · 로그)
M5 도구
  T1 [계획] sbx check 의 스크립트 진단, sbx new 템플릿에 tsconfig.json · sandbox.d.ts, 에디터 "콘텐츠" 창의 스크립트 오류 줄 ·
            적용 (오버레이 종류 Script — 프로토콜 ++)
  T2 [계획] IClientContext 최소 (onUpdate — 읽기 · 화면 글자), 프로파일 (시스템별 연료 · 시간 오버레이)
M6 E2E · 회귀
  T1 [계획] 데모 samples/mana_regen (Mana 컴포넌트 + 재생 시스템): Inspector · 세이브/로드 · 복제 · 로컬 = 원격 해시, 보호 시험
            (무한 루프 · 권한 위반 · 불법 바이트코드 · 핸들 위조), 10k 대상 벤치 (sbx_bench)
  T2 [계획] 버전 종료: 요구사항 done / deferred / removed · 릴리스 노트 releases/v0.3.0 · 제품 버전 0.3.0
```

v0.3 완료 기준 (초안 5장 → 자동 검증): 스크립트로 Mana Component · Regeneration System (M6-T1), 자동 완성 · Inspector ·
세이브/로드 · 복제 (M3 · M5), 실행량 초과 · 권한 위반 · 결정론 위반 · VM 크래시 보호 (M2 · M4 · M6), VM 과 Native 의 API 스키마
계약 자동 검사 (M2-T2 — 바인딩 등록 검사 + d.ts 골든). 기존 det_* · 골든 회귀 없음 (스크립트 없는 월드).

## 7. 문서 · ADR 계획

```text
지금      ADR-0039 범위 · 버전 줄, ADR-0040 GameProject 매니페스트, 00 (1 · 7장), 16 (2 · 6장 → 이 문서), 17, 13, 11 (프로젝트 폴더)
M1-T2 ✅  15 (실행 옵션), 08 (WorldSource), ADR-0041 (서버가 프로젝트를 여는 규칙)
M1-T3 ✅  08 (핸드셰이크에 게임 정체성 · 프로토콜 8), ADR-0042
M1-T4 ✅  15 (sbx), 17, 13, ADR-0043
M1-T5 ✅  10 9장, 15, 13, ADR-0044
M2-T1 ✅  11 (1 · 8 · 9장 GameMode), 18 5.2 (gameMode), ADR-0045 (D-11 포함)
M2-T2 ✅  03 2장 (17b GameRules), 04 6장 (simVersion 그대로), 02 (game.session), 17, 13, ADR-0045 구체화
M2-T3 ✅  08 (프로토콜 9 · 게임 진행), 10 7 · 9장 (권한 · "게임" 패널), 15 (--start-game), 03 4장, 17, 13, ADR-0046
M3-T1 ✅  11 (6 · 8장 PlayerAction · V9), 03 4장, 08 (프로토콜 10), 10 7 · 9장 ("행동" 패널), 13, 17, ADR-0047
M3-T2 ✅  11 (0 · 8 · 9장 점수 · 팀 · 스폰 지점), 03 (2 · 4 · 6.3장), 02, 04 (simVersion 이력), 10 9장 (점수 줄), 13, 17, ADR-0048
M3-T3 ✅  07 5장 (게임 키), 10 9장 (게임 키 · 카메라 처음 자리), 11 (0 · 1 · 9장 데모), 15 7장 (sbx new --mode · 데모), 02, 13, 17,
          ADR-0049
M4-T1 ✅  11 (1 · 8 · 10장 Trigger · V10), 03 6.4 (Trigger 와 Rule), 13, 17, ADR-0050
M4-T2 ✅  03 2장 (17c), 04 (simVersion 이력), 11 (0 · 10장 실행), 13, 17, ADR-0051
M4-T3 ✅  10 8장 (트리거 폼), 08 (프로토콜 11), 09 3장 (overlay/triggers), 11 10장, 13, 17, ADR-0052
M5-T1 ✅  08 (프로토콜 12), 10 9장 (테스트 플레이), 13, ADR-0053
M5-T2 ✅  09 2 · 9장 (GamePackage · 세이브 결속), 15 7장 (sbx build · --world 패키지), 13, 17, ADR-0054 (D-02)
M6-T1 ✅  13 5.1 (자동 플레이), 15 7장 (--script · --exit), 17 (samples/ · PlayScript), ADR-0055
M6-T2 ✅  releases/v0.2.0 (릴리스 노트 — 요구사항 판정), 18 (0 · 1 · 2 · 4 · 5.3장), 13 5.1, 16, 17, README (문서 지도)
v0.3 진입 ✅  ADR-0056 (D-01 언어) · ADR-0057 (D-04 Host API), 18 (0 · 1 · 3 · 6.1 · 6.2장), 03 6.4
v0.3 M1-T1 ✅  19-SANDBOXSCRIPT (규범), 01 2장 (SandboxScript 타깃 · 경계), 04 4.6, 17, 13, README, AGENTS 4장
v0.3 M1-T2 ✅  19 (1 · 5 · 7 · 8 · 9 · 10 · 11장 — 타입 규칙 · Host 표 · 검사기 · S4xx), 17, 13
v0.3 M1-T3 ✅  19 (5 · 8 · 9 · 12장 — 바이트코드 · 검증기), 17, 13, ADR-0058
v0.3 M2-T1 ✅  19 (0 · 6 · 9 · 13장 — 인터프리터 · 수 규칙), 17, 13
```

## 8. 기준선 (2026-10-19, master f4a72c1)

```text
Linux clang 19 Debug (-Werror, Vulkan · MSL 셰이더) ctest -LE "balance|bench": 51/51 통과 (det 16 · unit 23 · client 7 · …)
GitHub master = f4a72c1 (git ls-remote 확인). Actions 결과는 이 환경에서 읽을 수 없다 (1.1)
v0.2 M1-T1 뒤: 같은 구성 55/55 (+ unit_project · project_minimal_validate · project_bad_manifest_rejected · _message)
v0.2 M1-T2 뒤: 58/58 (+ server_project_exit · server_project_invalid · client_local_project_headless)
v0.2 M1-T3 뒤: 59/59 (+ net_server_probe_project — 실제 UDP 로 Welcome.game)
v0.2 M1-T4 뒤: 61/61 (+ project_new_empty · project_new_from_scenario)
v0.2 M1-T5 뒤: 62/62 (+ server_project_saves_root)
v0.2 M2-T1 뒤: 62/62 (unit_content · unit_project 케이스 추가)
v0.2 M2-T2 뒤: 62/62 (unit_core 에 game 8 케이스, unit_ecs · unit_content 갱신 — 골든 해시 전부 그대로)
v0.2 M2-T3 뒤: 63/63 (+ project_game_start — sbx new → 모드 추가 → sbx check → 서버 --start-game)
v0.2 M3-T1 뒤: 63/63 (unit_core · unit_content · unit_client · unit_network 케이스 추가)
v0.2 M3-T2 뒤: Windows MSVC Debug 58/58 (render_tests_warp 제외 — 아래) · unit 케이스 +9 (test_game_score 5 · content 3 · client 1)
              render_tests_warp 는 이 기계에서 D3D12 Debug Layer 배리어 메시지로 실패 (doctest 28/28 통과, M3 는 render 를 건드리지 않음)
v0.2 M3-T3 뒤: Windows MSVC Debug 59/59 (render_tests_warp 제외, + project_demo_protect) · unit 케이스 +6
v0.2 M4-T1 뒤: Windows MSVC Debug 59/59 (render_tests_warp 제외) · unit_content +5 (test_trigger)
v0.2 M4-T2 뒤: Windows MSVC Debug 59/59 (render_tests_warp 제외) · unit_core +8 (test_trigger_run)
v0.2 M4-T3 뒤: Windows MSVC Debug 59/59 (render_tests_warp 제외) · unit_editor +4 (test_trigger_form) · unit_core +1 · unit_network 갱신 (프로토콜 11)
v0.2 M5-T1 뒤: Windows MSVC Debug 59/59 (render_tests_warp 제외) · unit_network +2 · unit_client +1 (프로토콜 12)
v0.2 M5-T2 뒤: Windows MSVC Debug 60/60 (render_tests_warp 제외, + project_package) · unit +7 (sha256 2 · package 4 · 결속 1)
v0.2 M6-T1 뒤: Windows MSVC Debug 61/61 (render_tests_warp 제외, + sample_playthrough) · unit +4 (play script 3 · server options 1)
v0.2 M6-T2 뒤 (0.2.0): Windows MSVC Debug 61/61 (render_tests_warp 제외) — sample_playthrough 가 패키지로도 (프로젝트 · 패키지 × 두 호스트)
v0.3 M1-T1 뒤: Windows MSVC Debug 62/62 (render_tests_warp 제외, + unit_script — 렉서 6 · 파서 10 케이스) · arch 픽스처 10 건
v0.3 M1-T2 뒤: Windows MSVC Debug 62/62 (render_tests_warp 제외) · unit_script +14 케이스 (test_checker — Host 표 · 경로 · 규칙별)
v0.3 M1-T3 뒤: Windows MSVC Debug 62/62 (render_tests_warp 제외) · unit_script +7 케이스 (test_bytecode — 역어셈블 기준 · 왕복 · 거절 · 비트 뒤집기)
v0.3 M2-T1 뒤: Windows MSVC Debug 62/62 (render_tests_warp 제외) · unit_script +8 케이스 (test_vm — 수 규칙 · 산술 · 배열 · 트랩 · Host · 결정성)
```
