# 10. 에디터

> **규범 문서.** 샌드박스 에디터의 구조와 편집 흐름을 정합니다 (Phase 12 에 구현 — 그 전에는 설계 문서였다). 상태: **Phase 12A 구현** — SandboxEditor(`editor/`), IEditorHost ·
> ICommandSink, 툴 다섯(Select · Move · Place · TerrainBrush · Erase), EditPreview, "편집" 패널(Palette · Terrain 의 첫 모양) —
> 2 · 3 · 6.1 · 9장의 "Phase 12A 구현" 단락. **Phase 12B 구현** — Inspector(리플렉션 필드 → ChangeComponent, 서버 전용 값은
> 선택 상세로) · Hierarchy("목록") · Undo/Redo — 2 · 4 · 6.2 · 9 · 10장의 "Phase 12B 구현" 단락. **Phase 12C 구현** — 역할
> 바꾸기 · "접속자"(Players) 패널 · 역할에 맞춘 툴 숨김, 콘텐츠 편집(EditContent → 월드 오버레이 · "콘텐츠" 창) — 2 · 7 · 8 · 9 · 10장의
> "Phase 12C 구현" 단락. **Phase 12D 구현** — 세이브 · 불러오기 · 리플레이 기록 · 재생("월드" 패널 · Ctrl+S), 월드가 바뀌면
> 에디터 기록 비우기, 완료 기준 시험(역할 × 명령 표 · 두 클라이언트 동시 편집 · edit_session D3) — 2 · 7 · 9 · 10장의 "Phase 12D
> 구현" 단락. Phase 12 완료. 7장 권한 표는 서버 CommandValidator 가 판단한다. 결정 근거:
> [ADR-0028](adr/0028-editor-library-host-tools-preview.md), [ADR-0029](adr/0029-inspector-server-values-undo-entries.md),
> [ADR-0030](adr/0030-roles-player-list-content-overlay.md), [ADR-0031](adr/0031-world-ops-generation-replay-playback.md).

> v0.2 (2026-10-21): "게임" 패널 · 게임 키 (9장), "행동" 패널 (PlayerAction), 트리거 폼 (8장), 테스트 플레이 (9장), 프로젝트 저장 ·
> 패키지. v0.3 (2026-10-26): "콘텐츠" 창의 스크립트 편집 · 오류 줄 · 적용 (8장 — 오버레이 Script).
---

## 1. 원칙

```text
E1. 에디터는 월드를 직접 바꾸지 않는다. 모든 편집은 SimCommand 로 서버에 보낸다 (싱글플레이 포함).
E2. 에디터는 Network 를 모른다. ICommandSink 하나로 명령을 내보낸다.
E3. 선택·카메라·툴 상태는 클라이언트 로컬이다. 복제·저장되지 않는다.
E4. Inspector 는 리플렉션으로 자동 생성한다. 컴포넌트마다 UI 코드를 쓰지 않는다.
```

## 2. 구조

```text
SandboxEditor
 ├─ EditorContext     ClientWorld&, ContentDatabase&, Selection, ActiveTool, Camera2D&, ICommandSink&, Role
 ├─ panels/
 │    Hierarchy       관련 엔티티 목록 (필터: 태그, Prefab, 이름)
 │    Inspector       선택 엔티티의 컴포넌트 (리플렉션), 컴포넌트 추가/제거
 │    Palette         Prefab 목록 (드래그 → 배치), 즐겨찾기
 │    Terrain         머티리얼 팔레트, 브러시 모양·크기
 │    Rules           Rule 목록·편집 (JSON 폼), 검증 결과
 │    Behaviors       BehaviorGraph 목록·상태/전이 편집 (폼 → 후속 노드 그래프)
 │    Simulation      재생/일시정지/스텝/속도, 현재 틱, 해시
 │    Players         접속자·역할 (Owner/Admin 이 역할 변경)
 │    Stats           14-PERFORMANCE 의 Graphics/Network/Simulation 오버레이
 │    Console         로그, CommandResult 거절 사유
 ├─ tools/            Select, Move, Place, TerrainBrush, Erase
 ├─ selection/        Selection = NetEntityId 집합
 ├─ commands/         툴 동작 → SimCommand 빌더, UndoStack
 └─ inspector/        ImGuiInspector Visitor (Hint → 위젯)
```

