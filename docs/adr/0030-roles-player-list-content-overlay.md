# ADR-0030. 역할은 Net 절반이 접속자 표로 바꾸고 알리며, 콘텐츠 편집은 EditContent 명령이 월드 오버레이를 바꿔 팩 원본에서 다시 만든다

- 상태: **Accepted** · 날짜: 2026-10-12
- 관련: [10-EDITOR](../10-EDITOR.md) 2 · 7 · 8장, [08-NETWORK](../08-NETWORK.md) 4 · 5장(프로토콜 6),
  [09-SERIALIZATION](../09-SERIALIZATION.md) 3장, [04-DETERMINISM](../04-DETERMINISM.md) 5장,
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md), [ADR-0024](0024-network-foundation-enet-serverhost-two-halves.md),
  [ADR-0028](0028-editor-library-host-tools-preview.md), [16-ROADMAP](../16-ROADMAP.md) Phase 12C

## 맥락

12C = 12.5 역할 바꾸기 · Players 패널 · 권한이 없을 때 툴 숨김 + 12.4 Rule · Behavior · ContentOverlay (ADR-0028 결정 1).
Phase 9 부터 역할은 접속할 때 정해지고(--default-role) 바뀌지 않았다. 콘텐츠(ContentDatabase)는 불변이고, 월드는 그것을 참조로
들고 있다. 설계 초안은 ChangeRule · ChangeBehavior · CreatePrefab 명령과 RoleChanged · ContentOverlay 메시지를 예약해 두었다.
정할 것:

```text
1. 누가 Owner 인가 (전용 서버), 역할 바꾸기의 판단 · 알림 · 다시 접속
2. 클라이언트가 접속자 · 자기 역할을 어떻게 아나, 에디터가 무엇을 숨기나
3. 콘텐츠 편집 명령의 모양, 실행 중인 월드의 콘텐츠를 어떻게 바꾸나 (불변 DB · 결정론 · 해시 · 세이브)
4. 다른 클라이언트 · 늦게 들어온 클라이언트가 바뀐 콘텐츠를 어떻게 받나
5. 에디터 UI (Rules · Behaviors 패널)
```

## 결정

