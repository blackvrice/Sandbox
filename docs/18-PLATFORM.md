# 18. 플랫폼 계획 (v0.1 ~ v1.0)

> **계획 문서** (5.2 의 매니페스트 계약은 규범). Simulation Maker 를 게임 제작 · 배포 플랫폼으로 넓히는 버전 계획과 지금 상태.
> 결정: [ADR-0039](adr/0039-platform-scope-version-line-after-phase-15.md). 요구사항의 출처: [플랫폼 설계 초안](platform/START_HERE.md)
> (2026-10-13, 동결 — 거기의 "현행 구현" 서술은 이 문서 2장이 고친다). 기준: 2026-10-19 (master f4a72c1 + v0.2 M1).

---

## 0. 지금

```text
버전     v0.1 Engine Foundation ◐ (구현 끝, 실기 검증 대기)   v0.2 Game Creator ◐ (M1 진행 중)
다음     v0.2 M1-T4 — 새 프로젝트 만들기 · 에디터 저장을 프로젝트 worlds/ 로
작업 단위 "v0.N Mk-Tn" (ADR-0039 2). 태스크마다 빌드 · 전체 CTest 통과 커밋, 미래 버전 기능은 미리 만들지 않는다
```

## 1. 버전 줄

| 버전 | 목표                   | 제품 검증 (초안 04_RELEASE_MATRIX)         | 상태                                            |
|------|------------------------|--------------------------------------------|-------------------------------------------------|
| v0.1 | Engine Foundation      | ECS · 서버 · 에디터 회귀 없는 기준선       | ◐ = Phase 0~15. 남은 것은 실기 검증 (1.1)       |
| v0.2 | Game Creator           | 비개발자가 미니게임을 만들어 끝까지 플레이 | ◐ M1 진행 (5장)                                 |
| v0.3 | SandboxScript VM / SDK | 사용자 Component / System · 격리           | [계획] (6장)                                    |
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

## 2. 현황 (초안 대비, 2026-10-19 소스 · 테스트 기준)

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
| GameProject · Manifest  | Partial     | 매니페스트 · 검사 (T1), --world <프로젝트> (T2), 핸드셰이크 게임 정체성 (T3). 새 프로젝트 · 저장은 M1-T4                    |
| World / Map             | Implemented | 세이브 폴더 형식이 곧 월드 (ADR-0040) — 지형 · Prefab 인스턴스 · 오버레이                                                   |
| SaveGame                | Partial     | 세이브 = 월드 상태 전체. 게임 버전 · 패키지 다이제스트 결속은 Planned (M5)                                                  |
| GamePackage (.sbpkg)    | Planned     | D-02. 지금 공유 단위는 콘텐츠 팩 폴더 + 세이브 폴더                                                                         |
| 게임 라이프사이클       | Partial     | 일시정지 · 단계 · 속도 (SimulationClock 편집 단계), 세션 · 역할. Lobby → Running → Finished 상태기계는 Planned (M2)         |
| 플레이어 행동           | Partial     | 역할 Player · CommandClass::PlayerAction 예약 (network/server/CommandValidator) — 그 분류의 명령은 아직 없다 (M3)           |
| Feature Bundle          | Partial     | life.health · energy · Spawn(Prefab 생성 · Reproduce) · Camera2D · ActionMap. Score · Timer · Win/Lose 는 Planned (M3)      |
| Visual Trigger          | Partial     | Rule(source · action · target · effects) · BehaviorGraph FSM · Effect op. 게임 이벤트 · 조건 · 효과 노드는 Planned (M4)     |
| Edit ↔ Test Play        | Partial     | 편집 단계 · 월드 저장/불러오기 · 리플레이. 분기 세션 · 원본 보호는 Planned (M5)                                             |
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
나중 (v0.3~, 초안 architecture/01 3장)
  SandboxScriptRuntime (Core 를 모름) · SandboxScriptCompilerTools (빌드 · 에디터 프로세스) · SandboxScriptBindings (Core + Runtime)
  → 경계 검사(sbx_check_link_boundaries · check_includes.py)에 규칙을 더하는 커밋과 함께
