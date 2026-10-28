# 21. 플랫폼 기능 분석 — 구현 상태 (Gap Analysis) · 신규 기능 12 종 · 버전 배치

> **분석 문서** (2026-10-28, master 637be03 — v0.4.0 직후). 현재 코드 · 시험 · 문서를 직접 대조한 구현 상태 (1장), 신규 기능
> 12 종의 분석 (2 · 3장), v0.5 ~ v1.0 배치 (4장). v0.5 의 실행 계획은 [20-CREATOR-ALPHA](20-CREATOR-ALPHA.md), 버전 줄 · 다음 태스크는
> [18-PLATFORM](18-PLATFORM.md) 0 · 1 · 6장, 배치 결정은 [ADR-0082](adr/0082-v06-v10-feature-placement.md).
> 상태 용어: **Implemented** = 코드 + 자동 시험이 있다 · **Partial** = 일부만 (빠진 것을 적는다) · **Planned** = 18-PLATFORM 또는 ADR 에
> 계획이 있고 코드는 없다 · **Missing** = 계획도 코드도 없다.

---

## 1. 현재 구현 상태 (v0.1 ~ v0.4)

18-PLATFORM 2장의 표 (게임 제작 영역) 를 v0.4 까지로 넓히고, 이번 요청의 영역을 더했다. 근거는 경로 · 시험 이름 · 문서 장.