Phase 12A 구현 (ADR-0028) — 위 구조와 다른 점:

```text
editor/EditorHost.hpp   ICommandSink (submit → sequence) · IEditorHost : ICommandSink — 결과(takeOutcomes) · 콘텐츠 · 복제 지형 ·
                        그린 개체 고르기(pickAt · pickBox) · 선택 · EditPreview · 스냅숏 수 · 서버 틱 · 그린 틱.
                        SandboxClient 의 NetworkSession 이 구현한다 (IWorldSession::editorHost) — 에디터는 Network 를 모른다
editor/Editor.{hpp,cpp} 툴 다섯 · 설정(Prefab · 간격 · 격자 맞춤 · 머티리얼 · 브러시) · preview · 결과 처리 · 덧그림(박스 · 브러시 ·
                        배치 자리). EditorContext 는 따로 없다 — Editor + host 가 그 몫 (카메라는 Application 이 커서를 월드로)
editor/ui/EditorPanel   "편집" 패널 (오른쪽 아래, F1 로 숨김): 툴 · Prefab 목록(거르기) · 끌기 간격 · 머티리얼 · 브러시 반지름 ·
                        모양 · 격자 맞춤 · 보낸/수락/거절 수 · 최근 거절 사유
선택                    NetworkSession 이 계속 갖는다 (Inspect · 외곽선 · 제목 줄) — 에디터는 host.selection · setSelection
Play / Edit             나누지 않는다 — InWorld 에서 늘 툴, 기본 툴(선택) = 8B 와 같다. WorldMode::Edit [계획]
(12B) editor/Undo       UndoEntry(보낸 명령 · 역명령) · IdRemap · 역명령 만들기 — Editor 가 기록 · 되돌리기 · 다시 하기 (6.2)
(12B) ui/InspectorPanel "인스펙터"(4장) · "목록"(Hierarchy — 복제 월드의 개체, 관심 영역 안). IEditorHost 에 components ·
                        listEntities · positionOf · catalog 가 더해졌다
(12C) ui/ContentPanel   "콘텐츠" 창 = Rules · Behaviors (+ Prefab) 패널의 첫 모양 (8장) — "편집" 패널의 "콘텐츠 창" 단추로 연다.
(v0.2 M4-T3) ui/TriggerForm  "콘텐츠" 창의 트리거 폼 — 노드 · 연결 양식 (8장)
                        IEditorHost::permissions() = 지금 역할로 할 수 있는 것 (7장), content() 는 월드 오버레이까지 적용한 것
(12C) Players           apps/client/ui/PlayersPanel "접속자" — 접속자 · 역할 표, 바꿀 수 있는 사람에게 역할 콤보 (7장).
                        에디터가 아니라 클라이언트에 둔다 (역할 · 접속은 Network 의 것 — 에디터는 permissions 만 안다)
(12D) World             apps/client/ui/WorldPanel "월드" — 이름 · 저장 · 기록 시작/끝 · 새로고침, 서버 저장 폴더의 세이브 · 리플레이
                        목록 → 불러오기 · 재생, 기록 · 재생 상태 한 줄 (call · 비교한 해시 · 일치), 마지막 결과. 처음엔 접혀
                        있고 제목 줄에 상태 ("월드 — 보통"). 판단은 서버 (ADR-0031). IEditorHost::worldGeneration() — 서버 월드가 바뀌면(불러오기 · 재생) 에디터가 되돌리기 ·
                        다시 하기 · 기다리던 편집 · EditPreview 를 비운다 (옛 월드의 netId 다)
```

## 3. 툴

| 툴           | 입력                                            | 명령                                                       |
|--------------|-------------------------------------------------|------------------------------------------------------------|
| Select       | 클릭, 박스 드래그, Shift 추가, Ctrl 토글        | 없음 (로컬)                                                |
| Move         | 선택 드래그 (그리드 스냅 옵션)                  | 드래그 중 EditPreview, 놓을 때 `MoveEntity{netIds, delta}` |
| Place        | Palette에서 Prefab 선택 → 클릭/드래그 연속 배치 | `CreateEntity{prefab, position}` (드래그 시 간격마다)      |
| TerrainBrush | 좌클릭 칠하기, 우클릭 지우기(기본 머티리얼)     | `PaintTerrain` — 프레임당 최대 1개로 셀 묶음 전송          |
| Erase        | 클릭/박스                                       | `DeleteEntity{netIds}`                                     |