```

## 4. 버전 · 호환성 규칙

```text
제품 버전      CMake project VERSION (지금 0.1.0) — 버전이 끝날 때 올린다 (v0.2 완료 = 0.2.0)
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
  T4 [계획] 프로젝트 만들기 · 저장: 템플릿에서 새 프로젝트 (빈 월드 + 팩 뼈대), 에디터 "월드" 저장이 프로젝트의 worlds/ 로
M2 GameMode · 라이프사이클 (서버 권위)
  T1 GameMode 데이터 (참가자 수 · 팀 · 승리 조건 · 재시작 정책) — 콘텐츠 팩 스키마 확장 (11 갱신)
  T2 GameSession 상태기계 Created → Loading → Lobby → Starting → Running → Finished → Stopped (서버 · 결정적 · 복제)
  T3 Lobby 준비 · 시작 · 재시작 명령과 역할 권한, 오류 상태 (버전 · 콘텐츠 불일치)
M3 Feature Bundle (Native)
  T1 플레이어 행동 명령 (CommandClass::PlayerAction 을 채운다 — 자기 유닛 이동 · 상호작용, 서버 검증)
  T2 game.score · game.timer · game.team · 스폰 지점 컴포넌트와 System (Health 는 life.health 재사용)
  T3 Win / Lose 판정 → GameSession Finished, 카메라 · 입력 바인딩 기본값
M4 Visual Trigger
  T1 트리거 스키마: 이벤트(틱 · 타이머 · 영역 진입 · 점수 · 사망) · 조건 · 효과 노드 — 원본 그래프와 실행 표현 분리, 예산 · 깊이 · 순환 검사
  T2 실행: Rule · Effect op 재사용, 결정적 순서 (Stage 고정), 폭주는 예산으로 끊고 월드는 무결
  T3 에디터 폼 (노드 목록 · 인스펙터 양식) — 편집은 EditContent 명령 (오버레이 경로)
M5 Edit ↔ Test Play · GamePackage
  T1 Test Play: 편집 월드의 메모리 스냅숏에서 분기 세션, Stop 이면 버리고 원본 그대로 (선택 저장)
  T2 GamePackage 빌드 · 로드 (D-02: 형식 · 다이제스트), SaveGame 결속 (패키지 다이제스트 · 게임 버전)
M6 E2E · 회귀
  T1 데모 미니게임 프로젝트 (samples/) — 헤드리스 서버와 로컬 플레이에서 같은 결과, 시나리오로 끝까지 (자동)
  T2 버전 종료: 요구사항 done / deferred / removed, 제품 버전 0.2.0
```

v0.2 완료 기준 (초안 5장 → 자동 검증): 템플릿만으로 미니게임을 만들어 저장 · 실행 · 종료 (M6-T1 시나리오), 종료 조건 ·
Lobby · Restart 의 서버 권위 상태 시험 (M2 · M3), Edit / Play 분리 · Undo / Redo · 패키지 검증 시험 (M5). 기존 det_* · 골든 회귀 없음.

### 5.2 GameProject 매니페스트 (규범 — v0.2 M1-T1, ADR-0040)

```text
MyGame/
  sandbox.project.json   매니페스트
  content/<팩 폴더>/      콘텐츠 팩 (11-CONTENT-SCHEMA 그대로, pack.json 의 id 로 찾는다)
  worlds/<이름>/          월드 = 세이브 폴더 형식 (09 6장 — world.json · entities.jsonl · chunks/)
