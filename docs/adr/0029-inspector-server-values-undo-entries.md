# ADR-0029. Inspector 는 복제 값 + 선택 상세의 서버 전용 값을 리플렉션 필드로 그리고, 되돌리기는 받아들여진 편집 한 번을 한 칸으로 역명령과 함께 기록한다

- 상태: **Accepted** · 날짜: 2026-10-09
- 관련: [10-EDITOR](../10-EDITOR.md) 2 · 4 · 6.2 · 9 · 10장, [08-NETWORK](../08-NETWORK.md) 5장(프로토콜 5),
  [ADR-0026](0026-network-session-local-server-inspect.md), [ADR-0028](0028-editor-library-host-tools-preview.md),
  [16-ROADMAP](../16-ROADMAP.md) Phase 12B

## 맥락

12B = Inspector · Hierarchy · Undo/Redo (ADR-0028 결정 1). 클라이언트의 복제 월드에는 복제(Replicated) 컴포넌트만 있다 —
core.transform · core.prefab · core.tags · life.energy · life.health · life.growth. 사용자가 고치고 싶은 값 다수는 서버 전용이다
(10-EDITOR 5장의 예 Rabbit.maxSpeed = core.movement, life.age · ai.sensor …). 정할 것:

```text
1. 서버 전용 값을 Inspector 가 어디서 읽나
2. 위젯을 어떻게 만드나 (E4 — 컴포넌트마다 UI 코드를 쓰지 않는다)
3. 언제 보내나, 여러 개를 고르면
4. 되돌리기의 한 칸 · 역명령 · 다시 만든 개체의 netId · 거절 · 동시에 여러 명령
5. Hierarchy 에 무엇을
```

## 결정

```text
1. 선택 상세(Inspect, 10B)를 넓힌다 — 프로토콜 5: InspectEntry.components[] = {stableId u64, json str ≤ 4 KB} × ≤ 16, 복제되지
   않는 EditorVisible 컴포넌트의 리플렉션 JSON. 선택한 개체(≤ 32)만, 스냅숏 간격마다. IEditorHost::components(netId) =
   복제 월드의 EditorVisible 값 + 선택 상세의 값 ("서버" 표시), stableId 오름차순.
2. 위젯 = ComponentInfo::fields (reflect 순서 · Hint · FieldMeta) × JSON 값의 종류: bool → 체크, 정수 · 실수 → 끌기 (범위 폭이
   1000 이하면 슬라이더, Percent → 0 ~ 1, Angle → 도), [x, y] → 두 칸, 문자열 → 글자 칸 (PrefabRef → Prefab 목록), EntityRef ·
   배열 · 객체 → 읽기만. 컴포넌트 더하기(카탈로그의 EditorVisible 중 없는 것) · 빼기.
3. 편집이 끝날 때(놓기 · Enter · 포커스 이탈, 체크 · 콤보는 바로) Editor::setField — 선택한 개체 중 그 컴포넌트가 있는 것마다
   ChangeComponent{target, stableId, {field: 값}} (값이 같으면 보내지 않는다). 첫 개체의 값을 보인다.
4. 되돌리기 (editor/Undo, 클라이언트 로컬): 한 칸 = 사용자 동작 하나 (배치 끌기 한 번 · 붓질 한 번 · 이동 한 번 · 지우기 한 번 ·
   Inspector 값 하나 · 컴포넌트 더하기/빼기). 결과가 다 와야 칸이 생기고 받아들여진 명령만 담는다 (다 거절이면 없음). 역명령은
   보내기 전의 클라이언트 값으로: Move → -delta, Change → 이전 값, Add ↔ Remove(이전 값), Paint → 칸의 이전 머티리얼마다,
   Create → Delete{결과의 새 netId}, Delete → CreateEntity{core.prefab, 알던 컴포넌트 값 — EntityRef 필드 · core.prefab ·
   net.identity 빼고} (새 netId). 다시 만든 개체는 IdRemap(옛 → 새, 사슬)으로 이후의 기록 속 대상을 바꿔 보낸다.
   되돌리기 · 다시 하기는 결과를 기다리는 편집이 없을 때만 (Editor::idle), 그 명령들 중 하나라도 거절되면 그 칸을 기록에서 뺀다.
   새 편집은 다시 하기 목록을 비운다. 100 칸. 그 사이 남이 같은 값을 바꿨으면 덮는다 (경고 [계획]).
5. Hierarchy = 복제 월드의 개체(관심 영역 안) "prefab #netId", 거르기(prefab · 번호), 클릭 = 선택, Ctrl = 넣기/빼기, 두 번 =
   카메라 맞추기. F = 선택의 가운데로 카메라 (editor.focus). Ctrl+Z · Ctrl+Y (editor.undo · redo, 기본 바인딩에 있던 것).
```