| 영역 | 상태 | 근거 (코드 · 시험 · 문서) | 빠진 것 |
|------|------|---------------------------|---------|
| ECS · 리플렉션 · 동적 컴포넌트 | Implemented | core/ecs (Sparse-Set · stableId · ComponentCatalog · DynamicComponent), unit_ecs · property_ecs, 02 | — |
| 기본 엔진 컴포넌트 | Implemented | core/components (core.transform · velocity · movement · collider · tags · lifetime · prefab · identity, life.* · ai.* · game.session · score · spawn_point · camera_start), RegisterCoreComponents | 스크립트에서의 접근 (아래) |
| SystemScheduler · 틱 파이프라인 | Implemented | core/simulation/SystemScheduler · SimulationWorld (17b · 17c · 16b · 17d), det_* 16 개, 03 2장 | — |
| SimCommand · ECB | Implemented | core/command (CreateEntity · delete · move · add · remove · change · paint · speed · action · game · content), CommandJson, 02 · 03 4장 | 스크립트가 쓰는 길 (아래) |
| 결정론 · WorldHash · Replay | Implemented | core/replay (Replay · WorldHash), replay.sbxr (09 4장), det_replay_* · D1~D5 | — |
| 리플레이 보기 | Partial | 서버가 기록 · 재생 (WorldOp — ADR-0031), 클라이언트 "월드" 패널의 재생 단추 · 상태 줄 (apps/client/ui/WorldPanel) | 혼자 여는 뷰어 · 타임라인 · 되감기 (seek) · 파일 열기 |
| 관전 | Partial | 역할 observer (08 · 10 7장 — 편집 못 하는 접속자), LAN 접속 | 인증된 원격 관전 · 관전 전용 지연 · 시점 고정 |
| 월드 · 세이브 | Implemented | core/persist (world.json · entities.jsonl · chunks, 마이그레이션), unit_persist, 09 | 게임 버전 사이 세이브 마이그레이션 (v0.2 노트에서 미룸) |
| 네트워크 · 복제 | Implemented (LAN) | network/ (ENet · ServerHost · 차분 스냅숏 · Interest · 역할 · 속도 제한), net_convergence, 08 | 암호화 · 인증 (R6 → v0.7 D-07) |
| 입력 | Partial | platform/common/ActionMap · apps/client/DefaultInput · PlayKeys, 팩 presentation/input.json 의 게임 키 (11) | 사용자 키 다시 묶기 UI · 저장, 게임 패드, 스크립트의 입력 |
| 오디오 | Missing (Null 만) | platform/audio/NullAudioBackend (07 7장 — IAudioBackend 인터페이스만) | 실제 출력 백엔드 · 믹서 · 소리 에셋 |
| 게임 UI · HUD | Missing | 게임 화면의 것은 ImGui 디버그 · 게임 패널 (apps/client/ui — GamePanel 점수 줄 · ActionPanel) 과 클라이언트 스크립트의 label · print (19 21장) 뿐 | 게임 제작자가 만드는 UI 문서 · 배치 · 바인딩 · 이벤트 |
| 에디터 UI | Implemented (도구용) | editor/ui (ImGui — 편집 · 인스펙터 · 콘텐츠 · 트리거 폼 · 팩별 목록), unit_editor, 10 | 게임 UI 편집기 (게임 UI 가 없다) |
| 다국어 · 접근성 | Missing | 문자열이 코드 · 콘텐츠에 한국어로 박혀 있다. 폰트 malgun.ttf (Windows) · ImGui FontScaleDpi (배율 — DPI 만) | 문자열 표 · 로케일 · 폰트 대체 · UI 배율 설정 · 키 설정 |
| SandboxScript 컴파일러 · VM | Implemented | script/ (렉서 · 파서 · 검사기 · 코드 생성 · .sbc 5 · 검증기 · 인터프리터), unit_script · 보호 시험 · 퍼즈, 19 | JIT/AOT (계획 없음 — 5장), 함수별 프로파일 |
| Host API | Partial | script/api/HostApi (지문 e02f109682ecb304): ctx.tick · log · read · has · query · write (server), client.tick · frame · dt · read · has · query · label · print, 19 10장 | 엔진 컴포넌트 · 생성 · 파괴 · 붙이기 · 떼기 · random · 입력 · 소리 · 플레이어 · 세션 (v0.3 노트 5장 — v0.5) |
| 스크립트 시스템 · 예산 · 프로파일 | Implemented | core/script (ScriptSystemAdapter 16b · 17d, ClientScriptSystems, 예산 · 트랩 정책, 시스템별 프로파일), 19 17~22장 | 함수별 프로파일 · 디버거 |
| 게임 규칙 (노코드) | Implemented | Rule · Behavior · Trigger (노드 21 종) · GameMode · PlayerAction, 11 · ADR-0045~0052 | 노드 그래프 캔버스 · 쿨다운 · 비용 |
| GameProject · GamePackage | Implemented | core/project · core/package (.sbpkg 형식 2 · 라이선스 요약), sbx new · check · build · test, 09 9장 | 에디터 안의 새 프로젝트 · 빌드 마법사, 압축 |
| 패키지 관리자 | Implemented | core/package (Resolve · Lockfile · Install · GitSource · PackTemplate), sbx add · remove · update · install, ADR-0071~0077 | 레지스트리 · 서명 (v0.6) |
| 템플릿 | Partial | sbx new --from <시나리오> · --mode (빈 월드 · ecosystem_small · eco.protect 데모), samples (rabbit_guard · mana_regen · stash) | 초안의 "템플릿 3종" (미니게임 · RTS · 생존) 으로 고른 틀, 에디터 안 고르기 |
| 오류 보고 · 충돌 로그 | Missing | 로그는 콘솔 · 파일 (foundation/log). 충돌 처리기 · 덤프 · 보고 묶음 없음 | 충돌 처리 · 민감정보 제거 · 보고 묶음 · 보기 |
| 진단 (호환성) | Partial | sbx check (P1~P8 · V1~V12 · S 진단), 서버 · 클라이언트의 열기 오류 문장 | 다른 PC 사용자가 보는 진단 화면 · 묶음 (엔진 · 패키지 · 콘텐츠 · 드라이버) |
| 자동 업데이트 · 복구 | Missing | — | 버전 확인 · 받기 · 되돌리기 · 안전 모드 |
| 시험 · 벤치 | Implemented | ctest 67 (render_tests_warp 제외) · doctest 636, sbx_bench (50k · script.regen), 13 · 14 | CI 결과 (v0.1 열린 항목), Linux · macOS 실기 |

v0.1 의 열린 항목 (18 1.1 — 실기 QA · 50k 판정 · CI) 은 그대로다. **Linux · macOS 는 빌드 · 시험을 이 환경에서 돌리지 않았다** —
지원 매트릭스 (20 7장) 에서 "검증 안 됨" 으로 둔다.

## 2. 신규 기능 12 종 — 분석

외부 엔진 · UGC 플랫폼 참고 (3장) 와 섞지 않는다. "기존과 겹침" 은 위 1장의 코드다. 복잡도는 이 저장소 기준의 추정 (S · M · L ·
XL — 측정 아님).

### 2.1 게임 UI / HUD 제작 시스템

