# ADR-0107. 게임패드 — Windows XInput 소스 (실행 중에 연 DLL), 상태는 InputSystem 이 프레임마다, 패드는 먼저 게임 UI 탐색에만

- 상태: **Accepted** · 날짜: 2026-11-09
- 관련: [07-PLATFORM](../07-PLATFORM.md) I5, [ADR-0103](0103-game-ui-input-controls-var-state-focus.md) 5 (UiKey — 게임패드 탐색의 바탕),
  [ADR-0105](0105-game-ui-dropdown-tabs-index-var-overlay-list.md) 5, [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 2.6 (NG-06),
  task-registry RT-06-13 (게임패드 트리거 — 게임 키)

## 맥락

I5 는 게임패드 인터페이스만 정했다 (`IGamepadSource` · `GamepadState` — "구현은 콘텐츠가 요구할 때"). NG-06 의 마지막 조각인 게임 UI 의 게임패드
탐색이 그 요구다. 게임 UI 키는 이미 플랫폼과 무관한 UiKey (방향 · 누르기 · 놓기 · 차례) 다. 게임 키 (ActionMap) 의 게임패드 바인딩 · Hold ·
Combo 는 RT-06-13 (v0.12) 이다. 이 기계 · CI 에 패드가 없다 — 시험은 가짜 소스와 순수 표로 한다.

## 결정

```text
1. 소스: Windows = XInput. DLL 은 실행 중에 연다 (xinput1_4 → xinput1_3 → xinput9_1_0, XInputGetState) — 없으면 패드 없음. 끊긴 칸은
   매 프레임 묻지 않는다 (XInputGetState 가 장치를 찾느라 느리다) — 120 번 물을 때마다 한 번. Linux evdev · macOS GameController 는
   [계획] — 그 OS 는 NullGamepadSource (createGamepadSource 가 고른다)
2. 표: XINPUT_GAMEPAD → GamepadState 는 순수 함수 (platform/windows/XInputMap — windows.h 없음, 모든 OS 에서 시험). 버튼 비트 그대로,
   스틱은 죽은 영역 (왼 7849 · 오른 8689) 밖을 0 ~ 1 로 다시 늘리고 (y 위 양수), 트리거는 30 위를 0 ~ 1. Guide 는 없음
3. InputSystem::setGamepads (프레임마다 앱이 부른다): pressed = 앞 프레임에 없던 버튼. 창 포커스가 없으면 버튼 · 축 0 (I3 — 연결은
   알린다). 키보드 캡처 (게임 UI 포커스 · ImGui 글자 칸) 는 downstream 의 패드도 비운다 — 포커스를 가진 쪽이 패드도 갖는다 (I1)
4. 패드 → 게임 UI (앱 routeGamepads — 도구 패널이 키보드를 원하지 않을 때): 십자 · 왼 스틱 (0.5 를 넘을 때 한 번, 0.3 아래로 돌아오면
   다시 — 반복은 [계획]) = 방향, South (A) = 누르기, East (B) = 놓기, LB · RB = 차례 앞 · 뒤. 포커스가 없으면 방향 · 누르기는 첫
   컨트롤에 포커스를 준다 (놓기는 아무것도 아니다). 패드는 아직 게임 키가 아니다 — RT-06-13 까지 패드는 게임 UI 만 움직인다
```

## 근거

```text
- XInput: Windows 10 · 11 에 들어 있고 Xbox 패드 · 대부분의 호환 패드를 덮는다. GameInput 은 재배포 런타임이 필요하다 — 뒤에 바꿀 수
  있게 소스 인터페이스 뒤에 둔다
- DLL 을 실행 중에: 링크 의존 (xinput.lib 의 버전) 을 만들지 않고, 오래된 · 줄인 Windows 에서도 시작이 실패하지 않는다
- 순수 표 분리: 패드가 없는 CI 에서도 비트 · 죽은 영역 · 부호를 시험한다 (Win32KeyMap 과 같은 이유)
- 포커스가 없으면 첫 컨트롤에: 패드만 든 사람이 키보드 없이 메뉴에 들어갈 수 있다. 놓기는 게임에 돌려주는 것이라 포커스를 만들지 않는다
```

## 결과

- 얻는 것: Xbox 계열 패드로 게임 UI 를 모두 조작 (메뉴 · dropdown · tabs · slider · scroll 의 포커스 따라 밀기), 입력 상태에 패드.
- 포기하는 것: Linux · macOS 패드, 진동, 게임 키의 패드 바인딩 (RT-06-13), 스틱 · 십자 반복, 오른 스틱 밀기, Guide 버튼.
- 위험: 실제 패드로 확인하지 못했다 — MANUAL-QA [ ] (연결 · 끊김 · 죽은 영역 · 두 패드).

## 대안

| 대안                                | 기각 사유                                                                   |
|-------------------------------------|-----------------------------------------------------------------------------|
| GameInput (Windows)                 | 재배포 런타임 · 새 API — XInput 으로 시작하고 소스 뒤에서 바꾼다             |
| SDL 의 게임패드                       | 새 서드파티 의존 (창 · 입력은 우리 플랫폼 층) — 패드만을 위해 들이지 않는다  |
| 패드를 바로 게임 키 (ActionMap) 에도  | 바인딩 형식 · 충돌 · 키 패널 · Hold 를 함께 정해야 한다 — RT-06-13           |
| 끊긴 칸도 매 프레임 묻기              | 패드가 없을 때 프레임마다 수 ms (알려진 XInput 동작)                        |

## 재검토 조건

RT-06-13 (게임 키의 패드 · 트리거), Linux · macOS 패드 요구, GameInput 으로 옮기기, 실제 패드 확인 (MANUAL-QA).
