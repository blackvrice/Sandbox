# ADR-0079. 게임 UI — 팩의 선언형 문서 (presentation/ui), 독립 타깃 SandboxGameUI, 렌더 어댑터 (v0.5 는 ImGui 그리기 목록), GraphicsUI 를 기다리지 않는다

- 상태: **Accepted** · 날짜: 2026-10-28
- 관련: [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 2장, [ADR-0078](0078-v05-creator-alpha-scope-expansion.md),
  [ADR-0080](0080-gameplay-api-engine-component-access-structural-ops.md) (신호 · owned), [ADR-0083](0083-i18n-accessibility-foundation.md),
  [ADR-0067](0067-client-scripts-domain-client-onupdate-read-only-display.md) (client 스크립트), 01 (경계), 06 · 10

## 맥락

게임 화면의 UI 는 지금 ImGui 도구 패널 (점수 줄 · 행동 패널) 과 client 스크립트의 label · print 뿐이다. 제작자가 HUD · 메뉴 · 버튼을 만들
길이 없다. 정할 것: 데이터 모양 (코드 · 데이터), 어느 모듈에 두나, 그리기, 입력, 서버와의 관계, 에디터 UI (ImGui) 와의 경계, 따로
연구 중인 GraphicsUI 프로젝트와의 의존.

GraphicsUI (D:\source\GraphicsUI — 이 작업에서 README · ROADMAP 을 읽었다) 는 스스로 "연구 단계, 작성 문법 · 최종 UI 아키텍처 미선정,
제품용 UI Core 아님" 이라고 적는다. Sandbox 는 지금 이 프로젝트를 참조하지 않는다.

## 결정

```text
1. 게임 UI 는 데이터다: 팩의 presentation/ui/*.json (문서 · 스타일). contentHash 밖 (presentation/ 규칙 그대로 — input.json 과 같다),
   서버는 읽지 않는다. 게임 패키지 · 잠금 digest 에는 파일로 들어간다
2. 요소 v0.5: panel · text · image · button · progress · list. 배치 = 부모 비율 앵커 (min · max) + 픽셀 offset · 크기 · 쌓기 (row ·
   column) · gap · padding · align · 자름. 결과는 정수 픽셀 사각형 — 같은 입력 = 같은 결과 (헤드리스 골든)
3. 새 타깃 SandboxGameUI (ui/) — 의존 Foundation 만 (모델 · 검사 U1~U8 · 배치 · 바인딩 평가 · 입력 판정 · 문자열 표). core · render ·
   ImGui 를 모른다 (check_includes 규칙). 데이터는 UiDataSource, 글자 크기는 ITextMeasure, 그리기는 IUiRenderer 로 받는다
4. v0.5 렌더 백엔드 = ImGui 그리기 목록 (사각형 · 이미지 · 글자 · 자르기만 — ImGui 위젯 · ID · 상태는 쓰지 않는다). 클라이언트의
   GameUiLayer 가 게임 화면 위 · 도구 패널 아래에 그린다. SandboxRender 직접 백엔드 · GraphicsUI 백엔드는 같은 인터페이스로 나중
5. 바인딩 소스: var (client 스크립트 ctx.ui.set) · session (game.session) · component@owned (내 플레이어의 개체 — game.owner) ·
   문자열 "@키". 참조 풀기 (임의 개체) 는 v0.6
6. 이벤트: 버튼 → PlayerSignal 명령 (선언된 신호 · 서버 판정) · PlayerAction (위치 없는 행동) · 클라이언트 안 보이기/숨기기. 서버 권위
   그대로 — 위조 클릭은 거절되는 명령일 뿐
7. 입력 순서: modal → menu → 도구 패널 (ImGui 가 잡으면) → hud 버튼 → 게임 키 · 월드. hud 는 버튼 위가 아니면 통과
8. 에디터 · 도구 UI 는 ImGui 그대로 — 게임 UI 를 ImGui 위젯으로 만들지 않는다. 에디터의 "UI" 편집 = JSON + 미리보기 (v0.5), 끌어 놓는
   시각 편집은 v0.6
9. 상한: 문서 32 · 노드 합 4,096 · 바인딩 1,024 · 문서마다 노드 512 · 깊이 16 (넘으면 로드 경고 · 그리지 않음)
```

## 근거

```text
- 데이터 문서: 노코드 제작자 (C3) 가 스크립트 없이 HUD 를 만든다. 검사 · 패키지 · 잠금 · 라이선스 요약이 지금 규칙 그대로 닿는다
- 독립 타깃: 배치 · 바인딩을 헤드리스로 시험하고, 백엔드를 바꿔도 문서 · 배치가 그대로
- ImGui 그리기 목록: 클라이언트가 이미 링크 · 한국어 폰트를 가진다 — 새 의존 없이 v0.5 에 그린다. 위젯을 쓰지 않아 ImGui 의 즉시 모드
  상태가 게임 UI 의 모델로 새지 않는다
- GraphicsUI 를 기다리지 않음: 연구 단계 (제품 UI Core 없음) — 의존하면 v0.5 가 그 일정에 묶인다
```

## 결과

- 얻는 것: 코드 없는 HUD · 메뉴 · 버튼, 스크립트 연동, 결정적 배치 시험, 백엔드 교체 가능.
- 포기하는 것: v0.5 의 글자 품질 · 효과 (ImGui 폰트 그대로 — 그림자 · 외곽선 · 애니메이션 없음), 시각 편집.
- 위험: ImGui 백엔드의 성능 · 품질 (20 12.3 측정 — 나쁘면 SandboxRender 직접 백엔드를 앞당긴다), 입력 라우팅이 도구 패널과 겹침.

## 대안

| 대안                                          | 기각 사유                                                                     |
|-----------------------------------------------|-------------------------------------------------------------------------------|
| ImGui 위젯으로 게임 UI (스크립트가 ImGui 호출) | 도구 UI 와 섞인다, 데이터가 아니라 노코드가 안 된다, 즉시 모드 상태가 게임에 샌다 |
| GraphicsUI 를 의존으로                        | 연구 단계 — 제품 UI Core 가 없다                                               |
| 서드파티 HTML/CSS 런타임 UI                   | 새 큰 의존 (빌드 · 보안 표면 · 폰트 · 렌더 통합), 노코드 제작자에게 HTML/CSS 는 무겁다 — v0.6 이후 백엔드 후보로만 |
| SandboxRender 위에 처음부터 (글자 아틀라스 · 셰이핑) | v0.5 일정에 글자 렌더 (셰이핑 · 아틀라스 · 대체) 가 크다 — 같은 인터페이스로 나중 |
| 서버가 UI 를 복제                              | UI 는 표시다 — 대역폭 · 결정론 표면만 는다. 서버는 컴포넌트를 바꾸고 바인딩이 본다 |

## 재검토 조건

M3 측정에서 프레임 예산을 넘을 때, GraphicsUI 가 제품 UI Core 를 내놓을 때, 텍스트 입력 · IME · 복잡한 글자 (RTL) 가 필요할 때.