Phase 12A 구현:

```text
Select        클릭(4 px 안) = 맨 위 개체, 끌기 = 박스(가운데가 안에), Shift = 클릭은 넣기/빼기 · 박스는 더하기
Move          선택한 개체를 누르면 선택 전부, 선택에 없는 개체를 누르면 그것만 (Shift = 더해서) 끈다. 빈 곳 = Select 처럼.
              4 px 넘게 끌어야 MoveEntity{선택, delta, 상대} — 격자 맞춤이면 delta 를 정수 칸으로 (Ctrl 토글 [계획])
Place         CreateEntity{prefab, 커서} — 누른 채 끌면 간격(기본 2 칸)마다, 프레임당 하나. 격자 맞춤 = 칸 가운데. 월드 밖은 안 보낸다
TerrainBrush  왼쪽 = 고른 머티리얼, 오른쪽 = 월드 바탕(fill). 프레임마다 지난 커서 → 지금 커서를 반 칸 간격으로 훑어 새 칸만
              PaintTerrain{cells} 하나 (경계 안 · 이번 붓질에서 처음 · 복제 지형이 이미 그 머티리얼이 아닌 칸, 상한 4096).
              브러시 원 · 사각형, 반지름 0 ~ 8
Erase         클릭 = 그 개체, 끌기 = 박스 안 전부 → DeleteEntity. Delete 키 = 어느 툴에서든 선택 전부
CreateEntity 결과의 새 netId 는 Editor::lastCreated (선택하지는 않는다)
```

## 4. Inspector 위젯 매핑

| 필드 타입 / Hint                   | 위젯                                                       |
|------------------------------------|------------------------------------------------------------|
| float / int + `FieldMeta{min,max}` | 슬라이더 (없으면 드래그 숫자)                              |
| `Hint::Position`                   | Vec2 입력 + "뷰에서 선택" 버튼                             |
| `Hint::Angle`                      | 각도(도) 입력                                              |
| bool                               | 체크박스                                                   |
| Enum                               | 콤보                                                       |
| `Hint::EntityRef`                  | 엔티티 선택기 (뷰 클릭)                                    |
| PrefabId / TagSet                  | 콘텐츠 목록 콤보 / 태그 칩                                 |
| Opaque                             | 원시 JSON 텍스트 (읽기 전용 → 편집 시 AddComponent로 교체) |

편집 확정(포커스 이탈·Enter·슬라이더 놓기) 시 `ChangeComponent{netId, stableId, fieldPath, bytes}` 1개.
다중 선택 시 같은 컴포넌트를 가진 엔티티 전부에 같은 값을 보냅니다 (명령 N개를 한 번에 묶은 `batchId`).

Phase 12B 구현 (`editor/ui/InspectorPanel`, ADR-0029) — 위 표와 다른 점:

```text
값의 출처   IEditorHost::components(netId) = 복제 월드의 EditorVisible 컴포넌트 + 선택 상세(Inspect, 프로토콜 5)의 복제되지 않는
            EditorVisible 컴포넌트 JSON ("(서버)" 표시 — 선택한 개체만, 한 스냅숏 늦을 수 있다). 선택한 첫 개체의 값을 보인다
위젯        ComponentInfo::fields(reflect 순서 · Hint · FieldMeta) × JSON 종류: bool → 체크, 정수 · 실수 → 끌기 — 범위 폭 ≤ 1000
            이면 슬라이더 (life.energy 의 ±1e6 같은 넓은 범위는 끌기 + 자르기), Percent → 0 ~ 1, Angle → 도, [x, y] → 두 칸
            (Position 의 "뷰에서 선택" [계획]), 문자열 → 글자 칸, PrefabRef → Prefab 콤보, EntityRef · 배열(TagSet 비트) · 객체 →
            읽기만, Enum 은 정수 칸 (이름 콤보 [계획])
보내기      편집이 끝날 때 (끌기 놓기 · Enter · 포커스 이탈, 체크 · 콤보는 바로) Editor::setField — 선택한 개체 중 그 컴포넌트가 있는
            것마다 ChangeComponent{target, stableId, {필드: 값}}, 값이 같으면 보내지 않는다. batchId 대신 되돌리기 한 칸으로 묶는다
더하기/빼기 콤보(카탈로그의 EditorVisible 중 없는 것) → AddComponent{기본값}, "컴포넌트 빼기" → RemoveComponent
```