```text
겹침        클라이언트 스크립트 label · print (개체 위 글자 · 한 칸), ImGui (도구 · 디버그 패널 — 게임 UI 가 아니다), ActionMap
신규        UI 문서 (팩 presentation/ui/*.json — Panel · Text · Image · Button · ProgressBar · 목록), 앵커 · 크기 (비율 + 픽셀),
            스타일 · 테마, 바인딩 (스크립트 변수 · 게임 세션 · 내 개체의 컴포넌트), 이벤트 (버튼 → 서버 검증 행동), 입력 라우팅
            (UI 가 먼저 — 게임 키와 겹침), 에디터의 UI 미리보기 · 편집, 렌더 백엔드 경계 (ADR-0079)
가치        제작자: 코드 없이 점수 · 시간 · 체력 · 버튼 HUD. 사용자: 디버그 패널이 아닌 게임 화면
API · 모델  UiDocument { id, root: UiNode }, UiNode { kind, id, anchor{min,max}, offset{l,t,r,b}, style, bind?, on? , children }
            client 스크립트 ctx.ui.set(key, value) · ctx.ui.visible(id, bool), server 이벤트 "ui" (버튼 id · 플레이어)
의존        2.2 (플레이어 · 내 개체 · 입력 행동), 2.4 (문자열 키 · 배율)
서버 권위   UI 는 클라이언트 표시뿐 — 서버가 UI 문서를 읽지 않는다 (contentHash 밖, presentation/). 버튼은 행동 명령을 보내고
            서버가 규칙으로 판정한다 (위조한 클릭 = 그냥 거절되는 명령)
복잡도      L (배치 · 글자 · 입력 라우팅 · 에디터)
버전        v0.5 (기반 · 기본 요소 · 바인딩 · 이벤트 · 미리보기), v0.6 (끌어 놓는 시각 편집 · 애니메이션 · 9-slice), v1.x (GraphicsUI 백엔드)
시험 · 완료  배치 · 앵커 결정적 시험 (헤드리스 — 크기마다 사각형), 바인딩 갱신, 버튼 → 명령 → 서버 거절/수락, 데모 HUD 화면 확인
```

### 2.2 기본 게임플레이 API 확장

```text
겹침        ctx.read · has · query · write (스크립트 컴포넌트), SimCommand (생성 · 삭제 · 붙이기 · 떼기 · 이동 — 편집 명령), ECB,
            PlayerAction (서버 판정 행동), game.session · score, 공간 색인 (05)
신규        엔진 컴포넌트 접근 표 (컴포넌트 · 필드마다 읽기/쓰기 도메인 · 쓰기 규칙), ctx.spawn · destroy · add · remove (ECB —
            틱 끝에 결정적 순서로), ctx.random (시스템마다 결정적 흐름), ctx.near (반경 질의), 플레이어 · 세션 정보
            (ctx.players · ctx.session), 행동 · 입력 이벤트 (onEvent "action" · "ui"), client ctx.input · ctx.send (행동 명령),
            ctx.sound (client — 표시용 이벤트)
가치        스크립트로 턴제 · 보드 · 수집 게임 규칙을 끝까지 (v0.3 데모는 컴포넌트 값 바꾸기만 가능)
API         20 5장 (계약)
의존        2.1 (ui 이벤트), 오디오 백엔드 (소리 — 없으면 Null)
서버 권위   쓰기 · 구조 변경은 server 도메인만, 엔진 컴포넌트는 접근 표가 허락한 필드만 (게임이 만든 값 범위 · 엔진 관리 필드 거절),
            예산 (호출 · 틱마다 구조 변경 상한), client 는 읽기 · 표시 · 명령 보내기만 — 서버가 행동 규칙으로 다시 판정
복잡도      L (결정성 — 생성 id · 순서 · random, 검증기 · 보호 시험)
버전        v0.5 (위 전부 — 단 소리 출력은 백엔드 결정 D-14 에 따라), v0.6 (물리 질의 · 경로 요청 · 타이머 API), v0.7 (원격 플레이어 정보 · 권한)
시험 · 완료  두 월드 같은 해시 (생성 · 파괴 · random), 세이브 → 로드, 리플레이, 권한 거절 (client 쓰기 · 엔진 관리 필드), 예산 트랩, 오목 데모
```

### 2.3 리플레이 · 관전