```text
1. 역할은 서버 Net 절반의 클라이언트 표가 갖는다 (월드 상태가 아니다 — 명령이 아니라 메시지). SetRole{clientId, role}(63) →
   net::roleCanAssign(요청자, 대상, 새 역할) = 10-EDITOR 7장 마지막 열 그대로: Owner = 누구든 무엇이든, Admin = Admin 아래인
   사람을 Editor 이하로, 그 밖 · 자기 자신 · 없는 번호 = 거절. 요청자에게 SetRoleResult(64). 바뀌면 다시 접속 표(토큰)에도 —
   끊겼다 들어와도 그 역할. 전용 서버의 Owner = Auth.ownerKey 가 --owner-key 와 같은 사람 (틀린 열쇠 · 열쇠 없는 서버에 열쇠 =
   Reject{BadHandshake} — 조용히 무시하지 않는다). 싱글플레이는 언제나 Owner (그대로).
2. PlayerList(61 — 예약된 RoleChanged 를 대신한다) = 접속자 표 전체 {clientId, role, name}, 들어옴 · 나감 · 역할이 바뀔 때마다
   모두에게 (Control). 클라이언트는 자기 줄로 지금 역할을 안다. IEditorHost::permissions() = EditPermissions{entities ·
   terrain · content · simulation} (roleAllows 와 같은 표) — 에디터는 막힌 툴을 숨기고(지금 툴이 막히면 선택 툴로), Delete ·
   Inspector · 되돌리기 · 콘텐츠 적용을 하지 않으며, 시뮬레이션 패널은 실행 제어 단추를 끈다. 판단은 언제나 서버.
3. EditContent{kind(prefab · rule · behavior), id, value(팩 파일과 같은 JSON 객체 | null)} 한 명령 (설계 초안의 셋을 합친다,
   ContentEdit 열 — Editor+). 월드 오버레이 = (종류, id) → JSON. 같은 id 의 팩 항목을 바꾸거나 새로 더하고, null 은 오버레이에서
   뺀다 (팩 값으로). 콘텐츠 로더가 읽은 파일 텍스트를 DB 에 남기고(ContentSources, 불변 · 공유) ContentLoader::applyOverlay 가
   팩 원본 + 오버레이로 DB 를 통째로 다시 만든다 — 검증(V1~V7)은 팩과 똑같고, 실패하면 명령 거절 · 아무것도 바뀌지 않는다.
   SimulationWorld 는 팩 DB(생성자) + 오버레이 DB(shared_ptr)를 들고 content() 가 지금 것을 준다 (틱 안 System 은 그 틱의
   SystemContext 로 — 명령 단계에서만 바뀐다). BehaviorGraph 를 바꾸면 그 그래프를 쓰는 개체의 상태 번호를 상태 id 로 옮긴다
   (없어진 상태면 처음부터). 바뀐 Rule 은 원래 정의 순서(동점 정렬 키)를, 더한 Rule 은 맨 뒤를 갖는다. 태그 · 머티리얼은
   오버레이로 바꿀 수 없다 (비트 · 번호가 흔들리지 않게 — 오버레이가 모르는 태그를 쓰면 V7). contentHash 는 팩 그대로
   (접속 검사 · 리플레이 머리), 오버레이 지문은 WorldHash 끝에 (비어 있으면 넣지 않는다 — 옛 골든 그대로). 세이브는
   overlay/<prefabs|rules|behaviors>/<id>.json (항목 하나에 파일 하나), 로드할 때 검증에 실패하면(팩이 바뀜) 경고하고 팩
   콘텐츠로 (해시 비교는 건너뛴다). 리플레이는 명령 JSON {"op":"content", …} 으로 그대로.
4. ContentOverlay(42, Bulk, 16 MB 까지) {reset, items[] {kind, id, json — "" = 지움}}. Sim 절반이 contentRevision 이 바뀌면
   마지막으로 보낸 것과의 차이를 모두에게, 들어온 클라이언트에는 reset 전체 (스냅숏보다 먼저 내보낸다). 클라이언트는 모아 둔
   오버레이를 자기 팩 콘텐츠에 applyOverlay 해 content() 로 (에디터 · 팔레트가 새 Prefab · 규칙을 본다). 서버가 받아들인
   것이니 같은 팩 · 카탈로그면 통과한다 — 실패하면 팩 그대로 쓰고 경고.
5. "콘텐츠" 창 (editor/ui/ContentPanel, "편집" 패널의 단추로 연다): 종류 → 목록(● = 오버레이) → JSON 편집 · "검사"(이 컴퓨터의
   로더로) · "적용"(Editor::editContent — 되돌리기 한 칸, 역명령 = 이전 오버레이 값 | null) · "팩 값으로" · "새 항목"(틀).
   행동은 상태 · 전이 요약, 규칙은 action · 효과 요약. "접속자" 패널 (apps/client/ui/PlayersPanel): 표 + 바꿀 수 있는 사람에게
   역할 콤보 (net::roleCanAssign — 서버와 같은 함수). 프로토콜 6 (EditContent 태그 8 · 42 · 61 · 63 · 64 · Auth.ownerKey).
```

## 근거

```text
- 역할은 시뮬레이션과 무관하다 (해시 · 세이브 · 리플레이에 없다) — Net 절반이 이미 역할로 명령을 거른다(ADR-0024). 메시지로 두면
  Sim 스레드를 거치지 않고 바로 판단 · 알린다. 표 전체를 보내면 클라이언트 쪽 상태 기계가 없다 (16 명 × 수십 바이트).
- 명령 하나에 종류를 실으면 와이어 · JSON · 검증 · 되돌리기가 한 길이다. 값을 팩 파일 형식 그대로 두면 로더 검증을 다시 쓰고,
  "오버레이를 팩으로 내보내기"(10-EDITOR 8장 후속)가 파일 복사가 된다.
- 다시 만들기(부분 패치 대신): Rule 색인 · action 번호 · 감지 질의 번호 · 교차 참조(V2)가 서로 얽혀 있다 — 로더 한 길로 만들면
  팩으로 읽은 것과 같은 DB 가 보장된다. 팩은 작다 (ecosystem 수십 KB — 다시 만들기는 ms).
- 불변 DB 를 갈아 끼우면 System 코드는 그대로다 (ctx.content). 틱 캐시(ai.behavior.cacheIndex)는 id 를 확인하고 쓰므로 안전하다.
- 같은 명령 순서 → 같은 오버레이 → 같은 DB (D1). 오버레이를 해시에 넣어야 세이브 왕복(D2) · 리플레이가 콘텐츠 차이를 잡는다.
- 오버레이 메시지를 따로 두면 스냅숏(비신뢰 · 예산)과 섞이지 않는다 — 콘텐츠는 잃으면 안 된다.
```