## 5. 편집 흐름 (멀티플레이)

```text
Client A: Inspector 에서 Rabbit.maxSpeed = 3.0
  → ChangeComponent{…} → ICommandSink → ClientSession → Control 채널
Server: CommandValidator (권한·대상·필드·범위) → executeTick 스탬프 → Stage 2 적용 → changed[] 갱신
  → 다음 스냅샷에서 관련 클라이언트 전원(A 포함)에게 복제
  → A 에게 CommandResult{seq, Accepted}
Client A: Undo 항목 확정 (6.2)
```

## 6. 즉시 피드백과 Undo

### 6.1 EditPreview

```text
Move 드래그 중: 대상 엔티티에 클라 전용 editor.preview{offset} 컴포넌트 → Extraction 이 offset 을 더해 그린다.
서버 확정 스냅샷이 오면 preview 제거. 거절되면 preview 제거 + Console/토스트에 사유.
시뮬레이션 예측이 아니므로 롤백·재시뮬레이션이 없다.
```

Phase 12A 구현: preview 는 컴포넌트가 아니라 IEditorHost::setPreview 의 표(netId → offset) — NetworkSession 이 그릴 때 스냅숏
위치에 더한다 (고르기 · 외곽선 · 선택 상세도 옮긴 자리). 끄는 중 + 결과를 기다리는 이동의 합. 걷기: 거절이면 바로 (사유는 패널),
받아들여지면 그 뒤 처음 적용한 스냅숏의 서버 틱까지 그린 틱이 오면 (일시정지면 바로), 3 초 안에 결과가 없으면 (끊김). 연결이
바뀌면(다시 접속) 기다리던 결과는 거절로 돌아온다.

### 6.2 Undo

```text
- UndoStack 은 클라이언트 로컬. 항목 = { 보낸 명령들(batchId), 역명령들 }.
- 역명령은 "서버가 확정한 시점의 이전 값"으로 만든다:
    ChangeComponent → 명령 전 ClientWorld 값 (확정 시 서버 값과 비교, 다르면 경고)
    CreateEntity   → DeleteEntity{새 netId} (CommandResult 에 생성된 netId 포함)
    DeleteEntity   → CreateEntity{prefab, 삭제 직전 전체 컴포넌트} (삭제 전에 Inspector 데이터 보관)
    PaintTerrain   → 이전 머티리얼로 PaintTerrain
- 역명령도 일반 명령으로 보낸다. 서버는 Undo 를 특별 취급하지 않는다.
- 충돌: 그 사이 다른 사용자가 같은 필드를 바꿨으면 Undo 가 덮는다 (last-writer-wins) — 경고만. 잠금은 후속.
- 거절된 명령은 스택에 남기지 않는다.
```

Phase 12B 구현 (`editor/Undo`, Editor 의 기록, ADR-0029):

```text
한 칸        사용자 동작 하나 — 배치(끌어서 여러 개) · 붓질 한 번 · 이동 한 번 · 지우기 · Inspector 값 하나 · 컴포넌트 더하기/빼기.
             결과가 다 와야 칸이 생기고 받아들여진 명령만 담는다 (다 거절이면 없음). 100 칸, 새 편집은 다시 하기를 비운다
역명령       보내기 전의 클라이언트 값으로 — Move → -delta, ChangeComponent → 이전 값(복제 값 또는 선택 상세의 서버 값),
             Add ↔ Remove(이전 값), PaintTerrain → 칸의 이전 머티리얼마다, CreateEntity → DeleteEntity{결과의 새 netId},
             DeleteEntity → 개체마다 CreateEntity{core.prefab, 알던 컴포넌트 값 (core.prefab · net.identity · EntityRef 필드 빼고)}
             — 고르지 않은 개체는 서버 전용 값을 몰라 Prefab 기본값으로 돌아온다
netId 잇기   다시 만든 개체는 새 netId — IdRemap(옛 → 새, 사슬)으로 기록 속 명령의 대상을 보낼 때 바꾼다
언제         결과를 기다리는 편집이 없을 때만 (Editor::idle — 아니면 "기다리는 중"). 되돌리기 · 다시 하기의 명령이 하나라도
             거절되면 그 칸을 기록에서 뺀다 (사유는 편집 패널)
확정 시 비교 경고 · 잠금 [계획]
```

