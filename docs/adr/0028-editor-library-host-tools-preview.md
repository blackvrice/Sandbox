# ADR-0028. 에디터는 SandboxEditor 라이브러리로 두고 IEditorHost(= NetworkSession)로 명령을 내며, 툴은 숫자 키로, EditPreview 는 그린 위치에만 더한다

- 상태: **Accepted** · 날짜: 2026-10-08
- 관련: [10-EDITOR](../10-EDITOR.md) 1 · 2 · 3 · 6.1 · 9장, [01-ARCHITECTURE](../01-ARCHITECTURE.md) 2장,
  [ADR-0005](0005-dependency-boundaries.md), [ADR-0026](0026-network-session-local-server-inspect.md), [16-ROADMAP](../16-ROADMAP.md) Phase 12

## 맥락

Phase 12(멀티플레이 에디터, 1차 목표선)는 12.1 ~ 12.7 일곱 묶음이다. 한 번에 하기엔 크다. 10-EDITOR 는 SandboxEditor 가
Network 를 모르고(E2) ICommandSink 로 명령을 내보낸다고 정했지만, 복제 월드(ClientWorld)는 network/client 에 있고 선택 ·
스냅숏 · 명령 결과는 SandboxClient 의 NetworkSession 이 갖고 있다. 정할 것:

```text
1. Phase 12 를 어떻게 나눌까
2. 에디터가 월드를 보는 길 (Network 를 링크하지 않고)
3. 툴 단축키 — 10-EDITOR 9장 원안 Q W E B X 는 카메라 W A S D 와 겹친다
4. EditPreview — 원안: 클라 전용 editor.preview{offset} 컴포넌트. 언제 걷을까
5. 지형 브러시를 어떤 명령으로 (프레임당 몇 개, 어떤 칸)
6. Play / Edit 모드
```

## 결정

```text
1. 넷으로 나눈다. 12A = 12.1 (EditorContext · ICommandSink · 선택) + 12.3 (툴 다섯 + EditPreview) + 12.2 의 Palette · Terrain
   (편집 패널). 12B = 12.2 Inspector(리플렉션) · Hierarchy + 12.6 Undo/Redo. 12C = 12.5 역할 바꾸기 · Players + 12.4 Rule ·
   Behavior · ContentOverlay. 12D = 12.7 세이브 · 로드 · 리플레이 UI + 완료 기준(edit_session D3 · 두 클라이언트 동시 편집 ·
   권한 매트릭스).
2. editor/ = SandboxEditor (static, Core + Render + imgui, Network 링크 금지 — 경계 검사). 경계 = IEditorHost : ICommandSink
   (editor/EditorHost.hpp): submit(payload) → sequence, takeOutcomes(), content() · grid(), pickAt · pickBox (그린 스냅숏 기준),
   selection · setSelection, setPreview, snapshotsApplied · serverTick · renderTick. NetworkSession 이 구현하고
   IWorldSession::editorHost() 로 내준다 (가짜 세션은 null → Application 은 8B 선택만). 선택은 세션이 계속 갖는다 (Inspect ·
   외곽선 · 제목 줄이 쓴다) — 클라이언트 로컬인 것은 같다 (E3). EditorContext 는 따로 만들지 않는다: Editor + host 가 그 몫.
3. 툴 = 숫자 1 선택 · 2 이동 · 3 배치 · 4 지형 · 5 지우기 (editor.tool.*). Delete = 선택 지우기, Esc = 끌기 취소 · 선택 해제.
   왼쪽 = 툴, 오른쪽 = 지형 툴에서 바탕 머티리얼로 지우기, 가운데 = 카메라 (그대로).
4. EditPreview 는 컴포넌트가 아니라 host 의 표 (netId → offset) — 그릴 때 스냅숏 위치에 더한다 (고르기 · 외곽선 · 선택 상세도
   옮긴 자리). 걷기: 거절이면 바로, 받아들여지면 "그 뒤 처음 적용한 스냅숏의 서버 틱"까지 그린 틱이 오면 (일시정지면 바로),
   결과가 3 초 안에 오지 않으면 (끊김). 다른 연결로 바뀌면 기다리던 결과는 거절로 돌려준다.
5. 지형: 프레임마다 지난 커서 → 지금 커서를 반 칸 간격으로 훑어 브러시(원 · 사각형, 반지름 0 ~ 8) 칸을 모으고, 이번 붓질에서
   보낸 칸 · 경계 밖 · 이미 그 머티리얼인 칸(복제 지형 기준)을 뺀 PaintTerrain{cells} 하나 (상한 4096). 오른쪽 = 월드 바탕(fill).
6. Play / Edit 를 나누지 않는다: InWorld 에서 늘 툴이 있고 기본 툴(선택)은 8B 와 같다. WorldMode::Edit 는 [계획 12B 이후].
   배치 = CreateEntity{prefab, 위치} (끌면 2 칸마다, 격자 맞춤 = 칸 가운데), 이동 = MoveEntity{선택, delta} (격자 맞춤 = 정수 칸),
   지우기 = DeleteEntity (클릭 하나 · 박스 전부). 패널 "편집" (오른쪽 아래): 툴 · Prefab 목록(거르기) · 머티리얼 · 브러시.
```

