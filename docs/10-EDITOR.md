# 10. 에디터

> **설계 문서.** 샌드박스 에디터의 구조와 편집 흐름을 정합니다. 상태: 전부 `[계획]` — Phase 12 (일부 패널은 Phase 8부터).
> 예외: 7장 권한 표는 Phase 9 의 서버 CommandValidator 가 이미 판단한다 (기본 역할 = `--default-role`, 역할 바꾸기는 Phase 12).

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

## 3. 툴

| 툴           | 입력                                            | 명령                                                       |
|--------------|-------------------------------------------------|------------------------------------------------------------|
| Select       | 클릭, 박스 드래그, Shift 추가, Ctrl 토글        | 없음 (로컬)                                                |
| Move         | 선택 드래그 (그리드 스냅 옵션)                  | 드래그 중 EditPreview, 놓을 때 `MoveEntity{netIds, delta}` |
| Place        | Palette에서 Prefab 선택 → 클릭/드래그 연속 배치 | `CreateEntity{prefab, position}` (드래그 시 간격마다)      |
| TerrainBrush | 좌클릭 칠하기, 우클릭 지우기(기본 머티리얼)     | `PaintTerrain` — 프레임당 최대 1개로 셀 묶음 전송          |
| Erase        | 클릭/박스                                       | `DeleteEntity{netIds}`                                     |

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

## 8. 콘텐츠 편집 (Rule / Behavior / Prefab)

```text
- 편집 결과는 "월드 오버레이"로 저장된다 (콘텐츠 팩 원본 파일은 바꾸지 않는다).
- 서버는 ChangeRule/ChangeBehavior/CreatePrefab 을 스키마 검증 후 적용 → ContentDatabase 오버레이 갱신
  → Bulk ContentOverlay 로 전 클라이언트에 전송.
- 오버레이 변경은 WorldHash 에 포함된다 (04 5.1).
- "오버레이를 콘텐츠 팩으로 내보내기"는 후속 기능.
```

## 9. 단축키 (기본값, ActionMap으로 변경 가능)

```text
Q Select   W Move   E Place   B Terrain   X Erase
Space 재생/일시정지   . 한 틱 진행   Ctrl+Z / Ctrl+Y Undo/Redo   Delete 삭제
Ctrl+S 저장   F 선택으로 카메라 이동   G 그리드 토글   F3 Stats 오버레이
```

## 10. 필수 테스트

```text
- 헤드리스 Editor + 가짜 ICommandSink: 툴 조작 → 기대 명령 (Place/Move/Erase/TerrainBrush/Inspector)
- Undo 역명령 생성 (각 명령 종류), 거절 시 스택 미반영
- 2클라이언트(Loopback) 동시 편집: 같은 필드 연속 변경, 삭제된 엔티티 편집 거절
- 권한 표 전체: 역할 × 명령 종류 매트릭스
```