## 7. 권한

| 역할     | 관찰 | 플레이어 행동 | 엔티티·컴포넌트 편집 | 지형 편집 | Rule·Behavior·Prefab 편집 | 시뮬레이션 제어 | 역할 부여   |
|----------|------|---------------|----------------------|-----------|---------------------------|-----------------|-------------|
| Owner    | ✓   | ✓            | ✓                   | ✓        | ✓                        | ✓              | 전부        |
| Admin    | ✓   | ✓            | ✓                   | ✓        | ✓                        | ✓              | Editor 이하 |
| Editor   | ✓   | ✓            | ✓                   | ✓        | ✓                        | –               | –           |
| Player   | ✓   | ✓            | –                    | –         | –                         | –               | –           |
| Observer | ✓   | –             | –                    | –         | –                         | –               | –           |

```text
- 판단은 서버 CommandValidator 한 곳. 클라 UI 의 버튼 숨김은 편의일 뿐.
- 월드를 처음 만든(또는 서버를 띄운) 사용자가 Owner. 싱글플레이에서는 항상 Owner.
- 기본 역할은 서버 설정 --default-role (기본 Editor).
```

Phase 12C 구현 (ADR-0030):

```text
Owner        싱글플레이 = 언제나. 전용 서버 = SandboxServer --owner-key <k> 를 띄우고 SandboxClient --owner-key <k> 로 들어온 사람
             (틀린 열쇠 · 열쇠 없는 서버에 열쇠 = 접속 거절). 열쇠는 평문 — LAN · 신뢰하는 환경 전용 (Phase 9 와 같다)
역할 바꾸기  "접속자" 패널의 콤보 → SetRole → 서버 Net 절반이 위 표의 "역할 부여" 열대로 (net::roleCanAssign — 패널도 같은 함수로
             콤보를 채운다): Owner = 누구든 무엇이든, Admin = Admin 아래인 사람을 Editor 이하로, 자기 자신은 못 바꾼다. 결과는 패널
             아래 한 줄. 바뀐 역할은 다시 접속해도 그대로 (세션 토큰 표)
알림         들어옴 · 나감 · 역할이 바뀔 때마다 모두에게 접속자 표 (PlayerList) — 자기 줄이 지금 역할
에디터       IEditorHost::permissions() = {개체 · 지형 · 콘텐츠 · 실행 제어} (위 표의 열). 쓸 수 없는 툴은 "편집" 패널에서 숨기고
             단축키도 막는다 (지금 툴이 막히면 선택 툴로). Delete · 인스펙터(보기만) · 되돌리기 · 콘텐츠 적용도 막는다. "시뮬레이션"
             패널의 일시정지 · 한 틱 · 속도는 Admin 부터 (단추를 끈다 — 키는 보내고 서버가 거절한다)
```

Phase 12D 구현 (ADR-0031):

```text
월드 요청    목록 = 모든 역할. 저장 · 불러오기 · 리플레이 기록 · 재생 = 시뮬레이션 제어 열 (Admin 부터). "월드" 패널은 단추를 끄고,
             서버 Net 절반이 거절한다 ("… (admin 부터)")
시험         역할 × 명령 12 종 + 월드 요청 5 종을 서버 끝까지 (test_multi_client — 위 표를 시험 안에 따로 적어 서버 코드와 대조)
```

v0.2 M2-T3 (ADR-0046):

```text
게임 진행    시작 · 중단 (명령 GameControl) · 재시작 (WorldOp Restart) = 시뮬레이션 제어 열 (Admin 부터). 시작은 역할 Player 이상
             접속자가 GameMode.players.min 이상일 때만 (서버가 센다). 플레이어 행동 열은 M3-T1 PlayerAction 부터 채운다
시험         역할 표 시험이 명령 14 종 (+ GameControl start · abort) + 월드 요청 6 종 (+ Restart) 으로
```

v0.2 M3-T1 (ADR-0047):

```text
플레이어 행동 위 표의 "플레이어 행동" 열 = 명령 PlayerAction (CommandClass::PlayerAction — Player 부터). 콘텐츠 actions.json 의
             행동을 월드의 한 점에 쓴다. 게임 세션이 있으면 Running 에서만 (Lobby 의 편집 단계에서는 거절)
시험         역할 표 시험이 명령 15 종 (+ PlayerAction — Player 부터)
```