## 근거

```text
- 서버 전용 값을 모두 복제하면 모든 개체 · 모든 클라이언트의 대역폭이 는다 (14 7.11 — 개체당 바이트가 그대로 커진다). Inspector
  는 고른 몇 개만 본다 — 이미 있는 선택 상세 길에 얹으면 고른 것만 오간다. JSON 문자열은 컴포넌트 표(Welcome.replicated)가
  필요 없고 리플렉션 JsonVisitor 를 그대로 쓴다.
- 필드 목록은 카탈로그가 이미 갖고 있다 (검증기 · Rule 이 쓰는 FieldDesc) — 컴포넌트를 더해도 Inspector 코드는 그대로 (E4).
- 끌기마다 보내면 서버 명령이 초당 수십 개 · 되돌리기 칸이 수십 개가 된다 — 끝날 때 하나.
- "결과가 온 뒤에 기록" 은 10-EDITOR 6.2 (거절은 남기지 않는다) 그대로이고, 역명령에 필요한 새 netId(Create) 를 그때 안다.
  netId 는 재사용되지 않으니(08 7장) 옛 → 새 표가 모호하지 않다.
- 기다리는 동안 되돌리기를 막으면 "아직 서버가 모르는 편집의 역" 을 보내는 일이 없다 (순서가 꼬이지 않는다).
```

## 결과

- 얻는 것: 서버 전용 값까지 고치는 Inspector, 모든 툴 · Inspector 동작의 되돌리기 · 다시 하기, 개체 목록. 시험: editor 스위트
  (가짜 host — 명령 종류마다 역명령, netId 잇기, 거절 · 기다림), network (선택 상세의 컴포넌트), client (로컬 서버에서 서버 전용
  값 고치기 → 되돌리기, 지운 개체 되살리기, 앱 Ctrl+Z).
- 포기하는 것 / [계획]: 서버 전용 값은 선택한 개체만 · 한 스냅숏 늦게 보인다. 관심 밖 개체 · 고르지 않은 개체를 지우면 서버 전용
  값을 모르므로 Prefab 기본값으로 되살아난다. 되살린 개체는 옛 saveId · 관계(다른 개체가 그것을 가리키던 것)를 잃는다.
  여러 명령을 한 batchId 로 묶기, 덮어쓰기 경고, 위젯의 enum 이름 · 태그 칩 · EntityRef 고르기, 되돌리기 기록 저장.
- 위험: InspectResult 가 Snapshot 채널(비신뢰)에 커진다 — 32 개 × 컴포넌트 몇 개면 수 KB, ENet 조각. 잃으면 다음 간격에 다시 온다.

## 대안

| 대안                                               | 기각 사유                                                                 |
|----------------------------------------------------|---------------------------------------------------------------------------|
| EditorVisible 컴포넌트를 전부 복제                 | 모든 개체 · 클라이언트의 대역폭이 는다 — 고치는 것은 몇 개뿐               |
| 컴포넌트마다 패널 코드                             | E4 위반, 컴포넌트를 더할 때마다 에디터를 고쳐야 한다                       |
| 보내는 즉시 되돌리기 칸 (낙관)                     | 거절 · 새 netId 를 모른다 — 6.2 원칙과 다르다                              |
| 서버가 Undo 를 안다 (역명령을 서버가 만든다)       | 서버가 클라이언트별 기록을 들고 있어야 한다 — 서버는 명령만 (단순성)       |

## 재검토 조건

여러 사람이 같은 개체를 자주 함께 고칠 때 (덮어쓰기 경고 · 잠금), 되살린 개체의 관계가 필요할 때 (saveId 보존 명령), 선택 상세가
대역폭을 눈에 띄게 먹을 때 (바뀐 것만 · Control 채널), 위젯 종류가 늘 때 (enum 이름표 · 태그 칩 — 리플렉션에 이름 표가 필요).
