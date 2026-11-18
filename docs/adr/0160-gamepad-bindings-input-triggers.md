# ADR-0160. 게임패드 바인딩 · 입력 트리거 (hold · doubleTap · combo) = 팩 input.json 의 가상 게임 키

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0107](0107-gamepad-xinput-source-ui-navigation.md) (게임패드 소스), [ADR-0142](0142-client-onaction-action-event.md) (onAction),
  [ADR-0143](0143-input-contexts-menu-consumes-game-keys.md) (입력 Context), [07-PLATFORM](../07-PLATFORM.md) 5장,
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) (presentation/input.json), task-registry RT-06-13

## 맥락

RT-06-13 의 완료 기준은 "트리거 판정". 게임패드 상태 (InputState::gamepads — XInput) 는 v0.7 부터 있지만 게임 키 (play.*) 에 묶을 수
없다 — 패드는 UI 이동에만 쓰인다. 길게 누르기 · 두 번 누르기 · 연속 입력 (콤보) 은 스크립트가 프레임 시간을 세어 만들어야 한다.

## 결정

```text
1. 패드 바인딩  ActionMap 바인딩 종류 Gamepad — "Pad.A" · "Pad.B" · "Pad.X" · "Pad.Y" · "Pad.LB" · "Pad.RB" · "Pad.Back" · "Pad.Start" ·
                "Pad.LS" · "Pad.RS" · "Pad.Up" · "Pad.Down" · "Pad.Left" · "Pad.Right" (Guide 는 OS 몫). 연결된 어느 패드든, 수정자
                없음 (붙이면 오류). 키 다시 묶기 (captureBinding) 도 패드 버튼을 받는다. 스틱 · 트리거 축은 [계획]
2. 트리거       팩 input.json "triggers": { "play.<이름>": { "hold" | "doubleTap": "play.<원본>" | "combo": [원본 2 ~ 8 개], "ms": 1 ~ 5000 } }
                — 그 이름이 바인딩 없는 가상 게임 키. hold = 원본을 ms 동안 누르면 켜짐 (뗄 때까지), doubleTap = 두 번째 누름이 ms 안이면
                한 프레임, combo = 차례대로 · 각 누름이 앞 것으로부터 ms 안이면 마지막 누름의 프레임에 한 번 (차례가 틀린 원본을 누르면
                처음부터, 첫 원본은 언제든 다시 시작)
3. 판정 자리    PlayGate — 원본은 입력 Context 로 거른 값 (메뉴가 먹은 키로는 트리거가 켜지지 않는다). 시간은 앱의 월드 입력 프레임
                시계 (초). 가상 키는 keyDown · keyPressed · keyReleased · onAction · "play.action.*" 와 같은 길로 나간다
4. 표시 쪽      트리거 · 패드는 클라이언트 입력 — 시뮬레이션 · 해시와 무관 (신호 · 행동 명령이 될 때만 서버로)
```

## 결과

- "길게 눌러 모으기" · "두 번 눌러 대시" · "↓ → 공격" 을 팩 데이터만으로, 키보드 · 패드 같이.
- 원본이 다른 트리거인 트리거 · 축 (스틱) 바인딩 · 사용자 키 설정 화면의 트리거 편집은 [계획].

## 고른 것 · 버린 것

- 스크립트 도우미 (stdlib 의 타이머로 판정): 게임마다 같은 코드를 쓰고, 메뉴 Context 와 프레임 차례를 따로 맞춰야 한다.
- 트리거를 ActionMap (엔진 키) 에: 엔진 키 (카메라 · 편집기) 에는 쓸 곳이 없고 ActionMap 은 시간을 모른다 — 게임 키 (PlayGate) 에만.