## 8. 콘텐츠 편집 (Rule / Behavior / Prefab / Trigger / Script)

```text
- 편집 결과는 "월드 오버레이"로 저장된다 (콘텐츠 팩 원본 파일은 바꾸지 않는다).
- 서버는 ChangeRule/ChangeBehavior/CreatePrefab 을 스키마 검증 후 적용 → ContentDatabase 오버레이 갱신
  → Bulk ContentOverlay 로 전 클라이언트에 전송.
- 오버레이 변경은 WorldHash 에 포함된다 (04 5.1).
- "오버레이를 콘텐츠 팩으로 내보내기"는 후속 기능.
```

Phase 12C 구현 (ADR-0030):

```text
명령         EditContent{kind: prefab | rule | behavior, id, value: 팩 파일과 같은 JSON 객체 | null} 하나 (위의 셋을 합친다) —
             같은 id 의 팩 항목을 바꾸거나 새로 더한다 (id 의 팩 접두사는 월드가 읽은 팩). null = 오버레이에서 뺀다 (팩 값으로)
적용         서버 월드가 팩 원본(로더가 남긴 파일 텍스트) + 오버레이로 콘텐츠를 다시 만든다 — 팩과 같은 검증(V1~V7, 문제는
             "overlay/<종류>/<id>.json"), 실패면 거절 · 그대로. 태그 · 머티리얼은 바꿀 수 없다 (모르는 태그 = V7).
             BehaviorGraph 를 바꾸면 그 그래프를 쓰는 개체의 상태를 상태 id 로 옮긴다 (없어진 상태면 처음부터). 바꾼 Rule 은 원래
             정의 순서 자리, 더한 Rule 은 맨 뒤. 이미 만든 개체는 Prefab 이 바뀌어도 그대로 (새로 만드는 것부터)
전송         ContentOverlay (Bulk) — 들어오면 전체, 바뀌면 바뀐 항목만. 클라이언트도 자기 팩에 얹어 content() 로 (팔레트에 새 Prefab)
해시 · 세이브 오버레이 지문은 WorldHash 끝에 (비어 있으면 넣지 않는다). contentHash(접속 검사)는 팩 그대로.
             세이브 overlay/<prefabs|rules|behaviors>/<id>.json, 리플레이는 명령 그대로 ({"op": "content"})
"콘텐츠" 창  종류(규칙 · 행동 · Prefab) → 목록 (● = 이 월드가 바꾼 것) → JSON 편집 · "검사"(이 컴퓨터의 로더 — 서버와 같은 검사) ·
             "적용"(되돌리기 한 칸, 역명령 = 이전 오버레이 값 | null) · "고친 것 버리기" · "팩 값으로" · "새 항목"(틀에서).
             행동은 상태 · 전이, 규칙은 action · 우선순위 · 효과 수를 요약해 보인다. 역할이 콘텐츠를 못 바꾸면 보기만
트리거       (v0.2 M4-T3, ADR-0052) 종류 "트리거" — EditContent{trigger} (프로토콜 11, 세이브 overlay/triggers/). 바꾼 트리거는
             팩의 실행 순서 자리, 더한 것은 맨 뒤, 게임 중에도 다음 단계부터. "폼" 탭 (editor/ui/TriggerForm): 노드마다 머리
             ("[이벤트] 타이머 · id") · id · 표(TriggerSchema)의 키 위젯 (선택 키는 "사용" 상자), 노드 더하기 (종류 콤보 — 필수 키는
             기본값) · 지우기 (연결도), 연결 from → to · else (조건에서만), 이름 · 틱마다. "JSON" 탭은 같은 글. 요약 = 실행 표현 줄
스크립트     (v0.3 M5-T2, ADR-0066) 종류 "스크립트" — 팩의 scripts/*.ts (id = "<팩>.<경로를 . 로>" 로 쓸 수 있는 파일) + 오버레이로
             더한 파일. 소스를 그대로 고친다. "검사" = 이 컴퓨터에서 팩을 다시 컴파일 (서버와 같은 로더) — 오류마다 그 줄
             ("<팩 폴더>/scripts/a.ts:줄:열: 오류 S426 — …" 다음에 "  12 | <소스 줄>"). "적용" = EditContent{script, {"id", "source"}}
             (프로토콜 13, 세이브 overlay/scripts/) — 서버가 팩을 다시 컴파일해 그 틱부터 새 시스템. 컴포넌트 정의는 바꿀 수 없다
             (V11 — 팩을 고치고 월드를 다시 연다). 요약 = 이 파일의 시스템 (콜백 · 주기), "새 항목" = 시스템 하나의 틀
[계획]       노드 그래프 캔버스 (pos 배치 · 끌어 잇기), 행동 · 규칙의 폼, 팩 항목 끄기, 오버레이를 팩으로 내보내기, 태그 · 머티리얼 편집,
             스크립트 편집기의 구문 색 · 줄 번호 (v0.5 검토)
```