```text
겹침        replay.sbxr 기록 · 재생 (서버), 해시 비교, "월드" 패널, observer 역할 (LAN 관전), 자동 플레이 (playthrough)
신규        v0.5: 혼자 여는 뷰어 (SandboxClient --replay <폴더> — 로컬 서버로 재생), 재생 · 멈춤 · 속도 · 되감기 (처음 세이브부터
            다시 — 결정적이라 같은 자리), 타임라인 UI, 자동 플레이 · 테스트 플레이를 기록으로 남기기. v0.7: 인증된 원격 관전 ·
            관전 지연 · 관전 권한 (방 설정)
가치        버그 재현 · 공유 (A 가 겪은 문제를 B 가 같은 화면으로), 토너먼트 다시 보기
API · 모델  ReplayViewer { open(dir), play, pause, setSpeed, seek(tick) }, 기존 .sbxr (replayVersion 그대로 — 형식 바꾸지 않음)
의존        2.1 (타임라인 UI — 없으면 ImGui 도구 패널), 2.2 와 무관 (명령 재생)
서버 권위   재생은 기록된 명령만 — 새 입력 없음. 원격 관전은 v0.7 보안 전송 뒤 (관전자는 복제만 받는다)
복잡도      M (v0.5 뷰어) · L (v0.7 원격)
버전        v0.5 뷰어, v0.7 원격 관전
시험 · 완료  같은 기록을 두 번 재생 = 같은 해시 열, seek 후 해시 = 처음부터 그 틱까지의 해시, 뷰어 헤드리스 끝 줄
```

### 2.4 다국어 · 접근성

```text
겹침        ImGui 폰트 (malgun.ttf) · DPI 배율, ActionMap (키 → 행동)
신규        v0.5: 팩 문자열 표 (presentation/strings/<로케일>.json — 키 "<팩>.<이름>"), UI · 행동 이름 · 게임 메시지의 키 참조,
            로케일 설정 · 대체 (ko → en → 키), 폰트 대체 목록 (글자가 없으면 다음 폰트), 글자 늘어남 대응 (줄 바꿈 · 맞춤),
            키 다시 묶기 (사용자 설정 파일 + UI), UI 배율. v0.9: 에디터 · 도구 문자열, v1.0: 접근성 점검 (색 대비 · 색각 ·
            화면 읽기 검토)
가치        한국어 밖 사용자, 손에 맞는 키, 큰 화면 · 작은 화면
API · 모델  StringTable { locale, entries{key→text} }, ctx.t(key, args) (client), UI Text { text: "@eco.hud.score" }
의존        2.1 (글자를 그리는 곳)
서버 권위   표시뿐 — 서버는 키만 보낸다 (게임 메시지는 키 + 인자). contentHash 밖 (presentation/)
복잡도      M
버전        v0.5 기반, v0.6 (Hub 목록 다국어), v0.9 (도구 다국어), v1.0 (접근성 점검)
시험 · 완료  없는 키 · 없는 로케일 대체, 인자 끼우기, 긴 문자열 배치 (잘림 없음), 키 다시 묶기 저장 · 겹침 경고, 배율 배치
```

### 2.5 플레이어 데이터 영구 저장 API

```text
겹침        세이브 (월드 상태 전체 — 게임 한 판의 상태), world.json "game" 결속
신규        플레이어마다 · 게임마다의 키-값 저장 (판을 넘는 진행도 · 설정), 할당량 · 쓰기 속도 제한, 서버만 쓰기, 버전 · 마이그레이션
가치        판을 넘는 성장 · 수집
의존        계정 · 신원 (v0.6), 신뢰할 서버 (v0.7 보안 전송 · 공식 세션), 저장 서비스 (Hub 밖 저장소 — D-06)
서버 권위   클라이언트는 절대 쓰지 않는다. 자체 호스트 서버의 쓰기는 그 서버 안에서만 믿는다 (공식 순위 · 업적과 섞지 않는다)
복잡도      L (서비스 · 할당량 · 장애)
버전        v0.7 (신원 · 보안 전송 뒤). 로컬 싱글플레이 저장 (파일) 은 v0.7 에 같은 API 의 로컬 구현으로
시험 · 완료  위조 쓰기 거절, 할당량, 동시 쓰기 (마지막 쓰기 · 버전 검사), 서비스 장애 때 게임 계속
```

### 2.6 게임 발견 · 추천

```text
겹침        .sbpkg 색인 (이름 · 버전 · 라이선스 요약), 패키지 SBOM
신규        Hub 목록 · 검색 (태그 · 이름 · 라이선스) — v0.6, 추천 (플레이 데이터가 필요) — v0.8
의존        계정 · Hub 서비스 (v0.6), 플레이 통계 (2.10 — v0.8)
보안        검색 결과의 악성 게임 — 신고 (2.9) · 서명 (v0.6)
복잡도      M (검색) · L (추천)
버전        v0.6 검색 · 목록, v0.8 추천 (데이터가 생긴 뒤)
```

