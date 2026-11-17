# ADR-0143. 입력 Context — 팩 input.json 의 contexts (game · menu · always), 메뉴 · 모달이 보이면 game 키를 먹고 눌린 키는 뗌 한 번

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0049](0049-game-keys-pack-presentation-camera-start-demo-mode.md) (게임 키), [ADR-0142](0142-client-onaction-action-event.md) (onAction — 같은 태스크의
  조각 1), [07-PLATFORM](../07-PLATFORM.md) 5장, [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) (presentation/input.json),
  [22-RUNTIME-V2](../22-RUNTIME-V2.md) 6장, task-registry RT-06-8 (조각 2)

## 맥락

게임 UI 의 메뉴 (일시정지 · 상점) 를 열어도 게임 키 (점프 · 공격) 가 그대로 켜진다 — 메뉴 버튼은 마우스 · UI 키 (UiInputRouter — v0.7 M3-T3)
로만 먹고, 팩 키 바인딩은 막지 않는다. 키보드를 잡은 UI (입력 칸 · 도구 창) 는 이미 모든 키를 먹는다 (InputSystem 캡처 — I1). 22 6장의
"메뉴 > UI > 게임" 스택에서 남은 것은 메뉴가 게임 키를 먹는 규칙과, 메뉴에서만 쓰는 키 (확인 · 닫기) 를 콘텐츠가 고르는 길이다.

## 결정

```text
1. 선언      팩 presentation/input.json 에 "contexts": { "play.<이름>": "game" | "menu" | "always" } (선택 — 없으면 "game"). 그 파일의
             actions 에 있는 이름만 (아니면 경고 · 무시), 모르는 값은 경고 · "game". 뒤 팩이 이긴다. 사용자 키 다시 묶기 (--input · "키" 패널)
             는 바인딩만 바꾸고 Context 는 팩의 것 — 형식 version 1 그대로 (contexts 는 팩 파일만의 선택 키 — PlayKeys 가 떼어 읽는다)
2. 스택      위에서부터: 키보드를 잡은 UI (입력 칸 포커스 · ImGui 도구 — 모든 키를 먹는다, 지금 규칙) > 메뉴 (게임 UI 의 menu · modal 층
             문서가 하나라도 보인다 — GameUiLayer::menuOpen) > 게임. 메뉴가 위면 "menu" · "always" 키만, 아니면 "game" · "always" 키만 켜진다
3. 소비      먹힌 키는 down · pressed 가 없다. 눌린 채 먹히면 (메뉴가 열림) 그 프레임에 뗌 한 번 — 스크립트 · onAction 이 눌린 상태에 갇히지
             않는다. 먹힌 동안 뗀 키는 다시 뗌을 보내지 않는다. 메뉴가 닫힐 때 아직 눌린 game 키는 down 만 (pressed 없음 — 새로 누른 것이
             아니다)
4. 닿는 곳   PlayGate (apps/client/PlayKeys) 가 거른 값을 client 스크립트 입력 (keyDown · keyPressed · keyReleased · onAction) 과
             play.action.<id> 키의 PlayerAction 이 같이 쓴다. 엔진 키 (카메라 · 에디터) 는 따로 (ActionMap 이 다르다)
5. 시뮬레이션 밖 — 명령으로만 들어간다 (I2). 해시 · 세이브 · 프로토콜 · Host 지문 무관
```

## 결과

- 메뉴를 연 동안 게임 키가 서버로 행동 · 신호를 보내지 않는다. 메뉴용 키 (Enter 로 확인 · Escape 로 닫기) 를 팩이 고른다.
- 지금 팩 (contexts 없음) 은 모든 키가 "game" — 메뉴 · 모달 문서가 없는 팩은 동작이 같다. 메뉴 문서가 있는 팩은 메뉴가 보이는 동안 키가 막힌다
  (의도한 바뀜 — 열어 둔 채 쓰던 키는 "always" 로).
- 게임패드 버튼 · Hold · Combo 는 같은 Context 를 따르게 된다 [계획 — RT-06-13].

## 고른 것 · 버린 것

- 스크립트가 Context 를 넣고 빼기 (ctx.pushInputContext): 상태가 스크립트에 흩어지고 세션 끝 · 트랩에서 새는 것을 엔진이 정리해야 한다 — 지금은
  문서 층 (콘텐츠) 이 정한다. 필요하면 [계획].
- 키마다 "먹는다 / 지난다" 를 문서마다 적기: UI 문서 형식이 커진다 — 층 (menu · modal) 하나로 충분하다.
- 메뉴가 열릴 때 뗌을 보내지 않기: onAction 의 눌림만 받은 스크립트가 "누르고 있음" 에 갇힌다 — 버림.
