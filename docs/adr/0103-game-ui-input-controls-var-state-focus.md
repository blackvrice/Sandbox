# ADR-0103. 게임 UI 입력 컨트롤 — 값은 var 바인딩 (클라이언트), 바뀜은 신호, 키보드 포커스는 Tab · menu · modal 에서만

- 상태: **Accepted — 일부 Superseded** (4 — 글자 칸은 hud 클릭도 포커스, [ADR-0104](0104-game-ui-text-input-client-var-length-signal.md)) · 날짜: 2026-11-09
- 관련: [ADR-0079](0079-game-ui-runtime-declarative-documents-renderer-adapter.md) (게임 UI 런타임),
  [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 2.6 (NG-06), [audit/SECOND-PASS-GAP-ANALYSIS](../audit/SECOND-PASS-GAP-ANALYSIS.md) 4.6,
  [ADR-0083](0083-i18n-accessibility-foundation.md) 5 (키)

## 맥락

NG-06 은 게임 UI 에 TextInput · Slider · Checkbox · Toggle · Dropdown · ScrollView · Tab 과 포커스 · 키보드 · 게임패드 탐색을 더한다.
게임 UI 는 선언 문서 (ui/model) · 배치 · 그리기 · 라우팅이 헤드리스로 돌고, 값은 바인딩으로 읽는다 (var · session · component). 서버는
UI 를 모른다 (버튼은 신호 · 행동 명령). 엔진 키 (Space = 일시정지, 화살표 = 카메라) 가 이미 있어 키보드 포커스가 게임 키와 부딪힌다.

## 결정

```text
1. 컨트롤의 값 = "var:<키>" 바인딩 하나 (다른 식이면 U4). 컨트롤이 바꾸면 클라이언트의 var 표에 쓴다 (client 스크립트 setUi 와 같은
   표 — 스크립트도 읽고 쓴다). 서버 상태가 아니다
2. 바뀜 이벤트 on.change = {"signal": "<팩>.<이름>"} 만 — 값은 컨트롤이 정한다 (checkbox · toggle 0 · 1, slider 는 반올림한 i32).
   서버가 PlayerSignal 로 판정한다 (선언 · 범위 · perTick — 위조 값은 거절되는 명령일 뿐). slider 는 끌기를 놓을 때 · 키로 바꿀 때 한 번
3. disabled = 참거짓 또는 바인딩 — 꺼진 컨트롤은 포인터 · 키를 받지 않고 포커스 차례에서 빠진다 (흐리게 그린다)
4. 키보드 포커스: Tab (Shift+Tab 뒤로) 이 보이는 컨트롤 중 첫째 (다음) 에 준다 — 차례 = tabIndex (작은 것 먼저) 다음 문서 차례.
   menu · modal 층의 컨트롤을 누르면 그것에 준다. hud 컨트롤을 누르는 것은 포커스를 주지 않는다 (게임을 하며 HUD 를 눌러도 WASD ·
   화살표가 게임에 남는다). 포커스가 있으면 키보드는 게임 UI 가 갖는다 (엔진 · 게임 키에 가지 않는다), Esc 가 놓는다
5. 포커스가 있을 때의 키: Enter · Space = 누르기 (버튼 click · checkbox 바꾸기), 왼 · 오른 = slider 한 칸 (step, 없으면 범위의
   1/20) — slider 가 아니면 위 · 아래와 같이, 위 · 아래 = 그 방향의 가장 가까운 컨트롤 (주 축 거리 + 옆 축 거리 × 2 — 게임패드
   탐색의 바탕). 키는 ui 가 모르는 플랫폼 키가 아니라 UiKey (Tab · 방향 · 누르기 · 놓기) 로 받는다 (ui/ 는 Foundation 만)
6. 단계: NG-06 (1) checkbox · toggle · slider · 포커스 · 키, (2) TextInput (IME — 27 9장), (3) Dropdown · ScrollView · Tab · 게임패드
   (I5 — 플랫폼 게임패드 입력 뒤)
```

## 근거

```text
- var 표 하나: client 스크립트가 같은 값을 읽고 쓴다 (옵션 메뉴 → 스크립트가 반응). 서버 상태를 UI 가 직접 바꾸는 길을 만들지 않는다
- 신호만: 버튼과 같은 판정 길 (N1) — 새 명령 · 프로토콜이 필요 없다
- HUD 클릭이 포커스를 주지 않는 것: 게임 키와 겹치는 화살표 · Space 를 HUD 한 번 눌렀다고 빼앗지 않는다. 메뉴 · 모달은 게임을
  멈춘 화면이라 키보드를 가져도 된다
```

## 결과

- 얻는 것: 옵션 · 설정 메뉴 (소리 크기 · 켜고 끄기), 키보드만으로 메뉴 조작, 같은 값을 스크립트와 나눔.
- 포기하는 것: 컨트롤 값을 서버 상태에 바로 묶기 (신호 + 서버 스크립트로), 문자열 값 (TextInput — (2) 에서 문자열 신호 결정).
- 위험: 신호의 i32 범위 (slider 는 반올림 — 소수 값은 var 에만), Tab 이 게임 키로 묶이면 포커스를 못 얻는다 (지금 기본 키에 없다 —
  키 패널이 겹침을 보인다).

## 대안

| 대안                                         | 기각 사유                                                           |
|----------------------------------------------|---------------------------------------------------------------------|
| 컨트롤마다 상태를 GameUiLayer 안에만          | 스크립트가 읽지 못한다 · 문서를 다시 읽으면 값이 사라진다             |
| 값이 바뀔 때마다 신호 (slider 끌기 중 매 프레임) | perTick 상한에 걸리고 서버를 흔든다 — 놓을 때 한 번                  |
| HUD 클릭도 포커스                             | 게임 키 (화살표 · Space) 를 클릭 한 번에 빼앗는다                     |

## 재검토 조건

게임패드 입력 (I5), TextInput 의 문자열 신호, 접근성 점검 (v1.0 — 화면 읽기 · 포커스 표시).