## 9. 단축키 (기본값, ActionMap으로 변경 가능)

```text
Q Select   W Move   E Place   B Terrain   X Erase
Space 재생/일시정지   . 한 틱 진행   Ctrl+Z / Ctrl+Y Undo/Redo   Delete 삭제
Ctrl+S 저장   F 선택으로 카메라 이동   G 그리드 토글   F3 Stats 오버레이
```

Phase 12A 구현: 툴은 숫자 **1 선택 · 2 이동 · 3 배치 · 4 지형 · 5 지우기** (`editor.tool.*` — W A S D 가 카메라라서, ADR-0028).
Delete · Esc(끌기 취소 → 선택 해제) · Space · . · G 는 위와 같다.
Phase 12B 구현: Ctrl+Z 되돌리기 · Ctrl+Y(Ctrl+Shift+Z) 다시 하기, F 선택한 개체들의 가운데로 카메라 (`editor.focus`, 줌 그대로).
"목록"(Hierarchy) 패널: 클릭 = 선택, Ctrl+클릭 = 넣기/빼기, 두 번 클릭 = 카메라 맞추기.
Phase 12C 구현: "콘텐츠" 창은 "편집" 패널의 "콘텐츠 창" 단추로 열고 닫는다 (단축키 `[계획]`). 역할이 막는 툴의 숫자 키는 무시하고
편집 패널에 사유를 보인다.
Phase 12D 구현: Ctrl+S (`editor.save`) = "월드" 패널의 이름(기본 quicksave)으로 서버에 저장 — 결과는 패널 아래 한 줄. 리플레이를
재생하는 동안 Space · . · = / - 는 재생을 멈추고 · 한 call 씩 · 배율을 바꾼다 (서버가 재생 상태로 처리, 편집 명령은 거절).
v0.2 M5-T1 (ADR-0053): "월드" 패널의 "테스트 플레이" — 서버가 편집 월드를 메모리에 그대로 맡기고 그 복사본으로 바꾼다 (게임
프로젝트면 세션 Lobby → 시작, 일시정지면 재개). 상태 줄 "테스트 플레이 — 멈추면 편집 월드로". "테스트 플레이 멈추기 (되돌리기)" 면
복사본을 버리고 편집 월드가 그대로 돌아온다 (틱 · 개체 · 오버레이 · 일시정지). 그 동안 저장은 되고 (플레이 결과를 남기려면),
불러오기 · 재생 · 재시작 · 기록 시작 · 자동 재시작은 하지 않는다. 시작 · 멈춤은 Admin 부터.
v0.2 M1-T5: 게임 프로젝트(`--world <프로젝트>`)면 세이브 폴더가 <프로젝트>/worlds 이고 Ctrl+S 의 처음 이름이 시작 월드(main)
— 고치고 Ctrl+S 하면 게임의 시작 월드가 바뀐다 (ADR-0044). 다른 이름은 "월드" 패널에서.
v0.2 M2-T3: 게임 모드가 있는 월드면 "게임" 패널 — 모드 이름 · 참가 인원 · 상태 한 줄 (Lobby 대기 · 카운트다운 남은 초 · 경과
m:ss · 승리/패배/무승부/중단과 팀), 시작 · 중단 · 재시작 단추 (Admin 아래면 끈다), 거절 사유 · 재시작 결과 한 줄. 제목 줄에도
상태 한 줄. 프로젝트를 열면 Lobby 라 시뮬레이션은 멈춰 있다 — 편집하고 "시작" (ADR-0046). 단축키는 `[계획]`.
v0.2 M3-T1: 콘텐츠에 PlayerAction 이 있으면 "행동" 패널 — 목록에서 하나를 고르면 쥐고 (제목 줄에 그 id), 쥔 동안 월드 왼쪽
클릭 = 그 자리에 행동 (에디터 툴 · 선택은 클릭을 받지 않는다), 다시 고르거나 Esc = 놓기. 결과 · 거절 한 줄. Player 아래 역할은
고를 수 없다 (ADR-0047). 행동 단축키는 `[계획]`.
v0.2 M3-T3 (ADR-0049): 게임 키 — 팩의 presentation/input.json 이 준 `play.action.<id>` 를 누르면 커서 자리에 그 행동
(쥐고 클릭과 같다, Player 아래 역할이면 보내지 않는다). "행동" 패널 목록에 키를 보인다 ("[Q] 먹이 주기"). 카메라 처음 자리 —
개체에 `game.camera_start {height}` 를 붙여 놓으면 월드를 받을 때마다 (처음 · 불러오기 · 재시작) 그 자리 · 높이(화면 세로에 보이는
월드 단위)로 맞추고 Home 도 그리로 (없으면 월드 전체).
v0.2 M3-T2: "게임" 패널에 점수 줄 — 팀 모드면 "빨강 3 · 파랑 5" (공용 칸이 0 이 아니면 "· 공용 n"), 아니면 "점수 n" (복제된
game.score, ADR-0048). 스폰 지점은 개체에 `game.spawn_point` 를 붙여 놓는다 (인스펙터 — prefab · count · radius). 지점 표시
아이콘은 `[계획]`.