### 2.7 친구 초대 · 파티 · 커뮤니티

```text
겹침        LAN 접속 · 역할, 방 (Room) 은 v0.7 계획
신규        친구 목록 · 초대 · 파티 (함께 방에 들어가기), 커뮤니티는 직접 만들지 않고 외부 (GitHub Discussions 등) 연결
의존        계정 (v0.6), Room · JoinTicket (v0.7)
복잡도      M
버전        v0.7 (방과 함께). 커뮤니티 링크는 v0.6 Hub 목록에
```

### 2.8 등급전 · 레이팅 · 랭킹

```text
겹침        GameSession 결과 (승 · 패 · 팀 · 점수 — game.session), 자동 플레이의 결과 줄
신규        경기 결과의 신뢰 (공식 세션 서버의 결과만), 레이팅 (Elo/Glicko 류 — 선택은 그때 ADR), 시즌 · 순위표
의존        신원 (v0.6), 공식 세션 서버 · 보안 전송 (v0.7), 플레이어 데이터 (2.5)
보안        자체 호스트 서버의 결과는 공식 순위에 넣지 않는다 (위조 가능), 담합 · 고의 패배는 운영 도구 (2.9)
복잡도      L
버전        v0.7 (공식 세션 서버의 결과만 — 등급전 기본), 시즌 · 다지역은 v0.8 이후
```

### 2.9 신고 · 차단 · 운영자 도구

```text
겹침        역할 (owner · admin · editor · player · observer), 접속 끊기 (서버), 패키지 거부 시험
신규        v0.6: 패키지 · 게임 신고 · 사용자 차단 (Hub), 운영자 검토 큐 · 감사 로그. v0.7: 방 안 신고 · 차단 · 음소거 · 강퇴 기록.
            v0.9: 운영 콘솔 성숙 (처리 시간 · 정책)
의존        계정 (v0.6)
복잡도      M ~ L (운영 정책이 더 크다)
버전        v0.6 (기반), v0.7 (방), v0.9 (운영)
```

### 2.10 제작자 게임 분석 대시보드

```text
겹침        스크립트 프로파일 · 서버 통계 (ServerStats), 패키지 다운로드 (v0.6 계획)
신규        플레이 통계 (세션 수 · 길이 · 이탈), 동의 · 익명화 (opt-in 원격 측정), 대시보드
의존        Hub (v0.6), 공식 세션 (v0.7)
보안 · 개인정보 수집 동의 · 최소 수집 · 보존 기간
복잡도      L
버전        v0.6 패키지 분석 (다운로드 · 의존 — 초안 v0.6 요구 6), v0.8 플레이 분석
```

### 2.11 업적 · 통계 · 순위표 API

```text
겹침        점수 (game.score), 게임 결과
신규        업적 정의 (팩 데이터 — 조건 · 아이콘 · 문자열 키), 통계 키, 순위표 — 모두 서버 기록
의존        플레이어 데이터 (2.5 — v0.7), 순위 (2.8)
보안        업적 · 통계 쓰기는 서버만 · 공식 서버의 것만 공개 순위
복잡도      M
버전        v0.8 (초기 제안 그대로 — 2.5 · 2.8 이 v0.7 에 먼저 와야 한다). 순위표의 기본 (등급전) 은 v0.7
```

### 2.12 클라이언트 자동 업데이트 · 복구

```text
겹침        sbx --version · 빌드 정보, 패키지 digest 검증
신규        v0.5: 충돌 로그 · 보고 묶음 · 안전 모드 (설정 · 캐시 초기화로 시작), 새 버전 알림 (공개 Alpha 채널의 최신 버전 비교 —
            받기는 사용자가). v0.9: 서명된 업데이트 받기 · 원자적 교체 · 되돌리기 · 단계적 배포
의존        배포 채널 (v0.5 공개 Alpha — GitHub Releases), 서명 (v0.6 패키지 서명과 같은 키 관리)
보안        업데이트 = 실행 파일을 바꾸는 일 — 서명 없이는 하지 않는다 (v0.5 는 알림만)
복잡도      S (v0.5) · L (v0.9)
버전        v0.5 (진단 · 안전 모드 · 알림), v0.9 (자동 업데이트 · 복구)
```

## 3. 외부 참고 — 확인 범위

**이 작업에서 외부 엔진 · 플랫폼의 공식 문서를 다시 열어 확인하지 않았다.** 아래는 공개적으로 널리 알려진 설계의 일반 지식이고,
설계를 정할 때 (해당 ADR) 원문 확인이 필요하다. 사실처럼 인용하지 않는다.