```

| 키              | 필수 | 형식                                                                                                                 |
|-----------------|------|----------------------------------------------------------------------------------------------------------------------|
| `schemaVersion` | 예   | 1. 다르면 P3 하나만 내고 멈춘다 (나머지 키의 뜻이 다를 수 있다)                                                      |
| `gameId`        | 예   | 소문자 · 숫자 · `_` 마디 둘 이상을 `.` 로 (`studio.arena`), ≤ 64 바이트 (P2)                                         |
| `name`          | 예   | 1~64 글자, 제어 문자 없음 (P1)                                                                                       |
| `version`       | 예   | SemVer 2.0.0 (P3)                                                                                                    |
| `engine`        | 예   | 버전 범위 (4장) — 이 엔진이 범위 밖이면 P3                                                                           |
| `description`   | 아니 | ≤ 1024 글자 (P1)                                                                                                     |
| `content`       | 아니 | `{"packs": [팩 id …]}` — `<프로젝트>/content` 에서 찾는다. 없거나 비면 내장 콘텐츠 (중복 · `core` · 16 개 초과는 P2) |
| `entryWorld`    | 예   | 프로젝트 기준 상대 경로, `/` 구분, 마디 `[A-Za-z0-9_.-]`, `.` · `..` · Windows 예약 이름 금지 (P4)                   |

모르는 키는 P1 오류 (pack.json 과 같은 엄격함 — 새 키는 schemaVersion 을 올려 더한다). 문제는 `ContentIssue` 로 모두 모은다
(`sandbox.project.json:/gameId: 오류 P2 — …`). 정규형 쓰기(키 순서 고정 · 들여쓰기 2 · 끝 줄바꿈)는 다시 읽으면 같은 값이다.

프로젝트 검사 (`project::validateProject`, `sbx_sim_check --validate-project <폴더>` — 오류면 종료 코드 1):

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

| 번호 | 결정                                                              | 기한         | 상태                                         |
|------|-------------------------------------------------------------------|--------------|----------------------------------------------|
| D-01 | SandboxScript 숫자 타입 · 예외 · GC · import 부분집합             | v0.3 진입 전 | 열림                                         |
| D-02 | GamePackage 직렬화 · 서명 · 압축                                  | v0.2 종료 전 | 열림 (M5-T2)                                 |
| D-03 | 사용자 Component 스키마의 동적 풀                                 | v0.3 초기    | 열림                                         |
| D-04 | Script SDK / Host API 단일 정의 (IDL)                             | v0.3 초기    | 열림                                         |
| D-05 | SemVer 충돌 해결 · 범위 문법 확장 · 유료 패키지 EULA              | v0.4 초기    | 일부 — v0.2 범위 문법 (4장)                  |
| D-06 | GitHub 연동 범위 · 서비스(Hub · Cloud)를 둘 저장소                | v0.6 초기    | 열림                                         |
| D-07 | 공개 네트워크 보안 전송 · NAT                                     | v0.7 진입 전 | 열림 (R6)                                    |
| D-08 | 무료 세션 자원 · 릴레이 비용 한도                                 | v0.7 초기    | 열림                                         |
| D-09 | 판매 국가 · 수수료 · 환불 · 정산 사업자                           | v0.8 진입 전 | 열림                                         |
| D-10 | Business 좌석 · CI 할당 · SLA                                     | v0.9 초기    | 열림                                         |
| D-11 | 플레이어 행동 모델 (유닛 명령 · 직접 조작, 지연 체감 — 예측 없음) | v0.2 M3 전   | 열림 — 비목표 "클라 예측 없음"과 함께 정한다 |

## 7. 문서 · ADR 계획

```text
지금      ADR-0039 범위 · 버전 줄, ADR-0040 GameProject 매니페스트, 00 (1 · 7장), 16 (2 · 6장 → 이 문서), 17, 13, 11 (프로젝트 폴더)
M1-T2 ✅  15 (실행 옵션), 08 (WorldSource), ADR-0041 (서버가 프로젝트를 여는 규칙)
M1-T3 ✅  08 (핸드셰이크에 게임 정체성 · 프로토콜 8), ADR-0042
M2 · M3   03 (GameSession Stage · 게임 System), 11 (GameMode · 게임 컴포넌트), 04 (상태기계 결정론), ADR
M4        03 6.4 갱신 (Visual Trigger 와 Rule 의 관계), 10 (트리거 편집), ADR
M5        09 (GamePackage · SaveGame 결속), ADR (D-02)
v0.3 전   ADR (D-01 · D-04), 01 (스크립트 모듈 경계)
```

## 8. 기준선 (2026-10-19, master f4a72c1)

```text
Linux clang 19 Debug (-Werror, Vulkan · MSL 셰이더) ctest -LE "balance|bench": 51/51 통과 (det 16 · unit 23 · client 7 · …)
GitHub master = f4a72c1 (git ls-remote 확인). Actions 결과는 이 환경에서 읽을 수 없다 (1.1)
v0.2 M1-T1 뒤: 같은 구성 55/55 (+ unit_project · project_minimal_validate · project_bad_manifest_rejected · _message)
v0.2 M1-T2 뒤: 58/58 (+ server_project_exit · server_project_invalid · client_local_project_headless)
v0.2 M1-T3 뒤: 59/59 (+ net_server_probe_project — 실제 UDP 로 Welcome.game)
```