## 10. 필수 테스트

```text
- 헤드리스 Editor + 가짜 ICommandSink: 툴 조작 → 기대 명령 (Place/Move/Erase/TerrainBrush/Inspector)
  ✅ 12A — Inspector 빼고 (editor 스위트 test_editor, 로컬 서버까지는 client 스위트 test_editor_session)
  ✅ 12B — Inspector 명령 (setField · 더하기 · 빼기 — test_undo, 로컬 서버의 서버 전용 값 — test_editor_session)
- Undo 역명령 생성 (각 명령 종류), 거절 시 스택 미반영
  ✅ 12B (test_undo — 이동 · 배치 · 지우기 · 지형 · 값 · 컴포넌트, netId 잇기, 거절 · 기다림, 로컬 서버에서 지운 개체 되살리기)
- 2클라이언트(Loopback) 동시 편집: 같은 필드 연속 변경, 삭제된 엔티티 편집 거절
  ✅ 12D (test_multi_client — 같은 개체를 상대 · 절대 이동, 같은 필드 회전, 겹치는 칸 칠하기, 지우는 개체 옮기기 → 거절, 모든
    명령에 결과 하나, 두 복제본의 개체 · 위치 · 회전 · 지형이 서버와 같다)
- 권한 표 전체: 역할 × 명령 종류 매트릭스
  ✅ 12C — 역할 부여 열 전체(test_roles_overlay — Owner · Admin · Editor, 자기 자신 · 없는 번호, 다시 접속), 역할이 바뀐 뒤 편집 ·
    콘텐츠 명령 거절, 패널과 서버가 같은 표 (test_roles_content)
  ✅ 12D — 5 역할 × 명령 12 종 + 월드 요청 5 종 (test_multi_client — PermissionDenied 여부를 시험에 따로 적은 표와 대조)
- 콘텐츠 편집 (12C): 오버레이 검증 · 자리 · 지문 (content), EditContent · 상태 옮기기 · 결정론 (core), 세이브 왕복 (persist),
  전달 · 늦게 들어온 클라이언트 (network), 권한 · 되돌리기 · 검사 (editor), 로컬 서버에서 편집 → 되돌리기 (client)
- 세이브 · 리플레이 (12D): 월드가 바뀌면 기록 비우기 (editor test_editor_world_change), 로컬 서버에서 저장 · 목록 · 불러오기 →
  오버레이 복원 · 기록 비우기, 기록 → 재생 · 재생 멈춤 · 한 call, "월드" 패널 그리기 · 앱 Ctrl+S (client test_world_panel), 서버 쪽은
  08 12장 (test_world_ops), 편집 명령 전 종류를 담은 시나리오 edit_session 의 D1 · D2 · D3 · D5 (13-TESTING)
```