## 결과

- 얻는 것: 실행 중 Rule · Behavior · Prefab 바꾸기 · 더하기(모두에게, 세이브 · 리플레이 포함), 역할 바꾸기와 접속자 패널, 역할에
  맞춘 에디터. 시험: content(오버레이 검증 · 자리 · 지문), core(EditContent · 상태 옮기기 · 결정론 · 명령 JSON), persist(세이브
  왕복 · 깨진 오버레이), network(메시지 · 주인 열쇠 · 역할 표 · 다시 접속 · 오버레이 전달), editor(권한 · 콘텐츠 되돌리기 · 검사),
  client(원격 역할 → 에디터, 로컬 콘텐츠 편집 → 되돌리기, 패널 그리기).
- 포기하는 것 / [계획]: 폼 · 노드 그래프 편집 (지금은 JSON 글자), 팩 항목 끄기(오버레이로 지우기), 오버레이를 팩으로 내보내기,
  태그 · 머티리얼 편집, 역할을 바꾼 기록 · 강퇴 · 차단, 열쇠 없이 Owner 넘기기의 감사, 소유 개체(PlayerAction — alwaysRelevant).
  콘텐츠가 바뀌면 개체의 상태 번호만 옮긴다 — Prefab 을 바꿔도 이미 만든 개체는 그대로다.
- 위험: 큰 팩에서 편집마다 전체를 다시 만든다 (팩이 MB 단위가 되면 [계획 15] 증분). 오버레이가 1024 항목 × 16 KB 에 닿으면
  메시지가 16 MB 에 가깝다. 주인 열쇠는 평문 (Phase 9 처럼 LAN · 신뢰하는 환경 전용).

## 대안

| 대안                                                    | 기각 사유                                                                     |
|---------------------------------------------------------|-------------------------------------------------------------------------------|
| 역할 바꾸기를 SimCommand 로                             | 시뮬레이션 상태가 아니다 — 해시 · 리플레이에 섞이고 Sim 스레드를 거친다         |
| RoleChanged{clientId, role} 만 (표 없이)                | 접속자 목록은 따로 필요하다 — 표 하나로 들어옴 · 나감 · 역할을 다 알린다        |
| 첫 접속자 = Owner (전용 서버)                           | 누가 먼저 들어오느냐에 달린다 — 서버를 띄운 사람이 열쇠로                      |
| ChangeRule · ChangeBehavior · CreatePrefab 세 명령      | 와이어 · 검증 · 되돌리기가 세 벌 — 하는 일은 같다 (종류만 다르다)               |
| DB 를 부분 패치 (그 항목만 바꾸기)                      | 색인 · 번호 · 교차 참조를 다시 맞춰야 한다 — 로더와 다른 두 번째 길            |
| 콘텐츠 해시에 오버레이를 넣는다                         | 접속 검사가 오버레이마다 깨진다 — 오버레이는 서버가 내려준다                    |

## 재검토 조건

팩이 커져 다시 만들기가 프레임을 넘길 때 (증분 · Worker), 폼 · 그래프 편집기가 생길 때 (JSON 대신 구조화 편집 — 명령은 그대로),
인터넷에 여는 서버 (열쇠 · 역할을 계정 · 암호화로), 팩 항목을 끄는 요구가 생길 때 (오버레이의 "지움" 표식).