## 근거

```text
- 넷으로 나누면 단위마다 끝까지(코드 · 시험 · 문서 · 커밋) 간다. 12A 만으로 "놓고 · 옮기고 · 칠하고 · 지운다" 가 서버를 거쳐
  돈다 — 나머지 묶음이 그 위에 선다 (Inspector 는 ChangeComponent, Undo 는 이 결과 흐름, 역할은 이 거절 흐름).
- host 하나: 에디터에 필요한 것은 "명령 내기 + 결과 + 그린 것 고르기 + 선택" 이고, 그 셋 다 NetworkSession 이 이미 갖고 있다.
  ClientWorld 를 에디터로 넘기려면 Network 의존이 생긴다. 가짜 host 로 툴 로직을 단위 시험한다 (10장 첫 줄).
- 숫자 키: WASD 는 Phase 8A 부터 카메라이고 사용자가 쓰고 있다. 단축키는 ActionMap 이라 사용자가 바꿀 수 있다.
- preview 를 컴포넌트로 두면 다음 스냅숏이 덮어쓴다 (복제 월드는 서버 값). 그린 위치에만 더하면 복제와 섞이지 않는다.
  "결과가 왔다" 만으로 걷으면 스냅숏 · 보간이 아직 옛 자리라 한 번 되돌아 보인다 — 그린 틱을 기다린다.
- 브러시를 프레임당 하나로 묶으면 끌기 한 번에 명령이 수십 개를 넘지 않고(속도 제한 120/s 안), 칸 목록이라 커서가 빨리
  움직여도 빈틈이 없다. 같은 칸 · 같은 머티리얼을 빼면 제자리 붓질이 명령을 만들지 않는다.
```

## 결과

- 얻는 것: SandboxClient 에서 바로 편집 (싱글플레이 · 원격 같은 길, 서버 권한 그대로). 시험: editor 스위트(가짜 host — 툴 다섯 ·
  preview · 거절 · 경계 · 붓질), client 스위트(로컬 서버까지 배치 · 이동 · 지형 · 지우기, 앱 숫자 키 · 클릭).
- 포기하는 것 / [계획]: Inspector · Hierarchy · Undo/Redo (12B), 역할 바꾸기 · 편집 권한이 없을 때 툴 숨기기 (12C — 지금은
  서버가 거절하고 패널에 사유), 배치 미리보기 그림(지금은 자리 사각형), 다중 선택 이동의 회전 · 정렬, 툴 커서 모양.
- 위험: 붓질 중 "이미 그 머티리얼" 판단은 복제 지형 기준이라 다른 사람이 같은 칸을 동시에 칠하면 한 번 빠질 수 있다 (다시 칠하면
  된다). 받아들여진 이동의 스냅숏이 예산 때문에 늦으면 3 초 뒤 preview 가 걷혀 잠깐 옛 자리가 보일 수 있다.

## 대안

| 대안                                                     | 기각 사유                                                          |
|----------------------------------------------------------|--------------------------------------------------------------------|
| Phase 12 를 한 번에                                      | 수십 파일 · 커밋 단위가 커서 검증 · 전달이 어렵다                   |
| 에디터가 ClientWorld 를 직접 (SandboxEditor → Network)  | 01 2장 경계 위반, 툴 시험에 서버가 필요하다                        |
| 원안 단축키 Q W E B X                                    | 카메라 W A S D 와 겹친다                                           |
| editor.preview 컴포넌트를 ClientWorld 에                 | 다음 스냅숏이 지운다 — 복제 월드는 서버 값만 둔다                  |
| 붓질마다 PaintTerrain{center, radius} 를 프레임마다      | 같은 칸을 반복해 보내고, 빠르게 끌면 칸 사이가 빈다                |

## 재검토 조건

Undo(12B)가 역명령을 만들려고 명령 전 값을 읽어야 할 때 (host 에 컴포넌트 읽기가 더 필요하면), 에디터 패널이 늘어 도킹 레이아웃이
필요할 때, 사용자가 Q W E B X 를 원할 때 (카메라 키를 바꾸는 프로필), 붓질 명령이 속도 제한에 걸릴 때.