```text
게임 UI     엔진들이 흔히 쓰는 모양: 부모 사각형 기준의 비율 + 픽셀 앵커 (Roblox UDim2 · Godot Control 앵커 · Unity RectTransform),
            선언형 문서 + 스타일 시트 (Unity UI Toolkit UXML/USS, 웹 HTML/CSS 계열 런타임 라이브러리). → 이 저장소의 설계 제안: 비율 +
            픽셀 앵커, JSON 문서 + 스타일 이름 (ADR-0079)
다국어      번역 표 (CSV · gettext .po) + 키 참조 + 대체 로케일. → 제안: JSON 문자열 표, 팩 이름공간 키
영구 저장   UGC 플랫폼들은 게임마다 키-값 저장을 서버 API 로 주고 할당량 · 속도 제한을 둔다. → 제안: v0.7, 서버만 쓰기
순위 · 업적 공식 서버 기록만 신뢰하는 구조가 흔하다. → 제안: 공식 세션 서버의 결과만
```

## 4. 버전 배치 (v0.5 ~ v1.0)

초기 제안 (요청) 을 따르되, 의존 때문에 바꾼 것은 이유를 적는다. 결정은 [ADR-0082](adr/0082-v06-v10-feature-placement.md).

| 기능 | v0.5 | v0.6 | v0.7 | v0.8 | v0.9 | v1.0 |
|------|------|------|------|------|------|------|
| 1 UI / HUD | 기반 · 기본 요소 · 바인딩 · 이벤트 · 미리보기 | 시각 편집 · 애니메이션 | — | — | — | 품질 점검 |
| 2 게임플레이 API | 엔진 컴포넌트 · 생성/파괴 · random · 이벤트 · 입력 · 소리 이벤트 | 물리 · 경로 · 타이머 | 원격 플레이어 · 권한 | — | — | API 동결 |
| 3 리플레이 · 관전 | 로컬 뷰어 · seek · 타임라인 | — | 원격 관전 · 권한 | 저장된 리플레이 공유 | — | — |
| 4 다국어 · 접근성 | 문자열 표 · 로케일 · 폰트 대체 · 키 설정 · 배율 | Hub 목록 다국어 | — | — | 도구 다국어 | 접근성 점검 |
| 5 플레이어 데이터 | — | (신원) | 서버 API · 로컬 구현 | 백업 · 이전 | — | — |
| 6 발견 · 추천 | — | 검색 · 목록 | — | 추천 | — | — |
| 7 친구 · 파티 | — | 커뮤니티 링크 | 친구 · 초대 · 파티 | — | — | — |
| 8 등급전 · 랭킹 | — | — | 공식 세션 결과 · 레이팅 기본 | 시즌 | — | — |
| 9 신고 · 차단 · 운영 | — | 패키지 · 게임 신고 · 차단 · 검토 큐 | 방 안 신고 · 음소거 | 판매 분쟁 | 운영 콘솔 | — |
| 10 제작자 분석 | — | 패키지 분석 | — | 플레이 분석 (동의) | — | — |
| 11 업적 · 통계 · 순위표 | — | — | 순위표 (등급전) | 업적 · 통계 API | — | — |
| 12 업데이트 · 복구 | 충돌 로그 · 안전 모드 · 새 버전 알림 | — | — | — | 서명 자동 업데이트 · 되돌리기 | — |

초기 제안에서 바꾼 것:

```text
- 순위표 (11) 를 v0.8 하나에서 v0.7 (등급전 순위표) · v0.8 (업적 · 통계 API) 로 나눴다 — 등급전 (8) 이 v0.7 이고 순위표 없이는
  쓸모가 없다
- 신고 (9) 를 v0.6 하나가 아니라 v0.6 · v0.7 · v0.9 로 — 방 안 신고는 방 (v0.7) 이 있어야 한다
- 자동 업데이트 (12) 의 일부 (충돌 로그 · 안전 모드) 를 v0.5 로 당겼다 — 초안 v0.5 요구 4 (버그 리포트 · 충돌 로그) 와 같은 일이다.
  실행 파일을 바꾸는 자동 업데이트는 서명이 있는 v0.9 그대로
- 플레이어 데이터 (5) 는 v0.7 그대로 — 계정 (v0.6) 과 신뢰할 서버 (v0.7) 가 없으면 위조를 막을 수 없다
```
