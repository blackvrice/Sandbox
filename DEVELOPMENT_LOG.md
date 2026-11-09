# DEVELOPMENT_LOG

작업 단위의 **이력**입니다: 무엇을, 왜 바꿨고, 어떻게 검증했고, 무엇이 남았나.
"지금 어떤 상태이고 무엇을 지켜야 하나"는 `docs/`에 있습니다. 둘을 섞지 않습니다.

새 항목은 위에 추가합니다.

---

## 2026-11-09 — v0.8 ST-06-1 (2): DocumentService · 하나의 되돌리기 줄 UndoService (ADR-0109)

**무엇을**

- editor/services/UndoService: 줄의 칸 {World · Document, 이름, tag}, 참여자 IUndoParticipant (undoLast · redoLast · dropRedo) 가 내용을,
  서비스가 차례를. record · undo · redo (참여자가 못 하면 줄 그대로) · clear(종류) · clear(종류, tag) · 상한 1000.
- editor/services/DocumentService (Document 참여자): open (같은 경로 = 같은 문서) · apply (바이트 범위 — 범위 밖 · UTF-8 가운데 거절) ·
  setText · dirty · save (확장자 검사 — .json 기본 · 바깥 변경 (디스크 SHA-256) · 없어진 파일이면 거절, force · io::writeFileAtomic) · reload ·
  close (고친 채면 force) · diskState · setValidator.
- 문서: 01 · 17 · 25 · 13 · ADR-0109.

**왜**

- ST-06-1 의 둘째 조각 — 파일 편집의 한 길 (ADR-0089 2) 과 하나의 Ctrl+Z (25 3장) 의 바탕. 월드 참여자 (Editor 의 Undo) 는 (3).

**검증**

- test_document_service 4 개 (편집 · 저장 · 되돌리기 줄 (두 문서 + 가짜 월드) · 상한). 시험 하나를 고쳤다 (Expected 끼리 == — Error 에
  == 가 없어 값으로). clang-tidy (-Wall -Wextra -Wpedantic -Wshadow) 바뀐 파일 모두 경고 없음.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄).

**남은 일**

- ST-06-1 (3): Editor 의 월드 Undo 를 World 참여자로 (앱의 Ctrl+Z · Ctrl+Y 를 UndoService 로), ICommandSink 를 render 없는 헤더로 — 그다음
  콘텐츠 창의 [저장 (팩 파일)] 을 DocumentService 로.

## 2026-11-09 — tests: net_server_probe 스모크가 쓰인 포트면 다른 포트로 다시 (45e6ac5 CI linux-clang 고침)

**무엇을**

- tests/net/net_smoke.cmake: 서버 stderr 가 "UDP 포트를 열지 못했습니다" 면 PORT + 101 · + 202 로 두 번 더 (net_server_probe_smoke 47791 ·
  net_server_probe_project 47792 — 다른 시험이 쓰지 않는 포트).

**왜**

- 45e6ac5 (scroll — UI 만) 의 CI linux-clang RelWithDebInfo 에서 net_server_probe_smoke 하나가 실패 (서버가 47791 을 열지 못함). 고정
  포트가 Linux 의 임시 포트 범위 (32768 ~ 60999) 안이라 바쁜 CI 기계의 다른 연결이 먼저 잡을 수 있다 (같은 잡의 Debug 단계는 통과, 다른
  네 잡 통과). 전에도 같은 충돌이 한 번 있었다 (M2-T5 기록).

**검증**

- ctest -R net_server_probe 2 / 2. 47791 을 다른 SandboxServer 가 잡은 채로 net_server_probe_smoke — "포트 47791 가 쓰이고 있다 — 다른
  포트로 다시" 뒤 47892 로 접속 · 통과.

**남은 일**

- 없음 (포트를 OS 가 고르게 하려면 서버가 고른 포트를 알려야 한다 — 필요해지면).

## 2026-11-09 — v0.8 ST-06-1 (1): Editor Services 타깃 · include 규칙 · 명령 표 (ADR-0108)

**무엇을**

- editor/services/ — 새 정적 타깃 SandboxEditorServices (Core 만, 이름공간 sbx::editor::services). SandboxEditor 가 PUBLIC 으로 링크.
  루트 링크 경계: Network · Platform · Render · SandboxEditor · sbx_imgui 금지.
- tools/check_includes.py: 하위 경로 방향 규칙 SUBPATH_ALLOWED (editor/services/ → foundation · script · core · editor/services),
  editor/services/ 아래 금지 헤더 모두. 자체 시험 픽스처 (나쁜 서비스 1 · 허용 서비스 · 허용 툴) — 12 → 15 건.
- CommandService: 명령 {id · 제목 · 단축키 · context · when · run}, normalizeShortcut (수정자 차례 · 별칭 · F 키 · 이름 키), 등록 거절,
  run · runShortcut (그 곳 먼저 · global), search (제목 앞 → 안 → id), conflicts (겹치는 곳의 같은 단축키).
- 문서: 01 (타깃 표 · 검사 표) · AGENTS.md 경계 규칙 · 17 · 25 · 13 · ADR-0108.

**왜**

- v0.8 의 첫 태스크 ST-06-1 (Editor Services 층 · Command · Undo 통합 · check_includes 규칙). 이번은 층 · 규칙 · 명령 표 — 문서 서비스 ·
  Undo 통합은 (2) · (3).

**검증**

- test_command_service 3 개 · arch_include_lint_selftest 15 · arch_include_lint (실제 트리 0 건). clang-tidy (-Wall -Wextra -Wpedantic
  -Wshadow) 바뀐 파일 모두 경고 없음.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄).

**남은 일**

- ST-06-1 (2) DocumentService (열기 · 고침 · 원자적 저장 · 바깥 변경 감지) · UndoService (문서), (3) 월드 편집 Undo (Editor) 를 같은 기록 줄로 ·
  ICommandSink 를 render 없는 헤더로. 앱의 단축키를 명령 표로 (ST-06-2).

## 2026-11-09 — v0.7 종료: 0.7.0 게임 UI Runtime · 다국어 — 기준 판정 · 릴리스 노트 · 제품 버전 0.7.0

**무엇을**

- docs/releases/v0.7.0.md: 들어간 것 (M3-T1 ~ T6 · M4-T1 ~ T4 · NG-06 (1) ~ (4)), 호환 번호 (프로토콜 16 · .sbc 10 · kSimVersion 4 ·
  세이브 · 패키지 · 잠금 · 리플레이 그대로, Host API 1 의 지문만 0x1dfa888c39154dde → 0xcfce033639142a66 — 더하기만), release-gates v0.7
  행 판정 (항목마다 시험 이름), 계획 대비, 알려진 문제 (창 눈 확인 · IME 창 자리 · 게임패드는 Windows 만), 데모 · 재현.
- CMake project VERSION 0.7.0, README · docs/README · 18 · 23 · roadmap 의 지금 · 다음 줄 (다음 = v0.8 ST-06-1, 트랙 A M6-T4), versioning ·
  버전 매트릭스의 제품 버전.

**왜**

- v0.7 의 태스크 (releases.md v0.7 줄) 가 모두 Done — 버전이 끝날 때 제품 버전을 올린다 (18 4장).

**검증**

- 골든 · 콘텐츠 그대로 (`git diff 8dfab57 -- tests/golden content` 비어 있음) — 제품 버전만 바뀌었다. 기준 판정의 시험은 노트 4장.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). sbx --version = 0.7.0.

**남은 일**

- MANUAL-QA 의 v0.7 창 눈 확인 [ ] 줄, v0.8 ST-06-1 Editor Services.

## 2026-11-09 — v0.7 NG-06 (4): 게임패드 — Windows XInput 소스 · 게임 UI 탐색 (ADR-0107, NG-06 끝)

**무엇을**

- platform: GamepadState.pressed · wasPressed, NullGamepadSource · createGamepadSource (common/GamepadSource.cpp — Windows 밖은 없음),
  windows/XInputMap (순수 표 — 버튼 비트 · 스틱 죽은 영역 · 트리거, 모든 OS 에서 컴파일), windows/Win32Gamepad.cpp (XInput DLL 을 실행
  중에 — xinput1_4 → 1_3 → 9_1_0, 끊긴 칸은 120 번에 한 번). InputSystem::setGamepads (pressed · 포커스 없음 0 — I3), downstream 은 키보드
  캡처면 패드도 비운다 (I1).
- 앱: AppConfig.gamepads (main 이 createGamepadSource), 프레임마다 poll → setGamepads, routeGamepads (십자 · 왼 스틱 넘을 때 한 번 ·
  A 누르기 · B 놓기 · LB · RB 차례, 포커스가 없으면 첫 컨트롤에).
- 문서: NG-06 Done (등록부), v0.7 태스크 모두 ✅ (releases · roadmap README · 18 0장).

**왜**

- NG-06 의 마지막 조각 (ADR-0105 5) — I5 의 "콘텐츠가 요구할 때" 가 게임 UI 탐색이다.

**검증**

- test_gamepad 3 개 (XInput 표 · InputSystem · 소스) · test_game_ui_layer 앱 "게임패드" (가짜 소스 · 로컬 서버 — A 첫 포커스 · 십자 ·
  스틱 넘을 때 한 번 · B · 창 포커스 없으면 무시). 실행: SandboxClient --headless 가 "게임패드: XInput (xinput1_4.dll)" 을 남긴다.
  clang-tidy (-Wall -Wextra -Wpedantic -Wshadow) 바뀐 파일 모두 경고 없음.
- 실제 패드는 이 기계에 없다 — MANUAL-QA [ ].
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄).

**남은 일**

- Linux evdev · macOS GameController 소스, 스틱 · 십자 반복, 오른 스틱 밀기, 게임 키의 패드 바인딩 (RT-06-13), 0.7.0 릴리스.

## 2026-11-09 — v0.7 NG-06 (3b): 게임 UI scroll — 휠 · 포커스 따라 밀기 (ADR-0106)

**무엇을**

- ui/model: 요소 scroll (panel 과 같은 키 — 컨트롤 아님). ui/layout: scrollOf 콜백 → 0 ~ scrollRange (내용 높이 - 안쪽 높이) 로
  자르고 정수로, 자식의 부모 사각형을 밀린 만큼 위로 (clip 은 scroll), UiLayoutNode.scroll · scrollRange. ui/render: panel 처럼 +
  오른쪽 막대 (모든 노드 뒤 두 번째 돌기 — 자식 위).
- GameUiLayer: 밀기 표 (노드 · list 번호 → 픽셀, 원문이 바뀌면 잊음), wheel (층 차례 · modal 만 · 가장 안쪽 · 40 × 배율 · 끝에서도
  씀), revealFocus (Tab · 방향 포커스가 가려지면 가장 안쪽 scroll 을 민다).
- Application: 포인터 아래 scroll 이 휠을 쓰면 그 프레임 마우스를 게임에서 뺀다 (카메라 확대 없음).

**왜**

- NG-06 (3) 의 남은 ScrollView (ADR-0105 5). 게임패드 탐색만 플랫폼 게임패드 입력 (I5) 뒤로 남는다.

**검증**

- test_ui_scroll 4 개 (문서 · 배치 · 그리기 · 라우팅) · test_game_ui_layer "scroll" (휠 · Tab 보이게 · modal) · 앱 "HUD scroll 위의 휠"
  (로컬 서버 — 밀기 40, 카메라 pixelsPerUnit 그대로, 밖은 확대). 그리기 기대값 하나를 손 계산으로 고쳤다 (글자 y = 0 - 25).
  clang-tidy (-Wall -Wextra -Wpedantic -Wshadow) 바뀐 파일 모두 경고 없음.
- 창 눈 확인은 MANUAL-QA [ ].
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). 런타임: rabbit_guard 복사본에 menu scroll (글자 · 버튼) 문서를 더해 sbx check — 오류 0.

**남은 일**

- 막대 끌기 · 가로 밀기 · 겹친 scroll 의 바깥 맞추기 · dropdown 목록 밀기, 게임패드 탐색 (I5 뒤 — NG-06 의 마지막).

## 2026-11-09 — v0.7 NG-06 (3): 게임 UI dropdown · tabs (ADR-0105)

**무엇을**

- ui/model: 요소 dropdown (value = "var:<키>" 의 번호 · options 1 ~ 32 · on.change) · tabs (value · labels 1 ~ 32 · children 은
  labels 와 하나씩 — U1). ui/layout: dropdown 크기 (가장 긴 option + 여백 + 화살표 칸), tabs 머리 칸 (UiLayoutNode.parts) · 고른
  자식만 배치 (selectedOf · 크기는 가장 큰 자식), 목록 자리 optionListRect · optionAt. ui/render: dropdown 상자 · 고른 option · 화살표,
  tabs 머리 · 밑줄, 열린 목록 drawOptionList, tabs 의 포커스 고리는 머리 칸. ui/input: tabs 는 머리 칸 위에서만 잡는다 (partAt).
- GameUiLayer: 열린 목록 (하나) — 라우팅에서 문서들보다 먼저 (목록 위 포인터 · 고르기 · 밖 누르면 닫고 아래로 · 자신은 닫기만),
  모든 문서 뒤에 그리기. 고르기 select (같은 번호면 신호 없음), 키 choiceKey (왼 · 오른 한 칸, Enter 열기 · 위아래 · Enter 고르기 ·
  Esc 닫기), 포커스가 옮겨 가면 목록 닫기. option · label 의 "@<키>" 는 문자열 표.

**왜**

- 등록부 NG-06 의 셋째 단계. 게임패드 탐색은 플랫폼 게임패드 입력 (I5) 이 먼저, ScrollView 는 배치의 밀기 상태라 따로 (NG-06 (3b)).

**검증**

- test_ui_choice 4 개 (문서 · 배치 · 그리기 · 라우팅) · test_game_ui_layer "dropdown" (층 — 마우스 · 키 · tabs). 그리기 기대값 하나를
  손 계산으로 고쳤다 (글자 너비 = 영역 60 - 화살표 20 = 40). clang-tidy (-Wall -Wextra -Wpedantic -Wshadow) 바뀐 파일 모두 경고 없음.
- 창 눈 확인은 MANUAL-QA [ ].
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). 런타임: rabbit_guard 복사본에 tabs (머리 "@" 키) · dropdown (없는 키 option) 문서를 더해 sbx check — 오류 0, 없는 키는 U5 경고 하나.

**남은 일**

- NG-06 (3b) ScrollView (밀기 · 휠 · 포커스가 보이게 · 긴 목록), 게임패드 탐색 (I5 뒤), 타자로 option 찾기.

## 2026-11-09 — v0.7 NG-06 (2): 게임 UI 글자 칸 (textinput) · client ctx.uiVar · uiVarText (ADR-0104)

**무엇을**

- ui/model: 요소 textinput (value = "var:<키>" 의 글 · placeholder · maxLength 1 ~ 256 · multiline · on.change 신호). ui/input/UiTextEdit
  (새 파일): 코드 포인트 캐럿의 넣기 (제어 문자 · 상한) · 지우기 · 캐럿 줄 · 칸 이동. ui/layout 크기 (200 × 줄 + 글꼴 × 0.25 여백), ui/render
  글자 칸 (상자 · placeholder · 줄마다 글 · 캐럿 · 캐럿이 보이게 가로 · 세로 밀기), UiKey 에 Backspace · Delete · Home · End.
- GameUiLayer: text (확정 글자) · 편집 키 · Enter 보내기 (값 = 코드 포인트 수) · 떠날 때 바뀌었으면 보내기 (setFocus 한 곳), 글자 칸은
  hud 클릭도 포커스 · 편집 중 다른 곳 클릭은 놓기. draw 가 글자 재기를 받는다 (캐럿).
- Application: 글자 칸 편집 중 창의 글자 입력 (IME) 켜기 · 우리가 켠 것만 끄기, 확정 글자를 키보다 먼저, Space 는 글자.
- Host API: client.uiVar (수) · client.uiVarText (글) — 세션의 var 표 (ClientScriptFrame.uiData). 지문 0xd8e74d9eb927e9ae →
  0xcfce033639142a66 (더하기만 — 버전 1), SDK 4 곳 다시 생성.

**왜**

- 등록부 NG-06 의 둘째 단계. 글은 i32 신호로 보내지 못한다 — 프로토콜을 바꾸는 문자열 신호는 따로 결정하고 (ADR-0104 7), 이번은
  클라이언트 쪽 글자 칸 + 스크립트가 읽는 길 (사용자 결정: 클라이언트 쪽 먼저).

**검증**

- test_ui_textinput 5 개 (문서 · 편집 · 배치 · 그리기 · 라우팅) · test_game_ui_layer "글자 칸" (층) · 앱 "HUD 글자 칸" (로컬 서버 —
  창 글자 입력 켬 · 끔, 한글 · Space · Backspace, Enter → 서버 onEvent "signal eco.say 4", 일시정지 아님) · test_client_script_session
  "ctx.uiVar" · 지문 · 바인딩 수 (22). clang-tidy (-Wall -Wextra -Wpedantic -Wshadow) 바뀐 파일 모두 경고 없음.
- 한글 IME 창 눈 확인은 MANUAL-QA [ ]. 런타임: rabbit_guard 복사본에 글자 칸 문서 (한 줄 · multiline) 를 더해 sbx check — maxLength 300 은 U1 오류, 200 이면 오류 0 · 문서 2.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄).

**남은 일**

- 문자열 신호 ADR (글을 서버로 — 채팅 · 이름: 명령 · 프로토콜 17 · 길이 · 필터), IME 조합 창 위치 (창 API), 선택 · 복사 · 붙여넣기 ·
  누른 자리 캐럿 · 깜빡임, NG-06 (3) Dropdown · ScrollView · Tab · 게임패드 탐색 (I5).

## 2026-11-09 — v0.7 NG-06 (1): 게임 UI 입력 컨트롤 — checkbox · toggle · slider · 키보드 포커스 (ADR-0103)

**무엇을**

- ui/model: 요소 checkbox · toggle · slider (value = "var:<키>" — U4, on.change = 신호만, slider min · max · step), 버튼 · 컨트롤의
  disabled (참거짓 · 바인딩) · tabIndex. ui/layout 크기 (상자 = 글꼴 × 1.25 — controlBox), ui/render 그리기 (켬 · 끔 · 채움 · 손잡이 ·
  흐림 · 포커스 고리), ui/input 라우팅이 컨트롤도 잡는다 (UiPointerRoute::button → control), focusOrder · nearestFocus · UiKey.
- GameUiLayer: checkbox · toggle 누르기 · slider 끌기 (놓을 때 신호 하나) → var 쓰기 (setVarWriter → 세션 setUiVar — IWorldSession 에
  더함) · on.change 신호, key (Tab 차례 · 방향 · 누르기 · 놓기), 포커스는 menu · modal 클릭 · Tab 에서만.
- Application: 도구 패널이 키보드를 원하지 않을 때 Tab · 방향 · Enter · Space · Esc 를 게임 UI 에, 포커스가 있으면 키보드를
  게임에서 뺀다 (setCapture).

**왜**

- 등록부 NG-06 (v0.7 의 남은 태스크 — 2차 감사 4.6). 결정 (값 · 바뀜 · 포커스 규칙) 은 ADR-0103, 남은 컨트롤은 (2) · (3).

**검증**

- test_ui_controls 4 개 (문서 · 배치 · 그리기 · 라우팅 · 포커스 · 방향) · test_game_ui_layer "입력 컨트롤" (층) · 앱 "Tab 이 HUD
  checkbox 에" (로컬 서버 — Space 가 checkbox · 서버 onEvent, 일시정지 아님, Esc 뒤 Space 는 일시정지). 시험 기대값 둘을 손 계산으로
  고쳤다 (slider 길 y = 40 + 15 / 2 = 47, a 의 아래는 b 가 아니라 c — 점수 144 < 340).
- 창 눈 확인은 MANUAL-QA [ ] (다른 전체 화면 창). 옛 시험 하나 (test_ui_document — 모르는 요소의 보기로 "slider") 를 "knob" 으로.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19, -Wall · -Wshadow — -Wswitch 포함) — 새 경고 없음.

**남은 일**

- NG-06 (2) TextInput (한 줄 · 여러 줄 · 최대 길이 · IME — 문자열 신호 결정), (3) Dropdown · ScrollView · Tab · 게임패드 탐색 (I5).

## 2026-11-09 — v0.7 M4-T4: 게임 UI 배율 설정 — client.json gameUiScale · --game-ui-scale (0.75 ~ 2)

**무엇을**

- client.json "gameUiScale" (0.75 ~ 2, 범위 밖 · 수 아님 = 깨진 설정), --game-ui-scale (strtod — 부동소수 from_chars 가 없는
  macOS libc++ 를 피한다), AppConfig.gameUiScale → GameUiLayer::layout 의 배율 (M3-T6 의 배치 · 테두리 길 그대로). 안전 모드는 1.

**왜**

- 등록부 M4-T4 (ADR-0083 5 — UI 배율 0.75 ~ 2.0). 배율 계산은 M3-T6 에 있었고 클라이언트가 1 로 고정했다.

**검증**

- test_ui_layout 데모 HUD 골든 0.75 · 1.5 (손 계산 — 첫 실행에 맞았다), test_user_settings "게임 UI 배율", test_game_ui_layer 앱 HUD
  클릭을 배율 1.5 로 (버튼 180 × 60 · 가운데 클릭 → PlayerAction). SandboxClient --headless --game-ui-scale 1.5 · 3 (거절).
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19, -Wshadow 포함) — 새 경고 없음.

**남은 일**

- 창에서 배율 · 높은 DPI 눈 확인 (MANUAL-QA [ ]), 실행 중 배율 바꾸기 (설정 UI — 로케일과 함께).

## 2026-11-09 — v0.7 M4-T3: 키 다시 묶기 — "키" 패널 · 바꾸기 · 되돌리기 · 겹침 · 저장 (ADR-0102)

**무엇을**

- platform ActionMap: set (없으면 이름 순 자리에) · remove · overridesFrom (base 와 다른 것만 — merge 의 역), captureBinding (이
  프레임에 새로 누른 키 · 마우스 버튼 + 눌린 순간의 수정자, 수정자 키 자신은 아니다).
- apps/client/ui/KeysPanel: KeyBindingEditor (엔진 기본 · 지금 맵 — rebind · reset (엔진 = 기본, play.* = 덮어쓰기 빼기) · resetAll ·
  overrides · dirty), buildKeysView (엔진 · 게임 키 줄 · 겹침 — 엔진 × 게임 함께), drawKeysPanel.
- Application: 바꾸는 동안 누른 키는 바인딩이 되고 엔진 · 게임 키 · 월드 입력에 가지 않는다 (Esc 취소), 바꾸면 ActionId 를 다시
  찾고 (lookupActionIds) 게임 키를 다시 만든다. 저장 = <설정 폴더>/input.json 에 기본과 다른 것만 (안전 모드는 저장하지 않음).
- ADR-0102: 사용자 파일은 M1-T3 의 input.json (ADR-0083 5 의 bindings.json 대신 — 0083 은 일부 Superseded).

**왜**

- 등록부 M4-T3 (v0.7 P1 — "사용자가 게임 키를 바꾼다"). 읽기는 M1-T3 에 있었고 바꾸기 · 저장 · 되돌리기 · 겹침 표시가 없었다.

**검증**

- test_key_bindings 4 개 (ActionMap 왕복 · 키 잡기 · 편집기 · 보기 겹침 · 앱 헤드리스 — 바꾸는 중 Ctrl+Q 가 끝내지 않는다, 저장 →
  다시 읽기, 되돌리면 Ctrl+Q 가 끝낸다, 안전 모드). 처음에 시험이 Ctrl 을 누른 채로 두지 않아 (수정자는 누르고 있는 키로 본다)
  Q 만 잡혔다 — 시험을 고쳤다. 기본 키 G 가 view.grid 라 겹침 시험 키를 Alt+J 로.
- 창의 패널 눈 확인은 MANUAL-QA [ ] (다른 전체 화면 창).
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19, -Wshadow 포함) — 새 경고 없음.

**남은 일**

- 게임패드 바인딩 (I5), 프로젝트별 키 설정 (ADR-0102 재검토 조건), 한 액션에 여러 키를 패널에서 더하기 (지금은 하나로 바꾼다 —
  파일로는 여럿).

## 2026-11-09 — v0.7 M4-T2 (2): 폰트 대체 사슬 · 없는 글자 □ 와 경고 한 번 (M4-T2 끝)

**무엇을**

- apps/client/ui/FontFallback: buildFontChain (--font → client.json fonts → OS 묶음 — 묶음마다 처음 있는 파일, 없는 파일 경고 ·
  같은 파일 한 번), GlyphCoverage (사슬에 없는 글자 → □ · '?', 코드 포인트마다 캐시 · 처음 것만 경고), describeCodepoint.
- ImGuiLayer: 사슬의 첫 파일이 바탕, 나머지는 MergeMode 로 합친다 (로그 "폰트 a + b + c"). client.json "fonts" (8 개까지 — 안전
  모드는 읽지 않는다).
- 게임 UI 그리기 · 재기 (ImGuiUiRenderer · ImGuiTextMeasure) 가 같은 덮은 글자를 쓴다, 앱이 새 없는 글자를 경고로.

**왜**

- 등록부 M4-T2 의 남은 몫 (ADR-0083 4 — "글자가 없으면 다음 폰트, 끝까지 없으면 □ 와 경고 한 번"). 전에는 바탕 폰트 하나뿐 (맑은
  고딕에 없는 기호 · 라틴 확장은 '?').

**검증**

- test_font_fallback (사슬 · 덮기 · 실제 ImGui 헤드리스 — 이 기계 malgun + segoeui + seguisym) · test_user_settings "fonts".
- SandboxClient --headless --console: en 도움말에 😀 · Ω 를 넣은 복사본 → U+1F600 경고 한 번, Ω 는 사슬에 있다 (MANUAL-QA).
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19, -Wshadow 포함) — 바꾼 파일 모두 새 경고 없음.

**남은 일**

- 창에서 □ 눈 확인 · Linux · macOS 의 OS 묶음 (MANUAL-QA [ ]), ImWchar 32 비트 (이모지 — 색 글꼴은 v1.x), 도구 패널 글자 덮기.

## 2026-11-09 — CI 고침: test_ui_document 의 람다 매개변수가 지역 변수를 가린다 (-Wshadow, e6e5d46)

- 무엇을 · 왜: Linux GCC 빌드가 -Werror=shadow 로 멈췄다 — v0.7 M4-T1 (2) 의 표시 이름 시험에서 람다 매개변수 `text` 가 같은
  범위의 `std::string text` 를 가렸다. 매개변수를 `needle` 로. 커밋 전에 clang-tidy 를 그 시험 파일에 돌리지 않았다 — 바꾼 파일
  모두에 돌린다 (12-CODING-STANDARDS 체크리스트 그대로).
- 검증: clang-tidy (-Wshadow 포함) — 새 경고 없음, M4 시험 통과. 남은 일: 없음.

## 2026-11-09 — v0.7 M4-T2 (1): 게임 UI 글자 늘어남 — overflow clip · wrap · fit

**무엇을**

- ui/model: text · button 의 "overflow" (clip 기본 · wrap · fit — 모르는 값 U1, 다른 요소 U3).
- ui/layout: 넘침 글자가 쓸 수 있는 너비 (size · row 의 남은 자리 · column 의 안쪽 너비 · 부모 안쪽 오른 끝) 를 재기에 넘기고,
  wrap 은 wrapText (빈칸 · 긴 낱말은 코드 포인트 · '\n') 로 줄을 나눠 UiLayoutNode.lines · lineHeight 에, fit 은 글꼴을 너비에
  맞게 (kMinFitScale 0.5 까지) UiLayoutNode.fontSize 에. ui/render 는 줄마다 그린다.
- 데모 rabbit_guard 도움말에 "overflow": "fit".

**왜**

- 등록부 M4-T2 (ADR-0083 4 — 글자 늘어남은 배치가 감당, "긴 번역 배치 (잘림 없음)"). 폰트 대체 목록은 ImGui 글꼴 합치기 ·
  없는 글자 경고라 (2) 로 나눴다.

**검증**

- test_ui_layout (wrapText · overflow · 번역 2 배 · 배율 2 골든) · test_ui_document "overflow" · test_ui_render "wrap". 배율 2
  골든의 도움말은 M3-T6 때 띠 밖으로 잘렸고 (432 픽셀 — 남은 자리 338) 이제 fit 으로 든다 (손 계산 {918, 40, 338, 32}). 번역 2 배
  시험을 처음 1024 너비로 적었다가 en 이 절반 글꼴로도 넘쳐 (자르는 것이 맞다) 데모의 기준 크기 1280 으로 고쳤다.
- SandboxClient --headless --locale en --width 1000 · sbx check samples/rabbit_guard (오류 · 경고 0). 창 확인은 이번에 하지 못했다
  (다른 전체 화면 창이 앞에 있었다 — MANUAL-QA 에 [ ] 로 남김).
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19) — 새 경고 없음.

**남은 일**

- M4-T2 (2): 폰트 대체 목록 (설정 · OS 의 라틴 · 한국어 · 기호 — ImGui 글꼴 합치기), 끝까지 없는 글자 □ 와 경고 한 번.
- list 항목 · 버튼의 wrap 은 배치가 하지만 버튼 높이가 커지는 디자인은 제작자 몫 (fit 을 권한다).

## 2026-11-09 — v0.7 M4-T1 (2): client 스크립트 ctx.uiText · uiFormat · 표시 이름 "@<키>" — Host 결과 String (M4-T1 끝)

**무엇을**

- VM: Host 결과에 String (HostResult::text → 이 호출의 아레나 문자열 — 검사기 · 검증기는 이미 String 결과를 알았다).
- Host 표 끝에 client.uiText(key) · client.uiFormat(key, arg0, arg1) (Capability Ui · 읽기). ClientScriptFrame.strings = 앱의
  게임 UI 층의 표 (세션 setUiStrings — 없으면 키 그대로). Host API 지문 0x43ee8d9ef79b762a → 0xd8e74d9eb927e9ae (더하기만 — 버전 1),
  sdk/ · 샘플 셋의 sdk 다시 생성.
- 표시 이름: 행동 · 게임 모드 · 결과 팀 · 점수 줄의 name 이 "@<키>" 면 도구 패널 ("게임" · "행동") 이 표의 글로 (GameUiLayer::
  displayName). 프로젝트 검사가 표시 이름의 없는 키 · 모양 밖을 U5 경고로. 문서가 없어도 표만 있으면 세션이 원문을 준다.

**왜**

- 등록부 M4-T1 의 남은 몫 (ADR-0083 2 — client ctx.ui.text · 콘텐츠 표시 이름). Host ABI 에 가변 인자 · 문자열 배열이 없어
  ctx.ui.text(key, args) 를 uiText · uiFormat (인자 둘) 로 — M3-T4 의 setUi 처럼 평평한 이름.

**검증**

- test_client_script_session "ctx.uiText" (로컬 서버 — 오버레이 client 시스템, 표 없음 · ko · en · 없는 키 경고 한 번) ·
  test_game_ui_layer "표시 이름" · test_ui_document (행동 · 모드 이름의 U5) · test_checker 지문 · test_client_scripts 20 ·
  test_script_host SDK. 처음에 시험 스크립트가 String(3) 을 써서 (SandboxScript 에 없다 — "" + n) 시스템이 조용히 로드되지 않았다
  (오버레이의 컴파일 오류가 드러나지 않는 알려진 후속 일).
- 창 확인: rabbit_guard 복사본의 행동 이름을 "@eco.hud.feed" · "@eco.hud.trap" 로 바꾸고 --locale en → "행동" 패널이
  "[Q] Feed" · "[E] Set trap" (MANUAL-QA v0.7 M4-T1). 저장소 데모 콘텐츠의 이름은 그대로 (contentHash · 자동 플레이 기대값).
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄).

**남은 일**

- 실행 중 로케일 바꾸기 (설정 UI — M4-T4 와 함께), macOS CFLocale, 오버레이 client 스크립트 컴파일 오류 드러내기.

## 2026-11-09 — v0.7 M4-T1 (1): 문자열 표 · 로케일 대체 — presentation/strings · "@<키>" · --locale · 검사

**무엇을**

- ui/strings: 표 읽기 parseStringTable (U1 모양 · U2 키 이름공간 · U7 1 KiB · 4,096), StringTable (팩마다 표 — 찾는 차례 = 사용자
  로케일 → 언어 → 팩의 기본 표 → 키 그대로 + 경고 한 번), normalizeLocale · localeChain · formatString · resolveText. UI 글자
  "@<키>" 는 표의 글에 args 바인딩을 넣는다 (formatText), "@@…" = 글자 "@…".
- core/content: 팩 presentation/strings/<로케일>.json 원문 → PackInfo.uiStrings (32 개까지 — 클라 전용, contentHash 밖).
- 프로젝트 검사: 파일 이름 · 같은 로케일 U2, 문서의 없는 키 U5, 빠진 번역 U5 (경고), sbx check 요약 "문자열 표: 로케일 N · 키 M".
- 클라이언트: GameUiInfo.strings → GameUiLayer (setLocale — 원문을 다시 읽어도 남는다), --locale · client.json locale ·
  platform::userLocale (Windows API · POSIX 환경) 순. 데모 rabbit_guard HUD 를 키로 바꾸고 ko · en 표.

**왜**

- 등록부 M4-T1 (v0.7 P1, ADR-0083) — 완료 기준 "데모가 ko · en 으로 바뀐다". ctx.ui.text (Host API 지문이 바뀐다) · 행동 · 모드
  표시 이름은 (2) 로 나눴다 (M3-T4 처럼 — 한 커밋에 Host API 변경을 섞지 않는다).
- 팩의 기본 표 규칙 (여럿이면 en, 없으면 이름 순) 은 ADR-0083 3 의 "표가 하나면 그것" 을 여러 표로 넓힌 것 — pack.json 에 새 키 없이
  결정적이다 (20 5장에 적었다).

**검증**

- test_ui_strings (로케일 꼴 · 찾는 차례 · 표 검사 · 인자 · "@" 글자) · test_ui_document "ui strings project" · test_game_ui_layer
  "ko · en" · test_user_settings "로케일". test_ui_layout 데모 골든은 ko 표의 글로 재고 사각형이 그대로다. 첫 실행에 모두 맞았다.
- sbx check samples/rabbit_guard — "문자열 표: 로케일 2 · 키 4", 오류 · 경고 0. SandboxClient 창에서 --locale en · OS (ko-KR) 를
  눈으로 확인 (MANUAL-QA v0.7 M4-T1). --locale english → 명령줄 오류.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19, -Wall -Wextra -Wpedantic -Wshadow) — 새 경고 없음.

**남은 일**

- M4-T1 (2): client ctx.ui.text (Host API) · 행동 · 게임 모드 표시 이름 "@<키>" · 실행 중 로케일 바꾸기. macOS CFLocale.
- 복수형 · 성별 (v0.6 이후 — ICU 류 의존 결정), 도구 문자열 번역 (v0.9).

## 2026-11-09 — v0.7 M3-T6: 게임 UI 테마 · 배율 — styles.json themes · theme 버튼 · 테두리 배율 · 배율 2 골든

**무엇을**

- ui/model: UiStylePatch (적은 칸만 — color · border · fontSize · padding) · UiTheme · applyTheme. styles.json 에 선택 키 "themes"
  {"<팩>.<테마>": {"<스타일>": 덮기}} (이름 U2 · 모양 U1 · 키 U3). 버튼 이벤트 ui "theme" (target = 테마 · "none").
- 클라이언트 GameUiLayer: 바탕 스타일 + 테마 표, setTheme / theme 이벤트 → 테마를 씌운 표로 다음 배치 · 그리기. ui/render 테두리 두께 × 배율.
- 프로젝트 검사: 없는 스타일을 덮는 테마 = U5 경고, 없는 테마로 바꾸는 버튼 = U6. 데모 rabbit_guard styles.json 에 eco.night.

**왜**

- 등록부 M3-T6 (v0.7 P1) — "테마를 바꾸면 같은 배치에 다른 색", 배율 배치 골든.

**검증**

- test_ui_document "ui themes" · test_ui_render "ui theme" (같은 사각형 · 다른 색 · 테두리 × 2) · test_ui_layout "배율 2" (데모 HUD 8 사각형 —
  손 계산, 버튼 위 · 아래 26.5 → 27 반올림) · test_game_ui_layer "테마 버튼". 첫 실행에 모두 맞았다. sbx check samples/rabbit_guard — 오류 · 경고 0.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19) — 새 경고 없음.

**남은 일**

- 사용자 UI 배율 설정 (M4-T4 — 지금 클라이언트는 1), 테마를 고르는 사용자 설정 · client 스크립트 (필요해지면 Host API).

## 2026-11-05 — v0.7 M3-T5: 게임 UI 버튼 이벤트 마무리 — 행동 자리 · session:message · 서버까지 E2E

**무엇을**

- 버튼 행동의 자리: 첫째 선택 개체 → 내 개체 (game.owner — ownedEntity 로 묶음) → 화면 가운데 (IWorldSession uiActionPoint).
  위치 없는 행동을 시뮬레이션에 더하지 않고 클라이언트가 자리를 고른다 (kSimVersion · 골든 그대로).
- 바인딩 "session:message" — 이 클라이언트가 보낸 PlayerAction · PlayerSignal 의 마지막 결과 ("<명령> 적용 (tick N)" · 거절 문장).
  U4 검사가 받는 session 칸에 더함.
- M3-T3 의 signal · action · ui 이벤트를 서버 스크립트까지 잇는 시험.

**왜**

- 등록부 M3-T5 (v0.7 P0) — "버튼이 게임 규칙을 부른다": 버튼 → PlayerSignal → 서버 이벤트 · 거절이 화면에 보이는 길.

**검증**

- test_game_ui_layer "버튼 → PlayerSignal" (앱 + 로컬 서버 + ImGui 헤드리스): signal 버튼 → 서버 스크립트 onEvent 로그
  "signal eco.ring 3 <플레이어>" · session:message 적용, 선언 없는 신호 → "PlayerSignal 거절" · 서버 이벤트 없음, 선택한 개체의 자리 =
  uiActionPoint · range 0.25 행동 적용. 첫 실행에 모두 맞았다.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19) — 새 경고 없음.

**남은 일**

- 진짜 위치 없는 행동 (대상 = 내 개체 · 선택 — 시뮬레이션 계약, 필요해지면 ADR), 버튼 누름 뒤 결과를 기다리는 동안의 표시 (끄기 · 다시 누름 막기).

## 2026-11-04 — v0.7 M3-T4 (2): client 스크립트 ctx.setUi · setUiText · showUi — Host API 더하기 (M3-T4 끝)

**무엇을**

- Host 표 끝에 client.setUi(key, number) · client.setUiText(key, text) · client.showUi(doc, visible) (Capability Ui — onUpdate 의 허락에
  더함). 2.5 의 ctx.ui.set · show 를 평평한 이름으로 (Host ABI 에 중첩 객체가 없다). ClientScriptHost 가 프레임마다 64 개까지 모으고
  트랩난 호출의 것은 버린다 (신호 · 소리와 같은 규칙). NetworkSession 이 var 값을 세션 표에, 문서 보이기를 takeUiShows 로 앱에 →
  GameUiLayer.apply.
- Host API 지문 0x1dfa888c39154dde → 0x43ee8d9ef79b762a (더하기만 — 버전 1 그대로), sdk/ · samples 셋의 sdk/sandbox.d.ts 다시 생성 (sbx sdk).

**왜**

- 등록부 M3-T4 의 나머지 — client 스크립트가 HUD 값을 정한다 (2.5).

**검증**

- test_client_script_session "ctx.setUi" (오버레이로 더한 client 시스템 — var 수 · 글자 · 보이기 하나, 뒤 시스템의 트랩 호출 값은 버림),
  test_checker (지문) · test_client_scripts (항목 18) · test_runtime_contract (Ui 허락) · test_script_host (커밋된 SDK = 생성).
- 시험이 찾은 것: 시험 스크립트의 == (S311 — === 를 쓴다) 로 시스템이 실리지 않았는데 ns.overlayError() 는 비어 있었다 — sbx check 로
  찾았다. client 스크립트 컴파일 오류가 오버레이 오류 줄에 보이지 않는 것은 따로 남은 일.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19) — 새 경고 없음.

**남은 일**

- 오버레이로 더한 client 스크립트의 컴파일 오류를 overlayError · 콘텐츠 패널에 (지금은 조용히 실리지 않는다).
- ctx.ui.text (문자열 표 — M4-T1), 0.7.0 출시 노트에 Host API 지문 · 새 함수.

## 2026-11-04 — v0.7 M3-T4 (1): 게임 UI 바인딩 평가 · 클라이언트 데이터 — var · session · component@owned|selected

**무엇을**

- ui/binding: UiScalar (수 · 글자 · 참거짓 · 모름 — 표시 글자: 정수면 정수, 아니면 최단 십진, 모름 "-"), IUiDataSource, evaluate ·
  formatText ("{n}" → 인자 값) · progressOf (value / max, 0 ~ 1) · listCountOf (0 ~ 256).
- 클라이언트: IWorldSession uiData() — NetworkSession 의 UiData: session tick · time · state · team · score[i] (게임 세션 · 복제 틱),
  component@owned (game.owner == 내 clientId — 명령 issuer 와 같다, netId 가 가장 작은 개체) · @selected (첫째 선택) — 카탈로그 getNumber,
  var (setUiVar — 256 키). GameUiLayer.setData — 글자 · progress · list 수가 데이터를 쓴다 (M3-T3 의 "-" 대신).

**왜**

- 등록부 M3-T4 (v0.7 P0) — "HUD 가 점수 · 보관함 값을 따라간다". (2) 는 client 스크립트 ctx.ui.set · show (Host API).

**검증**

- test_ui_binding 3 · test_game_ui_layer "ui data (local server)" — 행동 (spawn owner: true) 으로 만든 내 토끼의 기운이 @owned 로 보이고,
  @selected 가 복제 월드에서 직접 읽은 값과 같다. 첫 실행에 모두 맞았다.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19) — 새 경고 없음.

**남은 일**

- (2) client 스크립트 ctx.ui.set(key, value) · ctx.ui.show(docId, bool) — Host API 더하기 (지문 · SDK · 샘플 sdk/ 다시 생성).
- list 항목마다 다른 글자, 바뀐 것만 평가 (측정 뒤), entity:<netId> 대상.

## 2026-11-04 — v0.7 M3-T3: 게임 UI 그리기 · 입력 — ImGui 배경 그리기 목록 · 라우팅 · 버튼 이벤트 (화면 확인)

**무엇을**

- ui/render drawLayout → IUiRenderer (panel · list 색 · 테두리, 버튼 채움 · 누름 밝게 · 글자, progress 바탕 + 채움 0 ~ 1, image).
  ui/input routePointer — modal (보이면 전부) → menu (root 안) → 도구 패널 (ImGui wantMouse) → hud 버튼 → 게임, 같은 층은 뒤 문서가 위.
  배치에 숨긴 노드 (UiLayoutOptions.hidden — 자리도 빠짐), UiLayoutNode.padding (배율 곱한 여백).
- 클라이언트: IWorldSession gameUi() (팩 presentation/ui 원문) · playerSignal(), GameUiLayer (원문 지문으로 다시 읽음 · 읽지 못한 문서
  경고 한 번 · 배치 · 라우팅 · 버튼 이벤트 · show / hide / toggle · 누름 표시 · "{n}" 자리), ImGuiUiRenderer · ImGuiTextMeasure (배경
  그리기 목록 — 게임 화면 위 · 도구 패널 아래, 논리 좌표). Application: ImGui beginFrame 뒤 routeGameUi — 잡으면 게임 입력에서 마우스를
  뺀다, signal → PlayerSignal 명령 · action → 화면 가운데 PlayerAction.

**왜**

- 등록부 M3-T3 (v0.7 P0) — 완료 조건 "데모 HUD 가 게임 화면에 보인다".

**검증**

- test_ui_render 3 · test_game_ui_layer 3 (앱: 로컬 서버 + ImGui 헤드리스 — HUD 버튼 클릭이 PlayerAction "적용", 배치 툴이어도 배치 0).
- 시험이 찾은 것: 앱 시험의 첫 판은 클릭이 가지 않았다 — 진단해 보니 그 자리에 ImGui "행동" 창이 있어 ImGui wantMouse = 참,
  차례 (도구 패널이 hud 보다 먼저) 대로 동작한 것이었다. 시험은 F1 로 도구 패널을 감추고 누른다 (라우팅 자체는 ui 시험이 본다).
  ImGui 1.92.8 의 AddRect 인자 차례 (thickness · flags 바뀜 — 옛 차례는 delete) 도 빌드가 잡았다.
- 화면 (MANUAL-QA v0.7 M3-T3): SandboxClient --world samples/rabbit_guard → F1 — 위쪽 띠 (그림 · "토끼 - · 점수 -" · 기운 막대 · 두 버튼 ·
  ? · 도움말) 가 보이고, ? 로 도움말이 사라졌다 다시 보이고, 먹이 주기 → 서버가 "게임이 진행 중일 때만 (지금 Lobby)" 로 거절 (로그).
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄 — 처음 돌림에서 docs_consistency 가 22 의 [계획 v0.5 M3-T3] 줄을 잡아 고쳤다). clang-tidy (LLVM 19) — 새 경고 없음.

**남은 일**

- 바인딩 평가 · 데이터 소스 (M3-T4 — "-" 대신 값), image 텍스처, 위치 없는 행동, 창 크기 · DPI 확인 (MANUAL-QA 두 줄), menu 와 ImGui 창이
  겹칠 때 둘 다 반응 (ImGui 가 이벤트를 먼저 받는다).

## 2026-11-04 — v0.7 M3-T2: 게임 UI 배치 엔진 — 앵커 · 쌓기 · 맞춤 · 배율 · 자름 (헤드리스 골든)

**무엇을**

- ui/layout layoutDocument: 가장자리 = 부모 앵커 위치 + offset · 배율 (네 가장자리 같은 식 — 20 2.3 의 예 [8, 8, -8, 48] = 늘린 띠 높이 40),
  size 는 먼 가장자리 대신, 없으면 내용 (ITextMeasure — 글자, 쌓은 자식, list 항목). 쌓는 부모 (row · column · list) 는 차례 + gap +
  align (start · center · end · stretch). 여백 = panel · 스타일 padding 중 큰 쪽, 글꼴 = 스타일 fontSize (16) · 배율 (0.5 ~ 4 로 자름).
  가장자리마다 floor(v + 0.5), clip = 조상 사각형의 교집합, 결과 4,096 노드까지. 글자 · list 항목 수는 콜백 (바인딩은 M3-T4).
- FixedTextMeasure (코드 포인트 × advance × 글꼴 — 헤드리스 · 시험).

**왜**

- 등록부 M3-T2 (v0.7 P0) — 그리기 백엔드 (M3-T3) · 입력 판정이 같은 사각형을 쓴다. 같은 입력 = 같은 사각형 (헤드리스 시험 · 서버 없음).

**검증**

- test_ui_layout 6 — 기대 사각형은 모두 손으로 계산했다 (글꼴 16 = 글자 8 × 20): 위쪽 띠 · align 넷 · 배율 2 · 창 크기 · 점 앵커 둘 ·
  반올림 (101 → 51 + 50) · 긴 글자 clip · 겹친 panel 맞춤 · 한글 · list · 상한 · 데모 HUD 1280 × 720 8 사각형. 첫 실행에 모두 맞았다.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (경고 0 줄). clang-tidy (LLVM 19) -Wall -Wextra -Wpedantic -Wshadow — 새 경고 없음.

**남은 일**

- 스크롤 (list), 줄 바꿈 (글자는 한 줄), 노드 visible. 그리기 · 입력 라우팅 M3-T3, 바인딩 평가 M3-T4.

## 2026-11-04 — v0.7 M3-T1: 게임 UI 타깃 SandboxGameUI — 문서 모델 · 검사 U1 ~ U8 · sbx check 연결

**무엇을**

- 새 타깃 SandboxGameUI (ui/ — Foundation 만, check_includes 규칙 · 픽스처 ui/BadCore): ui/model UiDocument — 팩의 presentation/ui/<이름>.json
  (panel · text · image · button · progress · list, 앵커 · offset · size · stack · gap · padding · align, 값 = 수 | bind, 이벤트 = signal ·
  action · ui) 와 styles.json 을 읽고 U1 ~ U8 을 낸다. 콘텐츠는 UiCheckContext (nullptr = 그 검사를 건너뜀).
- core/content 로더는 원문만 (PackInfo.uiDocuments · uiStyles — 팩마다 32). core/project validateProject 가 검사를 부른다 — 신호 (signals) ·
  행동 (playerActions) · 스타일 · material (팩 materials.json + ValidateOptions.engineAssets), 문서 사이 ui 대상 (U6), 게임 예산 (문서 32 ·
  노드 4,096 — U7). openProject 는 보지 않는다. sbx check 는 실행 파일 옆 assets/ 를 엔진 에셋으로 주고 "게임 UI: 문서 N · 노드 M" 줄.
- ADR-0101 (Core 가 GameUI 를 링크 — 검사의 단일 진입점), 데모 HUD samples/rabbit_guard/content/ecosystem/presentation/ui (eco.hud ·
  styles — 점수 · 기운 · 먹이 · 덫 · 도움말 토글).

**왜**

- 등록부 M3-T1 (v0.7 P0, 20 10장 Phase 3) — 제작자가 HUD · 메뉴 · 버튼을 만드는 첫 단계. 배치 (M3-T2) · 백엔드 (M3-T3) 가 이 모델 위에 선다.

**검증**

- test_ui_document 7 (모양 · U1 ~ U8 규칙마다 · 스타일 · 바인딩 식 20 · 퍼즈 3,000 · 데모 HUD 통과 · 문제 문서는 검사만 막음),
  arch_include_lint (582 → 0 위반) · selftest 12.
- 처음 판의 결함 (시험이 잡음): U6 행동을 db.actions() (Rule 의 action 이름) 로 모아 데모의 eco.feed · eco.trap 이 "없다" 였다 →
  playerActions () 의 id.
- 실행: sbx check samples/rabbit_guard → "게임 UI: 문서 1 · 노드 8" · 오류 0 · 경고 0, 복사본에 없는 스타일 · material → U5 · U8 경고.
- Windows MSVC Debug 빌드 · 전체 ctest 79 / 79 통과 (unit_ui 더함, 경고 0 줄). clang-tidy (LLVM 19) 로 바뀐 .cpp 5 개 -Wall -Wextra -Wpedantic -Wshadow — 새 경고 없음.

**남은 일**

- 컴포넌트 칸 존재 (M3-T4 — 바인딩 데이터 소스와 함께) · 문자열 키 존재 (M4-T1) · 노드 visible · theme. 배치 M3-T2 다음.

## 2026-11-04 — v0.6 종료: 0.6.0 — 릴리스 노트 · 기준 판정 · 제품 버전

**무엇을**

- docs/releases/v0.6.0.md: 들어간 것 (NG-12 · NG-13 · M6-T9 · NG-01 · NG-14 · NG-04 · RT-06-12 · NG-08 · NG-02 · 24 P7), 호환 번호 (프로토콜
  15 → 16 · .sbc 9 → 10 · .sbpkg 2 → 3 (에셋 있을 때) · 매니페스트 migrations 선택 키, 시뮬레이션 · 세이브 · Host API · 잠금 · 리플레이 그대로),
  release-gates v0.6 행 판정 (항목마다 시험 이름), 계획 대비, 알려진 문제, 데모 · 재현.
- CMake project VERSION 0.6.0, README · docs/README · 18 · 23 · roadmap 의 지금 · 다음 줄, versioning · 버전 매트릭스, 21 · 22 의 낡은 번호.

**왜**

- releases.md v0.6 "태스크" 줄이 모두 ✅ (등록부 목표 v0.6 인 미완료 태스크 없음) — 버전이 끝날 때 제품 버전을 올린다 (18 4장).

**검증**

- 골든 · 콘텐츠 그대로 (`git diff 8613d9e -- tests/golden content` 비어 있음) — 시뮬레이션 동작 그대로, 제품 버전만 바뀌었다.
- 판정 중 바로잡음: 0.5 엔진은 migrations 를 쓴 매니페스트를 모르는 키 (P1) 로 거절한다 (처음 초안은 "거절하지 않는다" 였다 — 코드로 확인).
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄), sbx --version = 0.6.0.

**남은 일**

- 태그 · GitHub 릴리스 (배포 묶음 업로드) 는 하지 않았다 — 사용자가 정한다. NG-04 의 서버 수준 적용 중 트랩 시험, 실기 QA (verifiedTargets).

## 2026-11-04 — CI 고침: BuildOptions 의 지정 초기화 경고 (GCC · Clang -Werror)

**무엇을** package::BuildOptions 의 멤버에 기본 초기화 ({}) — `{.profile = "win"}` 처럼 일부만 지정하면 GCC
-Wmissing-field-initializers · Clang -Wmissing-designated-field-initializers 가 나머지 (std::function) 를 경고했다.

**왜** 6fd3cc4 (NG-08) 의 CI 가 Linux 세 잡에서 빌드 실패 — Windows MSVC 는 이 경고가 없어 로컬에서 놓쳤다.

**검증** VS 의 clang-tidy (LLVM 19) 로 바뀐 .cpp 14 개를 -Wall -Wextra -Wpedantic -Wshadow 로 돌려 그 경고 0 (같은 모양의 시험 파일로
잡히는 것을 먼저 확인), Windows MSVC Debug unit_project · unit_tools · project_assets · project_package 통과.

**남은 일** 로컬에서 GCC · Clang 경고를 보는 길 (지금은 clang-tidy 로 대신). 규칙은 12 리뷰 체크리스트에 한 줄 더했다.

## 2026-11-04 — v0.6 24 P7: 악성 PNG — 디코드 전 파일 64 MiB · 헤더 가로 · 세로 4096

**무엇을**

- render loadPng: 읽기 전 파일 크기 (64 MiB — stb 는 int 길이), 디코드 전 stbi_info 로 헤더의 가로 · 세로 (4096) — 넘으면 OutOfRange.
  4096² × RGBA = 64 MiB 가 한 이미지의 최대 메모리. 실패는 지금처럼 그 스프라이트만 마젠타 (AssetManager).

**왜**

- release-gates v0.6 행의 P7 (악성 PNG). 작은 파일이 거대한 크기를 적는 압축 폭탄: stb 자신은 1 GiB 넘는 것만 막아, 16000² (1 GB) 를
  적은 PNG 하나가 Worker 에서 1 GB 를 잡을 수 있었다. 24 P7 의 "디코드 4096² 상한" 은 [계획 v0.23] 이었다 — 출시 기준이라 당겼다 (퍼즈는 그대로 RH-23-1).

**검증**

- test_image "huge size is refused before decode": IHDR + 빈 IDAT 의 16000² PNG = OutOfRange (처음 판은 IDAT 이 없어 stb 의 머리 훑기가
  실패해 디코드 쪽 ParseError 로 갔다 — stb 는 첫 IDAT 까지 훑는다), 4096 × 1 받음 · 4097 × 1 거절, 64 MiB 넘는 파일은 읽기 전 거절.
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).

**남은 일**

- 퍼즈 · 픽스처 말뭉치 (RH-23-1 v0.23). 아틀라스보다 큰 스프라이트는 지금처럼 "가득" 마젠타.

## 2026-11-04 — v0.6 NG-02: Asset Group — assets/groups.json · .meta labels · 빌드 보고 (A7 · A8)

**무엇을**

- .meta 선택 키 labels ([a-z0-9_-] 1 ~ 32, 16 개, 정렬 · 겹침 없음 — 없으면 .meta 바이트 그대로). AssetEntry.labels = 사본들의 라벨 합.
- core/asset/AssetGroups: assets/groups.json {"groups": [{name, include: [glob · "label:<라벨>"]}]} (A7 — 모양 · 이름 · 겹침 · include),
  assets/ 기준 glob (* 폴더 안 · ** 폴더 여럿 · ?), materialAssetIds (정적 참조), summarizeAssetUsage (그룹마다 개수 · 크기, 고르는 것
  없는 include 경고, A8 어디서도 부르지 않는 에셋 — 경고 하나). groups.json 은 에셋이 아니다 (훑기 · 가져오기 · 패키지가 건너뛴다).
- 프로젝트 검사 (validateProject 만 — 게임 열기는 막지 않는다): A7 · A8 · 그룹 줄. sbx build: groups.json 오류면 멈춤, 보고 (그룹마다 ·
  부르지 않는 에셋 — 그래도 싣는다). Export Profile 의 assetGroups 는 있는 그룹만 (ExportCheck.groups — 패키지 프로필을 파일과 견줄
  때는 groups.json 이 없어 보지 않는다). 푼 패키지의 검사 (toolFiles = false — sbx check <패키지>) 는 groups.json 을 싣지 않았으므로
  그룹 · A8 · 프로필의 그룹 이름을 보지 않는다 (처음 판은 패키지 검사에 A8 · P10 이 섞였다 — project_assets 가 잡았다).
- 이 커밋에 NG-08 (6fd3cc4) 파일의 clang-format 정리도 들었다 (동작 변화 없음).

**왜**

- 등록부 NG-02 (ADR-0097 2, 2차 감사 4.2) — 스크립트가 이름으로 만드는 참조는 따라갈 수 없어 선언이 필요하고, 쓰지 않는 에셋을 알 길이
  없었다. v0.6 범위는 그룹 · 라벨 · 보고 — 패키지는 지금처럼 모든 에셋을 싣는다 (그룹만 싣기 · 원격 묶음은 RT-08-4).

**검증**

- test_asset_groups 4 (labels · glob 9 · A7 12 · 프로젝트 그룹 · 크기 · A8 · groups.json 은 에셋 아님), test_game_package "Asset Group"
  (보고 · 싣기 · 검증 · validateProject · 푼 패키지 검사는 그룹 · A8 없음 · 프로필 그룹 · 깨진 groups.json 은 빌드만 멈춤),
  test_export_profile (있는 · 없는 · 보지 않는 그룹), project_assets (가져온 뒤 A8 경고 하나 → groups.json → 경고 0 · 그룹 줄 · 빌드 보고 ·
  패키지 검사 경고 0).
- 실행: sbx new → assets/enemies/slime.png · misc/old.png · groups.json → sbx asset import (2) → sbx check "그룹 enemies — 1 개 · 75 바이트"
  + A8 (old.png) rc 0 → sbx build 보고 "에셋 그룹 enemies — 1 개 · 75 바이트" · 부르지 않는 에셋 1 경고.
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).

**남은 일**

- 그룹으로 고른 것만 싣기 · 원격 묶음 · 실행 중 그룹 밖 이름 진단 (스크립트 에셋 API) — RT-08-4. Asset Browser 의 라벨 편집 (ST-06-3).
- v0.6 의 태스크는 끝났다 — 0.6.0 출시 (버전 · 출시 노트: 프로토콜 16 · .sbc 10 · .sbpkg 3 · index profile · 더 새 세이브 거절).

## 2026-11-04 — v0.6 NG-08: Export Profile — export/<이름>.json 검사 P10 · sbx build --profile · 다른 플랫폼 경고

**무엇을**

- core/project/ExportProfile: export/<이름>.json {platform (windows · linux · macos), arch (x64 · arm64), renderBackends (d3d12 · vulkan ·
  metal — 하나 이상, 앞 = 먼저), assetGroups?}. 오류 = 모양 · 모르는 키 · 플랫폼이 낼 수 없는 백엔드 · 없는 Asset Group (NG-02 전), 경고 =
  실기 기록 없는 대상 "미검증" (verifiedTargets — docs/qa/MANUAL-QA.md 에 통과 기록이 없어 비었다) · .json 아닌 파일. hostTarget (전처리기).
- 프로젝트 검사 (validateProject — sbx check · doctor) 가 export/ 를 모두 본다 (P10, "Export Profile: N"). openProject 는 보지 않는다 —
  내보내기 설정이 게임 실행을 막지 않게.
- 패키지: BuildHooks → BuildOptions {profile, beforeRead, afterRead}. --profile = 그 프로필만 검사 (없음 NotFound · 오류 = 만들지 않음),
  파일을 스냅숏에 싣고 index 선택 키 profile {name, platform, arch, renderBackends}. 형식 번호와 무관 (옛 엔진은 모르는 키를 지나친다 —
  형식 2 그대로). 읽을 때 그 파일이 있어야 하고, 파일을 볼 때 프로필을 다시 검사해 대상이 같아야 한다 (index 만 고친 패키지 거절).
- 다른 플랫폼 · 아키텍처에서 열면 경고 (package::profileHostWarning — sbx check <패키지> · WorldSource → 서버 · 클라이언트 로그). 막지 않는다.

**왜**

- 등록부 NG-08 (ADR-0097 6, 2차 감사 4.8) — 제작자가 어느 대상을 내보내는지 적을 곳이 없었고, 검증되지 않은 플랫폼을 알 길이 없었다.

**검증**

- test_export_profile 4 (대상 · 미검증 경고 · 오류 17 · 왕복 · 폴더 · hostTarget), test_game_package "Export Profile" (프로필 없는 빌드 = 같은
  digest · P10 · --profile · 고친 대상 · 없는 파일 · 없는 · 오류 프로필 · 다른 플랫폼 경고), test_sbx_options build.
- 실행: sbx new → export/win.json · mac.json (vulkan — P10 오류 rc 1) → mac 을 metal 로 → build --profile win · mac (미검증 경고) · nope
  (NotFound rc 2) → sbx check mac.sbpkg "이 패키지는 macos-arm64 용으로 내보냈다 … 지금 windows-x64" → SandboxServer --world mac.sbpkg
  --ticks 2 --exit 가 같은 경고 (warn) 로 tick 2.
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).

**남은 일**

- textureMaxSize · 플랫폼별 텍스처 변환 · 압축 (측정 뒤), Studio 빌드 화면의 프로필 고르기 (v0.8), 실기 QA 기록이 생기면 verifiedTargets.
- 0.6.0 출시 노트에: .sbpkg index profile 선택 키.

## 2026-11-04 — v0.6 RT-06-12: 비동기 에셋 로드 — 로드 순서 무관 표시 확인

**무엇을**

- AssetManager 의 비동기 로드 (requestSprite → 바로 SpriteId · 흰색 자리, Worker 디코드, 프레임 예산 업로드, 같은 경로 = 같은 id) 는
  Phase 8A 에 있었다. 등록부의 완료 조건 "로드 순서 무관 표시" 를 GPU 테스트로 묶었다 — 코드 변경 없음.
- test_sprites "load order does not change the picture": 빨강 6×6 · 초록 8×8 · 파랑 4×4 PNG 를 앞 · 뒤 차례로 요청 (JobSystem 2 · 업로드
  예산 1 바이트 = 프레임마다 하나라 아틀라스 자리가 다르다) → 셋 다 Ready → 같은 RenderWorld 의 두 그림이 바이트까지 같다.

**왜**

- 등록부 RT-06-12 (v0.6, P2) — RT-08-4 (스트리밍 · 메모리 예산) 의 앞 단계. 순서에 따라 그림이 바뀌면 스트리밍이 결정적 화면을 깬다.

**검증**

- Windows MSVC Debug 빌드 · 전체 ctest 통과 (render_tests_warp 포함). 처음 판은 stats().ready 가 내장 자리 (흰색 · 마젠타) 를 세어 5 ≠ 3
  — 요청한 id 마다 state == Ready 로 바꿨다.

**남은 일**

- 해제 · refcount · 핫 리로드 · 디스크 캐시는 RT-08-4 (v0.8) 와 06 7.4 의 [계획].

## 2026-11-04 — v0.6 NG-04 (2): 세이브 게임 버전 마이그레이션 — migrations · 체인 · 백업 · 더 새 세이브 거절 (NG-04 끝)

**무엇을**

- 매니페스트 migrations [{from, to, system}] (schemaVersion 3 의 선택 키 — network 처럼 번호를 올리지 않음): SemVer from < to, 같은 from 없음,
  system = 스크립트 시스템 id, from 순으로 기록 (모양 P1 · 버전 P3). 프로젝트 검사 P9: 시스템이 없거나 onMigrate 가 없음 (오류) · to 가 게임 버전 뒤 (경고).
- core/scenarios/SaveMigration: planSaveMigration (같음 · 더 새 세이브 VersionMismatch · 옛 세이브 + 단계 없음 = 알림 (지금까지) · 체인 — 빠진 ·
  넘어가는 단계 NotFound) · backupSave (<이름>.bak-<세이브 버전> 재귀 복사 · 처음 것을 덮지 않음) · applyMigration (단계마다 onMigrate).
- 서버의 세이브 불러오기 (ServerHost WorldOp Load): 게임 버전이 다르면 계획 → (단계가 있으면) 백업 → 열기 → 단계 적용 → 바꿔 끼우기. 실패하면 연 월드를
  버린다 (지금 월드 · 세이브 파일 그대로). 버전이 SemVer 가 아닌 옛 세이브는 지금까지처럼 열고 알린다. ServerHostDesc.migrations (setGameProject).

**왜**

- ADR-0098 2 · 2차 감사 SG-09 — 게임이 0.1 → 0.2 로 바뀌면 세이브를 옮기는 길이 없었고, 더 새 게임의 세이브를 옛 게임이 조용히 열었다.

**검증**

- test_save_migration (계획 7 경우 · 백업 3 경우), test_game_project (migrations 왕복 · 오류 8 · P9 4), test_script_callbacks (체인 · 실패 단계 이름 ·
  같은 세이브 두 번 + 같은 체인 = 같은 해시, 10 틱 뒤도), test_world_ops (더 새 세이브 거절 · 빠진 단계 = 세이브 파일 · 백업 없음).
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).
- 바로잡기: 앞 항목 (project_own_png 가 "돌지 않았다") 은 CI 시간만 보고 한 추정이었다 — 고친 판도 "OK" 줄과 함께 0.21 ~ 0.55 초로 통과한다 (Linux
  Release + lavapipe 는 그만큼 빠르다). 앞 판이 돌았는지는 확인할 수 없다. "OK" 줄 통과 조건은 그대로 둔다 (실제로 돈 것을 보장).

**남은 일**

- 서버 시작 (--world <프로젝트>) 의 시작 월드는 프로젝트의 것이라 마이그레이션하지 않는다. 클라이언트 단독 (로컬) 세션의 불러오기도 같은 ServerHost 길.
- 0.6.0 출시 노트에: 프로토콜 16 · .sbc 10 · .sbpkg 3 · 더 새 게임의 세이브 거절.

---

## 2026-11-04 — v0.6 NG-04 (1): 스크립트 onMigrate — 세이브 마이그레이션 콜백 (.sbc 형식 10)

**무엇을**

- defineSystem 의 새 콜백 onMigrate(ctx) (server 도메인): 콜백 표 (Active, v0.6, 서버 Capability 묶음) · 검사기 (onMigrate 만 있는 시스템도 된다 ·
  client 시스템은 S426) · 코드 생성 · 바이트코드 시스템 표 (형식 9 → 10 — 옛 형식은 다시 컴파일) · 검증기 · 덤프 (있을 때만) · SDK d.ts · sbx check 요약.
- ScriptSystemAdapter::runMigration(world, tick, id) · SimulationWorld::runScriptMigration(id) — 단계 밖에서 한 번 (틱 연료 하나 · 트랩 정책 그대로 —
  트랩 · 연료면 쓰기를 버리고 오류), 구조 변경까지 적용, 틱은 진행하지 않는다. 틱 · 17d 에서는 부르지 않는다.
- SDK 다시 만듦 (sdk/ · samples 셋 — onMigrate 두 줄).

**왜**

- NG-04 (ADR-0098 2) 의 실행 길 — 게임 버전이 바뀐 세이브를 게임 스크립트가 결정적으로 고친다. 어느 세이브에 언제 부르나는 (2) 에서.

**검증**

- test_script_callbacks "onMigrate": 검사기 · 형식 10 왕복 · 틱에는 부르지 않음 (다른 콜백이 더한 값 그대로) · 한 번 (+100 · 틱 그대로) · 트랩이면 오류와 쓰기
  버림 · 없는 · onMigrate 없는 시스템 오류. test_runtime_contract (Active 여섯) · test_script_systems (메서드 목록 문장) 갱신.
- 처음 판의 실수: 시험 스크립트에 i32 타입 (S401 — 쓸 수 없는 이름), 같은 팩의 다른 시스템이 값을 더한 것을 기준선 없이 비교 → 고침.
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).

**남은 일**

- NG-04 (2): 매니페스트 migrations [{from, to, system}] · 세이브의 게임 버전과 견줘 체인 · 빠진 단계 거절 · 더 새 게임의 세이브 거절 · 적용 전 백업 ·
  실패면 원본 그대로 — 서버의 세이브 불러오기에. (3) E2E.
- .sbc 형식 번호: M6-T4 (줄 표) 는 이제 11.

---

## 2026-11-04 — v0.6 NG-14: 시뮬레이션 해시 · 표시 digest 분리 — 프로토콜 16, 표시만 다르면 접속 + 경고

**무엇을**

- 콘텐츠 로더가 팩마다 presentation/** 의 지문 (PackInfo.presentationDigest — FNV-1a64, 상대 경로 순 · 링크 건너뜀) 을 남긴다 (contentHash 밖 그대로).
- scenario::presentationDigest(content, projectDir) — 팩마다 (로드 순) id · 지문 + 프로젝트 에셋 (경로 · id · SHA-256 · id 없는 원본). openWorldSource 가
  WorldSource.presentationDigest 로.
- 프로토콜 15 → 16: Auth · Welcome 끝에 presentationDigest u64. 서버 (SandboxServer · LocalServerHost) 는 ServerHostDesc 로 받아 Welcome 에 싣고, Auth 의
  값이 다르면 기록만 (info) 하고 들여보낸다. 클라이언트 (NetworkSession) 는 로컬이면 서버와 같은 값, 원격이면 자기 콘텐츠 (프로젝트 없음) 의 값을 보내고,
  Welcome 과 다르면 경고 "보이는 것이 서버와 다를 수 있습니다" · presentationMismatch(). contentHash 가 다르면 지금처럼 거절.

**왜**

- 2차 감사 SG-05 · ADR-0097 5 — 다른 그림 · 머티리얼을 가진 클라이언트가 조용히 다른 화면을 봤다 (M6-T9 로 게임이 자기 그림을 갖게 되어 원격 클라이언트는
  asset: 참조가 마젠타). 시뮬레이션이 같으면 막을 이유는 없고, 알려야 한다.

**검증**

- test_messages: 프로토콜 16 · Auth · Welcome 왕복. test_network_session: 원격 — 서버와 같은 digest = 경고 없음, 다른 digest = 들어가고 플래그.
  test_pack_materials: digest 가 presentation 의 아무 파일 · 같은 id 다른 그림에 바뀌고 contentHash 는 그대로, 프로젝트 없는 값은 다르다.
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).

**남은 일**

- 원격 클라이언트가 게임 패키지 (그림) 를 받는 길 — 지금은 경고만 (패키지 배포 · Player 는 v0.8 NG-03 · v0.15).
- 0.5 (프로토콜 15) 와는 접속하지 않는다 — 다음 출시 노트 (0.6.0) 에 적는다.

---

## 2026-11-04 — 시험 고침: project_own_png 가 Linux CI 에서 실제로 돌지 않았다 (명령줄 목록이 갈라짐) · 통과 = "OK" 줄

**무엇을**

- project_own_png: CLIENT_CMD 를 세미콜론 목록 (`xvfb-run;-a;-s;…;SandboxClient`) 으로 -D 에 넘겼더니 add_test 가 목록을 따로 인자로 갈라
  cmake 가 스크립트를 돌리지 않고 0 으로 끝났다 — CI 에서 "통과 0.26 · 0.82 초" (만들기 + 빌드 + 150 프레임 창이 그 시간에 될 수 없다).
  XVFB · CLIENT 를 따로 받고 명령 목록은 스크립트 안에서 만든다. 스크립트 끝에 "project_own_png OK", CTest 는 PASS_REGULAR_EXPRESSION 으로 그 줄만 통과로.
- project_assets 에도 같은 "OK" 줄 · 통과 조건 (보험 — CI 의 0.32 · 1.49 초는 Release 에서 그럴 만하지만).

**왜**

- 57a3cb5 의 CI 기록을 읽다가 시간이 맞지 않아 찾았다. 실행하지 않은 시험이 통과로 보였다 — 고치기 전까지 Linux 의 자기 PNG E2E 는 확인된 적이 없다.

**검증**

- 고친 스크립트를 이 PC 의 실제 창으로: "에셋: … 프로젝트 파일 1 (준비 1 · 실패 0)" · "project_own_png OK". project_assets 통과 (5.7 초).
- Linux 에서의 실제 실행은 이 커밋의 CI 에서 처음 — 결과를 다음 항목에 적는다.

**남은 일**

- 다른 cmake -P 시험에도 "OK" 줄을 둘지 — 지금 CI 시간으로는 모두 실제로 돈다 (tech_alpha_e2e 72 초 · release_bundle 13 초 …).

---

## 2026-11-04 — v0.6 NG-01: 빌드 스냅숏 — 검사한 것 = 실은 것, 빌드 중 바뀐 파일 거절, 옛 출력 · 임시 정리

**무엇을**

- buildPackage (core/package): 입력 파일을 한 번 읽은 바이트가 이 빌드의 입력 (스냅숏).
  1. 그 바이트로 만든 패키지를 읽는 쪽 규칙으로 검증 (verifyBytes — index · SHA-256 · 팩 · 에셋 요약; verifyPackage · extractPackage 와 같은 함수로
     합침) — 훑은 뒤 · 읽기 전에 바뀐 에셋도 여기서 걸린다.
  2. 그 바이트만 임시 폴더 (.sbx-build-* — 범위를 벗어나면 지운다) 에 풀어 openProject (toolFiles 끔) — 첫 검사는 디스크에서 따로 읽었으므로,
     검사와 읽기 사이의 고침이 검사 안 된 채 실리던 틈을 막는다.
  3. 쓰기 직전 원본을 다시 해시 — 다르면 "빌드 중 바뀐 파일 '<경로>'" (옛 출력은 그대로 — 아직 쓰지 않았다).
- BuildHooks {beforeRead, afterRead} (시험 — 그 사이의 편집 흉내 · Studio 진행 표시).
- io::writeFileAtomic: 쓰기 실패에도 반쯤 쓴 .tmp 를 지운다 (지금까지는 이름 바꾸기 실패 때만).

**왜**

- 2차 감사 4.1 · ADR-0097 1 — 빌드가 파일을 두 번 (검사 · 싣기) 따로 읽어 그 사이의 저장이 검사 안 된 내용으로 실릴 수 있었다.

**검증**

- test_game_package "빌드 스냅숏": 읽은 뒤 저장 → 바뀐 파일 이름 · 옛 출력 바이트 그대로 · .tmp 없음, 검사와 읽기 사이 콘텐츠 깨뜨림 → 실을 내용 검사 실패,
  훑은 뒤 에셋 바꿈 → 만든 패키지 검증 실패, 바뀌지 않으면 만든다 · 임시 폴더 안 남음. 같은 바이트 (기존 시험) · tech_alpha_e2e · sample_stash ·
  project_reproduce 그대로.
- 비용 (14 7.16): sbx build 가 검사 한 번만큼 늘었다 (Debug — stdlib_tour ~ 120 ms, mana_regen ~ 400 ms). 바꾸기 전 실행 파일은 남기지 않아 sbx check 로 추정.
- Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).

**남은 일**

- v0.6: NG-02 Asset Group · NG-14 해시 분리 · NG-08 Export Profile · NG-04 SaveGame · RT-06-12.
- 큰 프로젝트 · Release 의 빌드 시간은 M6-T3 (빌드 화면) 전에 다시 잰다.

---

## 2026-11-04 — v0.6 M6-T9 (2): 게임의 자기 머티리얼 · 에셋을 클라이언트가 그린다 — asset:<id> · 팩 materials.json · A6 (M6-T9 끝)

**무엇을**

- 팩 presentation/materials.json (클라 전용 — 로더가 원문만 PackInfo.materials 에, contentHash 밖): 엔진 머티리얼 위에 팩 로드 차례로 (같은 이름은
  뒤가 이긴다). sprite = "asset:<id>" (core/asset 색인 → 프로젝트 파일, 옮겨도 그대로) · "assets/…" (프로젝트 기준, 대소문자 그대로) · 그 밖 (엔진 루트).
- render: AssetManager::requestSpriteFile(key, 파일) — 루트 밖 파일을 대소문자 그대로 연다 (requestSprite 는 정규화한 소문자 경로 — 06 9장, Linux 에서
  대소문자 섞인 이름을 못 찾는다). "project:" 이름공간, 빈 파일 = 실패 (마젠타), stats files · filesReady · filesFailed. MaterialLibrary 에 SpriteResolver.
  render 는 core 를 모른다 — 풀이는 apps/client/presentation/PackMaterials.
- LocalServerHost::projectDir (패키지면 푼 폴더) → NetworkSession (로컬) 이 팩 머티리얼을 읽는다. 창 클라이언트가 끝날 때 "에셋: … 프로젝트 파일 N
  (준비 · 실패)".
- 프로젝트 검사 A6: 팩 materials.json 이 부르는 없는 id · 파일 (대소문자 다름 · 밖 포함) = 오류 (빌드를 막는다 — 받는 쪽은 마젠타), 읽지 못하는 문서 = 경고.

**왜**

- M6-T9 완료 기준 "자기 PNG 를 넣은 게임이 다른 폴더 · 빈 캐시에서 보인다" (v0.6, ADR-0090 4 · 5).

**검증**

- test_pack_materials: 풀이 (id · 경로 · 없는 id · 밖 · 엔진 · 프로젝트 없음 · 이름 바꾼 뒤 id 그대로), 팩이 엔진 것을 덮음, 깨진 문서 경고, 로컬 세션
  (프로젝트 · 형식 3 패키지). test_sprites "project file" (WARP — 대소문자 섞인 파일 Ready · 같은 key · 이름공간 · 빈 경로 Failed · 머티리얼 풀이로 빨간 픽셀).
  test_asset_database A6. 처음 판은 시험 프로젝트를 엔진 0.6 범위로 만들어 (지금 엔진 0.5) 세션이 열리지 않았다 → 0.1.
- 실제 창 (이 PC — Win32 · D3D12): tests/project/project_own_png.cmake 를 SandboxClient 를 직접 주어 돌림 — A 프로젝트 → 패키지만 다른 폴더 → 빈 설정 ·
  캐시 · 임시 폴더로 열어 "에셋: 스프라이트 6 · 준비 6 · 실패 0 · 프로젝트 파일 1 (준비 1 · 실패 0)", "팩 머티리얼 1 개". CTest project_own_png 는 Linux
  (Xvfb + lavapipe) 에서만 등록 — Windows CI 는 창 시험이 없다.
- check_includes 0 위반. Windows MSVC Debug 빌드 · 전체 ctest 78 / 78 통과 (경고 0 줄).

**남은 일**

- v0.6 의 나머지: NG-01 빌드 스냅숏 · NG-02 Asset Group · NG-14 해시 분리 · NG-08 Export Profile · NG-04 SaveGame · RT-06-12 비동기 로드.
- 원격 접속 (--connect) 은 프로젝트 파일이 없어 asset: 참조가 마젠타 — 표시 digest (NG-14) · 패키지 받기와 함께.
- PNG 디코드 크기 상한 (4096²) 은 아직 없다 — 24 P7 [계획 v0.23 RH-23-1].

---

## 2026-11-03 — v0.6 M6-T9 (1): sbx asset import · newid · 게임 패키지 형식 3 (프로젝트 에셋 싣기)

**무엇을**

- sbx asset import <프로젝트> (id 없는 원본마다 .meta — 겹치지 않는 새 id) · sbx asset newid <프로젝트> <assets/경로> (복제본에 새 id — A3 고침).
  끝에 훑기 요약 · 문제.
- .sbpkg 형식 3: index "assets" [{path, id, sha256}] (원본마다 — 같은 id 사본도, 경로 순), 원본 · .meta 를 files 에 싣는다. 에셋이 없는 게임은 형식 2 그대로
  (0.5 엔진도 열고 바이트 · digest 같음 — 샘플 · tech_alpha_e2e 그대로). 읽을 때 목록 모양 · files 와 sha256 · .meta 있음 · 같은 id = 같은 내용 ·
  assets/ 아래 몰래 실은 파일 없음, 파일을 볼 때 (검증 · 풀기) .meta 의 id 와 목록을 견줌. 빌드는 훑기 오류 (A3 · A5) · id 없는 원본 (A1 · A2) 이면 거절,
  짝 없는 .meta (A4) 는 싣지 않는다.

**왜**

- M6-T9 의 첫 조각 — A 가 그린 그림이 패키지로 B 에게 간다 (ADR-0090 5). 받는 쪽이 id 로 찾으려면 실린 에셋마다 id 가 있어야 한다.

**검증**

- test_game_package (형식 2 · 3, 거절 · 사본 · 짝 없음 · 읽기 · 검증 · 풀기 · 같은 바이트 · 목록 id 변조 · A3, 형식 4 = VersionMismatch) ·
  test_sbx_options (asset 인자) · CTest project_assets (sbx 흐름 끝까지 — 서버가 형식 3 패키지로 돈다). 처음 판은 빈 프로젝트로 서버를 돌려
  (일시정지라 틱이 안 감) 시간 초과 → ecosystem_small 템플릿으로.
- Windows MSVC Debug 빌드 (경고 0 줄) · 전체 ctest 78 / 78 통과. 앞 커밋 2db7335 의 CI: Windows (링크 갈래가 실제로 도는 러너) · macOS · Linux GCC · Clang · ASan 다섯 잡 모두 통과.

**남은 일**

- M6-T9 (2): 클라이언트 에셋 루트 차례 (패키지 → 프로젝트 → 엔진) · "asset:<id>" 참조 · 콘텐츠가 부르는 에셋 누락 검사 · 자기 PNG 게임 E2E.

---

## 2026-11-03 — v0.6 NG-12 고침: 에셋 훑기가 링크를 경로로 풀던 것 · 시험의 복사 루프 (CI 8c1e11a 실패)

**무엇을**

- core/asset/AssetDatabase: 상대 경로를 fs::relative 대신 lexically_relative 로. fs::relative 는 심볼릭 링크를 풀어 "assets/../secret.txt" 가 됐고
  (A5 가 "assets/link.png" 로 나오지 않음), 반복자와 같은 error_code 를 덮어 훑기를 조용히 멈출 수 있었다.
- test_asset_database: `for (const std::string bad : {...})` → `const std::string&` (GCC · Clang -Wrange-loop-construct, CI -Werror).

**왜**

- CI run 41619619020 (8c1e11a) 다섯 잡 실패 — Linux · macOS 는 컴파일 (-Werror), Windows 는 링크 갈래 (CI 러너는 링크를 만들 수 있다 — 로컬은 권한이 없어
  건너뛰었다). 로컬 빌드는 경고를 오류로 다루지 않아 잡지 못했다.

**검증**

- Windows MSVC Debug 빌드 · unit_project 통과 (로컬은 링크 갈래가 여전히 건너뜀 — CI 가 본다).

**남은 일**

- 로컬에서도 경고 = 오류로 빌드하는 길 (프리셋 · 스크립트) — 같은 실수를 미리 잡게.

---

## 2026-11-03 — v0.6 NG-12: 에셋 id 충돌 정책 — core/asset/AssetDatabase (.meta · 훑기 · 가져오기 · 새 id)

**무엇을**

- 새 모듈 core/asset/AssetDatabase (ADR-0090 3 의 자리 — UI · 렌더 모름, 시뮬레이션 밖):
  - .meta = {"id": 32 hex (128 비트), "importer", "settings"} — 읽기 (모르는 키 · 모양 거절) · 정규형 쓰기 (같은 meta = 같은 바이트).
  - scanProjectAssets (읽기만): <프로젝트>/assets 를 훑어 id → 경로 · SHA-256 · 크기 색인. 같은 id · 같은 내용 = 하나 (사본 목록), 같은 id · 다른
    내용 = 오류 A3 (두 경로를 보여 주고 고르지 않음), .meta 없음 A2 · 깨짐 A1 = id 없는 에셋 (경고 — 경로 해시 id 로 돌아가지 않음), 짝 없는 .meta A4,
    경로 (P4 규칙) · 64 MiB · 링크 · 개수 · 대소문자 A5.
  - importAssets: id 없는 원본마다 겹치지 않는 새 id (이미 쓰인 · 짝 없는 .meta 의 id 를 피함). assignNewAssetId: 복제본에 새 id (importer · settings 그대로).
- 프로젝트 검사 (validateProject — sbx check · doctor) 가 assets/ 가 있으면 A1 ~ A5 를 붙이고, sbx check 가 "에셋: id N · id 없음 M".

**왜**

- 2차 감사 4.11 · ADR-0097 3 — M6-T9 가 제작자 에셋을 패키지에 담기 전에, 복사한 .meta 로 생긴 같은 id 를 빌드 순서가 조용히 고르지 않게.

**검증**

- test_asset_database 6 케이스 (78 단언): 형식 왕복 · 거절, 사본, 충돌 → 새 id 로 고침, 없음 · 깨짐 → 가져오기, 짝 없음 · 경로 · 대소문자 · 링크
  (Windows 에서는 대소문자 · 링크 갈래가 건너뜀 — MESSAGE), 프로젝트 검사.
- check_includes 0 위반 (core/asset 은 render · UI 를 include 하지 않는다). Windows MSVC Debug 빌드 · 전체 ctest 77 / 77 통과.

**남은 일**

- M6-T9 — 가져오기 명령 (sbx · Studio) · 복제 자동 감지 · .sbpkg 형식 3 (에셋 싣기 · pkg:<name>/ 이름공간) · 클라이언트 에셋 루트 차례 · "asset:<id>" 참조.

---

## 2026-11-03 — v0.6 NG-13: 경로 정책 — 대소문자만 다른 경로 거절 · sbx build 의 심볼릭 링크 거절

**무엇을**

- project::findCaseConflict (core/project/GameProject): 경로의 앞 마디마다 ASCII 소문자로 견줘 대소문자만 다른 파일 · 폴더 (파일과 폴더가 한 이름도)
  를 찾는다. sbx build (buildPackage) · 팩 스냅숏 (snapshotPack — 설치 · digest) · 패키지 index 읽기 (readPackageIndex) 가 거절한다.
- buildPackage 의 addTree: 심볼릭 링크를 따라가지 않고 거절 — 프로젝트 안의 링크가 밖의 파일을 패키지에 싣지 않게 (24 P6). 팩 스냅숏은 전부터 거절했다.
- 조사로 확인한 것 (바꾸지 않음): 기존 P4 (checkRelativePath) 가 이미 ASCII [A-Za-z0-9_.-] · "/" · 200 바이트 · ".." · 끝 "." · Windows 예약 이름을
  거절한다 — ASCII 만 받으니 NFC 문제는 생기지 않는다 (비 ASCII 이름을 받을 때 NFC 를 함께 [계획]). 길이는 2차 감사 4.12 의 240 대신 기존 200 그대로.

**왜**

- 2차 감사 SG-11 · NG-13 (ADR-0097 4): Linux 에서 만든 패키지 · 팩이 Windows · macOS 에서 파일을 잃거나 폴더가 합쳐진다. M6-T9 (프로젝트 에셋) 이
  제작자 파일을 패키지에 담기 전에 막아야 한다.

**검증**

- test_game_project: findCaseConflict — 같은 철자 · 다른 이름 괜찮음, 파일 · 폴더 · 파일과 폴더 충돌, 보여 주는 문장.
- test_game_package: 대소문자 두 파일 · 밖을 가리키는 링크로 빌드 실패 → 고치면 빌드. Windows (NTFS — 대소문자 안 가름, 링크 권한 없음) 에서는 두 갈래가
  건너뛰고 MESSAGE 를 남겼다 (실행 확인) — Linux CI 가 실제로 돈다.
- Windows MSVC Debug 빌드 · 전체 ctest 77 / 77 통과.
- 덧붙여 찾은 것: doctest --success 로 돌리면 시험 도우미의 REQUIRE_MESSAGE(x.has_value(), x.error().describe()) 가 성공일 때도 메시지를 만들어
  std::expected 단언에 걸린다 (기존 — ctest 는 --success 를 쓰지 않아 영향 없음). 이번에 고치지 않았다.

**남은 일**

- NG-12 에셋 id 충돌 정책 → M6-T9.

---

## 2026-11-03 — v0.5 M7-T5: 버전 종료 0.5.0 (Technical Alpha) — 기준 판정 · 릴리스 노트 · 제품 버전

**무엇을**

- docs/releases/v0.5.0.md: 들어간 것 (M1 · M2 · M8 · M7-T6 · 문서 · 계획), 호환 번호 (프로토콜 14 → 15 — 0.4 와 접속 안 함, .sbc 5 → 9, Host API 지문,
  kSimVersion · 세이브 · 패키지 · 잠금 · 리플레이 그대로), release-gates v0.5 행 판정 (항목마다 시험 이름), 처음 계획 (20 13.1) 의 done · deferred ·
  removed 와 새 목표 버전, 알려진 문제, 데모 · 재현 명령.
- CMake project VERSION 0.5.0. README · docs/README · 18 · 23 · roadmap (README · releases · task-registry · versioning) · VERSION-MIGRATION-MATRIX 의
  지금 · 다음 줄.

**왜**

- ADR-0096 으로 v0.5 를 Technical Alpha 로 줄였고, 그 목표 (CLI 게임이 다른 환경에서 같은 해시) 를 M7-T6 이 시험으로 보였다.

**검증**

- 판정의 근거 시험 이름은 tests/ 에 파일이 있는지 확인. 골든 · 콘텐츠: `git diff 637be03..HEAD -- tests/golden content` 비어 있음 (데모 해시 그대로).
- Windows MSVC Debug 빌드 · 전체 ctest 77 / 77 통과 (server_version · client_version 이 "0.5.0", release_bundle · tech_alpha_e2e 의 zip 이름).
- CI: 앞 커밋 (564c8d5 · f40558c) run 41606261977 — 이 커밋 시점에 macOS · Linux GCC 통과, 나머지 셋은 진행 중 (push 는 그 결과 뒤).

**남은 일**

- 다음 버전 v0.6 (에셋 · 패키지 · SaveGame 기반) — NG-12 · NG-13 → M6-T9, NG-04. 트랙 A 병렬 M3-T1 (v0.7).

---

## 2026-11-03 — v0.5 M7-T6: Technical Alpha A → B E2E (CTest tech_alpha_e2e) · 패키지 검사에서 P6 끔

**무엇을**

- CTest tech_alpha_e2e (tests/project/tech_alpha_e2e.cmake): 만든 사람 A 의 프로젝트 (samples/stash — 스크립트 + 의존 패키지 inventory) 를 빈 사용자
  폴더로 복사 · sbx install · sbx test · 헤드리스 서버 자동 플레이 (결과 줄 = 기준, WorldHash 포함) · sbx build → .sbpkg 와 자동 플레이 대본만 건네고
  A 를 지운다. B 는 배포 묶음 (make_release zip) 의 엔진을 다른 폴더에 풀고 설정 · 캐시 · 보고 · 임시 폴더를 새로, 작업 폴더도 따로 →
  sbx check (digest = A, 오류 · 경고 0) · 서버 · 로컬 플레이가 같은 결과 줄 · 패키지는 B 의 임시 폴더에 풀림 · B 의 팩 캐시는 비어 있음 ·
  한 바이트 덧붙인 패키지 거절.
- 시험이 찾은 결함: 패키지를 검사 · 열면 P6 ("스크립트가 있는데 SDK 선언이 없다 — IDE 자동 완성용") 가 늘 나왔다 — 패키지는 sdk/ 를 싣지 않으니
  받는 사람이 고칠 수 없는 경고. project::ValidateOptions{toolFiles} 를 validateProject · openProject 에 더하고, sbx check (.sbpkg) ·
  sbx doctor (패키지) · 서버 · 클라이언트의 패키지 열기 (WorldSource) 가 끈다. 프로젝트 폴더는 그대로.

**왜**

- v0.5 Technical Alpha 의 목표 (roadmap/releases v0.5) — 기존 시험은 조각만 봤다 (sample_stash: 같은 엔진 · 같은 PC 의 다른 폴더,
  release_bundle: 저장소 샘플, project_reproduce: digest). "패키지 파일 하나 + 배포 엔진 + 빈 사용자 환경 → 같은 해시" 를 한 번에 보는 시험이 없었다.

**검증**

- tech_alpha_e2e 통과 (결과 줄 "게임 없음 · tick 900 · hash 0xaf95ab21bf58d0cb" 가 A 프로젝트 = B 서버 = B 로컬 플레이). 첫 실행은 P6 로 실패 → 위 고침.
- test_game_project: toolFiles = false 면 P6 없음 · 경고가 정확히 하나 (P6) 줄어듦 (P4 · P5 해시 경고는 그대로).
- Windows MSVC Debug 빌드 · 전체 ctest 77 / 77 통과 (tech_alpha_e2e 141 초).

**남은 일**

- M7-T5 0.5.0 — release-gates v0.5 행 판정 · 릴리스 노트 · VERSION 0.5.0. 실제 다른 PC 한 번 (수동 QA) 은 Closed Creator Alpha (v0.13 M7-T3) 몫.

---

## 2026-11-03 — 로드맵 2차 배치 (v0.10 나눔 · Closed Creator Alpha v0.13 · 안정화 v0.22 · v0.23 · RC v0.24 · GA 승인, ADR-0099 · ADR-0100)

**무엇을**

- 1차 배치 (ADR-0096) 의 v0.10 22 태스크를 v0.10 Creator Foundation (7) · v0.11 Gameplay Runtime (6) · v0.12 Advanced Creator Tools (9) 로 나누고
  버전마다 통합 시험 태스크 (NG-18 ~ 20) 를 더함. Closed Creator Alpha v0.11 → v0.13, 뒤 버전을 순서대로 다시 매김 (v0.14 계정 ~ v0.21 팀 · 플러그인).
- 1차 v0.20 안정화를 v0.22 Reliability & Performance · v0.23 Security & Release Hardening 으로 나누고, 빠졌던 검증 태스크 RH-22-1 ~ 7 (Soak ·
  Sanitizer · 누수 · 성능 기준선 · 네트워크 조건 · 교차 툴체인 결정론) · RH-23-1 ~ 8 (퍼저 · 탈출 · 네트워크 · 계정 · 공급망 · 복구 · 런북 · 실기 ·
  개인정보) · RH-24-1 ~ 5 (RC 정책 · 회귀 · 감사 조치 · 문서 · GA 승인) 를 더함. 런북 · 개인정보는 정의가 없어 상태 Missing.
- v0.12 의 NG-15 Tilemap · RT-06-14 애니메이션 P2 → P1 (통합 시험이 쓴다). 병렬화 Review 넷은 측정이 예산 초과일 때만, 아니면 v1.x.
- 새 문서 roadmap/stabilization (v0.22 ~ GA 검증 방법 · 지금 상태 — CI 에 TSan · 퍼저 · Soak · SBOM · Actions SHA 고정이 없음을 소스 · 워크플로로 확인),
  release-gates 를 다시 씀 (버전 행 · 성숙 단계 · 안정화 · RC · GA 차단 조건 · 기록 기반 최종 승인). ADR-0096 · 0098 은 버전 표기만 일부 Superseded.
- check_docs: D (선행이 같거나 앞 버전) · R (끝나지 않은 태스크는 releases 한 버전에만, 그 버전 = 등록부 목표) · RH ID · --expect,
  CTest docs_consistency_selftest (tests/docs/fixtures — 정확히 9 건).

**왜**

- v0.10 한 버전에 사용자 목표 셋 · 22 태스크 — ADR-0096 자신의 기준 (목표 하나 · P0 대략 10 개 이하) 을 어겼다. GA 기준은 목록만 있고 그것을
  만드는 태스크 · 방법 · 기록 형식이 없었다.

**검증**

- `python -X utf8 tools/check_docs.py --root .` → 181 files, 0 problem(s) (첫 실행 142 건 — 1차 배치 그대로인 releases · 버전 목록 · 아직 없던 ADR).
  자체 시험 `--root tests/docs/fixtures --expect 9` 통과 (L · A · T 2 · S · V · D · R 2).
- 버전별 태스크 수를 등록부에서 셈: v0.10 8 · v0.11 7 · v0.12 10 (통합 시험 포함), 상태 Done 21 · Planned 93 · Proposed 42 · Review 4 · Missing 2.
- Windows MSVC Debug 빌드 · 전체 ctest 76 / 76 통과 (docs_consistency · docs_consistency_selftest 포함).
- 안 한 것: 새 태스크의 구현 (CI 워크플로 · 퍼저 · Soak 는 각 태스크에서), 측정 (예산 수치는 14 그대로 — 새 수치는 [추정] 또는 "확정: 태스크").

**남은 일**

- 다음 P0 은 그대로 M7-T6 → M7-T5, 병렬 NG-12 · NG-13 → M6-T9 · M3-T1 · NG-04. 서비스 없이 일찍 시작할 수 있는 안정화: RH-22-3 (TSan 잡) ·
  RH-22-5 (기준선) · RH-23-4 (Actions SHA 고정 · SBOM).

---

## 2026-11-03 — 2차 공백 분석 · 로드맵 재구성 (작은 MINOR v0.5 ~ v0.21+, ADR-0096 ~ 0098) · 문서 일관성 검사기

**무엇을**

- 1차 Studio 감사 (GM-01 ~ 44 · R1 ~ R10) 를 M2 다섯 커밋 뒤의 소스로 다시 확인 — 트랙 B 공백은 그대로, GM-35 만 넓어짐 (client.send),
  R7 은 조금 나빠짐 (Application 828 · NetworkSession 1,099 줄). 지우지 않고 audit/SECOND-PASS-GAP-ANALYSIS 2 · 3장에 해결 여부 · 새 목표를 이음.
- 새 설계 NG-01 ~ NG-17 (빌드 스냅숏 · Asset Group · Player 모드 · SaveGame 마이그레이션 · 텍스트 위치 · UI 입력 컨트롤 · Studio 시험 층 ·
  Export Profile · 출시 채널 · 롤백 · Plugin API 버전 · 에셋 id 충돌 · 경로 정책 · 해시 분리 · 1.0 전환 · 타일맵 · 오디오 출력) — 감사 4장,
  요지는 20 2.6 · 25 9 · 12장 · 26 8장 · 27 9장 · 28 3.1 · 3.2.
- 로드맵 재구성 (ADR-0096): 제품 버전은 정수 SemVer (v0.10 > v0.9), MINOR 하나 = 목표 하나, 성숙 단계는 기준으로 따로 붙임, v1.0 = GA 기준 통과.
  v0.5 = Technical Alpha (M1 · M2 · M8 + M7-T6 E2E), Closed Creator Alpha = v0.11. 정본 docs/roadmap/ (README · versioning · task-registry ·
  releases · release-gates). 태스크 ID 는 그대로 (원래 · 목표 두 칸), 옛 번호 표 (ADR-0090 · 0091 의 재사용 번호).
- ADR-0097 (패키지 · 데이터 무결성) · ADR-0098 (배포 수명주기). ADR-0039 · 0078 · 0082 · 0088 · 0092 · 0095 는 버전 배치만 "일부 Superseded".
- tools/check_docs.py + CTest docs_consistency (arch): 링크 · ADR 참조 · 태스크 ID · 목표 버전 · Done 태스크의 [계획] 잔재.
  첫 실행 18 건 → 낡은 줄 10 개 고침 · 근거 줄 5 개에 ✅ · v0.2 이력 1 건 KNOWN → 0 건 (audit/ROADMAP-CONSISTENCY).
- 시험 더함: test_semver "0.x 의 자리는 정수" (0.10 > 0.9 · 0.20 > 0.19 · 프리릴리스 · 1.0 > 0.99 · 범위 <1.0.0 이 0.21 을 받고 1.0 을 거절),
  test_user_settings 피드에서 v0.10.0 > v0.9.3.

**왜**

- 옛 v0.5 한 칸에 M3 ~ M7 · Studio 감사 몫 · 2차 감사의 NG 가 몰려 끝낼 수 없는 크기였다. 버전이 진척을 보여 주지 못하고, "v1.0 = v0.9 다음"
  은 기준과 무관하게 번호를 1.0 으로 보낸다. 문서 사이의 태스크 번호 · 상태 어긋남은 사람이 찾기 어렵다 — 검사기로 고정.

**검증**

- `python -X utf8 tools/check_docs.py --root .` → 178 files, 0 problem(s). 음성 시험 (복사본에 잘못된 ID · ADR · 링크 · 목표 · 상태를 넣음) → 다섯 종류 모두 검출, 종료 코드 1.
- 감사의 시험 이름 · 소스 근거 (상수 · 줄) 는 이 커밋에서 grep · 직접 읽기로 확인 (VERSION-MIGRATION-MATRIX 의 시뮬레이션 행은 Replay.cpp ·
  WorldSave.cpp 를 읽고 "거절" 을 "해시 비교 건너뜀" 으로 고침).
- Windows MSVC Debug 빌드 · 전체 ctest 75 / 75 통과 (docs_consistency 포함 — 문서를 마저 고친 뒤 docs_consistency · unit_foundation · unit_client 를 다시 돌려 통과).
- 안 한 것: NG 설계의 구현 · 시험 (모두 [계획]), GUI · 실기 QA, 벤치 (코드 경로를 바꾸지 않음).

**남은 일**

- 다음 P0: M7-T6 (Technical Alpha E2E) → M7-T5 0.5.0, 병렬로 NG-12 · NG-13 → M6-T9 (v0.6), M3-T1 (v0.7), NG-04.
- 옛 문서의 "[계획 v0.x]" 표기는 고쳐 쓰지 않았다 — versioning 4장 변환표로 읽는다.

---

## 2026-11-03 — v0.5 M2-T6: client 입력 · send · 소리 이벤트 · 오디오 결정 (D-14) — M2 끝

**무엇을**

- Host 표 Client 항목 일곱 (평평한 이름 — VM 에 중첩 객체가 없다): keyDown · keyPressed · keyReleased(name) (팩 input.json 의 "play.<name>"),
  pointerX · pointerY (커서의 월드 좌표, UI 위면 0), send(signal, value: i32) (PlayerSignal), playSound(id). 새 효과 HostEffect::Output.
  Capability InputRead · ClientSend · Sound — onUpdate 의 허락 묶음에 더함. 지문 → 0x1dfa888c39154dde.
- ClientScriptHost: 프레임 입력 (ClientScriptInput — 게임 키 이름 정렬 · 커서), 내보낸 신호 · 소리 (프레임마다 8 · 32, 넘으면 버리고 셈 —
  droppedOutputs), 트랩난 호출의 것은 버림.
- 앱: Application 이 게임 키 (ActionState — ActionId 가 이름 순) · 커서로 입력을 만들어 setScriptInput, NetworkSession 이 client 시스템을 돌린 뒤
  신호를 PlayerSignal 명령으로 보내고 소리 이벤트를 모은다 (takeScriptSounds — 앱이 세고 debug 로그).
- D-14 정함 (ADR-0095): v0.5 = 소리 이벤트 · Null 백엔드 (센다), 실제 출력 · sound importer · sounds.json 은 v0.6.

**왜**

- 20 3.8 · 23 1장 카드 (P0) — client 스크립트가 클릭 · 키를 신호로 보내고 서버 스크립트가 판정하는 길 (UI 버튼 M3-T5 · 템플릿 · 오목의 바탕).
  20 3.8 의 ctx.input.down 같은 중첩 이름은 평평하게 바꿨다.

**검증**

- test_client_scripts: 입력 없음 · 키 down · pressed · released · 없는 이름 · 커서 (UI 위면 0), send · playSound 값 · 시스템, 다음 프레임은 새로,
  프레임 상한 8 (2 개 버림), 트랩난 호출의 신호 · 소리 버림, Client 항목 15.
- test_client_script_session (로컬 세션 끝까지): 키 누름 → client.send → PlayerSignal → 서버 onEvent "signal" 로그 (값 · 보낸 쪽 = 서버 플레이어 표의
  id), 소리 이벤트를 가져가면 비운다.
- Windows MSVC Debug 빌드 · 전체 ctest 74 / 74 통과, check_includes 0 위반.

**남은 일**

- M2 끝. 다음 M3-T1 (게임 UI — ui/ 타깃 · 문서 모델 · 검사), 트랙 B 는 M6-T9. 오디오 출력 · sounds.json 은 v0.6.

---

## 2026-11-03 — v0.5 M2-T7: Runtime v2 계약 동결 — 콜백 표 · 예약 콜백 · Host capability 열 · 매니페스트 network.model

**무엇을**

- script/api/Callbacks.hpp — kCallbacks: 콜백마다 (이름 · 도메인 · Active | Planned · 버전 · 허락 Capability) 와 HostCapability (지금 쓰는 일곱 +
  예약 여덟). Active = server onTick · onStart · onStop · onEvent, client onUpdate. Planned = server onPrePhysics (v0.6), client onAction ·
  onEvent · onLateUpdate · onRender · onStart · onStop (v0.6) · onTick (v0.7).
- 검사기: 예약 이름 → S426 "<이름> 는 <도메인> 시스템의 예약된 콜백 이름이다 — [계획 vX]" (다른 도메인의 예약 이름도), onPostPhysics → S426
  "두지 않는다 (onTick 이 물리 뒤)". 빠진 콜백 오류를 겹치지 않게 했다. client onTick 의 진단이 "onUpdate 뿐" 에서 예약 문장으로 바뀌었다
  (시험 둘의 기대값 고침).
- Host 표: HostFunction::capability 열 (36 항목 — Read → EcsRead, tick · time · frame · dt → Time, Write · Structural · Random · Log · Display 는 같은 이름).
  정규 서명 · 지문에 든다 → 0xad8eea732d7a8f8b (kHostApiVersion 1 · .sbc 9 그대로). 실행 경로는 이 열을 읽지 않는다 (grep — 지문 계산만).
- 매니페스트: network { model } — schemaVersion 3 의 키, "snapshot" 만 (없으면 snapshot), "predicted" 는 P2 [계획 v0.7], 다른 값 P2, 모르는 키 ·
  객체 아님 P1. GameManifest::networkModel.

**왜**

- ADR-0086 6 · 23 1장 카드 (P0) — v0.6 ~ v0.8 의 콜백 · Capability · 예측 모델을 나중에 넣어도 v0.5 게임 · SDK 의 뜻이 바뀌지 않게 이름 · 자리를
  지금 고정. M2-T6 의 client 함수가 처음부터 Capability 를 단 채로 생기게 (T6 앞).

**검증**

- 새 test_runtime_contract (예약 이름 9 경우 · 지금 콜백 그대로 · 표 일관 · Host 함수의 capability ⊂ 그 도메인 Active 콜백 허락 · 정규 서명),
  test_game_project 에 network.model 7 경우, test_checker 지문 · 정규 서명 문자열, test_client_scripts · test_script_systems 의 client onTick 문장.
- 벤치 (script.regen · script.stdlib 회귀 < 2 % — 카드의 완료 기준) 는 돌리지 않았다: 같은 기계의 이전 빌드 기준선이 따로 필요하고, 이 변경은
  실행 경로에 닿지 않는다 (capability 를 읽는 곳은 HostApi.cpp 의 정규 서명뿐 — grep). 측정은 v0.6 RT-06-1 (실행 검사 — 호출 토큰) 에서.
- Windows MSVC Debug 빌드 · 전체 ctest 74 / 74 통과, check_includes 0 위반.

**남은 일**

- 다음 M2-T6 (client 입력 · send · 소리 · D-14). 콜백 단계별 Capability 실행 검사는 v0.6 RT-06-1.

---

## 2026-11-03 — v0.5 M2-T5 (4b): 주인 적기 — actions.json spawn "owner": true · ctx.setOwner (M2-T5 끝)

**무엇을**

- 콘텐츠: spawn 효과에 "owner" (bool, 기본 false) — PlayerAction 에서 true 면 만든 개체에 game.owner = 명령 issuer (서버가 낸 행동은 주인 없음).
  Rule 의 spawn 에 쓰면 V3 오류, bool 이 아니면 V3.
- 스크립트: ctx.setOwner(e, player: i32) — 서버만 · Structural (구조 변경 버퍼 — 호출 끝 적용, 같은 호출의 새 개체 핸들, 붙이기 · 떼기와 같은 상한).
  없으면 붙이고 있으면 바꾼다, 0 ~ 65535 밖이면 HostError. 구조 변경 종류 SetOwner. Host API 지문 → 0xf2ff39fd63bfbaf9, sandbox.d.ts 넷.

**왜 · 바꾼 계획**

- 20 3.6 · 23 카드는 "엔진이 늘 적기 + kSimVersion 5" 였다. kSimVersion 은 WorldHash 머리에 들어가 모든 골든 (Windows · Linux GCC · Clang) 과 샘플
  기대 해시를 다시 찍어야 하고, 옛 리플레이는 "규칙 차이" 가 된다. 콘텐츠 키 · 스크립트 함수로 켜면 기존 결과는 그대로다 (04 6장 — 콘텐츠 데이터 변경은
  contentHash 가 구분) → ADR-0094, kSimVersion 4 그대로.
- 신호는 개체를 만들지 않는다 — 받은 스크립트가 e.player 로 setOwner.

**검증**

- test_player_table: "owner": true → owner = issuer 5 · 서버 행동 = 없음 · 켜지 않은 효과 = 없음, "owner": "yes" · Rule 의 owner = V3, ctx.setOwner 새
  핸들 (7) → 바꾸기 (9) → 70000 트랩 (그대로 9 · 트랩 1). test_checker 지문.
- Windows MSVC Debug 빌드 · 전체 ctest 74 / 74 통과 (det_* 골든 · 샘플 자동 플레이 그대로), check_includes 0 위반.

**남은 일**

- M2-T5 끝. 다음 M2-T7 Runtime v2 계약 동결 → M2-T6.

---

## 2026-11-03 — v0.5 M2-T5 (4a): 플레이어 표 — 서버 명령 PlayerJoin · PlayerLeave · ctx.playerCount · playerId · playerRole

**무엇을**

- SimulationWorld 안의 플레이어 표 (players() — std::map<ClientId, { role, name }>, id 순). ECS 개체가 아니고 세이브 · WorldHash 밖.
- 서버 명령 PlayerJoin { player, role, name (64 바이트) } (같은 id = 바꾸기) · PlayerLeave { player } — issuer 가 서버가 아니면 거절. 리플레이 JSON
  op "player.join" · "player.leave". 와이어 형식 없음 (코덱은 쓰려 하면 단언, 읽는 쪽은 태그가 없어 형식 오류) — 프로토콜 15 그대로.
- ServerHost: roster 이벤트에 이름 · 역할 · 역할만 바뀜을 싣고, Sim 절반이 서버 명령으로 넣는다 (issuer 0 — 같은 틱의 클라이언트 명령 · 스크립트보다
  먼저). 월드를 바꾼 뒤 (처음 · 불러오기 · 재시작 · 테스트 플레이 · 재생 끝) 한 번 맞추기. 재생 중에는 표만 고치고 명령은 넣지 않는다.
- 스크립트 (서버): ctx.playerCount() · ctx.playerId(index) (범위 밖 -1) · ctx.playerRole(id) (없으면 -1). Host API 지문 0xf5e11716b49e9a6e →
  0xd3dc282f66cc8820 (Host API 1 · .sbc 9 그대로), sandbox.d.ts 넷 다시 만듦.

**왜 · 바꾼 설계**

- 20 3.4 · 23 카드 — 스크립트가 플레이어를 알아야 하고, 결정론을 지키려면 명령으로 들어와야 한다.
- 처음 구현은 플레이어 = ECS 개체 (game.player 컴포넌트) 였다. 전체 ctest 에서 sample_playthrough · sample_mana_regen · sample_stash 가 실패 —
  클라이언트 호스트의 로컬 플레이어 개체가 뒤 개체의 saveId 를 밀어 (saveId 키 난수) **같은 게임이 헤드리스 서버 (점수 −60) 와 클라이언트 (−74)
  에서 달랐다**. 접속이 게임 결과를 바꾸면 안 되므로 버리고 ECS 밖 표로 바꿨다 (ADR-0093 — 측정 근거 포함). 그 구현에서 고쳤던 개체 수 시험들은
  되돌렸다.
- 20 3.4 의 ctx.players(): PlayerInfo[] 는 구조체 배열 결과가 필요해 스칼라 함수 셋으로. 이름 · 팀 · 클라이언트 쪽은 [계획].
- 행동 · 신호 · spawn 의 owner 자동 적기는 기존 리플레이 결과를 바꾸므로 kSimVersion 을 올리는 따로 조각 (4b).

**검증**

- 새 test_player_table (넣기 · 바꾸기 · 빼기 · 서버만 · 잘못된 값 · 개체 수 · WorldHash 그대로 (뒤 개체 saveId 그대로) · JSON 왕복 · 스크립트 세 함수),
  test_roles_overlay 에 접속 둘 → 표 둘 · 역할 바꿈 · 나감. test_checker 지문 고정.
- kSimVersion 4 그대로. Windows MSVC Debug 빌드 · 전체 ctest 74 / 74 통과 (sample_playthrough · mana_regen · stash 포함), check_includes 0 위반.

**남은 일**

- M2-T5 (4b) owner 적기 (kSimVersion 5) → M2-T7 → M2-T6. 이름을 스크립트에서 읽기 (Host 문자열 결과) 는 [계획].

---

## 2026-11-03 — Studio 감사: 벤치마크 · Gap Matrix · Studio · 에셋 · 스크립트 IDE 설계 · 로드맵 세 트랙 (+ 언어 서비스 프로토타입)

**무엇을**

- 감사 문서 docs/audit/: ENGINE-BENCHMARK (Roblox · Unity · Unreal · Godot 공식 문서 13 건 — 확인 · 일반 지식 구분), SANDBOX-GAP-MATRIX (44 행 —
  Implemented ~ Missing, ⟂접근 · ⟂낡음 · ⟂불일치 표시), ARCHITECTURE-REVIEW (구조적 위험 10 · 재사용 · 새 설계 · 문서 충돌), ALPHA-READINESS
  (G1 ~ G9 판정 — 알파 준비 안 됨), VERIFICATION-RESULTS (실행한 것 · 안 한 것과 이유).
- 설계 문서 25 (Studio — Client 모드 + editor/services 층 · 화면 10 와이어프레임 · UI 기술 대안), 26 (에셋 — .meta 안정 id · 데이터 수명주기),
  27 (스크립트 IDE — 언어 서비스 · 재생 디버거 · LSP), 28 (제작자 흐름 기준 G1 ~ G9 · 사용성 시험 계획 · 양식 — 결과 없음).
- ADR-0089 (Studio 층) · 0090 (에셋 DB · .sbpkg 형식 3) · 0091 (언어 서비스) · 0092 (세 트랙 · v0.5 M6 +T6 ~ T9 · T3 확장).
- 로드맵: 23 (0.1 세 트랙 · 1.1 v0.5 트랙 B 카드 · 2.1 v0.6 ST-06-* · 7 DAG + Mermaid · 8 미룰 순서), 20 10장 Phase 6, 18 (0 · 6 · 6.6 · 7),
  21 1장, 24 (P5 ~ P8 · D3 · C3 · v0.5 차단 줄), README 지도, 17.
- 코드 (프로토타입 — 엔진 동작 바꾸지 않음): script/lang/LanguageService — contextMembers(domain) · completeAt(source, offset, domain) ("ctx."
  후보를 Host 표에서 도메인으로 거른다), 시험 test_language_service.

**왜 · 찾은 것**

- 알파 목표 (CLI 없이 만들어 다른 PC 에서) 대조: 프로젝트 생성 · 빌드 로직은 core 에 있으나 UI 가 없고 (CLI 만), **패키지가 에셋을 싣지 않으며
  클라이언트는 전역 에셋 루트 하나** (core/package/GamePackage.cpp · apps/client/main.cpp:189) → 자기 그림을 쓴 게임은 v0.5 C1 (A → B) 을 통과할
  수 없다. 에셋 id 는 경로 해시라 이름 바꾸기가 참조를 끊는다. Studio 의 콘텐츠 "적용" 은 월드 오버레이에만 (팩 파일 저장 경로 없음). 자동 저장 ·
  레이아웃 저장 없음 (ImGuiLayer 의 "[계획] … (Phase 12)" 는 낡았다).
- 버전 이름 · 목표는 그대로 두고 (참조 안정), 요청의 Technical Alpha · Closed Creator Alpha 는 트랙 B 단계 이름으로 (ADR-0092).
- 기존 M6-T3 이 이미 "검사 · 시험 · 빌드" 패널이어서 GUI 빌드를 새 태스크로 만들지 않고 T3 에 누락 검사를 더했다 (중복 방지).

**검증**

- 프로토타입: 빌드 · unit_* 14 / 14 통과 (test_language_service — 서버에 label 없음 · 클라이언트에 write · spawn 없음 · 이름 순 · 설명 있음 ·
  접두 · 수신자 검사). 문서 상대 링크 0 깨짐 (스크립트 — 기존 ADR-0085 의 0057 링크 깨짐은 기존 ADR 이라 고치지 않았다).
- Windows MSVC Debug 전체 ctest 74 / 74 통과, check_includes 562 파일 · 0 위반.
- 실행하지 않은 것: Studio 화면 프로토타입 (와이어프레임만), 다른 PC A → B, 외부 사용성 시험, 성능 측정 — 이유는 VERIFICATION-RESULTS 4장.

**남은 일**

- 트랙 A 다음: M2-T5 (4). 트랙 B 다음: M6-T9 (에셋 · .sbpkg 3) → M6-T3 (Build) · M6-T1 (홈) · M6-T7 (자동 저장). 결정 필요: D-15 (편집기 위젯 —
  자체 · 외부 라이브러리), D-16 (3D 범위), D-17 (문서 동시 편집 — 잠금 · 3-way).

---

## 2026-11-02 — v0.5 M2-T5 (3): PlayerSignal · signals.json · 프로토콜 15 · onEvent "signal"

**무엇을**

- 콘텐츠 signals.json (팩마다, 선택): [{ id, min?, max?, perTick? }] — i32 범위 (기본 0 · 0), perTick 1~8 (기본 1), 팩 전체 64 개.
  새 규칙 V13 (범위 · perTick · 개수), 중복 · 이름공간 V1, 모르는 키 V3. ContentDatabase::signals · findSignal.
- 명령 PlayerSignal { name, value: i32 } — CommandJson {"op": "signal"}, 와이어 태그 31 (프로토콜 14 → 15), 권한 = 플레이어 행동 열
  (player 부터). 적용: 선언 · 범위 · 세션이 있으면 Running · 그 틱의 (issuer, id) 마다 perTick — 셈은 단계 0 에서 비운다. 월드는
  바꾸지 않는다.
- 스크립트: 구독 종류 signal (GameEventKind.Signal = 6), GameEvent 칸 signal (string) — player · value 를 같이 쓴다. Host API 지문
  0xaf93f2870c649b98 → 0xf5e11716b49e9a6e (Host API 1 · .sbc 9 그대로), sandbox.d.ts 넷 다시 만듦.

**왜**

- 20 3.7 · 23 1장 카드 — UI 버튼 (M3-T5) · client 스크립트 send (M2-T6) 가 서버 규칙을 부르는 유일한 길.
- 설계를 바꾼 것: 이름마다 "초당 rate" → "틱마다 perTick". 초 단위 창은 틱을 넘는 상태가 되어 세이브 → 로드 · 리플레이와 이어
  돌린 실행의 판정이 갈릴 수 있다 (04). 클라이언트마다의 초당 120 토큰 버킷은 그대로 (08).
- kReplayVersion 1 그대로 — 리플레이는 명령 JSON 줄이고 op 집합은 열려 있다 (옛 리플레이는 그대로 읽힌다, 새 리플레이의 signal
  줄은 옛 엔진이 거절).

**검증**

- 새 test_player_signal (로드 · V13 · V1 · V3 · 적용 차례 · perTick 의 issuer 별 셈 · 다음 틱 · 해시 그대로 · 세션 · JSON 왕복),
  test_messages (프로토콜 15 · 태그 31 왕복 — i32 끝값 · 분류), test_script_callbacks (signal 이벤트 · 거절된 신호 없음 · 칸 배치),
  test_checker 지문 고정.
- 명령 적용 차례 = (executeTick, issuer, sequence) — 시험의 기대값을 처음에 넣은 차례로 적어 틀렸고 고쳤다.
- kSimVersion 4 그대로 (det_* 골든 · 리플레이 해시 통과). Windows MSVC Debug 빌드 · 전체 ctest 74 / 74 통과, check_includes 0 위반.

**남은 일**

- M2-T5 (4) ctx.players · session · owner 적기. 클라이언트에서 신호를 보내는 길 (ctx.send · UI 버튼) 은 M2-T6 · M3-T5. 변형 퍼즈에
  PlayerSignal 넣기는 [계획] (24 N1).

---

## 2026-11-02 — v0.5 M2-T5 (2): server onEvent "action" — 받아들여진 PlayerAction 을 스크립트로

**무엇을**

- 구독 종류 action (GameEventKind.Action = 5, 종류 차례 맨 끝 — 기존 종류의 전달 차례 그대로). GameEvent 에 칸 셋: action (string —
  PlayerAction id), player (i32 — 명령의 issuer, 0 = 서버), targets (Entity[] — 고른 대상 중 17d 에 살아 있는 것, (거리², saveId)
  차례). x · y = 고른 점, entity null. 다른 종류는 action "" · player 0 · targets [].
- 엔진: applyCommand 가 받아들인 PlayerAction 을 (구독이 있을 때만) 이번 틱 목록에 — 적용 차례, 대상은 applyPlayerAction 이 고른 것.
  17d 가 산 대상만 넘긴다. 틱마다 비운다 (명령과 17d 가 같은 틱 — 세이브할 것 없음).
- VM: Interpreter::call 의 구조체 칸에 string (글 목록의 자리) · 불투명 값 배열 (개수 + 원소) 잎. 검사기 · 검증기 · d.ts 가 새 칸
  종류 (GameEventField String · EntityList) 를 안다.
- Host API 지문에 이벤트 종류 · GameEvent 필드를 넣었다 — onEvent 의 구조체 배치가 바뀌면 옛 바이트코드가 지문으로 거절된다 (전에는
  지문 밖이라 검증기의 "GameEvent 모양" 오류로만 걸렸다). 지문 0x6f75b0cc967a534f → 0xaf93f2870c649b98 (kHostApiVersion 1, .sbc 9
  그대로). sandbox.d.ts 넷 다시 만듦.

**왜**

- 20 3.5 · 23 1장 카드 — 오목의 "돌 놓기" 처럼 콘텐츠 행동 뒤의 규칙을 스크립트가 쓴다 (M7-T1 · M6-T2 의 선행).

**검증**

- test_script_callbacks: 글 · 개체 목록 칸 (값 · 자리 밖 · 개수 넘침 · 음수 = BadCall), 17d action (한 틱의 받아들임 · 거절 ·
  지운 대상 — 로그 차례 · 값, 대상 핸들로 쓰기), 구독 없는 월드와 같은 해시. 기존 칸 시험은 새 배치로. test_checker 지문 고정.
- kSimVersion 4 그대로 — 구독이 없으면 모으지 않고, 모아도 상태를 바꾸지 않는다 (det_* 골든 · 리플레이 해시 통과).
- Windows MSVC Debug 빌드 · 전체 ctest 74 / 74 통과 (det_* · 리플레이 골든 포함), check_includes 0 위반. clang-format 은 고친 줄만.

**남은 일**

- M2-T5 (3) PlayerSignal · signals.json · 프로토콜 15 · onEvent "signal" (23 1장). action 구독 거르기 (id 목록) 는 넣지 않았다 —
  스크립트가 e.action 으로 가른다 (필요하면 바이트코드 구독 표에 [계획]).

---

## 2026-11-02 — Runtime v2 · 통합 로드맵 v0.5 ~ v1.0 · 보안 출시 기준 (문서 — 22 · 23 · 24, ADR-0086 ~ 0088)

**무엇을**

- 새 문서 22-RUNTIME-V2: 실행 도메인 9 개 (Server · Network · Client · Physics · Render Prep · Render Submit · Worker · Asset I/O ·
  NetworkModule) 의 소유 · 공유 · 큐 · 주기 · 블로킹 · 동기화 · 종료 · 오류 · 예산 · OS 스레드 표, 규칙 T7 ~ T10, 콜백 표 (server ·
  client · network · worker — 상태 · 버전), Capability 모델 (세 겹 검사), Client 고정 Tick · 예측, 물리 단계, 입력, NetworkModule ·
  이벤트 · 비동기 계약, ECS 병렬화 순서, 상태 · 타이머, 렌더 · UI · 애니 · 오디오 · 에셋, 프로파일 · 권장 사양, 외부 엔진 비교 18 항목
  (출처 7 · 확인 못 한 것 표시).
- 새 문서 23-ROADMAP: 우선순위 P0 ~ P3, 14 칸 태스크 카드 (v0.5 의 M2-T5 (2) · (3) · (4) · M2-T7 · M2-T6), v0.6 ~ v0.9 의 RT-* 태스크,
  v1.0 동결, DAG · 병렬 가능 · 미룰 순서.
- 새 문서 24-SECURITY-GATES: 위협 표 (VM · ENet · NetworkModule SSRF · DNS 재바인딩 · TLS · 패키지 · 세이브 · 클라이언트 · 큐 · 운영 ·
  Worker) 와 버전별 릴리스 차단.
- ADR-0086 (Runtime v2), ADR-0087 (Client 고정 Tick · 예측 — 게임마다), ADR-0088 (배치 갱신 — 등급전 · 순위표 v0.8, ADR-0082 상태 줄에
  "일부 Superseded").
- 규범 문서에 [계획] 절: 01 5.4 · 03 2.x · 04 7.x · 07 5.x · 08 9.x · 19 26장. 20 10장 (M2-T5 를 네 조각, M2-T7 추가, 진행 차례),
  18 (0 · 6 · 6.6 · 7장), 21 3 · 4장, ADR README, docs/README 지도, platform 초안 (architecture 01 · 04 · 05 · 11 · 12, roadmap v0_5 ~
  v1_0) 머리에 현행 계획 링크 — 초안 본문은 그대로.

**왜**

- 통합 개발 계획 재설계 요청 — v0.6 ~ v1.0 의 콜백 · 물리 · 예측 · 외부 네트워크를 스레드 · Context 를 늘리지 않고 넣을 계약, 그리고
  버전마다 보안 · 안정성 출시 기준. v0.5 목표 (Creator Alpha) 는 그대로, 호환이 걸린 이름 · 자리만 v0.5 (M2-T7) 에 고정.

**결정 요지**

- onTick 은 16b 그대로 (이미 물리 뒤) — server onPostPhysics 는 두지 않고 onPrePhysics (8b) 만 더한다.
- ABI Context 는 둘 그대로, 단계는 호출 토큰 · Capability 로 — .sbc 형식 영향 없음.
- 예측은 게임마다 (network.model), 자기 개체만, 권위 아님. 입력은 고정 Tick 프레임으로 묶는다.
- NetworkModule 은 v0.8 서버만 · 응답은 명령 (리플레이) · SSRF · DNS 재바인딩 · TLS 차단이 릴리스 조건.

**검증**

- 문서만 — 코드 · 형식 · 번호 변경 없음. 새 · 고친 문서의 상대 링크가 실제 파일을 가리키는지 스크립트로 확인. Windows MSVC Debug
  빌드 · 전체 ctest 74 / 74 통과 (59eebc9 와 같은 빌드 — 이 커밋은 문서만).

**남은 일**

- 다음 구현: v0.5 M2-T5 (2) onEvent "action" (23 1장 카드). 열린 결정: 리플레이 명령 종류 추가가 kReplayVersion 을 올리는지
  (M2-T5 (3) 첫 단계), D-14 오디오 백엔드 (M2-T6), 권장 사양 기준 기기 등급 (v0.9).

---

## 2026-11-02 — 문서 상태 동기화: 21 1장 · 18 (1.1 · 2 · 8장) · README — v0.5 M8 뒤의 실제 값

**무엇을**

- 21 1장: v0.4.0 시점 (637be03) 의 표를 c651339 의 소스로 다시 맞춤 — Host API 지문 e02f109682ecb304 → 0x6f75b0cc967a534f,
  .sbc 5 → 9, ctest 67 → 74, 오류 보고 Missing → Implemented (M1-T1 · T2), 진단 Partial → Implemented (sbx doctor), 자동 업데이트
  Missing → Partial (알림 · 안전 모드 · 피드), Host API 의 v0.5 함수, 새 줄 (표준 라이브러리 · 물리 · 충돌 · 실행 구조). 버전 번호
  한 줄 (.sbc 9 · Host API 1 · kSimVersion 4 · 프로토콜 14 · .sbpkg 2 · 리플레이 1 · 세이브 1/1 — 각 헤더 값).
- 18: 1.1 의 "CI 를 읽을 수 없다" → 다섯 잡 녹색 (gh 로 읽음), macOS CI 잡 ✅ · 실기 QA 남음. 2장 날짜 · 줄 (진단 · 게임플레이 API ·
  표준 라이브러리 · 실행 구조 v2 · 게임 UI). 8장 기준선에 M1-T4 (3) ~ M8 줄 + CI 줄 (로컬과 따로).
- docs/README 0장 버전 줄에 v0.5 ◐.

**왜**

- 통합 계획 재설계 요청의 Phase A — 문서가 코드보다 늦은 곳 (21 의 옛 지문 · v0.4 상태, 18 의 CI 문장) 을 먼저 고친다.

**검증**

- 값은 헤더 (script/vm/Bytecode.hpp kFormatVersion · script/api/HostApi.hpp · core/simulation kSimVersion · network kProtocolVersion) 와
  test_checker 의 지문 고정, DEVELOPMENT_LOG 의 ctest 줄, `gh run list` (11e11fe · de65518 success, 3322110 failure → 0e1adab) 로 대조.
- 문서만. Windows MSVC Debug 빌드 · 전체 ctest 74 / 74 통과 (render_tests_warp 포함, 로컬).

**남은 일**

- 22 · 23 · 24 · ADR-0086 ~ 0088 (다음 커밋 — 설계 · 로드맵).

---

## 2026-11-02 — v0.5 M8-T8 (2): 충돌 질의 raycast · overlap

**무엇을**

- Host 표 +2 (서버): ctx.raycast(x0, y0, x1, y1) → Entity | null — 선분이 처음 닿는 core.collider 원 (매개변수 t 가 작은 것,
  같으면 saveId, 시작점이 원 안이면 t = 0, 길이 256 까지), ctx.overlap(x, y, r) → Entity[] — 거리 ≤ r + 반지름, saveId 순
  (r 128 · 1,024 개까지). 후보는 단계 4 색인을 충돌 반지름 상한 (2) 만큼 넓혀서, 후보마다 연료 1.
- 지문 → 0x6f75b0cc967a534f (Host API 1 그대로), sandbox.d.ts 넷 (sdk · 샘플 셋) 다시 만듦.

**왜**

- ADR-0085 11 · 20 M8-T8 — 지금 충돌 구조 (원) 가 받치는 만큼. layer · mask 거르기 · 연속 충돌 · 물리 반응은 [계획].

**검증**

- test_script_engine_queries 에 충돌 질의 케이스 (raycast 8 경우 · overlap 5 경우). unit_script 전체 통과. Windows MSVC Debug 전체
  ctest 74 / 74 통과.

**남은 일**

- M8 끝. 다음은 M2-T5 나머지 (20 10장 진행 차례).

---

## 2026-11-02 — v0.5 M8-T9: 표준 라이브러리 예제 · 벤치 · 안내

**무엇을**

- 예제 프로젝트 samples/stdlib_tour (sbx new + 팩 tour): 인벤토리 (Map<Kind, i32> · Set · sortNumbers · unique · sum), RTS 유닛
  AI (ctx.kNearest · PriorityQueue · vec2.distance · ctx.requestPath / pathState), 보드게임 AI (Graph 6 × 6 · bfs · pathTo ·
  topoSort). Prefab 다섯, playthrough.json (300 틱 · Windows 해시), README. 빈 시작 월드는 world.json 의 content ·
  components 를 지금 콘텐츠로 맞췄다 (엔티티 0 — 다시 저장과 같은 결과, sbx check 경고 0).
- CTest sample_stdlib_tour (tests/project/sbx_test_sample.cmake — sbx test 의 종료 코드 · 출력 줄, UTF-8 로 읽는다).
- 벤치 script.stdlib (bench/ScriptBench.cpp): 내장 대 스크립트 반복문, n 100 · 1k · 10k · 100k (그래프는 노드 상한 밖이면
  건너뜀). 측정 14 7.15.
- 안내 docs/tutorials/stdlib.md (제작자용).

**왜**

- 20 10장 M8-T9 · 제작자 요청의 예제 셋 (인벤토리 · RTS AI · 보드 AI) 과 실측 성능.

**검증**

- sbx check samples/stdlib_tour 오류 0 · 경고 0, sbx test 통과 (두 번 — 같은 해시 0x5c11cde6edb4b5dd), 스크립트를 tsc 5.6
  --strict (node:22 컨테이너) 가 받는다, 보드 결과 (닿는 칸 28 · 최소 수 10 · 기술 차례) 는 손으로 센 값과 같다.
- 벤치는 i7-14700K · MSVC 19.44 RelWithDebInfo 실측 (5 번 중앙값). Windows MSVC Debug 전체 ctest 74 / 74 통과 (새 sample_stdlib_tour).

**남은 일**

- M8-T8 (2) raycast · overlap, 그다음 M2-T5 나머지. 비교자 콜백 호출 비용 (sort(cmp) 가 스크립트 병합 정렬과 비슷) 은 측정 근거로
  [계획].

---

## 2026-11-02 — v0.5 M8-T8 (1): 엔진 질의 · 스크립트 경로 요청

**무엇을**

- Host 표 +7 (서버): ctx.nearest · kNearest · inBox (단계 4 색인 — (거리², saveId) · saveId 순, 상한 k 1,024 · 한 변 256 ·
  결과 1,024), ctx.requestPath · pathState · pathPoints · cancelPath. ABI NumberList (number[] — 끝에 더함: 검증기 ·
  검사기 · 인터프리터 · d.ts). 지문 → 0x3b37e380f77eddfd (kHostApiVersion 1 그대로), sandbox.d.ts 셋 다시 만듦.
- 경로 요청은 새 요청 표 없이 개체의 ai.path 를 쓴다: ScriptHost 가 호출 동안 모았다가 endCall(commit) 에 Pending (goal ·
  경유점 비움) · 취소는 None — 트랩이면 버린다. 제출 (단계 8, saveId 순 · 틱 예산) · 결과 (다음 틱 단계 5) · 세이브의
  Submitted 다시 계산은 엔진 흐름 그대로 (03 7.1). 요청 id = 개체 (개체마다 하나), 결과는 덮어쓸 때까지 컴포넌트에 남는다.

**왜**

- ADR-0085 11 — RTS AI (가까운 적 · 사각형 선택 · 이동 명령). "틱 안의 무제한 동기 A*" 없이 엔진의 예산 · 결정성을 쓴다.
  비동기 요청 표 (id · 만료) 를 따로 두지 않은 이유: 세이브 · 리플레이에 새 상태가 생기지 않게 (영속 상태는 ECS).

**검증**

- test_script_engine_queries 3 케이스 76 단언 (질의 동점 · 상한, 요청 → Pending → Submitted → Following · 경유점 · 취소 ·
  ai.path 없음 · 트랩 버림, 두 월드 같은 해시). unit_script 지문 고정 갱신. Windows MSVC Debug 전체 ctest 73 / 73 통과.
- 같이 고침 (따로 커밋 0e1adab): M8-T2 ~ T7 의 배열 내장 switch 에 default — CI (GCC · Clang -Wswitch) 실패. Docker CI 재현
  (sbx-ci-linux) 으로 linux-gcc debug 78 / 78 · relwithdebinfo 81 / 81, linux-clang debug 78 / 78 확인.

**남은 일**

- M8-T8 의 raycast · overlap (core.collider · 색인 위), M8-T9 예제 · 벤치 · 안내.

---

## 2026-11-02 — v0.5 M8-T7: Graph — BFS · DFS · Dijkstra · A* · 위상 정렬 · 연결 성분

**무엇을**

- 컬렉션 종류 Graph (Coll — 원소 칸 i32), 내장 10 (Graph.new · addEdge · neighbors · bfs · dfs · dijkstra · pathTo · astar ·
  topoSort · components). 내장 표의 인자 칸 4 → 6 (astar 다섯), collMask 를 u16 으로 (종류 8).
- VM: 노드마다 (끝, 가중치) 넣은 차례. 음수 · NaN · 무한 가중치는 넣지 않는다 (false). 최단 경로는 지연 삭제 힙 —
  (점수, h, 노드) 가 작은 것부터, 처음으로 짧아진 간선을 지킨다. DFS 는 재귀 앞 순서, Kahn 은 작은 번호 먼저, 성분은
  union-find (작은 뿌리). 연료 = 방문 · 간선 · 큐 연산마다 1, 상한 노드 65,536 · 간선 칸 1,048,576.
- 언어: "sandbox" 의 Graph (값 · 타입), 메서드 · size, addEdge 의 가중치 기본 1, pathTo · astar · topoSort = i32[] | undefined.
  d.ts class.

**왜**

- ADR-0085 10 · 19 25.6 — 보드게임 AI (BFS · 최소 수), 퍼즐 · 기술 트리 (위상 정렬), 지도 위 경로 (A*).

**검증**

- test_stdlib_graph 5 케이스 — 기대값은 같은 프로그램을 tsc 5.6 --strict 로 옮겨 node:22 에서 같은 동점 규칙의 JS 구현
  (선택 정렬 · 재귀 DFS) 으로 돌린 값과 모두 같다. unit_script 154 케이스. Windows MSVC Debug 전체 ctest 73 / 73 통과 (골든 · 리플레이 그대로).

**남은 일**

- M8-T8 엔진 API (nearest · kNearest · inBox · 비동기 경로 요청).

---

## 2026-11-02 — v0.5 M8-T6: 알고리즘 · 수학 · vec2

**무엇을**

- 내장 40 ("sandbox" 이름 내보내기): sortNumbers · binarySearch · lowerBound · upperBound · sum · average · minOf · maxOf ·
  countIf · prefixSum · rotate · unique · partition · shuffle (배열 경로), clamp (i32 판 따로) · lerp · inverseLerp ·
  smoothstep · smootherstep · repeat · pingPong · approximately · degToRad · radToDeg · vec2.* 15 (값 내장 — 받는 쪽 없음).
  서명 틀 ArrayNumeric · ArrayOrderable · ArrayKey · F64Array · Vec2 ({ x: f64, y: f64 } 모양).
- 코드 생성: 내장 인자의 자리 0 도 틀대로 바꾼다 (값 내장의 number 자리에 i32). 검사기: SdkFunction (Symbol::function =
  내장 번호) · vec2 이름공간, clamp 는 셋 다 i32 · 정수 상수면 i32. d.ts 선언 (오버로드 · 제네릭 · namespace vec2).
- 결정론: sum · prefixSum 은 차례대로, sortNumbers 는 안정 전순서, shuffle = SplitMix64 → Fisher–Yates (씨앗 = i32 —
  ctx.randomInt 로).

**왜**

- ADR-0085 · 19 25.5 — 게임 수학 · 집계 · 결정적 섞기.

**검증**

- test_stdlib_algo 7 케이스 — 기대값은 같은 프로그램을 tsc 5.6 --strict 로 옮겨 node:22 에서 같은 뜻의 JS 구현 (BigInt
  SplitMix64 포함) 으로 돌린 값과 모두 같다 (셔플 차례도). unit_script 149 케이스. Windows MSVC Debug 전체 ctest 73 / 73 통과 (골든 · 리플레이 그대로).

**남은 일**

- M8-T7 Graph.

---

## 2026-11-02 — v0.5 M8-T5: Stack · Queue · Deque · PriorityQueue · MinHeap · MaxHeap · BitSet

**무엇을**

- 바이트코드 형식 9: 타입 Coll (종류 CollKind · 원소 — 힙은 f64, BitSet 은 bool, 검증기), 내장 21 (Coll.new · BitSet.new ·
  size · clear, Seq.pushBack · pushFront · popBack · popFront · peekBack · peekFront, PriorityQueue.push · pop · peek ·
  peekPriority, Heap.insert · extract · peek, BitSet.set · clear · test · count). 검증기는 내장마다 받는 쪽으로 허락된 종류
  (collMask) 를 본다.
- VM: SeqData — Stack · 힙 = 배열 · 이진 힙, Queue · Deque = 고리 버퍼 (2^k), PriorityQueue = (원소 · 우선순위 · 넣은 차례) 힙
  (같은 우선순위 = 넣은 차례, -0 = 0, NaN 은 넣지 않고 false), MinHeap · MaxHeap = 전순서 (-0 < +0, NaN 가장 큼). 늘림은 논리
  용량 (vector::capacity 를 쓰지 않는다 — 플랫폼마다 늘림 시점 · 연료 · 아레나가 같게). 연료 = 연산 1 + 힙 단계 · 늘림 원소.
- 언어: "sandbox" 의 Stack · Queue · Deque · PriorityQueue · MinHeap · MaxHeap · BitSet (값 = new, 타입 = 같은 이름, d.ts
  class 선언). 꺼내기 · 보기 = T | undefined, size 속성. 가져오지 않고 new 하면 S401 (import 안내).

**왜**

- ADR-0085 · 19 25.4 — RTS AI (우선순위 큐 · 큐 BFS), 보드 판정 (비트셋).

**검증**

- test_stdlib_seq 6 케이스 — 기대값은 같은 프로그램을 tsc 5.6 --strict 로 옮겨 node:22 에서 같은 뜻의 JS 구현 (정렬 배열) 으로
  돌린 값과 모두 같다. unit_script 142 케이스. Windows MSVC Debug 전체 ctest 73 / 73 통과 (골든 · 리플레이 그대로).

**남은 일**

- M8-T6 알고리즘 · 수학 · vec2.

---

## 2026-11-02 — v0.5 M8-T4: Map · Set

**무엇을**

- 바이트코드 형식 8: 타입 Map (키 · 값) · Set (키) — 키는 i32 · f64 · bool · string (검증기). 내장 25 (Map.new · set · get ·
  has · delete · clear · size · keys · values · next · keyAt · valueAt, Set.new · from · add · has · delete · clear · size ·
  values · next · valueAt, setUnion · setIntersection · setDifference) 과 서명 틀 (Map · Set · Key · MapVal · OptMapVal · …).
- VM: 객체 종류 Map · Set — 넣은 차례의 항목 (지운 것은 자리를 지킨다) + 선형 탐사 색인. 지운 키를 다시 넣으면 그 키의 칸을
  다시 쓴다 (키마다 칸 하나). 해시 고정 (섞기 · FNV-1a), SameValueZero, -0 은 +0 으로 저장. 연료 = 탐사 칸 · 건너뛴 항목 ·
  다시 만들기, 아레나 = 항목 · 색인 바이트, 항목 상한 = maxArrayLength.
- 언어: Map<K, V> · Set<T> 타입, new Map · new Set (타입 인자 · 선언의 타입 · 배열에서), 메서드 · size 속성, for-of 의 Set ·
  m.keys() · m.values() (커서 — 반복 중 바꾸기가 JS 와 같다), Array.from (Set · keys · values · 배열), "sandbox" 의 집합 연산 셋
  (d.ts 다시 만듦). keys() 값 · entries · forEach · [k, v] for-of 는 S404 · S406 (튜플 · 반복자 값 [계획]).

**왜**

- ADR-0085 4 · 7 — 인벤토리 · 소유 목록 · 방문 집합. 보드게임 판정 (M7-T1) 의 바탕.

**검증**

- test_stdlib_collections 6 케이스 — 기대값은 같은 프로그램을 tsc 5.6 --strict 로 JS 로 옮겨 node:22 에서 돌린 값과 모두 같다
  (반복 중 지우기 · 넣기 · clear 포함). unit_script 136 케이스. Windows MSVC Debug 전체 ctest 73 / 73 통과 (골든 · 리플레이 그대로).

**남은 일**

- M8-T5 컬렉션 (Stack · Queue · Deque · PriorityQueue · 힙 · BitSet). Map · Set 의 forEach · entries · Entity 키는 [계획].

---

## 2026-11-02 — v0.5 M8-T3: 함수 참조 · 캡처 없는 화살표 · 콜백 메서드

**무엇을**

- 바이트코드 형식 7: 타입 Func (매개변수 → 결과) · 명령 LOADFN (값 = 함수 번호 + 1, 0 = 없음). 검증기는 Func 를 레지스터에만
  두게 한다 (배열 원소 · 필드 · 상자 · 함수 매개변수 · 결과 거절) — 바깥 (call 인자 · Host 결과 · 객체) 에서 함수 번호를 넣을
  길이 없어 LOADFN 이 서명을 본 값만 콜백이 된다. 콜백 서명 틀 (FnPred · FnEach · FnMap · FnCmp · FnReduce · MapResult · Same2).
- 인터프리터: run = 준비 + loop(stopDepth). 내장이 콜백을 invoke 로 같은 루프에서 부른다 — 연료 · 깊이 · 칸 · Host 호출 예산이
  하나, 콜백의 트랩은 그 자리 (함수 · pc) 가 그대로 결과에 남는다.
- 내장 9: sort(cmp) (안정 — 16 덩어리 삽입 + 병합, 복사본 정렬 후 다시 쓰기, NaN = 0) · find · findIndex · some · every ·
  filter · map · forEach · reduce (초기값 필수). 비교 · 원소마다 연료 1 + 콜백 연료.
- 파서: 화살표를 Arrow 마디로 (async 화살표는 S305, (a): T => … 의 => 를 함수 타입으로 읽지 않게). 검사기: 콜백 인자 자리만
  (그 밖 S304), 숨은 최상위 함수 "<화살표 줄:칸>", 매개변수 · 반환 타입은 문맥 · 적은 타입, 바깥 함수 · 화살표의 지역 이름은
  캡처 S304. 함수 이름 참조 (콜백 인자로만 — 그 밖은 S411). 코드 생성: 식 몸 = RETV.

**왜**

- ADR-0085 6 · 8 — 콜백의 우선순위 (1) 함수 참조 (2) 캡처 없는 화살표. 클로저 · 함수 값 변수는 [계획].

**검증**

- test_stdlib_callbacks 6 케이스 (시험 프로그램을 tsc 5.6 --strict 가 받고, JS 로 옮겨 node:22 에서 돌린 값 = 기대값),
  test_parser (화살표 → S305 만 파서), test_stdlib_array (map 은 이제 서명 오류 S404). unit_script 130 케이스. Windows MSVC
  Debug 전체 ctest 73 / 73 통과 (골든 · 리플레이 그대로).

**남은 일**

- M8-T4 Map · Set. 콜백의 세 번째 인자 (배열) · thisArg · 클로저는 [계획].

---

## 2026-11-02 — v0.5 M8-T2: 내장 표 · INTR · T | undefined · Array 메서드

**무엇을**

- script/vm/Intrinsics.hpp: VM 내장 표 (이름 · 인자 수 · 서명 틀 · 결과 틀 · 기본 연료) — 검사기 · 코드 생성 · 검증기 · VM 이
  같이 읽는다. 명령 INTR a b c (.sbc 형식 5 → 6), 검증기는 서명 틀 (배열 · 원소 · 원소 | undefined · 같은 타입 · 기본 타입) 을
  레지스터 타입으로 본다.
- 타입 T | undefined: TypeTable 의 Null · Nullable 에 맛 (index 1), kUndefined, optionalOf. null 맛과 서로 넣을 수 없고 섞으면
  S331 (T | null | undefined) · S404 · S422. 파서는 undefined 를 Null · NullType 마디 (Undefined 표지) 로, 금지 이름 표에서 뺐다.
  좁히기 안내 낱말 (null · undefined).
- Array 메서드 14: pop · shift (T | undefined) · unshift · slice · concat · reverse · fill · copyWithin · splice (지우기만) ·
  indexOf · lastIndexOf · includes · join · sort() (string[]). JS 의 상대 첨자 · NaN · -0 · SameValueZero 그대로, 선택 인자의
  기본값은 코드 생성이 채운다. 수 배열의 sort() 는 S404 (JS 는 문자열 비교), (T | null)[] 의 pop 은 S404.
- 연료: 내장마다 기본 2 + 원소 (옮김 · 찾기 · 복사) 마다 1, 정렬은 n·⌈log2 n⌉ (실제 비교 수는 표준 라이브러리마다 달라 쓰지
  않는다). 새 배열 · 문자열은 아레나 바이트, 길이 · 문자열 상한은 OutOfMemory.

**왜**

- ADR-0085 · 19 25.2 — 콜백 (M8-T3) · 컬렉션 (M8-T4) 의 바탕 (표 · 명령 · undefined).

**검증**

- test_stdlib_array 7 케이스 58 단언. 기대값은 같은 식을 node:22 (Docker node:22-alpine) 에서 돌린 값과 모두 같다, 시험 프로그램을
  tsc 5.6 --strict (sandbox.d.ts · lib ES2020) 가 받는다. Windows MSVC Debug 전체 ctest 72 / 73 — unit_script 의 옛 기대 다섯
  (.sbc 형식 == 5 넷, const x = undefined 가 S413) 을 새 규칙 (형식 = bc::kFormatVersion, S414) 으로 고친 뒤 unit_script 통과
  (124 케이스). 골든 · 리플레이 그대로 (스크립트 없는 콘텐츠 · 샘플 스크립트 모두).

**남은 일**

- M8-T3 (함수 참조 · 캡처 없는 화살표 · 콜백 메서드), splice 의 넣기 인자 · 세 번째 콜백 인자는 [계획].

---

## 2026-11-02 — v0.5 M8-T1: 스크립트 표준 라이브러리 — 분석 · 지원 매트릭스 · ADR-0085 · 계획 다시 쓰기

**무엇을**

- 저장소 분석 (script/lang · vm · api · sdk, core/path · world/SpatialIndex): 배열은 length · push · 첨자 · for-of 뿐, 문자열은
  length, Math 표 17 함수, 함수 값 · new · 제네릭 · undefined 없음, 객체 종류 String · Array · Struct · Box (호출 아레나), 경로는
  개체 단위 비동기 PathfindingService, 공간 색인은 반경 · AABB 순회.
- ADR-0085: 언어 수준 내장 = VM 내장 표 (INTR — 형식 6), 엔진 상태 = Host 표, 스크립트 stdlib (B) 는 쓰지 않음, "sandbox" 이름
  내보내기 하나 (하위 경로 없음), 내장 컬렉션만 new, T | undefined (null 과 다른 타입), 콜백 = 함수 참조 · 캡처 없는 화살표,
  넣은 차례 반복 · SameValueZero · 고정 해시, 안정 병합 정렬, 연료 · 아레나 규칙, 그래프 · 비동기 경로 요청.
- 19 25장 지원 매트릭스 (있음 · M8-Tn · Host · [계획]) 와 의미 (경계 · NaN · -0 · 빈 것 · 연료). 20 10장 Phase 8 (M8-T1 ~ T9) ·
  진행 차례 · M2 ~ M7 과의 충돌 (.sbc 형식 6 · 7, 지문) · 시험 · 벤치 계획, 18 0장 다음 = M8-T2.

**왜**

- 제작자 요청 (TS 같은 컬렉션 · 알고리즘 · 그래프 · 엔진 질의). M7 데모 · M6 템플릿이 쓰므로 M2-T5 나머지보다 먼저.

**검증**

- 문서만 — 코드 · 골든 그대로.

**남은 일**

- M8-T2 ~ T9 (20 10장 Phase 8), 그다음 M2-T5 나머지.

---

## 2026-11-02 — v0.5 M2-T5 (1): game.owner — 엔진 컴포넌트 · 접근 표 · 골든 영향 측정

**무엇을**

- 새 엔진 컴포넌트 game.owner { player: u32 } (Persistent · Replicated · EditorVisible — 해시에 든다), 카탈로그 23 개.
- 스크립트 접근 표에 Owner (player I32 — 읽기 server · client, 쓰기 server). static_assert · 쓰기 목록에 GameOwner.
- 지문 → 0xbc0adcf8244e2bca, sandbox.d.ts 셋 다시 만듦. 시험: 카탈로그 수 23 (test_catalog_serialization), 표 = 카탈로그 시험이
  Owner 를 본다.

**왜**

- 20 3.6 · ADR-0080 7 — 행동 · 신호 · spawn 의 "누구의 것" 과 클라이언트 "@owned" 의 근거. 첫 단계에서 골든 영향을 잰다 (20 의 약속).

**검증 (골든 영향)**

- Windows MSVC Debug 전체 ctest — det_* (random_walk · world_save_load · eco_lifecycle · 리플레이 넷) 모두 통과 = 해시 그대로. WorldHash
  는 풀이 있는 Hashed 컴포넌트만 센다 (붙은 개체가 없으면 풀이 없다). 세이브는 components 버전 표에 "game.owner": 1 이 더해질 뿐
  (읽기는 그대로) → kSimVersion 4 그대로 (04 6장에 줄).
- 그 실행에서 실패는 unit_ecs 하나 — 카탈로그 수 22 를 센 시험 (고쳤다). 고친 뒤 전체 ctest 72 / 73 — 실패한
  net_server_probe_smoke 는 포트 47791 충돌 (같은 시각 다른 작업 트리에서 이 시험을 100 번 돌리던 서버에 client #2 로 붙었다 —
  stderr "UDP 포트를 열지 못했습니다"). 혼자 다시 10 번 = 10 / 10 통과.

**남은 일**

- M2-T5 나머지: 행동 · 신호 · spawn 이 owner 를 적기, action · signal 이벤트, PlayerSignal (signals.json · 프로토콜 15), ctx.players ·
  session.

---

## 2026-11-02 — v0.5 M2-T4: ctx.time · random · randomInt · near · nearWith

**무엇을**

- Host 표 +5 (서버): ctx.time (속성 — tick / 30) · random () · randomInt (lo, hi — 양 끝 포함) · near (x, y, r) · nearWith (x, y, r,
  Comp). ABI 는 그대로 (Vec2 없이 x, y).
- 난수 흐름: RandomService 에 Purpose::Script (= 10, 뒤에 더함), 키 = fnv1a64(시스템 id). ScriptHost 가 틱마다 (키 → CounterRng) 를
  두고 같은 시스템의 호출들이 이어 쓴다 — 시스템을 더하거나 빼도 다른 시스템의 수열은 그대로, 세이브할 상태 없음.
  SimulationWorld::random() 접근자.
- near: 단계 4 공간 색인 (forEachInRadius) → saveId 오름차순, nearWith 는 풀 소속으로 거름, 반경 0 ~ 128 · 1,024 개 상한
  (HostError), 연료 개체마다 1.
- 지문 → 0x10737afc6916fb38 (kHostApiVersion 1 그대로), sandbox.d.ts 셋 다시 만듦.
- 시험: test_script_random 6. 문서: 19 16.3, 20 (3.3 · M2-T4), 03 9장 (Purpose 표), 04 6장, 18, 13.

**왜**

- 20 3.3 · ADR-0080 4 · 5. 20 과 다른 점: near 의 순서를 netId 가 아니라 saveId 로 (세이브 → 로드 뒤에도 같게 — query 와 같다),
  위치는 (x, y).

**검증**

- 스크립트 suite 117 케이스, SandboxTests 670 케이스, check_includes 0 위반. kSimVersion 그대로 — 스크립트 없는 골든 그대로.
- Windows MSVC Debug 전체 ctest 72/73 — net_server_probe_smoke 가 한 번 서버 충돌 (access violation). 충돌 보고 (M1) 의 콜스택:
  ServerHost::simStep → ReplicationWriter::build → collect → vector<Current>::push_back (ReplicationWriter.cpp:256) — 스크립트가
  없는 random_walk_1k (이번 변경의 길이 아니다). 같은 시험 20 번 되풀이는 모두 통과 — 간헐적. 복제 쪽의 경쟁 · 힙 손상이 의심되어
  따로 조사한다 (남은 일).

**남은 일**

- M2-T5 플레이어 · 세션 · game.owner · action · signal (프로토콜 15 — 골든 영향 측정).
- 간헐적 서버 충돌 (ReplicationWriter::collect — 위) 조사.

---

## 2026-11-02 — v0.5 M2-T3: 스크립트의 구조 변경 — spawn · destroy · add · remove · 상한

**무엇을**

- Host 표 +4 (서버, 효과 Structural): ctx.spawn(prefab, x, y): Entity | null · destroy(e) · add(e, Comp, value) · remove(e, Comp).
  ABI EntityOrNull 을 끝에 더함 (검사기 = nullable Entity, 레지스터 = 상자, 인터프리터가 결과를 상자로, 검증기 · 로더 범위).
- core/script/ScriptHost: 호출마다 ScriptStructuralBatch 에 모은다. 같은 호출의 새 개체는 표지 0x5E01 핸들 (read null · has false ·
  write HostError · add · destroy · remove 의 대상). 상한 (호출마다 spawn 64 · destroy 256 · add/remove 256, 틱마다 spawn 1,024) ·
  없는 Prefab · 경계 밖 · NaN · 죽은 핸들 · 엔진 컴포넌트는 버리고 센다. add 의 값은 write 와 같은 검사 (encodeDynamic — write 에서
  나눔). endCall(true) 가 월드 큐로, 트랩이면 버림.
- core/simulation: ScriptStructural.hpp (Op · Batch · Stats), SimulationWorld::queueScriptStructural · applyScriptStructural — 16b 는
  17 의 Native ECB · Prefab 생성 다음, 17d 는 그 끝에 넣은 차례로 (instantiate · registry.destroy · emplaceDefaultRaw + 값 · pool
  remove → syncIdentityLogs — 정체성 = 만든 차례). 통계 scriptStructuralStats (세이브 · 해시 밖).
- 검사기: add · remove 에 엔진 컴포넌트 = S404 (붙이기 권한이 표에 없다 — [계획]).
- 지문 0x873d… → 0x02fafbb0c03cb29b (kHostApiVersion 1 그대로), sandbox.d.ts 셋 다시 만듦.
- 시험: test_script_structural 6. 문서: 19 16.2, 20 (3.2 · M2-T3), 03 2장, 04 6장, 18, 17, 13.

**왜**

- 20 3.2 · ADR-0080 2 · 3 — 보드 · 수집 · 생성 규칙 (오목의 돌 놓기). 20 과 다른 점: spawn 은 (x, y) — Vec2 ABI 를 새로 두지
  않으려고, add 의 값은 필수, 엔진 컴포넌트 붙이기는 [계획].

**검증**

- 스크립트 suite 111 케이스, Windows MSVC Debug 전체 ctest 73/73 · SandboxTests 664 케이스, check_includes 0 위반.
  kSimVersion 그대로 — 스크립트 없는 골든 그대로 (det_* 통과).

**남은 일**

- M2-T4 random · near · time. 엔진 컴포넌트 붙이기 권한 (표에 attach), spawn 결과의 세이브 → 로드 · 리플레이 시험은 M5 (리플레이
  뷰어) 와 함께.

---

## 2026-11-02 — v0.5 M2-T2: 엔진 컴포넌트 쓰기 규칙

**무엇을**

- 접근 표에 쓰기 (EngineWrite None · Server) 와 필드 규칙 (EngineFieldRule ClampToWorld) — 쓰기 Server: Transform · Velocity ·
  Movement · Health · Energy, 엔진 관리 (None): Collider · GameSession. ClampToWorld: Transform.position · Movement.goal.
- core/script/EngineComponentAccess::applyEngineFieldValues: 칸 → 유한 · f32 확인 → ClampToWorld 는 WorldGrid::clampPoint →
  JSON patch → 카탈로그 patchJson (FieldMeta 범위 · 타입, 원자적 — 엔진 명령과 같은 검사). 표 검사에 ClampToWorld 는 Vec2 만 ·
  쓰기 Server 항목은 trivially copyable 목록 (static_assert) 안.
- ScriptHost::write: 권한 먼저 (쓰기 None 이면 개체에 없어도 "엔진 관리"), 그다음 현재 바이트 복사 → apply → 쓰기 버퍼 (스크립트
  컴포넌트와 같은 람다 — 호출 끝에 쓴 차례로, 트랩이면 버림, read-your-writes).
- 검사기: ctx.write 는 쓰기 None 항목만 S404 ("엔진 관리"). d.ts: 쓰기 None 항목만 readonly, 머리 줄에 쓰기 도메인. 지문에 쓰기 ·
  필드 규칙 (0xd03b… → 0x873db25651702f35, kHostApiVersion 1 그대로), 커밋된 sandbox.d.ts 셋 다시 만듦.
- 시험: test_engine_components +3 (버퍼 · 범위 · NaN · 트랩이면 앞 쓰기도 버림 / 경계 자름 · 다음 틱 공간 색인 / 체력 0 → 죽음 ·
  같은 WorldHash), 검사기 · d.ts 갱신, 보호 시험 (Transform 쓰기 거절 → Collider "엔진 관리"). 문서: 19 16.1, 20 (3.1 ·
  M2-T2), 04 6장 (simVersion 그대로 — 이유), 18, 17, 13.

**왜**

- 20 3.1 · ADR-0080 1 — 스크립트로 이동 · 체력 규칙을 쓴다. 범위를 표에 다시 적지 않고 카탈로그 FieldMeta 를 쓴다 (엔진 명령과
  한 규칙 — 어긋날 수 없다).

**검증**

- 스크립트 suite 105 케이스 통과, Windows MSVC Debug 전체 ctest 73/73 · SandboxTests 658 케이스, check_includes 0 위반.
  kSimVersion 그대로 — 스크립트 없는 골든 그대로 (det_* 통과).

**남은 일**

- M2-T3 구조 변경 (spawn · destroy · add · remove — 호출 버퍼 → ECB · 상한), M2-T4 random · near · time.

---

## 2026-10-29 — v0.5 M2-T1: 엔진 컴포넌트 접근 표 · 읽기 (server · client) · d.ts

**무엇을**

- script/api/EngineComponents: 접근 표 kEngineComponents — Collider · Energy · GameSession · Health · Movement · Transform ·
  Velocity (id · 스크립트 이름 · 읽기 도메인 · 필드 이름 순 · 종류). Collider · Movement 는 복제하지 않아 서버만.
- 검사기: import { Transform } from "sandbox" → ComponentRef<Transform> + 데이터 타입. ctx.write 에 엔진 컴포넌트 = S404 "읽기만",
  클라이언트 함수에 서버만 항목 = S404, 표에 없는 이름 = S416. 심볼 · 타입 인덱스에 kEngineComponentFlag (P.components 와 따로 —
  카탈로그에 넣는 스크립트 정의와 섞지 않는다). 코드 생성은 LOADK 의 id = 표의 id (stableId 규칙이 같다 — fnv1a64).
- 검증기: 엔진 이름공간 id 는 접근 표에 있을 때만 (v0.3 의 "엔진 이름공간 전부 거절" 을 좁힘).
- core/script/EngineComponentAccess: 카탈로그의 writeJson 에서 표의 필드만 칸으로 (I32 · F32 → f64 · Bool · Vec2 두 칸 —
  스크립트 컴포넌트와 같은 칸 규칙), checkEngineAccessTable (표 = 카탈로그). ScriptHost · ClientScriptHost 의 read · has ·
  query 가 받는다 (클라이언트는 Both 만), write 는 엔진 컴포넌트를 거절 ("읽기만").
- SDK: 항목마다 interface · const (readonly 필드). Host API 지문에 표 (0xe02f… → 0xd03bc50223a4ef73, kHostApiVersion 1 그대로
  — 더하기만, ADR-0080 9). 커밋된 sandbox.d.ts 셋 (sdk · samples/mana_regen · samples/stash) 을 다시 만들었다.
- 시험: test_engine_components 5, test_script_protection (위조 id 를 표에 없는 core.lifetime 으로 — core.transform 은 이제 읽기
  허용, Transform 쓰기 거절 추가), test_checker (지문 고정값). 문서: 19 16 · 16.1, 20 (3.1 · M2-T1), 18 (0 · 6.6), 17, 13.

**왜**

- 20 3.1 · ADR-0080 1 — 스크립트가 HUD · 규칙에서 위치 · 체력을 본다 (X2 게임플레이 API 의 첫 단계). 쓰기는 M2-T2.
- 20 3.1 과 다른 점: 표를 core/script 가 아니라 script/api 에 (검사기 · d.ts 가 Foundation 만 안다), tags · prefab · score 는
  칸 종류가 없어 [계획].

**검증**

- Windows MSVC Debug 빌드 · 전체 ctest 73/73 · SandboxTests 655 케이스, check_includes 0 위반. 시뮬레이션은 바뀌지 않았다 — 엔진
  컴포넌트 읽기만 (det_* 골든 그대로).

**남은 일**

- M2-T2 쓰기 규칙 (경계 자름 · 범위 · 공간 색인 갱신은 엔진이), M2-T3 구조 변경, game.owner (M2-T5).

---

## 2026-10-29 — v0.5 M1-T4 (4): 릴리스 피드 URL — 시스템 curl (ADR-0084) · SandboxProcess → M1 끝

**무엇을**

- 프로세스 실행기를 tools/sbx/Process 에서 새 타깃 SandboxProcess (platform/process — 의존 Foundation 만) 로 옮겼다
  (git mv). sbx 는 package::ProcessResult 로 바꾸는 어댑터만. 경계: SandboxProcess 는 Core · Network · Platform · Render ·
  Editor · ImGui 를 링크하지 않는다.
- ReleaseFeed: https:// URL 은 시스템 curl 을 셸 없이 (curlArguments — https 만 · 리디렉트도 · 5 초 · 1 MiB · "--" 뒤 URL,
  평문 http 등 거절). checkReleaseFeed 는 로그를 쓰지 않고 FeedCheck (알림 · 이유 한 줄) — 클라이언트는 URL 을 떼어 낸
  스레드 (packaged_task) 에서 읽고 Application::pollLateFeed 가 프레임에서 받아 로그 · "알림" 창.
- 기본 피드 URL 은 넣지 않았다 (공개 채널이 생기는 M7-T4 에서) — 사용자가 줄 때만 외부로 요청한다.
- ADR-0084. 문서: 20 (7.6 · M1-T4 ✅), 18 (0 — 다음 M2-T1 · 6.6), 01 (타깃 표), 15, 17, 13.

**왜**

- 20 7.6 의 공개 채널 읽기. 새 서드파티 없이 (ADR-0072 의 git 과 같은 길) — 대안 (OS API 셋 · libcurl) 은 ADR-0084.

**검증**

- test_user_settings +1 (URL 인자 · 거절 · 거절한 스킴은 curl 을 띄우지 않음), client_safe_mode 에 닿지 않는 https · 평문 http.
- 실행: `SandboxClient --headless --frames 400 --update-feed https://api.github.com/repos/blackvrice/Sandbox/releases` → "새 버전
  없음 (지금 0.4.0, 피드 0 개)", 릴리스가 있는 공개 저장소 피드 (cli/cli) → "새 버전 2.103.3 이 있다 … 받기 · 설치는 직접" 알림.
- 전체 ctest 73/73 · SandboxTests 650 케이스, check_includes 0 위반. CI 는 push 뒤.

**남은 일**

- M7-T4: 공개 채널 · 기본 피드 URL · GitHub Releases 에 묶음 올리기.

---

## 2026-10-29 — v0.5 M1-T4 (3): 배포 묶음 — sbx_release (zip · 기호 · SHA-256) · 실행 파일 옆 데이터 폴더 · CI 아티팩트

**무엇을**

- foundation/io/AppPaths: executablePath (Windows GetModuleFileNameW 선언만 — Console.cpp 처럼 windows.h 없이, Linux
  /proc/self/exe, macOS _NSGetExecutablePath), dataDir(이름, 빌드 때 경로) — 실행 파일 옆 폴더가 있으면 그것.
  SandboxServer · SandboxClient · sbx (new --from) 가 content/ · assets/ 를 이것으로 찾는다. 서버 로그 "콘텐츠 루트 …".
- tools/release: 타깃 sbx_release → make_release.cmake — sandbox-<버전>-<플랫폼>.zip (실행 파일 · content · assets ·
  samples · sdk · docs · README · BUILD.txt, .sandbox · *-saves 빼고) · -symbols.zip (PDB) · SHA256SUMS.txt. cmake -E tar
  --format=zip (세 OS 같은 길, 추가 도구 없음).
- CI windows 잡: 시험을 통과한 RelWithDebInfo 로 sbx_release → actions/upload-artifact (14 일).
- CTest release_bundle. 문서: 20 7.7 · 7.8, 15, 17, 13, 18.

**왜**

- 빌드가 저장소의 절대 경로를 실행 파일에 넣어서, 그대로 zip 으로 옮기면 다른 PC 에서 콘텐츠 · 에셋을 찾지 못했다 — 배포
  묶음 (7.7 · C1 "패키지만 옮겨 실행") 의 바탕.

**검증**

- release_bundle: 묶음을 build/…/unpacked 에 풀어 SandboxServer 가 "콘텐츠 루트 …/unpacked/sandbox-0.4.0-test-x/content",
  ecosystem_small · samples/rabbit_guard (--start-game) 30 틱, sbx doctor 오류 0, SandboxClient --headless 30 프레임.
- `cmake --build --preset windows-msvc-debug --target sbx_release` → zip 9,869,360 바이트 (287 파일) · 기호 zip ·
  SHA256SUMS.txt. 전체 ctest 73/73 (+ release_bundle), check_includes 0 위반. CI 아티팩트는 push 뒤 확인.
- 다른 PC (깨끗한 Windows) 에서 푸는 것은 하지 않았다 — 같은 PC 의 무관한 폴더까지 (M7-T4 수동 QA).

**남은 일**

- M1-T4 마지막: 릴리스 피드 URL (7.6). GitHub Releases 에 올리기는 M7-T4.

---

## 2026-10-29 — v0.5 M1-T4 (2): macOS CI — sbx_net_probe 의 부동소수 from_chars

**무엇을**

- 9ee17fa 의 CI (run 40823345024): windows-msvc · linux-gcc · linux-clang · linux-clang-asan 은 처음으로 통과 (Debug ·
  RelWithDebInfo 빌드 + 전체 ctest — Windows 는 render_tests_warp 포함). macOS 하나만 실패 —
  `tools/net_probe/NetProbeOptions.cpp:24: call to deleted function 'from_chars'` (Apple libc++ 에는 부동소수 from_chars 가
  없다). `__cpp_lib_to_chars` 가 없으면 C 로케일에 묶은 strtod_l (script/lang/Lexer.cpp 가 이미 쓰는 길).

**검증**

- Windows MSVC Debug 빌드 · 전체 ctest 72/72, test_net_probe_options 2 통과. macOS 는 push 뒤 CI (끝에 적는다).

---

## 2026-10-29 — v0.5 M1-T4 (1): CI 를 처음으로 녹색으로 — 경고 = 오류 고침 · render_tests_warp 배리어

**무엇을**

- CI (`.github/workflows/ci.yml` — Windows MSVC · Linux GCC · Clang · Clang ASan · macOS) 가 첫 실행 (2026-10-14) 부터 59 번
  모두 실패 · 취소였다 (gh run view --log-failed 로 확인). 원인은 CI 만 켜는 경고 = 오류 — 로컬 빌드는 꺼져 있어 쌓였다:
  - MSVC: C4702 (JsonVisitor decode 의 가지가 바로 돌려줌 · 시험의 break 루프), C4996 (InspectorPanel strncpy · 시험 getenv),
    C4127 (test_assert 상수 조건)
  - GCC · Clang: -Wmissing-field-initializers (집합 초기화에서 기본값 없는 필드 — ScriptPackProgram::events ·
    SaveOptions::game · Firing::tags · ReportInfo::stack · HostResult::values 에 기본 초기화자 {}, Codegen 의 TypeDesc 는
    도우미로), -Wtrigraphs (Checker 메시지의 "??=" → "?\?="), -Wshadow (setScriptClock 의 clock), -Wunused-local-typedef
- render_tests_warp: 시험 (world passes · imgui · sprites) 이 같은 대상에 두 번 그리며 배리어 "이전 상태" 를 늘 Undefined 로
  줬다 — 실제는 readback 뒤 CopySrc. D3D12 Debug Layer 가 오류로 막아 v0.2 부터 로컬에서 빼고 돌렸다 (18 의 "이 기계에서
  실패") — 엔진이 아니라 시험 버그. 대상 상태를 기억한다.
- project_doctor: 환경 줄 기대값이 Windows 전용이었다 (Linux 는 지원 매트릭스상 경고가 맞다).
- ci.yml 빌드에 `-- -k 0` (실패해도 끝까지 컴파일). 13 8장을 실제 잡 · 현황 · 로컬 재현 방법으로 고침.

**왜**

- M1-T4 완료 기준 "CI 녹색" — 그리고 이식성 문제를 CI 가 잡게 (13 8장의 원래 목적).

**검증**

- Windows MSVC 경고 = 오류 (build/ci-werror): Debug 빌드 · ctest 72/72 (render_tests_warp 포함), RelWithDebInfo 빌드 ·
  ctest 75/75. 정식 프리셋 빌드 · 전체 ctest 72/72 (render_tests_warp 포함 — 다시 넣었다), check_includes 0 위반, det_* 골든
  그대로 (기본 초기화자 · 이름 바꾸기 — 시뮬레이션 동작 무관).
- Linux (Docker ubuntu:24.04 + ci.yml 의 패키지): GCC 13 · Clang 19 Debug 경고 = 오류 빌드 통과, ctest 76/77 — 남은 하나가
  project_doctor 환경 줄 (위에서 고침). Linux ASan · RelWithDebInfo 와 macOS 는 로컬에서 끝내지 못했다 (Docker VM 이 동시
  빌드 넷에 내려갔다) — push 뒤 CI 결과로 본다 (끝에 적는다).

**남은 일**

- M1-T4 나머지: 배포 묶음 (zip · SHA-256) · 릴리스 피드 URL · 지원 매트릭스 문서.
- 로컬 기본 빌드도 경고 = 오류로 할지 (다시 쌓이지 않게) — 정하지 않았다.

---

## 2026-10-29 — v0.5 M1-T3: 사용자 설정 · 안전 모드 · 새 버전 알림

**무엇을**

- apps/client/UserSettings: 사용자 설정 폴더 (SBX_CONFIG_DIR · %APPDATA%\sandbox · macOS Application Support · XDG) 의
  client.json (schemaVersion 1 — uiScale · updateCheck · updateFeed) · input.json (키 바인딩). 클라이언트에 저장되는 사용자 설정이
  처음 생겼다 (그전엔 --input 뿐). 깨진 파일은 "<이름>.broken-<UTC>" 로 옮기고 알림 · 기본값, 더 새 판은 그대로 두고 읽지 않는다.
- --safe-mode: 두 파일 · UI 배율 · 패키지 캐시 (매번 빈 sbx-packages-safe — openWorldSource · LocalServerDesc ·
  NetworkSessionDesc 에 packageCache 자리) 없이 시작.
- apps/client/ReleaseFeed: GitHub Releases "List releases" 형식 피드 (파일) → 새 버전이면 알림만. --update-feed · client.json
  updateFeed (updateCheck false 면 끔). URL 은 [계획] (M1-T4 — 채널 · HTTPS 클라이언트와 함께).
- 알림: 로그 "알림: …" + ImGui "알림" 창 (F1 과 무관, 닫기). ImGuiLayerDesc.uiScale (DPI 배율에 곱한다).
- 시험: test_user_settings 4, CTest client_safe_mode. 문서: 20 (7.5 · 7.6 · M1-T3 · M1-T4 에 피드 URL), 18 (0 · 6.6), 15
  (클라이언트 옵션), 07 8장 (설정 폴더), 17, 13.

**왜**

- 20 7.5 · 7.6, 초안 요구 4 와 21 의 12번 기능 (업데이트 · 복구) 중 v0.5 몫 — 깨진 설정이 시작을 막지 않게, 새 버전을 알게.
- 피드를 파일로 먼저: 공개 Alpha 채널이 아직 없다 (M1-T4) — 없는 URL 을 읽는 코드는 검증할 수 없다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 71/71 통과 (+ client_safe_mode) · SandboxTests 649
  케이스, check_includes 0 위반. 새 경고 없음 (test_client_view.cpp:150 C4702 는 그 파일이 다시 컴파일되며 보인 기존 경고 —
  이번 변경과 무관). det_* 골든 그대로 (시뮬레이션 무관).
- 실행: CTest 출력 — "× 2.00" (uiScale 2) · "알림: 안전 모드 …" · "× 1.00" · "설정 파일 input.json 이 깨져 input.json.broken-
  20261029-032032 로 옮겼다 (JSON 구문 오류)" · "새 버전 99.0.0 이 있다 (지금 0.4.0) — https://example.invalid/v99". 창 모드
  (D3D12) `SandboxClient --console --frames 90 --safe-mode` → 알림 창과 함께 90 프레임 · Shutdown.
- macOS · Linux 의 설정 폴더 경로는 이 환경에서 실행하지 않았다.

**남은 일**

- M1-T4: 피드 URL (HTTPS) · CI · 배포 묶음. 클라이언트 오류 화면 (doctor 문장 · 복사 단추) 은 여전히 [계획].
- 설정을 바꾸는 UI (배율 · 업데이트 확인 끄기) 는 없다 — 파일을 고친다 (M4 다국어 · 접근성 설정과 함께).

---

## 2026-10-28 — v0.5 M1-T2: sbx doctor · sbx report · 충돌 콜스택 · 문맥

**무엇을**

- core/project/Diagnostics: `diagnose` → DiagnosticReport — 영역 (엔진 · 환경 · 패키지 · 매니페스트 · 엔진 범위 · 의존 · 설치 ·
  콘텐츠 · 시작 월드 · 도구) 마다 수준 · 한 줄 문장 · 고치는 법. 검사는 validateProject · readPackageIndex · extractPackage 그대로
  쓰고 결과를 묶어 문장을 붙인다. 엔진 범위는 범위의 비교자로 "더 새 엔진 (엔진 X 이상을 받는다)" · "옛 엔진용" 을 가른다. 깨진
  패키지는 머리 · 목록 (잘렸거나 .sbpkg 가 아님) 과 내용 (목록과 다름) 을 가르고, 풀지 못해도 목록의 엔진 범위를 본다. 지원
  매트릭스 (Windows 통과, Linux · macOS "검증 안 됨" 경고).
- 보고 묶음 읽기 `readReportBundle` · `listReportBundles` · `formatReportBundle` (덤프 알림 · 여는 법 · 버전 차이, 더 새 형식 거절).
- sbx doctor <폴더 | .sbpkg> (오류면 종료 1) · sbx report [<묶음 | 이름>] [--lines] [--reports-dir] (없으면 목록).
- 충돌 처리기: 콜스택 (report.json "stack" — Windows 예외 문맥 StackWalk64 + PDB 기호 · 줄, 파일은 이름만 / POSIX backtrace),
  `crash::setContext` (잠금 — 처리기는 try_lock). 서버는 월드를 연 뒤 game · package · packs 를 문맥에, --crash-test 도 --world
  면 연 뒤. 클라이언트 문맥 connect · world.
- 시험: test_diagnostics 6, CTest project_doctor, crash_report (콜스택 · 문맥 · sbx report). 문서: 20 (7.1 · 7.2 · 7.3 · M1-T2),
  18 (0 · 6.6), 15 (sbx doctor · report · --crash-test), 07 8장, 17, 13.

**왜**

- 20 7.2 · 7.3 · 완료 기준 C2 ("일부러 만든 호환성 문제를 doctor 가 문장으로") 와 초안 요구 4 (에러 대시보드의 로컬 몫).
- 콜스택이 없으면 묶음이 "access violation at 0x…" 뿐이라 덤프를 열 줄 아는 사람만 원인을 본다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 70/70 통과 (+ project_doctor) · SandboxTests 645
  케이스, check_includes 0 위반. det_* 골든 그대로 (시뮬레이션 무관).
- 실행: `sbx doctor samples/rabbit_guard` · `samples/stash` → 영역마다 통과 · "진단: 오류 0 · 경고 0". crash_report_server 묶음을
  `sbx report` 로 — 콜스택 9 프레임 (#0 crashForTest (CrashHandlerWin.cpp:177) · #1 runWorldServer (main.cpp:162) · #2 main …),
  문맥 packs = eco · world, 덤프 알림.
- POSIX backtrace 경로는 이 환경에서 컴파일 · 실행하지 않았다.

**남은 일**

- 클라이언트 오류 화면 (같은 문장 · 복사 단추) · GPU · 드라이버 항목 — [계획] (M1-T3 안전 모드와 함께).
- Release 빌드의 PDB 배포 여부 (기호 없는 콜스택은 모듈+오프셋만) — M1-T4 배포 묶음에서 정한다.

---

## 2026-10-28 — v0.5 M1-T1: 충돌 처리 · 로그 고리 · 보고 묶음 · 민감정보 제거 (새 타깃 SandboxCrash)

**무엇을**

- foundation/log: 최근 줄 고리 (마지막 256 줄 · 줄마다 1,024 바이트 — 싱크와 무관), recentLines · recentLinesForCrash (잠금을 못
  잡아도 읽는다).
- foundation/diag: scrub (비밀 → <SECRET> · 사용자 폴더 — '\' · '/' · 대소문자 → <HOME> · 사용자 이름 낱말 → <USER> · IPv4 → <IP>
  (127.0.0.1 · 0.0.0.0 그대로) · 전자우편 → <EMAIL>), 보고 묶음 createReportDir (<UTC 시각>-<앱>, 같은 초 -2) · writeReport
  (report.json — 앱 · 이유 · 시각 · 버전 · 커밋 · 툴체인 · 명령줄 · 문맥 · 파일 목록, log.txt — 모두 거른다), 보고 폴더
  (SBX_REPORTS_DIR · %LOCALAPPDATA% · XDG).
- platform/crash — 새 타깃 SandboxCrash (의존 Foundation 만 — 서버는 SandboxPlatform 을 링크하지 않고 platform/common 은 OS 헤더
  금지라 따로): Windows SEH → 미니덤프 crash.dmp (dbghelp) · std::terminate · SIGABRT, POSIX 시그널 (대체 스택 · 다시 올림).
  경계 규칙 (SandboxCrash 는 Core · Network · Platform · Render · Editor · ImGui 를 링크하지 않는다).
- SandboxServer · SandboxClient: 시작할 때 처리기 (소유자 열쇠를 비밀로), --crash-test (시험용 — 일부러 충돌).
- 시험: test_diag 3, CTest crash_report_server · crash_report_client. 문서: 20 (7.1 · M1-T1 — 자리를 platform/crash 로 고침),
  01 (타깃 표), 07 8장, 15 (서버 · 클라이언트), 17, 13, 18 (0 · 6.6).

**왜**

- 초안 v0.5 요구 4 (버그 리포트 · 충돌 로그 — 민감정보 제거) 와 완료 기준 C2 (다른 환경의 원인 진단) 의 바탕.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 69/69 통과 (+ crash_report_server · client), 첫
  실행에서. check_includes 0 위반. det_* 골든 그대로 (시뮬레이션 무관).
- 실행: `SBX_REPORTS_DIR=… SandboxServer --world ecosystem_small --port 0 --owner-key HUNTER2SECRET --crash-test` → stderr "충돌
  보고: …/20261028-094757-SandboxServer", 묶음 crash.dmp · log.txt · report.json ("exception 0xC0000005 (access violation) at …",
  version 0.4.0 · commit · toolchain, 명령줄의 열쇠는 "<SECRET>", files [log.txt, crash.dmp]), log.txt 에 마지막 줄 "[error] [crash]
  --crash-test: 일부러 잘못된 메모리에 쓴다".
- Linux · macOS 의 POSIX 처리기는 이 환경에서 컴파일 · 실행하지 않았다 (지원 매트릭스 — 검증 안 됨).

**남은 일**

- M1-T2: sbx report (묶음 읽기 — 덤프에 메모리가 든다는 알림) · sbx doctor. 묶음에 게임 · 패키지 digest 넣기.
- 스택 넘침의 덤프 (별도 스레드에서 쓰기) 는 [계획].

---

## 2026-10-28 — v0.5 계획: Creator Alpha 확장 설계 · 구현 상태 분석 · v0.6~v1.0 배치 (ADR-0078~0083) — 문서만

**무엇을**

- docs/21-PLATFORM-FEATURES.md (분석): v0.1~v0.4 의 구현 상태를 코드 · 시험 · 문서로 대조 (Implemented · Partial · Planned · Missing —
  게임 UI · 다국어 · 오디오 출력 · 충돌 보고 · 자동 업데이트는 Missing, 리플레이 보기 · 관전 · 입력 · Host API 는 Partial), 신규 기능
  12 종의 분석 (겹침 · 신규 · API · 의존 · 서버 권위 · 복잡도 · 버전 · 시험), 외부 참고의 확인 범위 (이 작업에서 외부 원문을 다시 보지
  않았다고 적음), v0.5~v1.0 배치 표와 초기 제안에서 바꾼 것.
- docs/20-CREATOR-ALPHA.md (계획 · 규범): 초안 v0.5 요구 R1~R6 · 완료 기준 C1~C4 그대로 + 확장 X1~X5, 게임 UI (문서 형식 · 배치 ·
  바인딩 · 이벤트 · 입력 순서 · 상한), 게임플레이 API 계약 (접근 표 · 구조 변경 · random · near · 플레이어 · 이벤트 · 신호 · client),
  리플레이 뷰어, 다국어 · 접근성 기반, 실행 경계 · 프로파일, 진단 · 보고 · 채널 · 지원 매트릭스, 제작 UX · 템플릿, 스크립트 개선,
  Phase 7 개 · 마일스톤 M1~M7 · 태스크 34 개 (모듈 · 인터페이스 · 선행 · 시험 · 완료), 데모 · 사용자 흐름 1~9, 시험 · 벤치 계획,
  완료 기준 · 릴리스 차단, 위험.
- ADR-0078 (범위) · 0079 (게임 UI — GraphicsUI 를 기다리지 않음) · 0080 (게임플레이 API) · 0081 (실행 경계 — JIT/AOT v1.x) · 0082
  (v0.6~v1.0 배치) · 0083 (다국어 · 접근성).
- 18-PLATFORM: 0장 (다음 = v0.5 M1-T1), 1장 표, 2장 표의 낡은 줄 (패키지 Partial → Implemented, 노드 캔버스 v0.6, 새 줄), 6장 요약 ·
  6.1 결정 D-12~D-16 · 6.6 v0.5 마일스톤, 7장. docs/README (지도 · 읽는 순서, "프로토콜 지금 13" → 14), 19 의 [계획] 두 곳을 v0.5
  계획으로, 13 · 14 머리에 v0.5 계획 가리키기.

**왜**

- v0.5 진입 전 계획 커밋 (v0.4 와 같은 방식). 요청: 초안 v0.5 를 줄이지 않고 게임 UI · 게임플레이 API · 리플레이 · 다국어를 더하고,
  신규 기능 12 종을 의존에 맞게 v0.6~v1.0 에 배치, 구현은 다음 단계.

**검증**

- 문서만 — 코드 변경 없음. 새 문서 · ADR 의 상대 링크가 모두 실제 파일을 가리키는지 확인 (스크립트). 빌드 · 전체 CTest 를 돌려 바뀐
  것이 없는지 확인 (아래 커밋 직전 실행).
- GraphicsUI (D:\source\GraphicsUI) 는 README · ROADMAP 을 읽어 "연구 단계 · 제품 UI Core 없음" 을 확인했다. 외부 엔진 · 플랫폼의
  공식 문서는 이 작업에서 열어 보지 않았다 (21 3장에 그렇게 적음).

**남은 일**

- v0.5 M1-T1 (충돌 처리 · 보고 묶음). M2-T1 (엔진 컴포넌트 접근 표) 도 선행 없이 시작할 수 있다.
- 열린 결정 D-14 (오디오 출력 백엔드) — M2-T6 에서.

---

## 2026-10-28 — v0.4 M6-T2: 버전 종료 — 요구사항 판정 · 릴리스 노트 · 제품 버전 0.4.0 → v0.4 완료

**무엇을**

- docs/releases/v0.4.0.md: 들어간 것 (M1~M6 · ADR-0071~0077), 호환성 번호 (제품 0.4.0 · 프로토콜 14 · kSimVersion 4 · 세이브 1 · 1 ·
  .sbc 5 · Host API 1 그대로, 매니페스트 3 · pack.json license · dependencies · sandbox.lock 1 · 패키지 형식 2 새것), 초안의 요구마다
  done / deferred / removed 와 근거, 미룬 것의 버전 재할당, 알려진 문제 (심볼릭 링크 시험을 이 PC 에서 건너뜀 · 재현되지 않은 실패
  둘 포함), 데모 · 실패 시 기대 오류.
- CMake project VERSION 0.4.0. 14 7.14 (패키지 명령 시간 — sbx RelWithDebInfo 로 resolve · install · check · build · check <패키지>).
- 18 (0 · 1 · 4장 · 6.4 M6 · 6.5 요구사항 검토 · 7 · 8장), README · docs/README 의 버전 표.

**왜**

- v0.4 의 끝 (ADR-0039 2 — 버전마다 요구사항 판정 · 노트 · 제품 버전).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 67/67 통과, SandboxTests 636 케이스. 골든 · 데모
  해시 그대로 (rabbit_guard 0x02d03b52e9655a3f · mana_regen 0x51ddbf1f7d1ab7e9 · stash 0xaf95ab21bf58d0cb).
- 실행: sbx --version "sbx 0.4.0", sbx new 의 엔진 범위 ">=0.4.0 <1.0.0". 패키지 명령 시간 (5 번 중앙값): resolve 87 · 68 ms,
  install 193 · 104 ms (빈 캐시 · 캐시), check 61 ms, build 95 ms, check <패키지> 97 ms.

**남은 일**

- v0.5 Creator Alpha 계획 커밋 (진입 조건 · 열린 결정 → ADR · 마일스톤 · 태스크).

---

## 2026-10-28 — v0.4 M6-T1: 패키지 데모 — samples/packages/inventory 를 설치하는 samples/stash, CTest sample_stash

**무엇을**

- samples/packages/inventory (Feature 패키지 1.0.0 · MIT): 컴포넌트 inventory.bag {items · capacity} + export function store ·
  isFull, 서버 시스템 inventory.collect (30 틱마다 하나씩), Prefab inventory.crate.
- samples/stash (게임 sandbox.stash): sbx new (빈 월드) → content/stash (requires inventory — Prefab stash.cart 가 패키지의
  컴포넌트를, 스크립트 stash.bonus 가 "@inventory/bag" 의 함수를 쓴다) → sbx add inventory --path ../packages/inventory --version
  ^1 (sandbox.lock · tsconfig paths) → 클라이언트에서 Ctrl+S 로 시작 월드를 지금 콘텐츠로 저장 (검사 경고 0). playthrough.json
  (상자 넷 · 수레 둘 · 300 단계에 상자 하나, 900 틱 — expect tick · 해시), README.
- .gitignore 에 .sandbox/ (설치 폴더는 잠금에서 다시 만든다). README.md · 15 · 13 5.1 · 17 · 18 (0 · 6.4 · 7장).
- CTest sample_stash (tests/project/sample_package.cmake): 다른 폴더에 저장소 파일만 복사 → 설치 전 check P8 ("설치되지
  않았다") → 빈 캐시 sbx install → sbx test (오류 · 경고 0, 통과) → sample_playthrough.cmake (헤드리스 서버 = 로컬 플레이 · 게임
  패키지로도 같은 결과 줄).

**왜**

- v0.4 의 끝-끝 목표 (ADR-0071 — "다른 제작자의 Feature 를 설치 → 빌드 → 테스트 플레이") 를 저장소의 데모 하나와 CTest 로.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 67/67 통과 (+ sample_stash). det_* 골든 그대로.
- 실행: sbx check samples/stash 오류 0 · 경고 0 (시스템 inventory.collect — .sandbox/packages/inventory/… · stash.bonus —
  content/stash/…), sbx test "통과 playthrough.json — 게임 없음 · tick 900 · hash 0xaf95ab21bf58d0cb", 헤드리스 서버 로그
  "inventory.collect t300: 보관함 7 · 가득 4" · "stash.bonus t450: 아이템 합 100" (상자 5 × 10 + 수레 2 × 25), 스크립트 트랩 0.
  sample_stash: "같은 결과: 게임 결과: 게임 없음 · tick 900 · hash 0xaf95ab21bf58d0cb" · "패키지도 같은 결과".
- 처음 sample_stash 는 설치 전 check 의 기대 문구를 "잠금 없음" 쪽 (P8 "설치하지 않았다") 으로 써서 실패했다 — 샘플은 잠금을
  담으므로 맞는 P8 은 "설치되지 않았다" 다 (엔진은 맞게 동작) — 시험의 기대값을 고쳤다.
- 시작 월드 저장 (Ctrl+S) 은 클라이언트 창에 키 입력이 가야 해서, 다른 알림 창이 포커스를 잡고 있을 때 바탕화면 셸의 클릭
  권한을 받아 창 제목을 눌러 포커스를 옮긴 뒤 했다 (세이브 로그 "월드 저장 'main' (#1): tick 0 · 엔티티 0").

**남은 일**

- M6-T2: 버전 종료 — 요구사항 done / deferred / removed, releases/v0.4.0, 제품 버전 0.4.0.

---

## 2026-10-28 — v0.4 M5-T2: 라이선스 요약 (SBOM) — .sbpkg 형식 2 → M5 완료 (ADR-0077)

**무엇을**

- core/package/GamePackage: 형식 2 (kFormatVersion 2, 1 도 읽는다). index packs = 읽은 팩마다 (팩 id 순) id · version · license ·
  팩 digest · 폴더. 빌드가 파일 목록에서 팩 digest 를 만들고 (packDigestOf — PackSource 와 같은 규칙), readPackageIndex 가 다시
  계산해 견주며 (모양 · 순서 · 폴더 규칙 포함), verifyPackage · extractPackage 가 pack.json 의 id · version · license 와 견준다
  (checkPackClaims). game digest 는 그대로 (파일 목록만).
- sbx build "팩 n — <라이선스> <수> · …", sbx check <패키지> 팩마다 "팩 <id> <version> · <license> · <폴더> · digest …".
- 시험: test_game_package 1 (+ 형식 번호 0 · 3 거절). 문서: ADR-0077, 09 9장, 15 7장, 18 (0 · 6.4 · 7장), 17, 13.

**왜**

- ADR-0071 의 M5 — 배포한 게임에 든 남의 팩과 그 라이선스 · 버전을 패키지만으로, 고칠 수 없게 확인한다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 66/66 통과, 첫 실행에서. det_* 골든 그대로.
- 실행: 프로젝트 팩 eco + 설치한 inv (MIT) 프로젝트 → `sbx build` "팩 2 — MIT 1 · 라이선스 없음 1" → `sbx check <패키지>`
  "팩 eco 0.1.0 · 라이선스 없음 · content/ecosystem · digest a27f8180…", "팩 inv 0.1.0 · MIT · .sandbox/packages/inv · digest
  2f5485be…" (sandbox.lock 의 inv digest 와 같다), 검사 오류 0 → `SandboxServer --world <패키지> --ticks 30 --exit` 종료 0
  (스크립트 inv.tick 호출 1 · 트랩 0).

**남은 일**

- M6-T1: 데모 samples/packages/inventory 를 다른 게임이 설치 → 빌드 → 자동 플레이.

---

## 2026-10-28 — v0.4 M5-T1: 패키지 거부 시험 · 깨끗한 머신 재현 (SBOM 은 M5-T2 로)

**무엇을**

- tests/unit/project/test_package_rejects: 좋은 설치 뒤 나쁜 소스마다 updateProject (sbx update · add 의 길) 가 거절하고,
  매니페스트 · sandbox.lock · .sandbox/packages 의 모든 바이트가 그대로이며 임시 폴더가 남지 않는다 — 충돌 · 설치 훅 (모르는 키
  "postinstall") · id 바꿔치기 · 깨진 JSON · 스크립트 오류 · 16 MiB 넘는 파일 · 절대 경로 · 위로 나가는 path 의존 · Git
  "ext::" · '-' rev (git 실행 0 번) · 심볼릭 링크. 고친 캐시는 소스가 그대로면 잠금 그대로 다시 설치 (캐시도 회복).
- tests/project/project_reproduce.cmake (CTest project_reproduce): 기계 A 에서 new --pack · new · add · build, 다른 폴더 B 에
  저장소에 둘 파일만 복사 · 빈 캐시로 install · build — game digest · .sbpkg · 잠금 바이트가 같다. B 에서 공유 팩을 고치면
  install 이 거절.
- 엔진 코드 변경 없음 (시험이 찾은 결함 없음). 로드맵: SBOM (패키지 형식 2) 을 M5-T2 로 나눴다.
- 문서: 13 표, 09 12장, 18 (0 · 6.4 · 7장), 17.

**왜**

- ADR-0071 의 v0.4 목표 — "깨끗한 머신에서 같은 잠금 파일로 같은 game digest, 충돌 · 악성 패키지는 설치 전에 거절".

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 66/66 통과 (+ project_reproduce), 첫 실행에서.
- project_reproduce: A · B 모두 game digest 3e0e450044a6d280…, 패키지 · 잠금 바이트 같음. B 의 바뀐 소스는 "'kit': 소스 (path)
  가 잠금과 다르다 (digest ede93ddd… → 71fb120a…) — sbx update" 로 종료 2.
- 심볼릭 링크 경우는 이 기계에서 건너뛰었다 ("A required privilege is not held by the client" — 링크를 만들 권한 없음). 링크
  거절 자체는 이 기계에서 확인되지 않았다 (개발자 모드 · 관리자 · Linux CI 에서 돈다).

**남은 일**

- M5-T2: SBOM — .sbpkg index 형식 2 (팩마다 id · version · license · 팩 digest · 출처), sbx build · check 의 라이선스 줄.

---

## 2026-10-28 — v0.4 M4-T3: 에디터의 팩별 목록 (배치 Prefab · 콘텐츠 창) → M4 완료

**무엇을**

- editor/PackGroups groupByPack: 항목 id 의 첫 마디 = 팩, 출처는 그 팩을 읽은 자리 (ContentSourcePack::folder — "content/…"
  프로젝트 · ".sandbox/packages/…" 설치한 패키지 · 그 밖 팩), 차례 프로젝트 → 설치 → 팩 → 기타, 머리 글 ("eco · 프로젝트",
  "inv 0.1.0 · MIT · 설치한 패키지"). ImGui 를 모른다.
- "편집" 패널의 배치 Prefab 목록과 "콘텐츠" 창의 목록이 묶음마다 SeparatorText 머리를 단다 — 묶음이 하나면 머리 없이 지금처럼.
- 시험: test_pack_groups 1. 문서: 10 2장, 18 (0 · 6.4 · 7장), 17, 13.

**왜**

- ADR-0071 8 — 설치한 Feature 팩의 Prefab · 스크립트를 프로젝트의 것과 나눠 찾게 한다 (어느 패키지 · 버전 · 라이선스인지).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 65/65 통과. det_* 골든 그대로 (에디터만).
- 실행 (화면): sbx new --from ecosystem_small + sbx new --pack inv --license MIT + sbx add → SandboxClient --world <프로젝트> (로컬
  플레이) → "편집" 패널 배치 툴 (3) — "eco · 프로젝트" (eco.grass · eco.rabbit · eco.wolf) 와 "inv 0.1.0 · MIT · 설치한 패키지"
  (inv.thing) 두 묶음. "콘텐츠" 창 Prefab 탭도 같은 두 묶음, 규칙 탭 (eco 만) · 스크립트 탭 (inv 만) 은 머리 없는 한 목록.
  콘텐츠 창의 좁은 목록에서 긴 머리는 잘린다 ("설치한 패키…" — 배치 목록에는 다 보인다).
- 확인 중 스크래치 폴더의 파일 하나 (앞 태스크의 m4t1/game/content/ecosystem/pack.json) 가 0 바이트들로 지워져 있었다 — 이번
  명령이 쓴 파일이 아니고, 저장소는 git fsck · git status 모두 깨끗했다. 같은 무렵 스크래치의 빌드 스크립트가 사라진 일도 있어
  스크래치 (임시 폴더) 쪽 문제로 보고 새 폴더 (m4t3) 에서 다시 했다. 원인은 확인하지 못했다.

**남은 일**

- M5-T1: 거부 시험 (충돌 · 악성 매니페스트 · 깨진 · 고친 파일 · 큰 파일 · 경로 탈출), 라이선스 요약 (SBOM), 깨끗한 머신 재현.

---

## 2026-10-28 — v0.4 M4-T2: sbx test · sbx new --pack (ADR-0076), 에디터 묶음은 M4-T3 으로

**무엇을**

- sbx test <프로젝트>: 프로젝트 검사 → playthrough.json · tests/*.json (scenario::projectPlayScripts) 을 스크립트마다 새로 연
  월드에서 같은 프로세스로 끝까지 (runPlayScript · playStepLimit) → expect. 실패가 하나라도 있으면 종료 1.
- sbx new --pack <폴더> --id <팩> [--license]: core/package/PackTemplate createPack — pack.json · scripts/main.ts (컴포넌트 ·
  export 함수) · scripts/tick.ts (시스템) · prefabs/thing.json, 임시 폴더에서 콘텐츠 검사 · 훑기를 통과해야 제자리로. 기본 라이선스
  LicenseRef-proprietary.
- 로드맵: 원래 M4-T2 의 "에디터 팔레트 · 콘텐츠 창의 패키지별 묶음" 을 M4-T3 으로 나눴다 (도구와 에디터를 한 커밋에 섞지 않는다).
- 시험: test_pack_template 3, test_sbx_options 1, CTest project_pack. 문서: ADR-0076, 13 (5.1 · 표), 15 7장, 18 (0 · 6.4 · 7장), 17.

**왜**

- ADR-0071 7 의 남은 CLI — 패키지 제작 (new --pack) → 설치 (add) → 시험 (test) 을 sbx 로 잇는다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 65/65 통과 (+ project_pack). det_* 골든 그대로.
- 실행: CTest project_pack 이 실제 sbx 로 — new --pack kit --license MIT ("… 팩 kit 0.1.0 · MIT · 파일 4") → 같은 폴더에 다시 new
  --pack 은 종료 2 → sbx new 게임 → add kit --path ../kit → check "시스템 kit.tick — .sandbox/packages/kit/scripts/tick.ts · onTick
  (30 틱마다)" → test "자동 플레이 0 개" · "시험: 통과 0 · 실패 0" → tests/smoke.json (maxTicks 30, expect tick 31) "통과
  tests/smoke.json — 게임 없음 · tick 31" → tests/wrong.json (expect tick 99) 종료 1 "시험: 통과 1 · 실패 1" → samples/rabbit_guard
  "통과 playthrough.json — 게임 Finished …".
- 첫 빌드에서 test_pack_template 의 게임 팩 id 를 "game" 으로 써서 시스템 id "game.twice" 가 엔진 이름공간으로 거절됐다 (S426 —
  검사기가 맞게 거절) — 시험의 팩 id 를 arena 로 고쳤다.

**남은 일**

- M4-T3: 에디터 팔레트 · 콘텐츠 창에서 설치한 팩 · 프로젝트 팩을 나눠 보이기.

---

## 2026-10-28 — v0.4 M4-T1: 팩 사이 스크립트 import "@<팩>/<경로>" — 정적 링크, tsconfig paths (ADR-0075)

**무엇을**

- script/lang/Checker: import "@<팩>/<경로>" → 그 팩의 "@<팩>/scripts/<경로>.ts" (PackImports — 팩마다 직접 의존만, 아니면 ·
  모양 · 없는 파일 S416). 의존 팩 모듈 안의 상대 경로는 그 팩 안에서. 이 팩의 모듈에서 import 로 닿는 모듈만 링크 (검사 · 코드
  생성), 의존 팩의 defineSystem 은 시스템 표에 넣지 않는다. Codegen: 의존 팩 모듈의 export 는 넣지 않는다.
- ContentLoader: 팩마다 컴파일할 때 의존 팩 (이어지는 의존까지) 의 scripts 를 "@<팩>/…" 로 함께, 의존 팩의 진단은 버리고
  (그 팩이 이미 냈다), 의존 팩의 컴포넌트는 등록하지 않는다 (정의한 팩이 한 번).
- script/sdk: generateTsconfig(팩들) — "@<팩>/*" paths · baseUrl · 설치 폴더 include (팩이 없으면 v0.3 바이트 그대로).
  project::refreshTsconfig — 만든 그대로의 tsconfig.json 만 고친다. sbx install · update · add · remove 가 부른다.
- 시험: test_pack_import 2 (링크 · 소유 · 월드에서 도는 함수, S416 넷 · 진단 한 번), test_game_project tsconfig 1.
  문서: ADR-0075, 19 (5 · 14장), 18 (0 · 6.4 · 7장), 15 7장, 17, 13.

**왜**

- ADR-0071 8 — 설치한 Feature 팩의 컴포넌트 · 함수를 게임 팩이 쓴다. VM · 바이트코드 · 세이브 · 복제를 바꾸지 않는 가장 작은
  모델이 정적 링크 (최상위에 상태가 없어 같은 함수를 두 팩이 따로 가져도 뜻이 같다).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 64/64 통과. det_* 골든 그대로 (import 가 없는
  팩의 바이트코드 · 해시는 그대로 — 의존 팩 파일은 링크하지 않는다).
- 실행: sbx new --from ecosystem_small + content/rpg (requires inv · eco, scripts/regen.ts 가 import { Mana, refill } from
  "@inv/mana") → `sbx add <프로젝트> inv --path ../shared/inv --version ^1` "… · tsconfig.json paths 갱신" (paths "@inv/*" →
  .sandbox/packages/inv/scripts/*, "@rpg/*" → content/rpg/scripts/*) → `sbx check` 오류 0 · "스크립트: 팩 2 · 컴포넌트 1 · 시스템 1"
  "시스템 rpg.regen — content/rpg/scripts/regen.ts · onTick" → `SandboxServer --world … --ticks 61 --exit` 종료 0, 로그
  "[script] rpg.regen t30: mana 0" · "t60" (시작 월드에 inv.mana 개체가 없어 0 — 값이 채워지는 것은 test_pack_import 의 월드가 본다).

**남은 일**

- M4-T2: 에디터 팔레트 · 콘텐츠 창의 패키지별 묶음, sbx test, sbx new --pack.

---

## 2026-10-27 — v0.4 M3-T3: sbx install · update · add · remove, 매니페스트와 잠금의 일치 → M3 완료 (ADR-0074)

**무엇을**

- core/package/Install: installProject (잠금이 없으면 해결해서 만든다, 있으면 잠금 그대로 — 매니페스트와 맞아야 하고 팩마다
  캐시 digest → 없으면 소스를 다시 받아 digest 가 같을 때만), updateProject (Refresh All · One · None — 바뀌면 안 되는 잠긴
  팩이 바뀌면 거절). Lockfile: checkLockMatches (의존마다 잠금에 있고 소스가 같다, 잠금의 팩은 모두 매니페스트에서 닿는다).
- GameProject P8 에 더함: 잠금이 매니페스트와 어긋남, 잠금 없이 path · git 의존. M1-T2 의 "path · git 의존도 content/ 에서
  찾는다" 를 끝냈다 (소스 없는 의존만 content/ — ADR-0074 6).
- sbx install · update [팩] · add <팩> (--path | --git --rev, --version) · remove <팩> (모두 --cache-dir). add · remove 는
  설치가 성공한 뒤에 매니페스트를 쓴다.
- 시험: test_install "명령의 바탕", test_game_project (소스 없는 의존 · 설치 안 한 path 의존 P8), test_sbx_options (새 명령),
  CTest project_install (실제 sbx · 서버). 문서: ADR-0074, 09 12장, 15 7장, 18 (0 · 5.2 · 6.4 · 7장), 17, 13.

**왜**

- ADR-0071 7 의 CLI 와 ADR-0073 7 이 남긴 매니페스트 ↔ 잠금 일치. 잠금 하나로 같은 설치를 재현하고, 한 팩 명령이 다른 팩을
  몰래 바꾸지 않게 한다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 64/64 통과 (+ project_install), SandboxTests 624
  케이스 (+2). det_* 골든 그대로. check_includes 0 위반.
  중간 실행 한 번에서 det_eco_lifecycle 이 1.2 초 만에 실패했다 (보통 16 초 — 출력을 남기지 못했다). 엔진 코드는 그 앞 실행과
  같았고, 따로 세 번 (ctest --repeat until-fail:3) · 직접 실행 · 마지막 전체 실행에서 모두 통과 (D1 · D2 · D5 일치, 골든 그대로) —
  세이브 폴더 입출력이 잠깐 막힌 것으로 보지만 원인은 확인하지 못했다.
- 실행: CTest project_install 이 실제 sbx · SandboxServer 로 — sbx add inv --path ../shared/inv --version ^1 → 매니페스트
  schemaVersion 3 · 잠금 팩 2 · check 의 "(.sandbox/packages/inv)" · 서버 30 틱 → 원본 · 설치 폴더를 지우고 sbx install (캐시에서,
  같은 잠금 바이트) → 고친 설치를 check (종료 1, P8) · 서버 (실패) 가 거절 → install 로 고쳐짐 → 없는 소스의 add 는 매니페스트 ·
  잠금 그대로 → remove 뒤 설치 폴더에서 빠짐 · check 통과.
- 처음 빌드에서 test_install 의 "바뀐 원본" 이 core.transform 에 없는 필드를 써서 설치의 콘텐츠 검사에 걸렸다 (설치 트랜잭션은
  맞게 거절) — 올바른 Prefab 을 더하는 것으로 시험을 고쳤다.

**남은 일**

- M4-T1: 팩 사이 스크립트 import (링크 모델 — 먼저 ADR), sandbox.d.ts · tsconfig 갱신.
- 매니페스트 쓰기가 설치 뒤에 실패하면 잠금과 어긋난다 — 열기가 P8 로 알린다 (ADR-0074 위험).

---

## 2026-10-27 — v0.4 M3-T2: sandbox.lock · 설치 트랜잭션 · 로더가 .sandbox/packages 를 읽기, 열기의 P8 (ADR-0073)

**무엇을**

- core/package/Lockfile: sandbox.lock (lockVersion 1, 팩 id 순 — version · license · 소스 · digest · dependencies) 의 정규형
  쓰기 · 읽기 (모양 오류 모두), lockFromResolution, verifyInstalled (설치 폴더 ↔ 잠금 — 없음 · id · digest · 남는 폴더).
  content/ 팩은 digest 로 잠그지 않는다 (제작자가 고치는 원본 — ADR-0073 2).
- core/package/Install: installPackages — staging 에 캐시의 팩을 훑기 파일만 복사 → digest 확인 → 콘텐츠 검사 (content/ +
  staging) → packages ↔ staging 이름 바꿔 끼우기 + 잠금 원자적 쓰기. 실패하면 프로젝트 그대로.
- ContentLoader: 루트 여럿 (ContentRoot — dir · 표시 접두사). 프로젝트 열기는 content/ 다음 .sandbox/packages 를 읽고, 팩 폴더
  표시가 "content/<팩>" · ".sandbox/packages/<팩>" 이 된다 (contentHash 와 무관 — 골든 그대로).
- GameProject: P8 (잠금 모양 · 고친 · 빠진 · 남는 · 잠금 없는 설치 → 설치 폴더를 읽지 않는다), 의존 요약 줄 끝에 읽은 폴더.
  GamePackage: .sbpkg 가 sandbox.lock 과 설치 팩을 담는다.
- 시험: test_install 4 (잠금 왕복 · 모양, 설치 → 열기 · .sbpkg, P8, 실패한 설치는 그대로). test_game_project 요약 줄 기대값.
- 문서: ADR-0073, 09 12장, 18 (0 · 5.2 · 6.4 · 7장), 15 7장, 17, 13.

**왜**

- ADR-0071 5 · 6 — 받은 남의 팩을 digest 로 고정하고, 고쳐진 팩은 열기 전에 거절한다. 설치는 중간 상태의 프로젝트를 남기지 않는다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과, SandboxTests 622 케이스 (+4).
  det_* 골든 그대로. check_includes 0 위반.
  SandboxTests 를 직접 한 번 돌렸을 때 1 케이스가 실패했으나 (어느 케이스인지 출력을 남기지 못함) 이어서 세 번 · ctest 에서는
  재현되지 않았다.
- 실행 (sbx install 은 T3 라 손으로 설치 — 팩 복사 + `sbx digest` 값으로 잠금): sbx new --from ecosystem_small + 의존 inv
  (path ../shared/inv, eco 를 요구) → 원본 폴더를 지워도 `sbx check` 오류 0, "inv ^1 → 1.2.0 · MIT · path ../shared/inv
  (.sandbox/packages/inv)", Prefab 4 → `SandboxServer --world <프로젝트> --ticks 60 --exit` 종료 0 → `sbx build` → .sbpkg 의 check ·
  서버 30 틱 종료 0. 설치 파일을 고치면 sbx check 종료 1 "P8 — .sandbox/packages/inv: digest a5f48c2a… 가 잠금의 296ce0e6… 와
  다르다", 서버는 ValidationFailed 종료 2. 잠금을 지우면 P8 "설치한 패키지가 있는데 sandbox.lock 이 없다".

**남은 일**

- M3-T3: sbx add · remove · update · install (잠금 그대로), 매니페스트 dependencies 와 잠금의 일치, 열기가 잠금을 확인.
- 열 때마다 설치 팩 전체를 SHA-256 으로 훑는다 — 큰 팩이 늘면 캐시 ([계획], ADR-0073 위험).

---

## 2026-10-27 — v0.4 M3-T1: 의존 그래프 해결 — 팩마다 한 버전 · 범위 확인 · 충돌 · 고정 · 순환, sbx resolve (ADR-0071)

**무엇을**

- core/package/Resolve: 매니페스트 dependencies 에서 너비 우선으로 소스를 받아 (content/ · path → 캐시 · git → 캐시) pack.json 의
  dependencies · requires 를 따라간다. 팩 id 마다 한 버전 — 같은 id 를 다른 소스로 가리키면 받아서 digest 를 견주고 (같으면 하나,
  다르면 충돌), 프로젝트가 직접 적은 팩은 고정 (다른 소스를 받지 않는다). 모든 범위 확인 (고정한 팩도), 순환, content/ 에 없는 팩,
  Git 실행기 없음, Git 패키지의 path 의존, 모양, 자기 자신 — 오류는 모두 모은다. 결과는 팩 id 순 (version · license · digest ·
  소스 · 읽을 폴더 · dependencies · requiredBy). readPackDependencies.
- sbx resolve <프로젝트> [--cache-dir].
- 시험: test_resolve 4. 문서: 09 11장, 15 7장, 18 (0 · 6.4 · 7장), 17, 13.

**왜**

- D-05 (ADR-0071 4) 의 해결 규칙을 코드로 — 잠금 파일 (M3-T2) 과 설치 (M3-T3) 가 이 결과를 쓴다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과, SandboxTests 618 케이스. det_* 골든 그대로.
- 실행: 스크래치 프로젝트 (dependencies {"inv": {"version": "^0.1", "path": "../shared/inv"}}, inv 의 pack.json 이 eco 를 git
  bare 저장소 태그 v0.1.0 으로) → `sbx resolve` "의존 그래프: 팩 2 · 오류 0", eco 0.1.0 · git … @ v0.1.0 · digest ed163556… ← inv,
  inv 0.1.0 · MIT · path ../shared/inv ← project (종료 0).

**남은 일**

- M3-T2: sandbox.lock · 설치 트랜잭션 · 로더가 .sandbox/packages 를 읽기.

---

## 2026-10-27 — v0.4 M2-T2: Git 소스 — 시스템 git 을 셸 없이, sbx fetch → M2 완료 (ADR-0072)

**무엇을**

- core/package/GitSource: url · rev 모양 검사 ('-' 시작 · 공백 · 제어 문자 · "::" 원격 도우미 · ".." 거절 — git 을 띄우기 전),
  fetchGitPack (clone --no-checkout → checkout --detach <rev> → rev-parse HEAD → 꺼낸 폴더를 캐시에, 임시 폴더 지움). 실행기
  (ProcessRunner) 는 부르는 쪽이 넣는다 — core 는 OS 헤더를 모른다.
- tools/sbx/Process: 셸 없는 실행 (Windows CreateProcessW + CommandLineToArgvW 따옴표 규칙, 그 밖 posix_spawnp + addchdir),
  stdout · stderr 를 합쳐 1 MiB 까지, 없는 실행 파일 NotFound.
- sbx fetch --git <URL> --rev <rev> [--id] [--cache-dir].
- 시험: test_git_source 3 (실행기 따옴표 왕복 · 모양 · 가짜 실행기 · 로컬 bare 저장소의 태그 · 커밋 · main · 없는 rev · 다른 id ·
  autocrlf 와 무관한 digest). 문서: ADR-0072, 09 10장, 15 7장, 18 (0 · 6.4 · 7장), 17, 13.

**왜**

- ADR-0071 의 Git 소스. 다른 제작자가 쓴 URL · rev 가 셸 · 옵션 · 원격 도우미로 읽히지 않게 경계를 코드로.

**검증**

- 실행 (스크래치 bare 저장소 — mana_regen 의 eco 팩, 태그 v0.1.0): 처음 `sbx fetch` 의 digest (f2771ff9…, 11,746 바이트) 가 같은
  팩의 `sbx digest` (ed163556…, 11,443 바이트) 와 달랐다 — 이 PC 의 core.autocrlf=true 가 꺼낼 때 LF 를 CRLF 로 바꿨다 (머신마다
  digest 가 달라지는 재현성 버그). 모든 git 호출에 -c core.autocrlf=false -c core.eol=lf 를 넣고 다시 → ed163556… (같다), 시험에도
  넣었다. `sbx fetch --git "--upload-pack=x" --rev v1` → "git URL 은 '-' 로 시작할 수 없다" (종료 2).
- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과, SandboxTests 614 케이스. M2-T1 의 미뤘던 전체
  ctest 는 63/63 통과 (이 작업 전에).

**남은 일**

- M3-T1: 그래프 해결. 로컬 폴더 소스의 digest 는 디스크 바이트 그대로 (autocrlf 로 꺼낸 폴더면 다르다) — M5 재현 시험에서 다시.

---

## 2026-10-27 — v0.4 M2-T1: 패키지 digest · 로컬 소스 · 사용자 캐시, sbx digest (ADR-0071)

**무엇을**

- core/package/PackSource: snapshotPack (팩 폴더 → 파일마다 SHA-256 → packageDigest, 팩 기준 경로 · .git · .svn · .hg 와 캐시 표시
  파일은 뺀다, 링크 · 장치 · 경로 규칙 · 4,096 개 · 16 MiB · 256 MiB 거절, pack.json id 필수), cacheLocalPack (임시 폴더에 복사 →
  다시 훑기 → 이름 바꾸기, 이미 있으면 그대로, 고쳐졌으면 다시, 의존 키와 다른 id 거절), findCachedPack (오프라인 — digest 로만),
  defaultUserCache (SBX_CACHE_DIR · 플랫폼 캐시 폴더), resolveLocalSource.
- sbx digest <팩 폴더> [--cache | --cache-dir].
- 시험: test_pack_source 4. 문서: 09 10장, 15 7장, 18 (0 · 6.4 · 7장), 17, 13.

**검증**

- 빌드 OK (중단된 세션이 남긴 손상된 .obj · vc140.pdb 때문에 다섯 대상의 중간 폴더를 지우고 다시 빌드했다).
- SandboxTests project · tools 스위트 통과 (593 단언). **전체 ctest 는 이 커밋에서 다시 돌리지 않았다** (사용 한도 — 바뀐 것은 새 모듈 ·
  sbx 명령 · 시험뿐, 다음 작업에서 전체를 돌린다). check_includes 위반 0.
- 실행: `sbx digest samples/mana_regen/content/ecosystem --cache-dir <임시>` → "팩 eco 0.1.0 — 파일 17 · 11443 바이트", digest
  ed163556…, "캐시 …/packages/ed163556bc4c75d0 (넣음)", 다시 돌리면 "(이미 있음)".

**남은 일**

- 전체 ctest 확인. M2-T2: Git 소스 (시스템 git · 태그 · 커밋 · 로컬 bare 저장소 시험).

---

## 2026-10-27 — v0.4 M1-T2: 프로젝트 매니페스트 schemaVersion 3 dependencies → M1 완료 (ADR-0071)

**무엇을**

- GameManifest::dependencies (팩 id 순 — pack.json 과 같은 모양: 범위 | {version?, path} | {version?, git, rev}), kManifestSchemaVersion
  3 (1 · 2 도 읽는다, 2 의 매니페스트에 dependencies 는 P1), 모양 · 팩 id · 16 개 상한 P7, 정규형 쓰기 (범위만이면 문자열 —
  writePackDependency).
- loadedPacks = content.packs + 거기 없는 의존 팩 — 프로젝트 검사 · 열기 (월드가 쓰는 팩 P5 검사 포함), .sbpkg 만들기, WorldSource
  가 같이 쓴다. 검사가 범위를 적은 의존마다 불러온 팩의 version 을 확인 (P7) 하고 요약 줄 (ProjectReport::dependencies →
  sbx check "의존:").
- sbx new: 새 프로젝트는 지금 판 (3) — --mode 가 판을 2 로 낮추던 것을 max 로.
- 시험: test_game_project +2 (매니페스트 3 · 프로젝트 검사의 의존), 판 숫자 기대 갱신 (test_game_project · project_demo.cmake). 문서: 18 (0 · 5.2 · 6.4 · 7장), 15, 13.

**왜**

- 게임 프로젝트가 쓰는 패키지와 그 범위를 매니페스트에 적어야 해결 · 잠금 (M3) 이 그것을 출발점으로 삼는다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과, SandboxTests 607 케이스. det_* 골든 그대로.
  (첫 실행에서 project_demo_protect 가 sbx new --mode 의 판을 2 로 고정해 보고 있어 실패 — "2 이상" 으로 고치고 project_* 12 개를 다시
  돌렸다.)
- 실행: rabbit_guard 복사본 (schemaVersion 3, content.packs ["eco"], dependencies {"inv": {"version": "^0.1", "path":
  "../shared/inv"}}) — `sbx check` → "의존: 1 / inv ^0.1 → 0.1.0 · MIT · path ../shared/inv (받기는 [계획 v0.4 M2] — 지금은
  content/)", 오류 0 (종료 0). `SandboxServer --world <그 폴더> --start-game --ticks 30 --exit` → "팩 2 개" 로 열고 tick 30.
  (처음에 --start-game 없이 돌려 게임 모드의 로비에서 틱이 오르지 않아 멈춘 것은 실행 실수 — 끝냈다.)

**남은 일**

- M2-T1: 패키지 digest · 로컬 폴더 소스 · 사용자 캐시 · 오프라인.

---

## 2026-10-27 — v0.4 M1-T1: pack.json 의 license · dependencies, SemVer 범위 "~" · "*", 검사 V12 (ADR-0071)

**무엇을**

- core/content/PackManifest: SPDX 식의 모양 검사 (식별자 · "+" · LicenseRef- · AND · OR · WITH · 괄호, 연산자 대문자, 256 바이트),
  의존 하나 (범위 문자열 | {version?, path} | {version?, git, rev} — 상대 경로만 · git 은 rev 필수 · 모르는 키 거절).
- ContentLoader: pack.json 에 license · dependencies (키는 requires 와 같은 뜻 — 로드 순서 · 팩 경계, 자기 자신 · core 거절),
  PackInfo::license · packages. 팩을 다 찾은 뒤 범위를 적은 의존마다 찾은 팩의 version 이 SemVer · 범위 안인지 (V12 — "팩 'a' 은
  'b' ^1.2 을 요구하는데 'b' 은 1.1.9 이다"). 범위가 없으면 version 을 보지 않는다 (지금 팩들 그대로). 소스 (path · git) 는 기록만.
- SemVer: "~1.2.3" (= >=1.2.3 <1.3.0) · "~1.2" · "~1" · "*" (모든 버전, 프리릴리스 포함). "||" · "1.x" · 하이픈은 계속 Unsupported.
- 시험: test_pack_manifest 4 · test_semver +1 (기존 "~" · "*" 거절 줄을 바꿨다) · test_game_project (engine 범위 예). 문서: 11 1.3 · V12, 18 (0 · 4장 · 6.4 · 7장),
  17, 13.

**왜**

- v0.4 의 패키지 = 콘텐츠 팩 (ADR-0071). 정체성 · 라이선스 · 의존 범위를 매니페스트에 적고 확인하는 것이 해결 · 잠금 (M3) 의 바닥.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과 (첫 실행에서 unit_project 의 매니페스트
  시험이 "~0.1" 을 거절 예로 쓰고 있어 실패 — 이제 맞는 범위라 "0.x" · "^0 || ^1" 로 바꾸고 다시 돌렸다). SandboxTests 605 케이스.
  det_* 골든 그대로 (콘텐츠 · 시뮬레이션 변경 없음). check_includes 위반 0.
- 실행: rabbit_guard 복사본에 팩 inv ({"license": "MIT", "dependencies": {"eco": "^0.1"}}) 을 더해 `sbx check` → 오류 0 (종료 0),
  범위를 "^0.2" 로 → "content/inv/pack.json:/dependencies/eco: 오류 V12 — 팩 'inv' 은 'eco' ^0.2 을 요구하는데 'eco' 은 0.1.0 이다"
  (종료 1).

**남은 일**

- M1-T2: 프로젝트 매니페스트 schemaVersion 3 dependencies · sbx check 의 의존 요약. 소스에서 받기는 M2.

---

## 2026-10-27 — v0.4 Package Manager 계획 · D-05 결정 (ADR-0071)

**무엇을**

- ADR-0071: 패키지 = 콘텐츠 팩 (pack.json 에 version · license · dependencies), 프로젝트 매니페스트 schemaVersion 3 dependencies,
  소스 = 로컬 폴더 · Git (시스템 git) · 오프라인 캐시 (digest), D-05 — 팩 id 마다 한 버전 · 소스가 곧 버전 · 범위는 확인 · 다른
  소스는 충돌 (프로젝트 고정이 이긴다) · 범위 문법에 "~" · "*", sandbox.lock, 트랜잭션 설치 (실패면 그대로) · 설치 훅 없음,
  v0.4 밖 (Registry · 서명 · 권한 심사 · 원격 받기 · 여러 버전 공존 · EULA).
- 18: 0장 (v0.4 ◐ 계획 · 다음 M1-T1), 1장, 4장 (제품 버전 0.3.0 · 범위 문법 예정), 6.1 D-05 결정, 6.4 마일스톤 · 태스크 (M1 정체성 ·
  M2 소스 · 캐시 · M3 해결 · 잠금 · 설치 · M4 Export/Import · M5 보안 · 재현 · M6 E2E · 릴리스), 7장 문서 계획.

**왜**

- v0.3 이 끝났다 — 다음 버전에 들어가기 전에 범위와 열린 결정 (D-05) 을 정하고 태스크로 쪼갠다 (ADR-0039 2). 지금 있는 팩 · .sbpkg ·
  SemVer 위에 쌓는다.

**검증**

- 문서만 바꿨다 (코드 · 빌드 그대로 — 바로 앞 커밋 38ea924 의 ctest 63/63). 링크한 ADR · 문서 파일이 있는지 확인했다.

**남은 일**

- v0.4 M1-T1: pack.json 넓히기 · 범위 "~" "*" · 검사 V12.

---

## 2026-10-27 — v0.3 종료: 요구사항 done / deferred / removed, 릴리스 노트, 제품 버전 0.3.0 (v0.3 M6-T3)

**무엇을**

- CMake project VERSION 0.3.0 (sbx · SandboxServer · SandboxClient --version = 0.3.0, sbx new 의 engine 범위 기본값 ">=0.3.0 <1.0.0").
- 릴리스 노트 docs/releases/v0.3.0.md: M1~M6 · 호환성 번호 (프로토콜 14 · kSimVersion 4 · .sbc 형식 5 · Host API 1 · 지문 · 세이브
  1 · 1 그대로) · 초안 요구 (3장 8 · 5장 4 · 6장 데모 · 9장 DoD) 의 done / deferred / removed · 미룬 것의 버전 재할당 · 알려진 문제 ·
  데모 재현 명령과 실패 시 기대 오류.
- 18 (0장 v0.3 ✅ · 다음 = v0.4 계획, 1장, 6.2 T3 ✅ · M6 · v0.3 완료, 6.3 요구사항 검토, 7 · 8장), 19 머리, README · docs/README ·
  16 의 버전 표시.

**왜**

- v0.3 완료 기준 (초안 5장) 이 모두 자동 시험으로 섰다 — 버전을 닫고, 못 한 것을 다음 버전에 이름으로 남긴다 (DoD 9-4).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과 (det_* 골든 · v0.2 데모 해시 · v0.3 데모 해시
  그대로 — 시뮬레이션 변경 없음, kSimVersion 4). SandboxTests 600 케이스. `sbx --version` → 0.3.0.

**남은 일**

- v0.4 Package Manager 계획: 18 6장을 마일스톤 · 태스크로 (새 6.4), 열린 결정 D-05 를 ADR 로. v0.1 실기 검증 (18 1.1) 그대로.

---

## 2026-10-27 — v0.3 M6-T2: 스크립트 보호 시험 · 바이트코드 변형 퍼즈 · script.regen 벤치 (ADR-0070)

**무엇을**

- tests/unit/script/test_script_protection (5): 무한 루프 · 무한 재귀 · 메모리 폭탄 시스템 (쓰기를 먼저 하고 끝나지 않는다) → 그
  시스템만 3 번 트랩 · 꺼짐, 끝 WorldHash = 그 시스템이 없는 콘텐츠의 해시. client 무한 루프 → 그 시스템만. 엔진 컴포넌트 —
  손으로 고친 컴포넌트 표는 검증기가, 엔진 ref 는 Host 가 거절. 무작위 개체 · ctx 위조 2,000 번 · 서버 핸들을 클라이언트 Host 에.
  직렬화한 모듈의 결정적 변형 3,000 개 — 거절 또는 검증된 모듈, 검증된 것은 함수를 불러도 죽지 않는다.
- 검증기: (1) 컴포넌트 표의 엔진 이름공간 거절 (새 규칙 — .sbc 형식 그대로), (2) 퍼즈가 찾은 버그 — CALLH 결과가 Entity[] 일 때
  없는 결과 레지스터를 범위 확인 전에 F->regs[in.a] 로 읽던 것 (Debug 단언 · Release 에선 범위 밖 읽기) 을 고쳤다.
- bench/ScriptBench — script.regen: 스크립트 컴포넌트 개체 10k (--quick 1k), 서버 시스템 하나가 매 틱 질의 · 읽기 · 쓰기, 스크립트
  없는 같은 월드와 tick 비교, 진단 시계로 스크립트 ms/틱 · ns/개체 · 연료/개체.
- 문서: ADR-0070, 19 (0 · 12 · 24장), 14 (2장 표 · 6장 후보 · 7.13 측정), 18 (M6-T2 ✅ · 다음 M6-T3), 13, 17.

**왜**

- v0.3 완료 기준의 "실행량 초과 · 권한 위반 · VM 크래시 보호" 를 끝 결과로 판정 (죽지 않는다 + 흔적이 없다). 스크립트 비용의
  기준 수치.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과 (det_* 골든 그대로). SandboxTests 600 케이스.
  check_includes 위반 0.
- 퍼즈: CTest 3,000 개 (거절 2,918 · 검증 82 · 부른 함수 328), 한 번 300,000 개 (씨앗 777 — 거절 292,030 · 검증 7,970 · 부른 함수
  31,880, 크래시 0, 약 22 초 Debug).
- 실행: 마나 콘텐츠 + 무한 루프 · 메모리 폭탄 시스템의 `SandboxServer --world ecosystem_small --content-root <루트> --ticks 90 --exit`
  → "eco.bad.loop … OutOfFuel" · "eco.bad.bomb … OutOfMemory" 3 번 뒤 꺼짐, 틱 90 해시 0xe7f8e361bd4eb697 = 나쁜 스크립트 없는 루트.
- 벤치 (MSVC 19.44 RelWithDebInfo, build/bench — SBX_BUILD_BENCH=ON, 사용자 PC 28 논리 코어): `sbx_bench --only script` →
  10k: tick 10.13 ms (스크립트 없이 0.193) · 스크립트 9.93 ms/틱 · 993 ns/개체 · 연료 59/개체, --quick 1k: 0.878 ms/틱.
  참고 sim.random_walk 10k 1.14 ms. 머신 기준선 파일은 만들지 않았다 (전체 실행이 아니라서).

**남은 일**

- M6-T3: 버전 종료 (요구사항 판정 · 릴리스 노트 · 0.3.0). 스크립트 Host 경로 최적화 (핸들 · 쓰기 버퍼 · 배치) 는 14 6장 조건으로
  [계획], 커버리지 퍼즈 (libFuzzer) 는 v0.4 검토.

---

## 2026-10-27 — v0.3 M6-T1: 스크립트 데모 samples/mana_regen · E2E (ADR-0069)

**무엇을**

- 계획의 M6-T1 (데모 + 보호 시험 + 10k 벤치) 을 셋으로 나눴다 — 이번은 데모 E2E, T2 = 보호 시험 · 벤치, T3 = 버전 종료 (18 6.2).
- samples/mana_regen (게임 모드 없음): 생태계 팩 복사본 + scripts/ — mana.ts (컴포넌트 eco.mana {current · max · regen}),
  regen.ts (server — 15 틱마다 채움 · 10 초마다 로그), newborn.ts (server — spawn 이벤트 → 마나 50), hud.ts (client — 토끼 위 값 ·
  "스크립트" 칸). 토끼 Prefab 에 eco.mana, 행동 eco.energize (조건 target.eco.mana.current >= 30 → 마나 −30 · 기운 +30), 키 R.
  스크립트를 넣은 콘텐츠 루트로 `sbx new --from ecosystem_small --content-root` — 시작 월드의 토끼 75 마리가 마나 50.
  playthrough.json (8 배속 · 기운 주기 20 번 · 1800 틱 · expect tick · 툴체인별 해시), README.
- CTest sample_mana_regen: sample_playthrough.cmake 에 RESULT_MATCH · SERVER_MATCH · CLIENT_MATCH 매개변수와 샘플 이름의 패키지
  파일을 더해 같은 스크립트로 — 헤드리스 서버 = 로컬 플레이 (프로젝트 · 패키지), 서버 끝 줄의 스크립트 시스템 · 트랩 0, 클라이언트
  끝 줄의 client 시스템.
- unit_core test_mana_regen_sample 4: 열기 · 자동 플레이 두 번 같다 (기운 주기 받아들여진 수까지) · 세이브 → 로드 → 이어 돌린 해시 ·
  로컬 세션 (멈춘 뒤 복제 월드의 마나 바이트 = 서버, client 글자 수, Inspector 값 · ChangeComponent 편집).
- sbx check 의 스크립트 요약에 " · client onUpdate".
- 문서: ADR-0069, 18 (M6 셋으로 · T1 ✅ · 다음 M6-T2), 19 (0 · 23장), 13 5.1, 15 7장, 17, README.

**왜**

- v0.3 완료 기준 "스크립트로 Mana Component · Regeneration System" 을 사용자가 따라 할 예제와 자동 검증으로. 컴포넌트 · 주기 ·
  이벤트 · client · Rule 연결을 한 게임에서 다 쓴다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과 (+ sample_mana_regen 약 36 초, det_* 골든
  그대로). SandboxTests 595 케이스. check_includes 위반 0.
- 실행: `sbx check samples/mana_regen` 오류 0 · 경고 0 (시스템 3). `SandboxServer --world samples/mana_regen --script …` 와
  `SandboxClient --headless --world samples/mana_regen --script …` 둘 다 "게임 결과: 게임 없음 · tick 1801 · hash
  0x51ddbf1f7d1ab7e9", 서버 로그 "토끼 75 · 마나 합 6480" (t300) … "토끼 124 · 마나 합 12084" (t1800), 클라이언트 끝 줄
  "[eco.mana.hud] 토끼 124 · 마나 가득 108".
- 창 (ImGui) 에서 R 키 · 글자를 눈으로 보지는 못했다 — 행동 · 표시는 자동 플레이와 세션 시험이 본다.

**남은 일**

- M6-T2: 보호 시험 (무한 루프 · 권한 위반 · 불법 바이트코드 · 핸들 위조) · 10k 대상 벤치 (sbx_bench 스크립트 단계). M6-T3: 버전 종료.

---

## 2026-10-27 — v0.3 M5-T4: 스크립트 프로파일 — 주입한 진단 시계, ServerStats 스크립트 표, "스크립트 프로파일" 패널 (ADR-0068, 프로토콜 14)

**무엇을**

- core/script/ScriptClock (u64 (*)() noexcept — 단조 나노초): ScriptSystemAdapter · ClientScriptSystems 의 setClock,
  SimulationWorld::setScriptClock (다시 만든 어댑터에도). 시계가 있으면 호출마다 앞뒤를 재어 통계에만 (ScriptSystemCounters::nanos ·
  maxNanos, 틱 · 프레임 통계 nanos, 클라이언트 시스템별 지난 연료 · 시간). describeScriptSystems · describeClientScriptSystems 에
  " · 시간 <합> ms · 최대 <호출 하나> ms" (시계가 있을 때만 — sbx_sim_check 끝 줄은 그대로).
- ServerHost: 단계마다 그 월드에 steady_clock 시계를 넣고, ServerStats (1 초) 끝에 windowSeconds · 스크립트 시스템 표 (창의 호출 ·
  연료 · 시간 = 누적 차이, 누적 트랩 · 예산 · 건너뜀 · 최대 연료 · 최대 시간 · 꺼짐, 64 줄 · scriptsOmitted). 프로토콜 13 → 14.
- 클라이언트: IWorldSession::scriptProfile — 서버 표 + client 시스템의 최근 1 초. apps/client/ui/ScriptPanel 의 "스크립트 프로파일"
  패널 (서버 · 클라이언트 표 — 호출/초 · 연료/호출 · ms/초 · ms/호출 · 최대 ms · 최대 연료 · 트랩 · 예산 · 건너뜀 · 상태,
  scriptProfileRow). M5-T3 의 "스크립트" 칸 안 통계 접기는 이 패널로 옮겼다 (ScriptOverlayInfo::systems 없앰).
- sbx_net_probe: 받은 서버 스크립트 표를 줄마다.
- 시험: test_client_scripts +1 (시계 — 해시 그대로), test_client_script_session +1 (프로파일 세션 · 패널 · 줄 계산),
  test_messages +1 (표 왕복 · 상한), 프로토콜 14. 문서: ADR-0068, 08, 19 (0 · 9 · 19 · 21 · 22장), 18 (M5-T4 ✅ · M5 완료 · 다음
  M6-T1), 10 9장, 15, 17, 13.

**왜**

- 통계에 시간이 없어 "어느 스크립트가 느린가" 를 볼 수 없었고, 끝 줄로만 보였다. 게임을 돌리면서 서버 · 클라이언트 표를 본다.
- 04 (시뮬레이션은 벽시계를 보지 않는다) 를 약속이 아니라 경계로: core 는 시계를 모르고, 부르는 쪽이 넣은 값이 통계로만 흐른다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — kSimVersion 4).
  SandboxTests 591 케이스. check_includes 위반 0.
- 실행 (스크래치 clientroot — 생태계 + Mana 스크립트 + client 시스템 eco.hud):
  `SandboxServer --world ecosystem_small --content-root <clientroot> --port 7791 --ticks 240 --exit` 에
  `sbx_net_probe --connect 127.0.0.1:7791 --content-root <clientroot> --seconds 3` → 1 초마다 "스크립트 eco.mana.regen: 창 1.03 초
  호출 1 · 연료 4564 · 1.289 ms | … 최대 1.430 ms", 서버 끝 줄 "스크립트 eco.mana.regen: 호출 8 · … · 시간 10.68 ms · 최대 1.510 ms".
  `SandboxClient --headless … --frames 120` 끝 줄 "클라이언트 스크립트 eco.hud: 호출 116 · … · 시간 259.15 ms · 최대 6.030 ms"
  (Debug — 프레임 하나 약 2.2 ms, 글자 75).
- 성능: 시뮬레이션 경로는 호출마다 시계 포인터 확인 하나 (시계를 넣으면 steady_clock 두 번) — 벤치는 돌리지 않았다. sbx_bench
  스크립트 단계는 M6-T1 (저장소 콘텐츠에 스크립트가 아직 없다).
- 패널을 창에서 눈으로 보지는 못했다 — 그리기는 헤드리스 ImGui 시험이 부른다.

**남은 일**

- M6-T1: 데모 samples/mana_regen · 보호 시험 · 10k 대상 벤치 (스크립트 단계 — 시계를 넣어 시스템별 시간). 함수별 프로파일 · 기록
  그래프 · 패널에서 시스템 끄고 켜기 · Native 시스템별 시간 (ServerStats) 은 [계획].

---

## 2026-10-26 — v0.3 M5-T3: 클라이언트 스크립트 최소 — domain "client" · onUpdate, 복제 월드 읽기 · 화면 글자 (ADR-0067)

**무엇을**

- 계획의 M5-T3 (IClientContext 최소 + 프로파일) 을 둘로 나눴다 — 이번은 클라이언트 스크립트, 프로파일은 M5-T4 (18 6.2).
- Host 표: 주인 ClientContext · 도메인 Client 항목 8 — client.tick · frame · dt · read · has · query (복제 월드, netId 순) · label ·
  print (새 효과 display). 더하기만이라 kHostApiVersion 1 그대로, 지문 0388e85856c3e9f3 → e02f109682ecb304. AbiType::ClientContext
  를 검사기 · 코드 생성 · 검증기 · .sbc 읽기 · d.ts 에.
- 검사기: defineSystem({ domain: "client", onUpdate(ctx) }) — domain 을 먼저 읽어 (리터럴, 키 순서와 무관) 메서드의 ctx 를
  ClientContext 로. client 시스템의 onTick · onStart · onStop · onEvent · intervalTicks · events · eventsPerTick, server 시스템의
  onUpdate, 상수 이름 domain 은 S426 (한 잘못에 진단 하나).
- .sbc 형식 4 → 5: 시스템 표에 도메인 (u8) · onUpdate. 검증기가 client = onUpdate 하나 · (ClientContext?) → void · 주기 1 · 구독
  없음, server = onUpdate 없음. 역어셈블 "system <id> <파일> client onUpdate fn n".
- 바인딩 계약을 도메인마다: BasicHostBinding<Host> · HostBindingShape · checkHostBindingShapes(…, 도메인) — 서버 hostBindings 는
  Server 항목만, 다른 도메인 항목의 구현은 문제. 컴포넌트 값 → VM 칸 변환을 ScriptValues.hpp 로 꺼내 서버 · 클라이언트가 나눈다.
- core/script/ClientScriptHost (IHost — 복제 월드 읽기, 줄마다 256 바이트 · 프레임마다 글자 1024 · 줄 64, 트랩이면 그 호출의 표시
  버림) · ClientScriptSystems (프레임마다 onUpdate, 프레임 연료 2,000,000, 트랩 3 번이면 끔 — 메모리에만, 통계 ·
  describeClientScriptSystems).
- 서버: ScriptSystemAdapter 가 client 시스템을 건너뛰고, SimulationWorld 는 server 시스템이 있을 때만 어댑터를 만든다
  (ScriptPrograms::systemCount(도메인)). 어댑터를 다시 만들 때 새 것을 먼저 만들고 바꿔 낀다 (옛 자리를 다시 써서 "다시 만들었다"
  시험이 우연에 기대던 것).
- 클라이언트: NetworkSession 이 콘텐츠 (오버레이 포함) 에 client 시스템이 있을 때 ClientScriptSystems 를 두고 스냅숏 적용 뒤
  돌린다 (바이트코드가 바뀌면 새로). IWorldSession::scriptOverlay → apps/client/ui/ScriptPanel 이 글자를 개체의 그린 자리 위에,
  줄을 "스크립트" 칸에 (패널을 숨겨도). 헤드리스 끝 줄에 시스템 통계 · 마지막 프레임 줄.
- d.ts: SystemDefinition.domain?: "server" | "client", onUpdate?(ctx: ClientContext), interface ClientContext.
- 시험: test_client_scripts 5 · test_client_script_session 1, 기존 시험의 형식 5 · 지문 · 서버 바인딩 수 · S426 문구 갱신.
  문서: ADR-0067, 19 (0 · 9 · 10 · 12 · 14 · 17 · 19 · 21장), 18 (M5-T3 ✅ · T4), 10 9장, 11 11장, 15, 17, 13.

**왜**

- 게임 제작자가 서버 규칙과 함께 화면 쪽 표시 (개체 위 값 · HUD 줄) 를 같은 언어로 쓰게. 읽기 · 표시만이라 서버 권위 ·
  결정론 · 해시 · 세이브에 닿지 않는다 — 쓰기 · 명령은 서버 검증을 다시 거쳐야 해서 최소 범위 밖.
- 도메인을 주인 타입으로 가르면 서버 항목 (ctx.write · ctx.log) 을 클라이언트 코드에서 부르는 일이 타입 검사에서 막힌다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — kSimVersion 4).
  SandboxTests 588 케이스. check_includes 위반 0.
- 실행 (스크래치 clientroot — 생태계 팩 + M4 의 Mana 스크립트 + client 시스템 eco.hud: 마나 있는 개체마다 글자 · 합계 줄):
  `SandboxClient --headless --world ecosystem_small --content <clientroot> --frames 120` → "클라이언트 스크립트 eco.hud: 호출 116 ·
  트랩 0 · … 최대 4348", "마지막 프레임 116: 글자 75 · 줄 1", "[eco.hud] tick 58 frame 116 rabbits 75 mana 3825".
  `SandboxServer --world ecosystem_small --content-root <clientroot> --ticks 60 --exit` 의 끝 줄은 서버 시스템 둘뿐 (eco.hud 없음).
- 성능: 서버 시뮬레이션 경로는 client 시스템을 건너뛰는 비교 하나뿐이라 벤치를 돌리지 않았다. 클라이언트 프레임 비용은 위 실행의
  연료 (프레임 평균 약 4,300 · 토끼 75) — 시간 측정은 M5-T4 프로파일에서.
- 창 (ImGui) 의 글자 · 칸을 눈으로 확인하지는 못했다 — 그리기 함수는 헤드리스 ImGui 시험이 부르고, 자리 계산은 카메라 변환 그대로.

**남은 일**

- M5-T4: 프로파일 (서버 · 클라이언트 스크립트 시스템의 연료 · 시간을 에디터 · 패널에). 클라이언트 스크립트의 명령 보내기 · 입력 ·
  그리기 · 엔진 컴포넌트 읽기 · 개체 참조 풀기는 [계획] (M6 데모가 요구하면).

---

## 2026-10-26 — 문서 정리: 입구 · 상태 줄 · 소스 지도 · 남은 [계획] 표시를 코드와 맞춤

**무엇을**

- 입구: README.md (Phase 12 시점의 상태 문단 → 버전 표 · 핵심 기술 · 해 보기 · 스크립트 예 — 예는 sbx check 로 컴파일 확인),
  docs/README.md (지도의 성격 · 설명 · 0장 "지금 어디인가" · 읽는 순서에 스크립트 · 게임 제작, Phase 0 문구 뺌), AGENTS.md 1장
  ("지금" = 18-PLATFORM 0장, 스크립트 코드 → 19).
- 상태: 16-ROADMAP 을 v0.1 Phase 이력 문서로 (머리 · 4장의 Phase 3 티켓 목록 → 남은 실기 검증), 18-PLATFORM (기준일 · 0장 v0.3
  진행 · 1장 · 2장 표의 SandboxScript · 사용자 Component 행이 Planned → Implemented, 사용자 System · 스크립트 도구 행, 3장 모듈).
- 17-SOURCE-MAP 2장: 설계 때의 폴더 트리 → 실제 트리 (없는 Validator · CommandApplier · time/ · metrics/ · Miniaudio 를 빼거나
  [계획] 으로, script/ · core/game · trigger · project · package · scenarios · script 를 넣음), 4장 "이 기능은 어디에?" 줄 더함.
- 규범 문서 머리의 상태 줄: 01 · 02 · 03 · 04 · 09 · 10 (설계 → 규범) · 11 · 13 · 15 에 v0.2 · v0.3 을. 15 4장 타깃 표에
  SandboxScript.
- 구현된 것에 남은 [계획]: 02 (Prefab · Binary Visitor · Opaque · EntityRef 복제), 03 (4.1 검증 · 스크립트 단계), 19 (M1~M3 에
  구현된 9 곳 — 검사기 · f32 · 쓰기 버퍼 · VM · .sbc 머리), 11 (점수 이벤트), 13 (--replay-roundtrip), 06 ([계획 12B]),
  14 · 16 ([계획 15] — Phase 15 가 끝났다).
- 같은 날 따로: `script:` 커밋 — 진단 문구 · 주석의 낡은 마일스톤 (ctx.random [계획 v0.3 M2] 등).

**왜**

- 문서 입구 (README · 지도 · AGENTS) 가 Phase 0 · 12 시점에 머물러 지금 어디인지 · 무엇이 있는지 알 수 없었다. 버전 계획으로
  넘어간 뒤 16 과 18 의 몫이 겹쳤다 — 16 = 이력, 18 = 지금 · 다음으로 나눴다.

**검증**

- 문서 안 상대 링크 전부 (README · AGENTS · docs/*.md) 가 있는 파일을 가리킨다 (스크립트 확인 — 끊긴 링크 0).
- [계획] 표시마다 코드를 찾아 보았다 (예: --record-replay · 오토세이브 · Miniaudio · Chat · TagMask · D6 은 아직 없어 그대로).
- `script:` 커밋: 빌드 · ctest 62/62 (render_tests_warp 제외).

**남은 일**

- docs/adr/README.md 의 표 다시 맞춤 (공백) 은 이 작업 전부터 작업 트리에 있던 바깥 변경이라 넣지 않았다.

---

## 2026-10-26 — v0.3 M5-T2: 스크립트 오버레이 · 에디터 "콘텐츠" 창의 스크립트 편집 (ADR-0066, 프로토콜 13)

**무엇을**

- 오버레이 종류 Script (4): 항목 = 팩의 scripts/ 파일 하나, id = "<팩>.<경로 마디를 '.' 로>" (`scriptOverlayId` ·
  `scriptOverlayPath`), 값 {"id", "source"}. 세이브 overlay/scripts/<id>.json. 오버레이 지문은 v0.2 의 넷 다음 종류가 비어 있으면 넣지
  않는다 (스크립트 없는 오버레이 · 세이브 해시가 그대로 — 시험이 옛 계산과 맞춘다). 프로토콜 12 → 13.
- ContentLoader: 오버레이에 Script 항목이 있으면 0 단계에서 소스를 팩 파일 목록에 넣고 (바꾸기 · 더하기 — applyScriptOverlay) 다시
  컴파일. 컴포넌트 정의 지문 (정의 자리 빼고) · id 목록이 팩 DB 와 다르면 V11, 카탈로그는 팩 DB 의 것을 나눈다. 구독 태그 풀기는
  이번에 컴파일한 것에만. loadOverlay 는 Script 를 건너뛴다.
- SimulationWorld::syncScriptSystems: 콘텐츠의 바이트코드가 바뀌면 (생성 · 오버레이) 어댑터를 다시 만들고 이어진 트랩 · 꺼짐을
  digest 가 같은 팩만 옮긴다.
- 에디터 "콘텐츠" 창 종류 "스크립트": 팩의 scripts/*.ts + 더한 파일, 소스 편집, 검사 (오류마다 "  12 | <그 줄>" — scriptExcerpt),
  적용 (contentEditValue → EditContent Script), 이 파일의 시스템 요약, 새 파일 틀 (scriptTemplate).
- CommandJson 의 content 오류 문구에 trigger · script.
- 시험: test_script_overlay 3 · test_script_editor 2 · test_roles_overlay +1 (루프백 — kind 4), test_messages · test_roles_overlay 의
  프로토콜 · 종류 상한 갱신. 문서: ADR-0066, 08, 09 3장, 10 8장, 11, 19 (0 · 20장), 18 (M5-T2 ✅), 17, 13.

**왜**

- 스크립트를 고치려면 팩 파일을 바꾸고 서버를 다시 열어야 했다. 오버레이 경로 (권한 · 서버 검증 · 복제 · 되돌리기 · 세이브 ·
  리플레이) 를 그대로 쓰면 게임을 멈추지 않고 모두에게 같게 바뀐다.
- 컴포넌트 정의는 살아 있는 월드의 풀 배치라 오버레이로 바꾸지 않는다 — 시스템 코드 편집이 대부분이다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). SandboxTests 581 케이스.
  check_includes 위반 0.
- 실행 (스크래치 EditProj — sbx new + M4 의 Mana 스크립트): 자동 플레이 스크립트 (단계 60 에 eco.regen_system 을 intervalTicks 10 ·
  "v2" 로그로 바꾸고, 단계 100 에 "export default 3" 으로 깨뜨린다) 를 `SandboxServer --world EditProj --script editplay.json` 과
  `SandboxClient --world EditProj --script editplay.json --headless` 로 — 둘 다 "eco.mana.regen t90/t120/t150/t180: v2 regen …"
  (바꾼 시스템이 그 뒤로 10 틱마다, 깨뜨린 편집은 거절되어 v2 그대로), 끝 해시가 같다 (0x2c4188b3b238866f).
- 에디터 창 (ImGui) 을 손으로 눌러 보는 확인은 하지 못했다 — 이 기계의 화면에서 클라이언트 창을 잡지 못했다 (다른 앱이 열린
  사용자 데스크톱). 창의 검사 · 적용 · 틀 함수는 단위 시험이, 와이어 · 서버 적용 · 클라이언트 컴파일은 루프백 시험과 위 실행이 본다.

**남은 일**

- M5-T3: IClientContext 최소 (onUpdate) · 프로파일. 스크립트 편집기의 줄 번호 · 구문 색 (v0.5 검토), 16 KiB 를 넘는 파일, 오버레이로
  컴포넌트 정의 바꾸기 (월드 다시 열기와 함께) 는 [계획].

---

## 2026-10-26 — v0.3 M5-T1: sbx new 의 스크립트 SDK 파일 · sbx check 의 스크립트 요약 · P6 (ADR-0065)

**무엇을**

- 18-PLATFORM 6.2 의 M5-T1 (CLI + 에디터 + 프로토콜) 을 둘로 나눴다: T1 = CLI (sbx new · check), T2 = 에디터의 스크립트 오류 줄 ·
  적용 (오버레이 Script — 프로토콜 ++), 옛 T2 (IClientContext · 프로파일) → T3.
- `script::sdk::generateTsconfig` (strict · noEmit, include = sdk/sandbox.d.ts · content/*/scripts/**/*.ts). `project::createProject`
  가 sdk/sandbox.d.ts · tsconfig.json 을 만든다 (스크립트가 없어도).
- `project::validateProject` 5 단계 (P6 경고): sdk/sandbox.d.ts 가 이 엔진의 생성 결과와 다름 (CR 은 빼고 비교) · 스크립트가 있는데
  없음, tsconfig.json 이 JSON 아님. ProjectReport 에 스크립트 팩 · 컴포넌트 수 · 시스템 줄 · SDK 상태.
- sbx check: "스크립트: 팩 n · 컴포넌트 n · 시스템 n · SDK … 최신|다름|없음" 과 시스템마다 한 줄. sbx new 가 SDK 파일을 알린다.
- 시험: test_game_project +2. 문서: ADR-0065, 18 (5.2 P6 · 6.2 M5 나눔), 15 7장, 19 14장, 17, 13.

**왜**

- 스크립트 진단은 M3-T2 부터 sbx check 에 나오지만, 새 프로젝트에 IDE 가 읽을 SDK 선언 · tsconfig 가 없었고, 엔진을 올리면 프로젝트의
  d.ts 가 낡아도 알 길이 없었다. 경고 (P6) 로 둔 것은 IDE 파일이 게임 실행과 무관하기 때문이다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과. unit_project +2. check_includes 위반 0.
- 실행 (스크래치): `sbx new NewScripted --id studio.newscripted --from ecosystem_small` → sdk/ · tsconfig.json 생성 · 검사 통과,
  M4 의 스크립트 셋을 넣고 `sbx check` → "스크립트: 팩 1 · 컴포넌트 1 · 시스템 3 · SDK sdk/sandbox.d.ts 최신" 과 시스템 셋 줄
  (onTick (30 틱마다) · onStart · onEvent (death · spawn)), sdk/ 를 지우면 "sdk/sandbox.d.ts:/: 경고 P6 — 스크립트가 있는데 SDK
  선언이 없다".
- tsc 로 템플릿을 검증하지는 못했다 — 이 기계 (빌드 체인) 에 Node 가 없다. tsconfig 는 오래된 tsc 도 읽는 옵션만 썼다 (ADR-0065).

**남은 일**

- M5-T2: 에디터 "콘텐츠" 창의 스크립트 오류 줄 · 적용 (오버레이 Script — 프로토콜 ++). sbx sdk 가 tsconfig 를 보존하며 갱신하는 길은
  필요해지면.

---

## 2026-10-26 — v0.3 M4-T3: 스크립트 예산 · 트랩 정책 · 시스템별 통계 (ADR-0064) → M4 완료

**무엇을**

- 호출 예산에 Host 호출 상한 (`vm::Limits::maxHostCalls` 65,536 — 넘으면 HostError). ScriptHost 의 ctx.log 는 틱 256 줄
  (넘으면 버리고 `droppedLogs` 로 센다).
- `ScriptSystemAdapter`: 틱 연료 4,000,000 을 16b + 17d 의 모든 호출이 나눠 쓴다 — 호출마다 min(1,000,000, 남은 연료), 남은 연료로
  멈춘 호출은 "예산" (쓰기 버림, 트랩 정책 밖), 남은 연료가 0 이면 "건너뜀". 트랩이 3 번 이어지면 시스템을 끈다 (경고 한 줄).
  이어진 트랩 · 꺼짐 = 시뮬레이션 상태 → `state()` / `restoreState()` (팩 바이트코드 digest 가 다르면 버리고 다시 켠다).
  시스템별 누적 통계 (호출 · 트랩 · 예산 · 건너뜀 · 연료 합 · 최대 · 마지막 트랩) 와 `describeScriptSystems`.
- 세이브: world.json "scriptSystems" (선택 키 — 기본값이 아닌 시스템만). 로드가 되살리고, 바뀐 · 없는 시스템은 경고.
- SandboxServer · sbx_sim_check 가 끝날 때 시스템마다 한 줄.
- 고친 것: 트랩 로그의 함수 이름이 깨져 나왔다 (M4-T1 부터) — 삼항의 공통 타입이 std::string 이라 string_view 가 임시값을
  가리켰다. std::string 으로 받고 시험이 함수 이름을 본다.
- 시험: test_script_budget 4 케이스 (unit_script). 문서: ADR-0064, 09 2장, 15, 19 (0 · 13 · 14 · 17 · 19장), 18 (M4 완료, 다음
  M5-T1), 17, 13.

**왜**

- 호출 예산만으로는 시스템 수만큼 틱이 길어지고, 계속 망가진 시스템이 틱마다 연료를 태운다. 연료 (명령 수) 로 끊어 결정적이다
  (벽시계를 보지 않는다 — 04).
- 끈 상태를 메모리에만 두면 세이브 → 로드가 시스템을 다시 켜 두 월드가 갈린다 (D2). digest 로 고친 스크립트는 저절로 다시 켠다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). unit_script +4.
  `tools/check_includes.py`: 478 파일 위반 0.
- 실행 (스크래치 sysroot — M4-T1 · T2 의 재생 · 이벤트 시스템 + 틱 1 ~ 3 에 트랩 나고 그 뒤에 값을 쓰는 `eco.mana.buggy`):
  `sbx_sim_check --scenario ecosystem_small --content-root sysroot --ticks 300 --repeat 2 --save-at 135 --threads 0,1` →
  "eco.mana.buggy t3: 트랩이 3 번 이어져 시스템을 껐다 (마지막: … (ecosystem/scripts/buggy_system.ts:onTick pc 7))", D1 · D2 · D5
  일치 (꺼진 상태를 세이브가 잃으면 틱 136 부터 값을 써서 갈린다), 끝 해시가 M4-T2 실행과 같다 (꺼진 시스템은 아무것도 안 썼다),
  끝 줄 "스크립트 eco.mana.buggy: 호출 3 · 트랩 3 · 예산 0 · 건너뜀 0 · 연료 합 27 · 최대 9 · 꺼짐" 외 둘.
- 벤치: 스크립트 없는 월드의 경로는 그대로 (어댑터 없음). 틱 연료의 실제 시간은 M6-T1 벤치에서 맞춘다.

**남은 일**

- M5-T1: sbx check 스크립트 진단 · 템플릿 tsconfig · 에디터 스크립트 오류 · 적용. 시스템 켜고 끄기 명령 · 시스템별 시간 프로파일은
  M5 [계획]. 시스템별 예산 키는 M6 벤치 뒤 검토.

---

## 2026-10-26 — v0.3 M4-T2: onStart · onStop · onEvent — 단계 17d, 그 틱의 이벤트 (ADR-0063)

**무엇을**

- `script/api/SystemEvents`: 이벤트 종류 (death · spawn · areaEnter · score — events 키 · GameEventKind 멤버), GameEvent 필드 (이름
  순 = 칸), 예산 상한. 검사기 · 검증기 · 엔진 · d.ts 가 같이 읽는다.
- 검사기: defineSystem 에 onStart · onStop · onEvent 메서드, `events` 구독 (종류마다 하나 — tags { all · any · none } 이름,
  areaEnter 의 min · max [x, y] — f32 로 반올림), `eventsPerTick` (기본 256). onTick 은 선택이 됐다 (콜백이 하나는 있어야 한다).
  SDK enum GameEventKind · 구조체 GameEvent (onEvent 의 e — 적지 않으면 그 타입). 잘못은 S426.
- 바이트코드 .sbc 형식 4: 시스템 표에 콜백 함수 번호 (없으면 0xFFFFFFFF) · eventsPerTick · 구독. 검증기가 콜백 서명 (onEvent =
  (ServerContext?, GameEvent?) — 구조체 모양까지) · 구독 차례 · 사각형 · 태그 · 예산을 본다.
- VM: `Interpreter::call(함수, 인자, 칸)` — 구조체 매개변수를 칸 목록으로 이 호출의 아레나에 만든다 (Host 결과와 같은 잎 규칙,
  칸 수가 다르면 BadCall).
- 콘텐츠: 로드 1 단계 (태그) 뒤 구독의 태그 이름을 태그 비트로 풀어 `ScriptPackProgram::events` 에 — 없는 태그는 V7 (시스템 id ·
  정의 자리).
- 단계 17d (`core/simulation/SimulationScripts.cpp`, 17c 뒤): 시작 (세션이 Running 이 된 틱 · 세션 없으면 틱 1) · 끝 (Finished 가
  된 틱) · Died · EntitySpawned · 지금 개체 · 점수 칸을 모아 `ScriptSystemAdapter::runCallbacks` 로 — ① onStart ② onEvent (시스템 →
  종류 → 일어난 차례, eventsPerTick · 틱 4096 예산 — 넘으면 cut) ③ onStop. 단계 0 스냅숏을 트리거와 공용으로 — 스크립트가 death ·
  areaEnter · score 를 구독하면 세션 없는 월드에서도 뜬다.
- sandbox.d.ts: GameEventKind · GameEvent · TagFilter · SystemEvents, SystemDefinition 의 콜백 · events · eventsPerTick.
- 시험: test_script_callbacks 9 케이스 (unit_script), M4-T1 시험의 바뀐 문구 · 형식 번호.
- 문서: ADR-0063, 03 2장 (17d · 그림), 04 (simVersion 이력 — 올리지 않음), 11 (8 · 11장), 19 (0 · 9 · 11 · 12 · 13 · 17 · 18장),
  18 (M4-T2 ✅, 다음 M4-T3), 17, 13.

**왜**

- "그 틱에 일어난 일" (죽음 · 생성 · 점수 · 세션 끝) 은 17 · 17b · 17c 가 끝나야 다 있다. 16b 에서 지난 틱의 이벤트를 받으면 사이의
  세이브가 이벤트를 잃거나 대기열이 세이브 상태가 된다 — 17d 에서 그 틱 안에 답한다.
- 시작 · 끝을 틱 번호 · 세션 stateTick 으로만 알아내 세이브 → 로드한 월드가 onStart 를 다시 부르지 않는다 (D2).
- 이벤트 원천 (스냅숏 · Died · 영역 규칙) 을 트리거와 같게 — 콘텐츠의 트리거와 스크립트가 같은 일을 같은 뜻으로 본다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). unit_script +9.
  `tools/check_includes.py`: 477 파일 위반 0.
- 작업 중 `tests/unit/script/test_bytecode.cpp` 1 줄 앞에 바깥에서 들어간 '+' 가 있어 빌드가 깨졌다 — 사용자에게 묻고 지웠다
  (HEAD 와 같아져 이 커밋에는 없다).
- 실행 (스크래치): 저장소 ecosystem 팩 복사본 (토끼에 eco.mana · M4-T1 의 재생 시스템) 에 `eco.mana.events` (onStart 로그,
  events { death · spawn — tags all prey }, spawn 이면 그 토끼의 current = 50, death 면 위치 · 까닭 로그) 를 더해
  `sbx_sim_check --scenario ecosystem_small --content-root sysroot --ticks 300 --repeat 2 --save-at 135 --threads 0,1,4` →
  onStart 는 실행마다 t1 한 번 (로드한 월드는 다시 없음), D1 · D2 · D5 일치. `--ticks 1500 --save-at 700` → 늑대에게 잡힌 토끼
  53 마리의 "rabbit died at x, y cause 3" (t637 부터), tick 700 저장 → 로드 월드가 끝까지 같은 해시.

**남은 일**

- M4-T3: 예산 (연료 · 메모리 · 질의 · 이벤트 · HostCall — 호출 · 틱) · 트랩 정책 (시스템 끄기) · 시스템별 통계. 스크립트의 생성 ·
  파괴 · 점수 Host 항목이 생기면 17d 의 이어짐 (그 이벤트를 같은 틱 목록 끝에). 영역 나감 · 머묾은 트리거와 함께 [계획].

---

## 2026-10-26 — v0.3 M4-T1: defineSystem · ScriptSystemAdapter · 단계 16b (ADR-0062)

**무엇을**

- 검사기: `export default defineSystem({ id, domain?, intervalTicks?, onTick(ctx) { … } })` — 상수 정의를 `Program::systems` 로,
  onTick 메서드 하나 = 함수 하나 (ctx 를 적지 않으면 ServerContext, 반환 void). 진단 S426 (정의) · S427 (자리), S423 은 "export
  default 의 다른 식 · defineSystem 밖의 메서드" 로 좁혔다. 가져오지 않은 defineSystem 은 import 를 안내하는 S401.
- 바이트코드 .sbc 형식 3: 모듈의 시스템 표 (id 오름차순 — id · 파일 · onTick 함수 번호 · intervalTicks). 검증기가 id 규칙 · 순서 ·
  겹침 · 함수 번호 · onTick 서명 · 주기를 본다. 코드 생성이 표를 id 순으로 쓴다.
- 콘텐츠: 로드 0 단계가 팩마다 만든 모듈을 `ContentDatabase::scriptPrograms` (core/content/ScriptPrograms) 에 둔다 — 오버레이 DB 와
  나눈다. 팩 사이 같은 시스템 id 는 V1.
- `core/script/ScriptSystemAdapter`: 팩 모듈마다 인터프리터, ScriptHost 하나. 단계 16b (Native System 뒤, 17 Structural② 앞) 에서
  (팩 순서, id) 차례로 틱 % intervalTicks == 0 인 시스템의 onTick 을 부른다 (beginCall → call → endCall(트랩 아님)). 트랩이면 그
  호출의 쓰기만 버리고 다음 시스템으로, 로그는 시스템마다 첫 트랩 한 줄. SimulationWorld 는 콘텐츠에 시스템이 있을 때만 만든다.
- sandbox.d.ts 에 SystemDefinition · defineSystem (sbx sdk sdk 로 다시 만듦 — Host 표 지문은 그대로).
- 시험: test_script_systems 7 케이스 (unit_script), test_component_host 의 형식 번호 2 → 3.
- 문서: ADR-0062, 03 2장 (16b · 그림), 04 (simVersion 이력 — 올리지 않음), 11 (8 · 11장), 19 (0 · 1 · 9 · 11 · 12 · 14 · 17장),
  18 (M4-T1 ✅, 다음 M4-T2), 17, 13.

**왜**

- 스크립트가 컴포넌트를 정의 · 읽기 · 쓰기까지 하지만 시뮬레이션이 부르는 자리가 없었다. ADR-0057 7 의 조건 (단계 하나, 순서 = 팩
  순서 · id, 스크립트 없는 월드는 그대로) 을 구체화했다.
- 16b (17 앞) 는 앞으로의 생성 · 파괴 (ECB) 가 같은 틱에 반영될 자리이고, 17b · 17c 가 스크립트 결과를 같은 틱에 본다. 스케줄러의
  System 으로 넣지 않아 스크립트 없는 월드의 스케줄러 목록 · 프로파일이 그대로다.
- 주기를 틱 번호로만 정해 세이브에 스크립트 상태가 생기지 않는다 (ADR-0056 4).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — kSimVersion 4).
  unit_script +7. `tools/check_includes.py`: 474 파일 위반 0.
- 실행 (스크래치): ManaGame 프로젝트에 `eco.mana.regen` (intervalTicks 30 — 16장의 regen 을 부르고 90 틱마다 ctx.log) 을 더해
  `sbx check` 통과, `SandboxServer --world ManaGame --ticks 300 --exit --port 0` 이 "[script] eco.mana.regen t90/t180/t270" 로그 뒤
  tick 300. 저장소 ecosystem 팩 복사본 (토끼에 eco.mana) 으로 `sbx_sim_check --scenario ecosystem_small --content-root sysroot
  --ticks 300 --repeat 2 --save-at 135 --threads 0,1,4` 가 토끼 75 마리를 재생하며 D1 · D2 · D5 일치. intervalTicks 0 · stage 키를
  넣으면 `sbx check` 가 "content/ecosystem/scripts/regen_system.ts:5:18: 오류 S426 — …" 두 줄로 거절.
- 벤치: 스크립트 없는 월드의 틱 경로는 포인터 검사 하나라 sbx_bench 를 다시 재지 않았다. 스크립트 시스템의 비용 (10k 대상) 은 M6-T1.

**남은 일**

- M4-T2: onStart · onStop · onEvent, M4-T3: 예산 · 트랩 정책 (시스템 끄기) · 시스템별 통계. 스크립트의 생성 · 파괴 (ECB) 는 Host 항목과
  함께 [계획].

---

## 2026-10-26 — v0.3 M3-T4: 스크립트 컴포넌트의 세이브 자동 마이그레이션 (ADR-0061) → M3 완료

**무엇을**

- `persist::migrateScriptComponent` (core/persist/Migration): 저장 version < 스키마 version 이면 스키마에 없는 필드는 버리고 (이름을
  모은다), i32 · enum 이 된 필드의 정수인 실수 (3.0) 는 정수로. 없는 필드는 그대로 (읽기가 기본값). 저장 version > 스키마 =
  VersionMismatch ("스크립트를 되돌렸나").
- `loadWorld`: 스크립트 컴포넌트 (dynamicLayout) 는 MigrationRegistry 없이 위 함수로. 옮긴 뒤 엄격한 읽기가 실패하면 "'id' v1 → v2
  마이그레이션 뒤 읽지 못했다 (필드의 종류 · 범위가 바뀌었다)". 같은 version 에서 스키마에 없는 필드 = 실패 ("version 을 올린다"),
  저장 값에 없는 필드 (더하기만) = 기본값 + 옮긴 것으로 친다. 컴포넌트마다 경고 한 줄 (개체 수 · 버린 필드).
- 시험: test_script_content +2 케이스. 문서 09 6장, 19 (0 · 15장), 18 (M3 완료), 17, 13, ADR-0061.

**왜**

- 스크립트 컴포넌트는 제작자가 자주 바꾸는데 C++ 마이그레이션 함수를 줄 자리가 없다 (스크립트에 전역 실행이 없다). 더하기 · 빼기를
  자동으로 하고, 값이 바뀌는 변환은 오류로 드러낸다 (04 — 세이브 값을 조용히 바꾸지 않는다).
- 같은 version 의 "더하기만" 을 처음엔 그대로 읽게 했는데, 컴포넌트 바이트가 달라 로드 해시 비교 (D2) 가 "해시 불일치" 로 실패했다
  (시험이 잡았다) — 옮긴 것으로 쳐서 해시 비교를 건너뛰고 경고하도록 고쳤다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). unit_content +2.
- 실행 (스크래치): 저장소 ecosystem 팩 복사본에 eco.mana v1 (current · old) 을 더하고 토끼 Prefab 에 붙여
  `sbx_sim_check --scenario ecosystem_small --content-root migroot --ticks 120 --save-at 60 --save-dir migsave` (D2 일치) →
  스크립트를 v2 (old 빼고 shield 더함) 로 → `SandboxServer --world migsave --content-root migroot --port 0 --ticks 90 --exit` 가
  "세이브: 스크립트 컴포넌트 'eco.mana' 를 v1 → v2 로 옮겼다 (개체 75) — 버린 필드: old" 경고 뒤 tick 90 까지. 같은 세이브를
  version 1 · old 없는 스크립트로 열면 "필드 'old' 가 스크립트 컴포넌트 'eco.mana' (version 1) 에 없다 — … version 을 올린다" 로 거절.

**남은 일**

- M4-T1: defineSystem · ScriptSystemAdapter · 단계 ADR. 이름 바꾸기 · 단위 변환 (스크립트 onMigrate) 은 v0.4 검토.

---

## 2026-10-26 — v0.3 M3-T3: 스크립트의 컴포넌트 읽기 · 질의 · 쓰기 + 호출 단위 쓰기 버퍼 (ADR-0060)

**무엇을**

- Host 표에 ctx.read(e, ref): T | null · ctx.has · ctx.query(ref): Entity[] · ctx.write(e, ref, T). 제네릭 ABI 타입 Component ·
  ComponentData · ComponentDataOrNull · EntityList (kHostApiVersion 1 그대로 — 더하기만, 지문 0388e85856c3e9f3).
- 검사기: TypeKind::Component (ComponentRef<T> — defineComponent 의 const 가 값이 된다), componentHostCall 이 ref 인자로 T 를 풀어
  나머지를 검사. 코드 생성: ComponentRef = 불투명 Component, LOADK + 모듈의 컴포넌트 표 → .sbc 형식 2. 검증기: 표의 id 규칙 ·
  겹침, LOADK, Entity[] 결과.
- VM: ComponentData 인자를 칸 목록으로 펼치고 (HostArg::fields), ComponentDataOrNull · EntityList 결과를 칸 목록에서 만든다
  (레지스터 타입을 따라 — 잎 모양 · 칸 수가 다르면 HostError), HostResult::extraFuel.
- ScriptHost: beginCall(tick, 이름, 월드) · endCall(commit). 호출마다 개체 핸들 표, 질의 saveId 순 (상한 65,536 · 개체마다 연료 1),
  읽기는 쓰기 버퍼 먼저, 쓰기는 바이트로 검사 (범위 · 유한 · f32 범위 · 개체 참조 → saveId) 해 버퍼에 — endCall(true) 가 적용,
  트랩이면 버림. 스크립트 컴포넌트만 (엔진 컴포넌트 · 붙이기 · 떼기는 [계획]).
- sandbox.d.ts 갱신 (read<T> … write<T>). ADR-0057 상태 줄에 "4 의 Query · 초안 setField 는 ADR-0060 이 대신" 표시.
- 시험: test_component_host 7 케이스 (unit_script), 기존 시험 갱신 (지문 고정값 · 형식 번호 · 컴포넌트 const 가 값이 된 것).

**왜**

- v0.3 완료 기준 "Mana 재생 시스템" 의 몸통 — 시스템 단계에서 부르기 (M4-T1) 전에 Host 쪽 의미 (순서 · 버퍼 · 검사 · 핸들) 를 시험으로 고정한다.
- 질의를 배열로, 쓰기를 값 통째로 한 것: 새 VM 객체 종류 · 필드 이름 문자열 없이 기존 규칙 (for-of · 구조체 맞추기 · d.ts 제네릭)
  으로 끝난다 (ADR-0060 5 · 6). VM 쪽 잎 모양 검사는 검증기가 T 를 모르는 대신 메모리 안전을 지킨다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — 엔진은 아직 스크립트를
  부르지 않는다, kSimVersion 그대로). unit_script 59 → 66 케이스.
- 실행: 스크래치 프로젝트 ManaGame (M3-T2) 에 regen.ts (query → read → Math.min → write) 를 더해 `sbx check` 오류 0, 일부러 빠진
  필드로 ctx.write 하면 "content/ecosystem/scripts/regen.ts:15:85: 오류 S404 — Mana 의 필드 'regen' 가 빠졌다" · 종료 코드 1.
  `SandboxServer --world ManaGame --port 0 --ticks 300 --exit` → tick 300 hash 0x6a9e46df903c0e1a (스크립트를 부르는 단계는 M4).
- `sbx sdk sdk` 로 sdk/sandbox.d.ts 를 다시 만들어 골든 시험 통과.

**남은 일**

- M3-T4: 스키마 version · 세이브 필드 마이그레이션. M4-T1: 시스템 단계에서 부르기 (그때 버퍼 적용이 틱 안 — 복제 changed 표시 맞음).
- 엔진 컴포넌트의 ComponentRef (권한 표), 붙이기 · 떼기 · 생성 · 파괴 (ECB), 여러 컴포넌트 질의, 질의 정렬 비용 측정 (M6 벤치).

---

## 2026-10-26 — v0.3 M3-T2: 콘텐츠 팩 scripts/ 컴파일 → 콘텐츠마다 카탈로그 → Prefab · 명령 · 세이브 · 복제

**무엇을**

- `core/content/ContentLoader`: 로드 0 단계 — 팩 순서로 `scripts/**/*.ts` 를 팩마다 검사 + 코드 생성. 진단은 콘텐츠 문제
  ("<팩 폴더>/scripts/a.ts:줄:열: 오류 S404 — …", 규칙 = 진단 코드). defineComponent 가 정의한 컴포넌트를 엔진 카탈로그의 복사에
  넣어 그 카탈로그로 Prefab · Rule · 트리거를 검사한다 (겹치는 id = V1). 다른 함수에 오류가 있어도 오류 없는 정의는 넣는다.
  오버레이는 스크립트를 다시 컴파일하지 않고 base 의 카탈로그를 나눈다. 동적 컴포넌트가 든 카탈로그를 받으면 그것을 빼고 시작한다.
- `ContentDatabase::worldCatalog(엔진)` · `scriptCatalog` · `scriptComponents`. 스크립트 컴포넌트가 없으면 카탈로그를 만들지 않는다
  (엔진 카탈로그 그대로 — 스크립트 없는 월드는 지금과 같다).
- 월드가 그 카탈로그를 쓴다: SimulationWorld 생성자 · persist::loadWorld · ClientWorld::create · 에디터 NetworkSession::catalog
  (복제 월드가 있으면 그 카탈로그 — Inspector · 되돌리기). ComponentCatalog::hasDynamic · withoutDynamic.
- `core/script/ScriptComponents`: 문제를 구조체 ScriptComponentProblem 으로 (모듈 · 줄 · 열 · id · 이유).
- 시험: test_script_content 8 케이스 (unit_content), test_replication +1 (unit_network). 문서 11 (1 · 1.2 · 8 · 11장), 19, 02, 18, 17, 13.

**왜**

- ADR-0059 6 의 "콘텐츠마다 카탈로그": 한 프로세스의 월드들 (에디터의 편집 · 테스트 플레이, 도구) 이 섞이지 않게 카탈로그를 콘텐츠
  DB 가 든다. 부르는 쪽 (서버 · 클라이언트 · 도구 · 에디터) 은 엔진 카탈로그를 그대로 넘기고, 월드를 만드는 세 곳이 고른다 —
  배선을 바꾸는 곳이 셋뿐이다.
- 진단을 콘텐츠 문제로 내면 sbx check · 서버 로드 · 클라이언트가 같은 목록을 본다 (v0.3 M5-T1 의 "sbx check 의 스크립트 진단" 중
  CLI 부분이 여기서 된다 — 에디터 "콘텐츠" 창 표시는 M5-T1 그대로).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — 스크립트 없는 콘텐츠는
  카탈로그 · 순서가 같다, kSimVersion 그대로). unit_content +8, unit_network +1 (스크립트 컴포넌트가 복제 표에 · serverOnly 는
  빠짐 · 첫 스냅숏과 바뀐 값이 같은 바이트로 수렴).
- 실행 (스크래치 폴더): `sbx new ManaGame --id studio.mana --from ecosystem_small` → `content/ecosystem/scripts/mana.ts` 에 eco.mana,
  토끼 Prefab 에 `"eco.mana": {"current": 5}` → `sbx check` 오류 0 (경고 2 = 시작 월드가 다른 contentHash 로 저장됨 — 예상) →
  `SandboxServer --world ManaGame --port 0 --ticks 900 --exit` 두 번 = tick 900 hash 0x240b869605933f4e entities 548 (같다) →
  `sbx build` · `sbx check mana.sbpkg` · 패키지로 서버 = 같은 해시 → `SandboxClient --headless --world ManaGame --frames 120` =
  스냅숏 29 · 다시 맞춤 0. 기본값을 범위 밖 (500) 으로 바꾸면 `sbx check` 가
  "content/ecosystem/scripts/mana.ts:4:32: 오류 S424 — 기본값 500 이 범위 [0, 100] 밖이다" + Prefab V3 로 종료 코드 1.

**남은 일**

- M3-T3: Host ctx.read · has · query · 필드 쓰기 + 호출 단위 쓰기 버퍼. M3-T4: 스키마 version · 필드 마이그레이션.
- 바이트코드를 DB 에 두기 (M4-T1 — 시스템과 함께). 에디터 "콘텐츠" 창의 스크립트 오류 줄 (M5-T1).

---

## 2026-10-26 — v0.3 M3-T1: 스크립트 컴포넌트 정의 → 스키마 → 동적 풀 · 카탈로그 (ADR-0059, D-03)

**무엇을**

- ADR-0059 (D-03 결정): defineComponent 는 최상위 const 의 상수 정의 — 검사기가 실행하지 않고 스키마로 읽는다. 필드는 이름 순,
  저장은 바이트 배치 동적 풀, 형식은 내장 컴포넌트의 방문자를 그대로 (같은 JSON · 해시 · 복제 바이트), 카탈로그는 콘텐츠마다 [M3-T2].
- `script/api/ComponentSchema`: 정규 스키마 (id · version · 플래그 · 필드 — 종류 i32 · f32 · f64 · bool · vec2 · entity · enumOf, 기본값 ·
  범위 · enum 멤버), 이름 규칙 · 엔진 이름공간 · f32 반올림. Foundation 만.
- 검사기: SDK 이름 defineComponent · field · Vec2. `const X = defineComponent({ … })` → Program::components (스키마 · 심볼 · 값 구조체).
  새 진단 S424 (정의 모양 · id · version · 필드 · 기본값 · 범위 · 플래그) · S425 (정의 밖의 defineComponent · field, 컴포넌트를 값으로 —
  ctx.read 는 M3-T3). 심볼 종류 Component.
- `core/ecs/DynamicComponent`: DynamicComponentLayout (이름 순 · 자연 정렬 · stride 8 의 배수 · 기본값 바이트), reflectDynamic
  (칸 → 타입 있는 임시값 → 방문자 field()), DynamicComponentPool (ComponentPoolBase + 바이트 배열), dynamicComponentTypeId (이름 → 번호
  프로세스 표).
- `core/ecs/ComponentCatalog`: ComponentInfo 의 함수 칸을 std::function 으로 (부르는 곳은 그대로), dynamicLayout, addDynamic.
- `core/script/ScriptComponents`: 스키마 → DynamicComponentDesc → 카탈로그 (엔진 이름공간 · 겹친 이름은 "파일:줄: 컴포넌트 'id' — 이유").
- sandbox.d.ts: Vec2 · Field · FieldRange · field 이름공간 · ComponentDefinition · ComponentRef · defineComponent (sbx sdk sdk 로 갱신).
- 계획 변경 (18 6.2): M3 을 넷으로 — T1 정의 · 카탈로그 (이번), T2 콘텐츠 로드 · 콘텐츠마다 카탈로그 · Prefab · 세이브 · 복제 ·
  Inspector, T3 Host ctx.read · has · query · 쓰기 버퍼, T4 version · 필드 마이그레이션. 원래 T1 의 "Host 읽기 · 질의 · 쓰기 버퍼" 는 T3 으로.

**왜**

- 초안의 "Native 와 사용자 컴포넌트는 같은 스키마 · 레지스트리 계약": 카탈로그 한 곳을 지나면 세이브 · WorldHash · 복제 · Rule ·
  Inspector 경로가 이미 있다. 형식 규칙을 두 벌 쓰지 않으려고 같은 방문자를 부른다 — 어긋나면 조용히 세이브 · 복제가 달라진다.
- 이름 순은 선언 순서를 바꾸는 흔한 편집이 세이브 · 해시를 바꾸지 않게 (VM 구조체 배치도 이름 순 — ADR-0058).
- M3 을 나눈 것: 콘텐츠마다 카탈로그는 서버 · 클라이언트 · 에디터 · 도구의 카탈로그 배선 전체를 건드린다 — 정의 · 저장 형식을 먼저
  시험으로 고정하고 배선을 따로 커밋한다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — 시뮬레이션 동작 변경
  없음, kSimVersion 그대로). unit_script +8 (test_components), unit_ecs +5 (test_dynamic_component — 같은 C++ 컴포넌트와 JSON · 해시 ·
  바이트 · 오류 메시지가 같다). check_includes 468 파일 위반 0.
- 벤치 (ComponentInfo → std::function): 같은 기계 Windows MSVC Release `sbx_bench --only save.world` (50k 저장 · 로드 + 해시 검증).
  바꾸기 전 (git stash) 저장 501 · 542 ms, 로드 588 · 596 ms / 바꾼 뒤 저장 493 · 484 ms, 로드 555 · 577 ms — 차이 없음 (측정값, 두 번씩.
  바꾼 뒤 첫 실행 1319 / 1033 ms 는 빌드 직후 냉시작이라 뺐다).
- 실행: 헤드리스 서버 `SandboxServer --scenario random_walk_1k --ticks 60` → tick 60 hash 0x0028fa3b59616a46, 종료 코드 0
  (스크립트 없는 월드 그대로).

**남은 일**

- M3-T2: 콘텐츠 팩 scripts/ 컴파일 → 콘텐츠마다 카탈로그 → Prefab · 세이브 · 복제 · Inspector. M3-T3: Host 컴포넌트 항목 · 쓰기 버퍼.
  M3-T4: 마이그레이션. enum 필드의 JSON 값이 멤버인지 보는 검사는 아직 없다 (내장 enum 과 같다 — i32 범위만).
- tsc --strict 로 sandbox.d.ts + defineComponent 예제를 확인하는 CI 작업 (Node 있는 기계) [계획 — M2-T2 와 같다].

---

## 2026-10-26 — v0.3 M2-T2: Host 바인딩 core/script · 계약 검사 · sandbox.d.ts 생성 → M2 완료

**무엇을**

- `core/script/ScriptHost`: hostBindings (엔진 구현의 명시 목록 — ctx.tick · ctx.log), checkHostBindings (표의 항목마다 구현 정확히
  하나 · 서명 = 표 · 표에 없는 구현 · 함수 없음), ScriptHost (vm::IHost — beginCall 마다 새 ctx 핸들, 지난 · 위조한 · 열리지 않은
  ctx 는 HostError, ctx.log = 로그 "script" 범주 + recentLogs, 한 줄 1024 바이트를 UTF-8 경계에서 자른다). SandboxCore 가
  SandboxScript 를 링크한다.
- `script/sdk/SdkGenerator` + `sbx sdk [<폴더>]`: Host 표 · 불투명 타입 · SDK 내장 → sandbox.d.ts. Host 표에 매개변수 이름
  (paramNames — 정규형 · 지문에는 들어가지 않는다, 지문 그대로). 저장소 `sdk/sandbox.d.ts` (생성해 커밋) = 생성 결과 (골든 시험).
- 계획 변경: 호출 단위 쓰기 버퍼는 M3-T1 로 — 지금 Host 항목은 읽기 · 로그뿐이라 버퍼에 넣을 것이 없다 (18 6.2).
- 시험 test_script_host 6 케이스 · test_sbx_options +1. 문서 19 (14장), 18 (M2 완료), 01, 15, 17, 13.

**왜**

- ADR-0057 의 "넷이 어긋나지 않게": 검사기 (M1-T2) · 검증기 (M1-T3) 에 이어 바인딩 (계약 검사) · d.ts (골든) 가 같은 표를 본다.
- ctx 핸들을 호출마다 새로 내는 것은 "핸들은 호출이 끝나면 무효" (ADR-0057 4) — 스크립트가 전역에 ctx 를 둘 수는 없지만
  (ADR-0056 4) 위조한 불투명 값은 VM 경계에서 막는다.

**검증**

- Windows MSVC Debug: 빌드, unit_script 51 케이스, unit_tools 12, 전체 ctest 62/62 (render_tests_warp 제외).
- `sbx sdk sdk` 를 실제로 돌려 sdk/sandbox.d.ts 를 만들었다 (그 파일이 골든). 시뮬레이션 변경 없음.

**남은 일**

- v0.3 M3-T1: D-03 (동적 컴포넌트 풀) ADR, defineComponent → 스키마 → 카탈로그, ctx.read · ctx.query, 호출 단위 쓰기 버퍼.
- d.ts 를 tsc --strict 로 확인하는 시험은 Node 가 있는 CI 잡에서 [계획].

## 2026-10-26 — v0.3 M2-T1: SandboxScript 인터프리터 · JS 수 규칙

**무엇을**

- `script/vm/Interpreter`: 검증한 모듈을 돌린다 — 레지스터 칸 한 줄 (프레임마다 0 으로), 호출마다 새 아레나 (문자열 · 배열 ·
  구조체 · 상자, 상수 문자열은 표를 그대로), 예산 Limits (연료 · 깊이 · 칸 · 아레나 · 배열 · 문자열), 트랩 10 종 (CallResult —
  함수 · pc · 설명 · 쓴 연료, 예외 없음, 트랩 뒤에도 다시 부를 수 있다), Value (8 바이트 칸), IHost (CALLH — 문자열 인자는 내용으로).
- `script/vm/Numbers`: JS 수 규칙 — ToInt32, Number#toString (to_chars 의 가장 짧은 왕복 자릿수 + JS 의 지수 규칙), round (반은
  +∞ 쪽 · -0), pow (pow(1, NaN) = NaN 등 C 와 다른 곳), min/max (NaN · -0), UTF-16 길이, Math 실행 (자리 → 함수는 consteval 이름
  찾기라 표와 어긋나면 컴파일 오류). 검사기의 상수 접기도 같은 toInt32 · jsPow 를 쓴다 (컴파일 때 값 = 실행 때 값).
- 시험 test_vm 8 케이스. 문서 19 (13장 · 0 · 6 · 9장), 18, 17, 13.

**왜**

- v0.3 요구 7 (fuel · 메모리 · 스택 제한) 과 M2 의 실행. 엔진 바인딩 (M2-T2) 없이 가짜 Host 로 VM 만 시험할 수 있게 IHost 로 나눴다.
- JS 와 같은 뜻 (ADR-0056 3) 을 지키는 곳은 C 표준 함수와 다른 가장자리 (pow · round · min · toString) — 표로 고정했다.

**검증**

- Windows MSVC Debug: 경고 없이 빌드, unit_script 45 케이스 · 4193 단언, 전체 ctest 62/62 (render_tests_warp 제외).
- 같은 호출 = 같은 결과 · 같은 연료, 200 번 불러도 아레나가 쌓이지 않음. 시뮬레이션 변경 없음.
- 성능 벤치는 아직 없다 — 10k 대상 벤치는 v0.3 M6-T1 (sbx_bench).

**남은 일**

- v0.3 M2-T2: core/script Host 바인딩 (표 항목마다 구현 등록 + 계약 검사, 호출 단위 버퍼), sandbox.d.ts 생성기 · 골든.
- 예산 기본값은 M4-T3 에서 시스템 예산과 함께.

## 2026-10-21 — v0.3 M1-T3: SandboxScript 코드 생성 · 바이트코드 .sbc · 검증기 (ADR-0058) → M1 완료

**무엇을**

- ADR-0058: 레지스터 기계, 레지스터마다 타입 · 0 초기화, 8 바이트 칸, 구조체 배치는 필드 이름 순, 선형 검증기, 따로 된 IR 층 없음
  (검사기의 Program 이 타입 있는 IR), 팩 하나 = 모듈 하나, .sbc 에 체크섬 없음.
- `script/vm/Bytecode`: 명령 68 종 (타입별 산술 · 비교 · 변환 · 문자열화, 타입을 나누지 않는 배열 · 필드 · 상자, 흐름 · 호출 · Host
  호출), 타입 표 · 모듈 · export, serialize · deserialize (개수마다 상한 · 남은 크기 검사, 남는 바이트 거절) · disassemble.
- `script/vm/Verifier`: verify (번호 범위 · 피연산자 타입 · 서명 · 끝 명령 · pad · 타입 표 · UTF-8 · import = Host 표 · export
  겹침 · 머리 — 다르면 VersionMismatch), sameType (구조적, 재귀), loadModule.
- `script/lang/Codegen`: Program → Module — 지역 변수 레지스터, 문장마다 타입별 임시 재사용, 넣는 자리 변환 (I2F · 상자 · null ·
  좁힌 이름 UNBOX · Nullable(A) → Nullable(B)), ===/!== 의 null 가능 · 문자열 내용 비교, 짧은 회로, ??, 복합 대입 (비트 연산은
  number 도 i32 로 계산해 되돌린다), for-of (길이를 돌 때마다), switch, 가변 Math 는 둘씩. 만든 모듈은 바로 검증 (S502).
- `script/api/MathBuiltins`: Math 표를 검사기에서 옮겼다 (검증기 · VM 공용 — 자리 = MATHF 번호).
- 검사기: 구조체 필드는 readonly 도 양쪽으로 맞춘다 (배치가 같아야 해서), Math.min · max 에 i32 와 정수 상수를 섞으면 i32.
- 시험 test_bytecode 7 케이스 (+ test_checker 2 단언). 문서 19 (12장 · 5 · 8 · 9장), 18 (M1 완료), 17, 13, adr/README.

**왜**

- v0.3 M2 의 VM 이 돌릴 형식. 바이트코드는 파일로 올 수 있어 (믿지 않는 입력) 메모리 안전을 검증기에 모았다 — 흐름 분석 없이 한 번
  훑기로 끝나게 레지스터 타입을 고정했다.
- 디스어셈블이 드러낸 문제: C++ 함수 인자의 계산 순서 (MSVC 는 오른쪽부터) 에 gen(a) · gen(b) 를 맡겨 a() > b() 가 b 를 먼저
  불렀다 — JS 순서 위반이자 툴체인마다 다른 바이트. 하나씩 지역 변수로 차례를 고정하고 시험으로 묶었다.

**검증**

- Windows MSVC Debug: 경고 없이 빌드, unit_script 37 케이스 · 3459 단언 (모든 바이트 × 8 비트 뒤집기 포함), 전체 ctest 62/62
  (render_tests_warp 제외). 같은 소스 = 같은 바이트, 직렬화 왕복 = 같은 바이트.
- 시뮬레이션 변경 없음.

**남은 일**

- v0.3 M2-T1: 인터프리터 (연료 · 스택 · 호출 아레나 · 트랩 · JS Number#toString · UTF-16 길이).
- 최적화 (명령 융합 · 죽은 코드) 는 M6 벤치 뒤 [재검토 — ADR-0058].

## 2026-10-21 — v0.3 M1-T2: SandboxScript 이름 · 타입 검사기, Host API 표 첫 항목

**무엇을**

- `script/lang/Types`: TypeTable (기본 · T[] · T | null · 구조체 · enum · 불투명), 넣기 규칙 (i32 → number · enum → 수 · null → T | null,
  배열 원소 · 구조체 필드는 양쪽 — 참조라, 재귀 구조체는 비교 중인 짝을 가정).
- `script/lang/Checker`: checkProgram (팩의 스크립트 전부) → Program — 파일 경로 정렬 파싱, import 그래프 (resolveImportPath · 없는
  파일 · 없는 export · 순환 S416, 파싱 오류 모듈의 이름은 Error 타입), 모듈마다 의존 순서로 이름 · interface (재귀) · type 별칭 (순환
  S417) · 함수 서명 · enum · 최상위 const (소스 순서, 기본 타입 상수식 S415), 함수 몸 (반환 타입 추론 — 처음 부를 때, 재귀 S421).
  식: number · i32 규칙 (정수 상수는 i32 자리에, 비트 연산은 i32, 상수 접기는 실행 규칙대로 — i32 감싸기), null 좁히기 (&& · || · ! ·
  ? : · 빠져나가는 if, 대입 없는 이름만), 흐름 (모든 길 return · switch 떨어짐 · break/continue 자리), 객체 · 배열 리터럴 (기대 타입),
  Math 내장, 금지 이름 (S413, 이유와 대안). 마디마다 NodeSema (타입 · 심볼/필드/Host/내장/enum 멤버/length/push) · ConstValue.
- `script/api/HostApi`: ADR-0057 의 단일 정의 표 — ctx.tick · ctx.log, 불투명 타입 Entity · ServerContext, 정규형 · 지문 (kHostApiVersion 1).
  계획에 적은 "읽기 · 질의" 는 컴포넌트 타입이 필요해 v0.3 M3 로 옮겼다 (18 6.2).
- 시험 test_checker 14 케이스. 문서 19 (5 · 10 · 11장, S4xx 표), 17, 13, 18.

**왜**

- v0.3 요구 1 (타입 · 숫자 · 할당 · 모듈 · 결정론 규범) 의 검사 쪽. IR (M1-T3) 이 타입 · 상수 · 참조가 정해진 나무를 받게 한다.
- "SandboxScript 가 받으면 tsc 도 받는다" 를 지키려고 TS 보다 엄격한 쪽을 골랐다: 조건은 boolean, 좁히기는 대입 없는 이름만, 배열 ·
  필드는 양쪽으로, let 에 초기값 (19 11장의 "TS 와 다른 점").

**검증**

- Windows MSVC Debug: 경고 없이 빌드, unit_script 30 케이스 · 1833 단언, 전체 ctest 62/62 (render_tests_warp 제외).
- 잘린 소스 (3 바이트마다) · 타입이 엉망인 소스에서 죽지 않는다. 시뮬레이션 변경 없음.

**남은 일**

- v0.3 M1-T3: 타입 있는 IR → .sbc · 검증기. 숫자 → 문자열 (JS Number#toString) 은 VM (M2).
- 표 · 구조체 상수 (최상위), 문자열 첨자 · 메서드, 배열 pop 등은 [v0.3 뒤 검토].

## 2026-10-21 — v0.3 M1-T1: SandboxScript 렉서 · 파서 → AST · 진단, 모듈 script/, 규범 문서 19

**무엇을**

- 새 모듈 `script/` = 타깃 SandboxScript (Foundation 만, sbx_simulation_flags). 경계: `sbx_forbid_link(SandboxScript SandboxCore …)`,
  `check_includes.py` (script/ → foundation · script, 그래픽 · OS 헤더 금지 — 픽스처 script/BadCore.hpp, 자체 시험 8 → 10 건).
- `script/lang/`: Diagnostic ("파일:줄:열: 오류 Sxxx — …", 모음), Token · Lexer (예약어 · 문맥 낱말, 숫자 — 정확 반올림 (from_chars, 없으면
  C 로케일 strtod_l) · 진법 · 구분자 · 정수 표시, 이스케이프 · 서로게이트 짝, 템플릿 중괄호 깊이 스택, 줄바꿈 표시, ASCII 밖 · UTF-8 ·
  4 MiB 상한), Ast (평평한 마디 풀 · 자식 구간 · dump S 식), Parser (JS 의 ASI, 우선순위 · ** 오른쪽 결합 · >> 타입 인자 나누기,
  as const, 부분집합 밖 문법 S301~S336 을 알아보고 대안을 말한다, 대입 · ++ -- 는 식 문장 맨 위에만 (S334 — 나무 전체를 한 번),
  문장 단위 회복, 깊이 200 · 오류 64 상한).
- 시험 unit_script: test_lexer 6 · test_parser 10 케이스 (S3xx 표 전부, 잘린 Mana 소스의 모든 앞부분 · 의사 난수 바이트열 300 개).
- 문서: 19-SANDBOXSCRIPT (규범 — 0~9장, 타입 · VM 은 [계획]), 01 2장 (타깃 · 경계), 04 4.6, 13, 17, 18 (M1-T1 ✅), README, AGENTS 4장.

**왜**

- v0.3 요구 1 "언어 부분집합 규범 동결" 의 앞단. 검사기 (M1-T2) 가 받을 나무와 진단 형식을 먼저 고정한다.
- 부분집합 밖 문법을 "알 수 없는 낱말" 이 아니라 이유 · 대안과 함께 거절해야 TS 사용자가 고칠 수 있다 (ADR-0056 2).
- 소스는 믿지 않는 입력 (패키지로 온다) — 깊이 · 오류 · 크기 상한과 "잘린 · 아무 바이트 소스에서 죽지 않는다" 를 시험으로 묶었다.

**검증**

- Windows MSVC Debug (windows-msvc): 경고 없이 빌드, unit_script 16 케이스 · 1195 단언 통과, 전체 ctest 62/62 (render_tests_warp 제외),
  arch_include_lint · _selftest (10 건) 통과, 링크 경계 검사 10 타깃.
- clang-cl 은 이 PC 에 없어 windows-clangcl 프리셋을 돌리지 못했다 (Linux clang -Werror 는 CI 에서).
- 시뮬레이션 변경 없음 (골든 그대로).

**남은 일**

- v0.3 M1-T2: 이름 · 타입 검사 (최상위 상수식 · import 순환 · number/i32 · null 좁히기 · Host 표 첫 항목), 이름 수준 금지 (any · undefined …).
- 이름의 한글 등 ASCII 밖 글자 (S101) 는 [v0.3 뒤 검토].

## 2026-10-21 — v0.3 진입: D-01 언어 (ADR-0056) · D-04 Host API (ADR-0057) · v0.3 마일스톤

**무엇을**

- ADR-0056 (D-01): SandboxScript = TypeScript 의 엄격한 부분집합 (tsc --strict 도 받는 파일), 자체 C++ 컴파일러 (Node 없음),
  number = f64 · i32 = 32 비트 감싸기 (JS 의 | 0 · Math.imul), f32 는 필드 종류, GC · 예외 · 스크립트 전역 가변 상태 없음 —
  호출 아레나, 트랩 = 그 호출의 쓰기를 버림, 정적 import 만 (순환 오류), 소스가 패키지에 들어가고 로드 때 컴파일 · 검증기는 항상.
  모듈 script/ = SandboxScript 하나 (Foundation 만) + 바인딩 core/script.
- ADR-0057 (D-04): Host API 단일 정의 = C++ constexpr 표 (script/api) → 컴파일러 타입 검사 · 로드 때 이름으로 묶기 (인덱스
  영속 없음, .sbc 에 표 지문) · 바인딩 등록 검사 · sandbox.d.ts 생성 (골든 파일). 쓰기는 호출 단위 버퍼 (read-your-writes),
  질의는 saveId 오름차순, 엔진 관리 컴포넌트 · 범위 밖 · 죽은 개체 쓰기 = 트랩, 난수 Purpose::Script, hostApiVersion.
- 18: 0장 (다음 = v0.3 M1-T1), 1 · 3장, 6.1 (D-01 · D-04 결정, D-03 = M3-T1), 6.2 v0.3 마일스톤 M1~M6 · 완료 기준, 7장.
  03 6.4 (ADR 연결), 16 (버전 줄), adr/README.

**왜**

- v0.3 진입 조건 (18 6.1 "D-01 v0.3 진입 전"). 언어 · Host API 계약이 정해져야 컴파일러 · VM · 바인딩을 작은 태스크로 나눌 수 있다.
- 결정의 축은 04 (결정론 최우선): 스크립트 상태가 컴포넌트 밖에 없으면 세이브 · 리플레이 · 해시 · 복제가 새 책임을 지지 않는다.

**검증**

- 문서만 (코드 변경 없음). Windows MSVC Debug ctest 61/61 (render_tests_warp 제외) — 아래 커밋 직전 실행.

**남은 일**

- v0.3 M1-T1: 규범 문서 19-SANDBOXSCRIPT · 모듈 script/ · 렉서 · 파서 · 진단.
- D-03 (동적 풀) 은 M3-T1, 스크립트 단계는 M4-T1 의 ADR.

## 2026-10-21 — v0.2 M6-T2: 버전 종료 — 요구사항 done / deferred / removed, 제품 버전 0.2.0

**무엇을**

- 제품 버전 CMake `project(Sandbox VERSION 0.2.0)` (BuildInfo → `sbx --version` · `SandboxServer --version` = 0.2.0, 새 프로젝트의
  engine 기본 범위 ">=0.2.0 <1.0.0"). 샘플 rabbit_guard 의 ">=0.1.0 <1.0.0" 은 그대로 맞는다.
- CTest sample_playthrough: 프로젝트로 두 호스트를 비교한 뒤 `sbx build` 로 패키지를 만들어 `--world <.sbpkg>` 로도 헤드리스 서버 ·
  로컬 플레이가 같은 "게임 결과:" 줄을 쓰는지 본다 (작업 폴더 = 빌드 폴더의 sample_playthrough, 끝나면 지운다).
- 릴리스 노트 `docs/releases/v0.2.0.md` (새 폴더 — 이력, 버전마다 새 파일): 들어간 것 (M1~M6 · ADR), 호환성 번호 (제품 0.2.0 ·
  프로토콜 12 · kSimVersion 4 · 세이브 1 · 1 · 매니페스트 1 · 2 · 패키지 format 1), 초안 요구 3 · 5 · 9장의 done / deferred / removed
  표와 근거 시험, 미룬 것의 버전 재할당, 알려진 문제.
- 문서: 18 (0 · 1 · 2 · 4장, 5.1 M6-T2 ✅, 5.3 요구사항 검토, 7 · 8장), 13 5.1, 16 (버전 줄), 17 (docs/releases), README (문서 지도).

**왜**

- v0.2 DoD 4 "미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당". 요구 6 "하나의 GamePackage 를 헤드리스 서버와 로컬
  플레이에서" 는 M6-T1 이 프로젝트 폴더로만 비교했다 — 패키지 경로도 자동 시험으로 묶었다.
- 18 2장의 SaveGame · Visual Trigger · Edit ↔ Test Play 가 "Partial (M4 · M5 계획)" 으로 남아 있었다 — 구현에 맞춰 고쳤다.

**검증**

- Windows MSVC Debug (windows-msvc): 빌드, `chcp 65001` 뒤 ctest 61/61 통과 (render_tests_warp 제외 — 알려진 문제).
- sample_playthrough: 프로젝트 · 패키지 × SandboxServer --script · SandboxClient --headless --script 모두
  `게임 Finished · win · team 0 · rule 3 · 점수 -60 · tick 3691 · hash 0x02d03b52e9655a3f`.
- 골든 det_* 그대로 (시뮬레이션 코드 변경 없음, kSimVersion 4).

**남은 일**

- v0.3 진입: D-01 (SandboxScript 언어 부분집합) · D-04 (Host API 단일 정의) ADR → 18 6장을 마일스톤으로.
- v0.1 열린 항목 (실기 QA · 50k 판정 · Actions 결과) 그대로. 미룬 것 목록은 releases/v0.2.0 5장.

## 2026-10-21 — v0.2 M6-T1: 데모 미니게임 samples/rabbit_guard · 자동 플레이 스크립트 — 헤드리스 서버 = 로컬 플레이 (ADR-0055)

**무엇을**

- `samples/rabbit_guard/` — 커밋한 게임 프로젝트 (sbx new --from ecosystem_small --mode eco.protect 그대로) + README +
  `playthrough.json` (8 배속 · 시작 · 늑대가 처음 있던 자리에 덫 15 번 · 30 초마다 먹이, expect = Finished · win · rule 3 ·
  tick 3691 · 해시 (MSVC 키)).
- `core/scenarios/PlayScript`: 스크립트 읽기 (format 1 · steps {at, command — 리플레이 JSON} · maxTicks · expect — 해시는 툴체인
  키별), attachPlayScript (ScenarioRunner::replaceScenario — 서버 러너의 시나리오로), playFinished · describePlayResult ("게임 결과:"
  한 줄 — 상태 · 결과 · team · rule · 점수 · tick · hash) · checkPlayExpectation · runPlayScript.
- ServerHostDesc.stopWhenGameFinished + reachedStop (게임 Finished — 자동 재시작 모드 말고 — 이거나 stopAtTick). --exit 와
  --script 가 켠다 → `--ticks N --exit` 가 일찍 끝난 게임에서 멈추지 않던 문제도 고쳤다 (테스트 플레이 중은 제외).
- SandboxServer · SandboxClient `--script <파일>` (클라이언트는 LocalServerDesc.script · AppConfig::quitWhen — 헤드리스면
  로컬 서버가 멈출 때 끝, 서버 월드의 결과 줄). expect 가 다르면 종료 코드 1.
- 시험: test_play_script (3 — 저장소 샘플을 같은 프로세스에서 끝까지 두 번), test_server_options +1, CTest sample_playthrough
  (헤드리스 서버와 로컬 플레이의 결과 줄이 같다).
- 문서: ADR-0055, 13 5.1, 15 7장, 17, 18 (M6-T1 ✅ — 다음 M6-T2), adr/README, samples/rabbit_guard/README.

**왜**

- v0.2 완료 기준 "템플릿만으로 미니게임을 만들어 저장 · 실행 · 종료" 를 사람 없이 자동으로. 서버가 넣는 명령은 러너 단계에
  묶여 있어 실시간 서버 (Threaded) 와 고정 dt 로컬 플레이 (Inline) 가 같은 입력을 받는다.

**검증**

- Windows MSVC Debug: ctest 61/61 통과 (render_tests_warp 제외). 수동: SandboxServer --script 16 초 · SandboxClient --headless
  --script 9 초, 둘 다 "게임 Finished · win · team 0 · rule 3 · 점수 -60 · tick 3691 · hash 0x02d03b52e9655a3f".
  sbx check samples/rabbit_guard 오류 · 경고 0.
- 골든 · 와이어 · 세이브 형식 · kSimVersion 그대로.

**남은 일**

- M6-T2 버전 종료 (요구사항 정리 · 제품 버전 0.2.0). 봇 클라이언트 (접속자 입력) 자동 플레이, 스크립트 녹화, Linux 툴체인의 샘플
  해시 기록 — [계획].

---

## 2026-10-20 — v0.2 M5-T2: 게임 패키지 .sbpkg · 세이브 결속 → M5 완료 (ADR-0054, D-02 결정)

**무엇을**

- `foundation/hash/Sha256` (FIPS 180-4).
- `core/package/GamePackage`: .sbpkg = magic · format 1 · index(JSON, 키 정렬) · 파일 바이트. 파일마다 SHA-256, digest =
  SHA-256("<sha256> <size> <path>\n" path 순). buildPackage (openProject 검사 → 매니페스트 · 읽은 팩 · 시작 월드만 → 원자적
  쓰기), readPackageIndex · verifyPackage · extractPackage (캐시 <임시>/sbx-packages/<digest 16>/, 다시 쓸 때 파일마다 확인),
  packageSavesRoot (<이름>-saves). 압축 · 서명 없음 (D-02).
- `openWorldSource`: .sbpkg 를 검증 · 풀어 프로젝트로 연다 (WorldSource.package · projectDir — 재시작의 시작 월드는 풀어 둔 폴더).
  서버 · 로컬 서버 · 클라이언트 세이브 기본값이 패키지 옆.
- 세이브 결속: persist::GameBinding · SaveOptions.game → world.json "game" {gameId, version, package?}. ServerHost 가 저장 ·
  기록 시작 세이브에 쓰고, 불러오기에서 다른 게임 거절 · 다른 버전 · 패키지 빌드는 결과에 알린다. SaveSummary.game.
- sbx: `build <폴더> [--out]`, `check <파일.sbpkg>`.
- 시험: test_sha256 (2), test_game_package (4), test_world_ops +1 (결속), CTest project_package.
- 문서: ADR-0054, 09 (2 · 9장), 15 7장, 13, 17, 18 (D-02 결정 · M5 ✅ — 다음 M6-T1), adr/README.

**왜**

- 게임 하나를 파일 하나로 주고받고, 고친 파일을 거절하고, 같은 프로젝트는 같은 바이트 (초안 02 7장 A · B · E). 세이브가 어느
  게임의 것인지 알아 다른 게임 세이브를 막는다.

**검증**

- Windows MSVC Debug: ctest 60/60 통과 (render_tests_warp 제외). 수동: 데모 프로젝트를 두 번 빌드 → 같은 바이트, sbx check 오류 ·
  경고 0, `SandboxServer --world <패키지> --start-game --ticks 120 --exit` 정상 · 세이브 폴더가 패키지 옆, 한 바이트를 고친
  패키지는 sbx check · 서버 모두 "sha256 과 다르다" 로 거절.
- 골든 · 와이어 · kSimVersion · worldVersion · schemaVersion 그대로 (world.json "game" 은 선택 키).

**남은 일**

- M6-T1 데모 미니게임 프로젝트 (samples/). 압축 · 서명 · 흐름 읽기, 원격 클라이언트의 패키지 받기, 캐시 지우기, 게임 버전 간
  세이브 마이그레이션 — [계획].

---

## 2026-10-20 — v0.2 M5-T1: 테스트 플레이 — 편집 월드는 서버 메모리에 맡기고 복사본으로, 멈추면 되돌린다 (ADR-0053)

**무엇을**

- ServerHost: WorldOp `TestPlay` — 편집 월드를 임시 폴더에 저장하고 다시 열어 복사본을 만들고 (폴더는 바로 지움), 원본 runner 는
  `m_editRunner` 에 그대로 맡긴다 (replaceWorld 의 keepOld). 게임 프로젝트면 복사본의 세션을 Lobby 로 놓고 GameControl Start,
  일시정지였으면 ResumeSimulation 을 서버가 넣는다. `TestStop` — 복사본을 버리고 맡긴 원본으로 되돌린다.
  그 동안 불러오기 · 재생 · 재시작 · 기록 시작 거절, 저장은 된다, 자동 재시작 없음. ServerStats mode 3.
- 프로토콜 11 → 12: WorldOp op 8 · 9, WorldChanged reason 4 · 5, ServerStats mode 3.
- 클라이언트 "월드" 패널: "테스트 플레이" · "테스트 플레이 멈추기 (되돌리기)" 단추, 상태 줄. WorldOp 열거 값 TestPlay · TestStop.
- 시험: test_game_control +1 (서버 끝까지), test_world_panel +1 (로컬 서버) · 패널 모드 3, test_messages +1 (프로토콜 12).
- 문서: ADR-0053, 08 (머리말 · 5장 표 · 프로토콜 12), 10 9장, 13, 18 (M5-T1 ✅ — 다음 M5-T2), adr/README.

**왜**

- 편집 → 플레이 → 되돌리기를 저장 · 불러오기 없이. 원본 객체를 그대로 맡겨 "원본 오염 없음" 을 가장 확실하게 (세이브에 담지
  않는 상태까지 그대로).

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외). 멈춘 뒤 편집 월드의 틱 · WorldHash · 개체 수 · 세션 Lobby 가 시작
  전과 같다 (test_game_control). 골든 · 세이브 형식 · kSimVersion 그대로.

**남은 일**

- M5-T2 GamePackage. 메모리 직렬화 (임시 폴더 없이), 여러 사람이 각자 테스트 플레이 — [계획].

---

## 2026-10-20 — v0.2 M4-T3: 트리거 편집 — 오버레이 종류 Trigger (프로토콜 11) · "콘텐츠" 창의 트리거 폼 → M4 완료 (ADR-0052)

**무엇을**

- 오버레이 종류 `ContentKind::Trigger` (값 3, "trigger", 폴더 "triggers"): EditContent 로 트리거를 바꾸거나 더하거나 뺀다.
  로더 loadTriggerJson (팩 · 오버레이 공용) — 바꾼 것은 팩의 실행 순서 자리, 더한 것은 맨 뒤 (TriggerDef.overlay).
  와이어 kind 0~3 → 프로토콜 11. 세이브 overlay/triggers/<id>.json (worldVersion 그대로). 리플레이 {"kind": "trigger"}.
- 에디터 `editor/ui/TriggerForm`: 표(TriggerSchema) 기반 노드 양식 (종류별 위젯 · 선택 키 "사용" · 노드 더하기/지우기 · id 바꾸면
  연결도), 연결 표 (from → to · else), 이름 · 틱마다. 순수 함수 triggerTemplate · defaultTriggerNode · freeNodeId ·
  removeTriggerNode · describeTriggerNode · describeProgram.
- "콘텐츠" 창: 종류 "트리거", 폼 · JSON 탭 (같은 글), 요약에 실행 표현 줄. 검사 · 적용 · 되돌리기는 기존 경로.
- 시험: test_trigger_form (editor 4), test_trigger_run +1 (오버레이 순서 · 게임 중 EditContent · 세이브 왕복), test_messages ·
  test_roles_overlay (프로토콜 11 · trigger 왕복 · kind 4 거절).
- 문서: ADR-0052, 08 (프로토콜 11), 09 3장, 10 2 · 8장, 11 10장, 13, 17, 18 (M4 ✅ — 다음 M5-T1), adr/README.

**왜**

- 트리거를 에디터에서 만들고 고치게 (v0.2 "비개발자가 템플릿으로"). 오버레이를 그대로 쓰면 서버 검사 · 역할 · 되돌리기 · 세이브 ·
  복제 · 해시가 따라온다. 폼은 표에서 위젯을 만들어 노드 종류가 늘어도 코드가 늘지 않는다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외). 골든 그대로 (오버레이 없는 월드). 모든 노드 종류의 기본값이
  로더 검사를 통과 (시험). 세이브 왕복 후 같은 해시.
- 와이어: 프로토콜 10 → 11 (열거 값). 세이브 형식: 선택 폴더 하나 (worldVersion 1). kSimVersion 그대로.

**남은 일**

- M5-T1 Test Play. 노드 그래프 캔버스, 행동 · 규칙 폼, 오버레이를 팩으로 내보내기 — [계획].

---

## 2026-10-20 — v0.2 M4-T2: Trigger 실행 — 단계 17c, 틱 처음 스냅숏, 같은 틱 이어짐, 예산 (ADR-0051)

**무엇을**

- 단계 17c Triggers (`core/simulation/SimulationTriggers.cpp` — SimulationWorld 의 일부): 세션이 Running 일 때만. 단계 0 (명령 전)
  스냅숏 = 점수 칸 + (area_enter · death 트리거가 있으면) 개체 saveId · 위치 · 태그. 처음 목록 (game_start · tick · timer ·
  score 가장자리 · area_enter 밖→안 · 이번 틱 Died) 을 FIFO 로, 효과가 낸 이벤트 (destroy → death · spawn → area_enter ·
  score.add → score) 는 같은 틱에 목록 끝으로. 예산 perTick · 틱 1024 실행 · 65536 줄 (lastTriggerStats). game.end 로 끝.
  조건 count · score · time · field · tags · chance (Purpose::Trigger).
- 효과 실행기 `applyEffectNow` — PlayerAction 의 효과 코드를 옮겨 Trigger 와 공용 (같은 동작 · 같은 난수 키).
  game::scoreSlot · game::finishGame (GameRules).
- eco 팩 트리거 `eco.rabbit_lost` (토끼가 죽으면 점수 −1) — 데모 "토끼 지키기" 의 점수 = 덫 − 잃은 토끼.
- 시험: test_trigger_run (core 8). 문서: ADR-0051, 03 2장 (17b · 17c 표 · 글 · 그림), 04 · SimVersion 이력, 11 (0 · 10장),
  13, 17, 18 (M4-T2 ✅ — 다음 M4-T3), adr/README.

**왜**

- M4-T1 의 실행 표현을 서버 권위 · 결정적으로 돌린다. 앞 상태를 저장된 상태에서 다시 만드는 스냅숏으로 잡아 세이브/로드한
  월드도 같다. 같은 틱 이어짐이라 "덫 → 죽음 → 점수 → 보너스" 가 읽히는 대로 한 틱에 끝나고, 예산이 고리를 끊는다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외). 골든 · 리플레이 해시 그대로 (세션 없는 월드), PlayerAction
  시험 그대로 (공용 실행기). 세이브 왕복 후 같은 해시 (chance · 스냅숏). `sbx_sim_check --validate-content eco` 오류 · 경고 0.
- 와이어 · 세이브 형식 · kSimVersion 그대로. eco contentHash 는 바뀐다 (triggers/ 추가).

**남은 일**

- M4-T3 트리거 에디터 폼 · 오버레이로 고치기. 영역 나감 · 머묾, 세션 없는 월드의 트리거, 스냅숏 비용 측정 — [계획].

---

## 2026-10-20 — v0.2 M4-T1: Visual Trigger 스키마 — triggers/*.json · 노드 종류 표 · 실행 표현 · V10 (ADR-0050)

**무엇을**

- `core/trigger/TriggerSchema`: 노드 종류 표 21 종 (event 6 · cond 6 · effect 9) — 키 · 타입 · 범위 · 필수, 대상(subject) 규칙,
  권한 (모두 ServerGameplay). 로더와 에디터 폼(M4-T3)이 같은 표를 읽는다.
- `core/trigger/TriggerCompiler::compileGraph`: 원본 그래프의 모양 검사 (이벤트 하나 · 이벤트로 들어옴 없음 · 들어오는 연결 하나 ·
  순환 · 닿음 · else 는 조건에서만 · 노드 64 · 깊이 16 · 대상) 와 나무 → 명령열 (Cond · Effect · Jump) 펼치기.
- 콘텐츠: 팩의 `triggers/*.json` → `TriggerDef` (event · conditions · effects · program · perTick · raw 원본).
  ContentLoader::loadTrigger — 값은 표로 (V3 · V7 · V2 · 초는 V8), 모양은 V10. crossCheck: id 중복 V1, 256 개 V10, 팀 id V2 경고,
  재진입 고리 (score.add → event.score · destroy → event.death · spawn → event.area_enter) V10 경고. ContentDatabase::triggers ·
  findTrigger. 효과는 RuleEffect 를 그대로 (실행기 재사용 — M4-T2), game.end 만 새것.
- 시험: test_trigger (content 5).
- 문서: ADR-0050, 11 (1 · 8 · 10장), 03 6.4, 13, 17, 18 (M4-T1 ✅ — 다음 M4-T2), adr/README.

**왜**

- 월드 수준 규칙 (타이머 · 영역 · 점수 · 죽음 → 조건 → 효과) 을 콘텐츠로. Rule 은 개체 둘의 interact 에 묶여 있어 못 쓴다.
- 나무 + 펼친 명령열: 실행 순서가 분명하고 예산 · 깊이 검사가 쉽다. 원본(편집 배치)과 실행 표현을 나눠 에디터가 배치를 바꿔도
  실행은 같다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외, `chcp 65001` 뒤). 골든 · 리플레이 해시 그대로 — 시뮬레이션은
  아직 트리거를 읽지 않는다. 와이어 · 세이브 · kSimVersion 그대로. eco 팩에는 트리거를 넣지 않았다 (contentHash 그대로).

**남은 일**

- M4-T2 실행: Stage 위치, 이벤트 수집 (영역 진입 = 이전 틱 위치 비교, 점수 가장자리), cond.chance 난수 키, perTick 예산.
- M4-T3 에디터 폼 · 월드 오버레이로 고치기. 합류 · 변수 · 반복 · 대기 노드 — [계획].

---

## 2026-10-20 — v0.2 M3-T3: 게임 키 · 카메라 처음 자리 · 데모 모드 "토끼 지키기" → M3 완료 (ADR-0049)

**무엇을**

- 게임 키: 콘텐츠 팩의 `presentation/input.json` (sandbox.input 형식) — 로더는 원문만 `PackInfo.inputBindings` 로 (contentHash ·
  검사 밖). 클라이언트 `apps/client/PlayKeys` 가 팩 순서로 합치고 play.* 만 남기며 (다른 이름 경고) `--input` 의 play.* 가 마지막에
  이긴다, 엔진 키와 겹치면 경고. `play.action.<id>` 를 누르면 커서 자리에 PlayerAction (Application::handlePlayKeys — 엔진 키와
  따로 둔 ActionMap · ActionState). "행동" 패널이 키를 보인다. `ActionMap::withPrefix` 추가.
- 카메라 처음 자리: 컴포넌트 `game.camera_start {height}` (월드에 놓는다). NetworkSession::cameraStart() (netId 가 가장 작은 것 ·
  generation), Application 이 월드마다 한 번 · Home 으로 맞춘다 (없으면 월드 전체).
- 데모: `content/ecosystem` 에 modes/protect.json (eco.protect "토끼 지키기"), actions.json (eco.feed · eco.trap),
  presentation/input.json (Q · E). `createProject` 의 gameMode · `sbx new --mode` (없는 모드는 NotFound + 있는 모드 목록).
- 시험: test_play_keys (client 4), test_game_score +1 (저장소 eco 의 데모 진행), test_game_project +1, test_content_loader +1,
  test_action_map (withPrefix), 카탈로그 22, CTest project_demo_protect.
- 문서: ADR-0049, 02 · 07 · 10 · 11 · 13 · 15 · 17 · 18 (M3 ✅ — 다음 M4-T1), adr/README.

**왜**

- 게임이 자기 키 · 처음 화면을 주고, `sbx new` 한 줄로 바로 해 볼 수 있는 미니게임이 있어야 v0.2 완료 기준("템플릿만으로
  미니게임")에 닿는다. 키는 표현이라 팩의 클라 전용 폴더에 두어 원격 클라이언트도 받고 해시는 그대로다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외 — M3-T2 항목 참고, `chcp 65001` 뒤). 골든 · 리플레이 해시 그대로
  (det_*), 프로토콜 10 · 세이브 형식 · kSimVersion 그대로.
- 런타임: `sbx new <d> --from ecosystem_small --mode eco.protect` → `SandboxClient --headless --world <d> --frames 90` (InWorld,
  Lobby tick 1 · 개체 301, 게임 키 경고 없음) · CTest 의 `SandboxServer --start-game --ticks 150`.
- eco 팩 contentHash 가 바뀌었다 (actions.json · modes/ 추가) — 예전 eco 세이브 · 리플레이는 P5 경고로 열린다.

**남은 일**

- M4-T1 Visual Trigger 스키마.
- 행동 말고 다른 play.* 키, 카메라 따라가기 · 이동 범위, 카메라 처음 자리 표시 아이콘 — [계획].
- (이전 항목의 알림 그대로) `--ticks N --exit` 이 게임이 일찍 끝나면 멈추는 문제는 따로 고친다.

---

## 2026-10-20 — v0.2 M3-T2: 점수 · 팀 · 스폰 지점 — game.score · 효과 score.add · 끝 조건 score · 팀 태그 · game.spawn_point (ADR-0048)

**무엇을**

- 컴포넌트 `game.score` (core/components/game/GameScore.hpp): points[9] — 0 공용, 1..8 GameMode.teams 순서. 세션 엔티티에
  붙고 엔진 관리 (명령 PermissionDenied · Prefab V3). resetSession · GameControl Start 가 0 으로 (없으면 붙인다).
- 효과 op `score.add {value 정수 ±1,000,000, team?, who 기본 target}` — Rule (Stage 11) · PlayerAction 이 `game::addScore` 를 쓴다.
  칸 = team → who 의 팀 태그 → 공용. 세션이 Running 일 때만, ±10억 으로 자른다. 어느 모드에도 없는 team 은 V2 경고.
- GameMode: `teams[].tag` (팀 소속 = 태그, V7 · 중복 V8), 끝 조건 `when.score {team?, op, value}` (time · count 와 AND).
- 컴포넌트 `game.spawn_point {prefab, count 1~64, radius 0~16}` (사용자 편집): GameControl Start 를 적용하면 saveId 순으로 지점마다
  prefab 을 ±radius 안에 만든다 (난수 Purpose::GameStart, 없는 prefab 건너뜀, 4096 개까지, 결과 created).
- 클라이언트 "게임" 패널 점수 줄 (GameInfo.scores · scoreRows · scoreLine).
- 시험: test_game_score (core 5), test_content_loader +3, test_game_panel +1 (+ 로컬 서버 복제 점수), 카탈로그 21.
- 문서: ADR-0048, 11 (0 · 8 · 9장), 03 (2 · 4 · 6.3장), 02, 04 (simVersion 이력), 10 9장, 13, 17, 18 (M3-T2 ✅ — 다음 M3-T3),
  adr/README, SimVersion.hpp 이력 주석.

**왜**

- "10 점 먼저" · 팀 대결 같은 미니게임을 콘텐츠만으로 (M3 Feature Bundle). 점수가 세션과 수명이 같아 재시작 = 0 점이고,
  세이브 · 리플레이 · 복제 · 인스펙터가 그대로 다룬다. 팀을 태그로 정하면 Prefab · 태그 식 · V7 을 그대로 쓴다.
- 스폰 지점: 시작 월드를 고치지 않고 판마다 같은 배치 (지점 saveId 로 결정적).

**검증**

- Windows MSVC Debug (windows-msvc 프리셋): ctest 58/58 통과 (render_tests_warp 제외). 골든 · 리플레이 해시 그대로
  (det_* 통과 — kSimVersion 4 그대로). 와이어 (프로토콜 10) · 세이브 형식 변경 없음.
- 런타임: `sbx new --from ecosystem_small` 프로젝트에 팀 태그 · score 끝 조건 모드와 늑대 Rule 의 score.add 를 넣고
  `SandboxServer --world <프로젝트> --start-game --ticks 600 --exit` 두 번 — 같은 해시 0x12019dabc96ea151.
- 알림 (이번 변경 전부터, 고치지 않음):
  - Windows 에서 CMake 가 도구의 UTF-8 출력을 콘솔 코드 페이지(cp949)로 읽어 project_new_* · project_game_start ·
    net_server_probe_* 가 실패한다 — `chcp 65001` 뒤 ctest 면 통과.
  - render_tests_warp: doctest 28/28 통과지만 D3D12 Debug Layer 배리어 메시지 (imgui target · world pass target 의
    COMMON → COPY_SOURCE) 로 "render tests OK" 를 내지 않는다.
  - 수동 재시작 모드에서 게임이 N 틱 전에 Finished 가 되면 `--ticks N --exit` 가 끝나지 않는다 (Finished 는 틱을 멈춘다).

**남은 일**

- M3-T3 카메라 · 입력 바인딩 기본값, 데모 모드 (생태계 "토끼 지키기").
- 다시 나오기 (respawn) · 주기적 스폰, 접속자별 점수, 가장 높은 팀 승리 (M4 트리거), 스폰 지점 표시 아이콘 — [계획].

---

## 2026-10-20 — v0.2 M3-T1: 플레이어 행동 — 콘텐츠 PlayerAction (actions.json) · 명령 PlayerAction · "행동" 패널 (ADR-0047)

**무엇을**

- 콘텐츠: 팩의 `actions.json` → `content::PlayerActionDef` (id · name · targets · range 0~32 · maxTargets 1~64 · conditions ·
  effects). 검사 V9 (who · 조건 경로는 target 뿐, 범위, 팩 전체 64 개) + V3 · V7 · V1. Rule 의 조건 · 효과 파서를
  `parseRuleConditions` · `parseRuleEffects` 로 떼어 Rule 과 함께 쓴다 (Rule 동작 그대로 — 기존 content 시험 통과).
- 명령 `PlayerAction{action, position}` (와이어 태그 30, 리플레이 JSON "action"): SimulationWorld 가 명령 단계에서 대상을
  고르고 (core.transform · 반경 · 태그 · 조건 · (거리², saveId) 순 maxTargets) 효과를 바로 적용 (field · destroy · spawn
  (난수 Purpose::PlayerAction) · tag · event). 대상이 없으면 거절 (원자적). 게임 세션이 있으면 Running 에서만.
  CommandClass::PlayerAction = 역할 player 부터. 프로토콜 10.
- 클라이언트 "행동" 패널 (apps/client/ui/ActionPanel): 행동을 쥐면 월드 왼쪽 클릭 = PlayerAction (에디터 · 선택은 클릭을 받지
  않는다), Esc = 놓기, 결과 · 거절 줄. NetworkSession::playerActions() · playerAction().
- 시험: test_player_action (core 5), test_content_loader +2, test_action_panel (client 2), test_messages (프로토콜 10 ·
  태그 30), test_multi_client 역할 표 +PlayerAction (player 부터).
- 문서: ADR-0047, 11 (머리말 · 0 · 6 · 8장 V9), 03 (0 · 4장), 08 (머리말 · 5 · 5.1장), 10 7 · 9장, 13, 17, 18 (M3-T1 ✅ —
  다음 M3-T2), adr/README.

**왜**

- D-11 (ADR-0045) 결정의 구현. 역할 player 가 처음으로 쓸 수 있는 명령이고, 콘텐츠만으로 플레이어 상호작용을 만든다.
- 대상을 시뮬레이션이 위치로 고른다: 복제 지연 · 관심 영역 밖 · 치트 검사가 필요 없고 리플레이에서 같다.

**검증**

- Linux clang 19 Debug -Werror: ctest 63/63. 골든 · 리플레이 해시 그대로 (명령이 없으면 아무것도 하지 않는다).
- 와이어: 프로토콜 9 → 10 (명령 태그 하나). 세이브 형식 · kSimVersion 그대로.

**남은 일**

- 행동 쿨다운 · 비용 (플레이어별 상태), 대상 직접 지정 · 인자, 행동 단축키, 월드 오버레이로 행동 고치기 — [계획].
- M3-T2 점수 · 팀 (game.score, 효과 op score.add, 끝 조건 score), 스폰 지점.

---

## 2026-10-20 — v0.2 M2-T3: 게임 시작 · 중단 · 재시작, "게임" 패널, 프로토콜 9 → M2 완료 (ADR-0046)

**무엇을**

- 명령 `GameControl{start | abort}` (core/command, 와이어 태그 24, 리플레이 JSON {"op": "game", "action": …}):
  start = Lobby → Starting, abort = Starting · Running → Finished (결과 없음). 권한 = 실행 제어 (admin 부터).
  서버는 역할 player 이상 접속자가 GameMode.players.min 에 모자라면 start 를 거절한다.
- `game::resetSession` — 세션을 Lobby 로 (없으면 만든다). openProject 가 gameMode 가 있으면 부른다 (시작 월드는 언제나 Lobby).
- WorldOp `Restart` (op 7): 서버가 시작 월드를 다시 열고 (openSave — contentHash 가 달라도 경고만) 세션을 Lobby 로 →
  WorldChanged{Restart}. GameMode.restart auto 면 Finished 뒤 delay 만큼 서버 단계가 지나면 스스로.
  `net::setGameProject` 로 서버 · 로컬 서버가 시작 월드 · 모드 · players.min 을 받는다. 프로토콜 9.
- SandboxServer `--start-game` (헤드리스 · CI 에서 Lobby 를 넘는다).
- 클라이언트 "게임" 패널 (apps/client/ui/GamePanel): 모드 · 상태 한 줄 (카운트다운 · 경과 · 결과와 팀) · 시작 · 중단 · 재시작 ·
  거절 줄. NetworkSession::game() 은 복제된 game.session 을 읽는다 (클라이언트에는 saveId 가 없어 세션 찾기에 대체 규칙).
- 시험: test_game_control (network 3), test_game_panel (client 2), test_game_rules +2, test_game_project (openProject = Lobby),
  test_messages (프로토콜 9 · 태그 24), test_multi_client 역할 표 +2 명령 · Restart, test_server_options, CTest project_game_start.
- 문서: ADR-0046, 08 (머리말 · 4 · 5 · 5.1장), 10 7 · 9장, 15 7장, 03 (2 · 4장), 13, 17, 18 (M2 ✅ — 다음 M3-T1), adr/README.

**왜**

- 시작 · 중단은 시뮬레이션 상태 전이라 명령이어야 리플레이 · 결과 · 역할 검사를 그대로 탄다. 재시작은 한 판의 모든 변경을
  되돌려야 해서 시작 월드를 다시 여는 것(12D 불러오기와 같은 경로)이 가장 확실하다.

**검증**

- Linux clang 19 Debug -Werror: ctest 63/63 (+ project_game_start). 골든 · 리플레이 해시 그대로 (세션 없는 월드).
- 와이어: 프로토콜 8 → 9 (명령 태그 · 열거 값만 늘었다). 세이브 형식 변경 없음.

**남은 일**

- players.max (접속 · 역할 제한), 준비(ready) · 팀 고르기, 여러 맵 순환 — [계획].
- M3-T1 PlayerAction (콘텐츠 actions.json, 역할 player).
- (알림) ADR-0045 에 M2-T2 에서 "구체화" 절을 더했다 — AGENTS.md 6장은 기존 ADR 을 고치지 않는다고 하므로, 이후 결정은
  ADR-0046 처럼 새 ADR 로 쓴다.

---

## 2026-10-20 — v0.2 M2-T2: game.session · 단계 17b GameRules (ADR-0045 구체화)

**무엇을**

- 컴포넌트 `game.session` (core/components/game/GameSession.hpp): mode · state(Lobby · Starting · Running · Finished) ·
  stateTick · result · team · rule. Persistent · Replicated · EditorVisible. 엔진 관리 — 사용자 명령(Create · Add ·
  Change · Remove)은 PermissionDenied, Prefab 의 components 는 V3.
- `core/game/GameRules`: 세션 찾기 (saveId 가 가장 작은 것), `gameHoldsSimulation` (Lobby · Finished), 끝 조건 평가
  (time · count(core.tags 태그 식) · AND), `stepGameRules` (Starting 경과 ≥ 카운트다운 → Running, Running 에서 end 를
  위에서부터 → Finished).
- SimulationWorld::tick: 세션이 Lobby · Finished 면 편집 단계 (clock.paused 와 별개, Step 을 쓰지 않는다), 17 (ECB ·
  Prefab 생성) 다음에 단계 17b GameRules.
- 시험: tests/unit/game/test_game_rules.cpp 8 케이스 (unit_core), unit_ecs 카탈로그 19, unit_content Prefab 의 game.session.
- 문서: ADR-0045 구체화 절, 03 (0 · 2장 17b · 그림), 04 6장 이력 (4), 02 (game.session), 11 9장, 17, 18 (T2 ✅ — 다음 T3),
  13, SimVersion.hpp 이력.

**왜**

- 게임의 진행 상태가 시뮬레이션 안에 있어야 세이브 · 리플레이 · WorldHash · 복제가 게임 진행을 안다 (ADR-0045 2).
- System(Stage 5~16)이 아니라 17 다음 단계로: ECB 적용 뒤라 그 틱의 파괴 · 생성이 끝 조건 수에 바로 보이고, 기존 System
  순서가 그대로다.

**검증**

- Linux clang 19 Debug -Werror: ctest 62/62. 골든 det_random_walk · det_world_save_load · det_ecosystem_small ·
  det_eco_lifecycle 과 리플레이 det_replay_* 그대로 — 세션이 없는 월드의 결과가 같다 (kSimVersion 4 그대로).
- 와이어 형식 변경 없음 (복제 표는 Welcome 이 stableId 로 싣는다 — 새 Replicated 컴포넌트는 같은 빌드끼리 자동).
  세이브: 새 컴포넌트만 더해졌다 (옛 세이브는 그대로 읽힌다).

**남은 일**

- M2-T3 시작 · 재시작 명령 (세션 만들기 · Lobby → Starting · Finished → 재시작), 역할 권한, 클라이언트 "게임" 패널,
  restart.auto 의 지연.
- M3-T1 PlayerAction.

---

## 2026-10-20 — v0.2 M2-T1: GameMode 콘텐츠 · 매니페스트 schemaVersion 2 gameMode — D-11 결정 (ADR-0045)

**무엇을**

- 콘텐츠 팩의 `modes/<이름>.json` → `content::GameMode` (id · name · description · players{min, max} · teams ≤ 8 ·
  start{countdownSeconds} · end[{when{time, count}, result, team?}] ≤ 32 · restart{manual|auto, delaySeconds}). 초는 로드할
  때 틱으로 (×30). `ContentDatabase::modes()` · `findMode(id)`, contentHash 에 든다. 검사 규칙 V8 (모르는 키 V3, 태그 V7,
  중복 id V1) — 문제가 있으면 그 모드는 넣지 않는다.
- 매니페스트 schemaVersion 2 (1 도 읽는다): 선택 키 `gameMode` (콘텐츠 id). v1 에서 쓰면 P1, 프로젝트 콘텐츠에 없으면 P5.
  `sbx check` 가 "게임 모드" 줄을 찍는다.
- ADR-0045: GameMode = 콘텐츠, 진행 상태 = 시뮬레이션 안의 game.session (M2-T2), 플레이어 행동(D-11) = 콘텐츠 PlayerAction
  부터 (M3-T1), 유닛 지시 · 직접 조작은 뒤로.
- 시험: unit_content 3 케이스 (로드 · 해시 · V8 오류 모음), unit_project 2 케이스 (v2 gameMode, P5).
- 문서: ADR-0045, 11 (1 · 8 · 9장 GameMode), 18 (2 · 5.2 · 6.1 D-11 · 7 · 8장, 다음 M2-T2), 13, adr/README.

**왜**

- 엔진에 "게임"(시작 · 끝 · 승패)이 없다. 승패 규칙은 게임 규칙이라 팩에 둔다 — V-규칙 검사 · contentHash 일치 · 오버레이 ·
  공유를 그대로 얻는다. 데이터를 먼저 고정하고 상태기계(T2) · 명령(T3)을 그 위에 올린다.

**검증**

- Linux clang 19 Debug -Werror: ctest 62/62. unit_content · unit_project 통과.
- 와이어 · 세이브 · 시뮬레이션 형식 변경 없음 (modes/ 가 없는 팩의 contentHash 는 그대로 — 골든 해시 그대로).
- 기존 v1 매니페스트 (tests/data/projects/minimal 등)는 그대로 읽힌다. 새로 쓰는 매니페스트는 schemaVersion 2.

**남은 일**

- M2-T2 game.session 컴포넌트 · GameSystem (Starting → Running → Finished, 끝 조건). game.session 이 없는 월드는 결과가
  같아야 한다.
- M2-T3 시작 · 재시작 명령 · 역할 · UI. GameMode 를 월드 오버레이로 고치기는 M4.

---

## 2026-10-20 — v0.2 M1-T5: 프로젝트의 세이브 폴더 = worlds/, Ctrl+S = 시작 월드 → M1 완료 (ADR-0044)

**무엇을**

- `project::projectSavesRoot(폴더)` = <프로젝트>/worlds, `project::entryWorldSaveName(매니페스트)` = entryWorld 가
  "worlds/<이름>" 이면 그 이름.
- SandboxServer: --world 가 프로젝트이고 --saves 가 없으면 세이브 · 리플레이 폴더 = <프로젝트>/worlds (로그 "세이브 폴더: …").
- SandboxClient: `worldSaveDefaults(ClientOptions)` — --saves 가 이기고, 프로젝트면 <프로젝트>/worlds 와 시작 월드 이름
  (세이브 이름 규칙에 맞을 때), 아니면 saves · quicksave. `AppConfig.saveName` → "월드" 창 · Ctrl+S 의 처음 이름.
- 시험: unit_client 2 케이스 (기본값 표, 프로젝트 싱글플레이에서 30 프레임 → Ctrl+S → worlds/main 이 바뀌고 프로젝트 검사
  오류 · 경고 0 · 시작 틱이 늘었다), unit_project (이름 규칙 표), CTest server_project_saves_root.
- 문서: ADR-0044, 18 (M1 ✅ — 다음 M2-T1), 10 9장, 15 7장 (서버 · 클라이언트 --saves), 13, adr/README.

**왜**

- 에디터에서 고친 맵이 작업 폴더의 saves/quicksave 로 가서 게임에 들어가지 않았다. 월드 = 세이브 폴더(ADR-0040)이므로 저장
  위치와 처음 이름만 맞추면 "고치고 저장하면 게임이 바뀐다". 새 메시지 · 명령은 없다.

**검증**

- Linux clang 19 Debug -Werror: ctest 62/62 (+1). unit_client · unit_project 통과.
- 와이어 · 세이브 · 시뮬레이션 형식 변경 없음.

**남은 일**

- M2 GameMode · 라이프사이클 (서버 권위 상태기계) — 먼저 D-11 (플레이어 행동 모델)을 정한다.
- M5-T1 Test Play: 실행 상태가 Ctrl+S 로 시작 월드가 되는 것 — 편집 원본과 분리.

---

## 2026-10-20 — v0.2 M1-T4: 새 프로젝트 만들기 — createProject · 도구 sbx (ADR-0043)

**무엇을**

- `project::createProject(폴더, {gameId, name?, fromScenario?, contentRoot, seed}, 엔진, 카탈로그)`:
  - 템플릿 = 빈 월드(내장 콘텐츠 · 16 × 16 청크 · 일시정지) 또는 시나리오(requiredPacks + requires 를 content/ 로 복사, 한 틱
    돌린 월드 = worlds/main). 매니페스트 version 0.1.0, entryWorld worlds/main, 엔진 범위 `defaultEngineRange`
    (1.0 전 ">=<엔진> <1.0.0", 이후 "^<major>").
  - 폴더는 없거나 비어 있어야 한다 (AlreadyExists). "<폴더>.sbx-new" 에 만들고 validateProject 오류 · 경고 0 이면 rename,
    실패하면 임시 폴더를 지운다. 끝의 '/' 는 정규화.
- 도구 `sbx` (tools/sbx, Core 만 — 경계 규칙 등록): `new <폴더> --id … [--name] [--from] [--content-root] [--seed]`,
  `check <폴더>`, `templates`, `--version`. 종료 코드 0 · 1 검사 오류 · 2 인자/실패.
- T1 의 `sbx_sim_check --validate-project` 를 지우고 `sbx check` 로 옮겼다 (sim_check 소스는 T1 전과 같아졌다).
  CTest project_minimal_validate · project_bad_manifest_* 는 이름 그대로 명령만 sbx check.
- 시험: unit_tools test_sbx_options (명령 · 인자 오류), unit_project 3 케이스 (빈 월드 · 시나리오 템플릿 — 서버 경로로 연다 ·
  덮어쓰기 거절 · 실패 시 아무것도 안 남김 · 알 수 없는 시나리오 · 팩 없는 루트), CTest project_new_empty ·
  project_new_from_scenario (tests/project/project_new.cmake — new → check → SandboxServer 30 틱 → 다시 new 거절).
- 문서: ADR-0043, 18 (T4 ✅, T5 = 에디터 저장 위치, 5.2 검사 명령, 7 · 8장), 15 (sbx · 타깃 · 옵션 표), 17, 13, adr/README.

**왜**

- v0.2 "프로젝트 생성 · 빌드 · 실행"의 생성: 손으로 매니페스트를 쓰고 세이브를 복사하지 않게. 시나리오가 이미 팩 · 경계 ·
  초기 배치를 가지므로 템플릿 형식을 따로 만들지 않았다. 만든 직후 같은 검사로 확인 — 서버가 열지 않는 프로젝트를 만들지 않는다.
- 결정론 하네스(sbx_sim_check)와 사용자용 프로젝트 도구를 나눴다 (v0.4 패키지 관리자가 sbx 에 붙는다).

**검증**

- Linux clang 19 Debug -Werror: ctest 61/61 (+2). unit_project · unit_tools 통과.
- 실행: `sbx new demo/Arena --id studio.arena --name 아레나 --from ecosystem_small` → 팩 eco, worlds/main tick 1 · 엔티티 300;
  `sbx check demo/Arena` → 오류 0 · 경고 0; 같은 폴더에 다시 new → AlreadyExists, 종료 코드 2.
- 세이브 · 와이어 · 시뮬레이션 형식 변경 없음.

**남은 일**

- M1-T5: 프로젝트 서버의 세이브 폴더 = <프로젝트>/worlds, Ctrl+S 기본 이름 = 시작 월드 (편집한 맵이 게임이 된다).

---

## 2026-10-19 — v0.2 M1-T3: 핸드셰이크가 게임 정체성을 싣는다 — 프로토콜 8 (ADR-0042)

**무엇을**

- `net::GameIdentity{gameId, name, version}` 을 Welcome 끝 · Reject 끝에 (kProtocolVersion 7 → 8, 상한 64 · 256 · 128 바이트,
  gameId 없이 name · version 만 있으면 형식 오류). `gameIdentityOf(매니페스트)`. ServerHostDesc.game — 서버가 상한으로 자르고
  모든 Welcome · Reject 에 싣는다. SandboxServer · LocalServerHost 가 WorldSource.game 에서 채운다.
- 클라이언트: WorldInfo.game → "월드" 패널 "게임 <이름> — <gameId> <버전>", --headless 끝 "game …" 줄, sbx_net_probe "게임 …" 줄.
  ContentMismatch 로 팩을 못 읽거나 해시가 다르면 실패 문구에 "--content <프로젝트 폴더>/content" 안내.
- 매니페스트 content.packs ≤ 16 (P2) — Reject 가 실을 수 있는 팩 수와 같게 (static_assert).
- 시험: test_messages (왕복 · 형식 오류 · 상한 · 프로토콜 8 고정), test_server_host (Welcome · Reject 의 게임, 이름 자르기),
  test_network_session (로컬 프로젝트 표시 · 시나리오는 빈 값 · 원격 팩 실패 안내), test_game_project (팩 17 개 P2),
  test_world_ops 의 "== 7" 고정을 ">= 7" 로 (지금 판 고정은 test_messages). CTest net_server_probe_project (실제 UDP —
  net_smoke.cmake 에 WORLD · WORLD_NAME · GAME, 게임이 아닌 서버는 게임 줄이 없어야 한다).
- 문서: ADR-0042, 08 (머리말 · 4장 게임 · 5장 표 · 프로토콜 8 이력), 18 (T3 ✅ · 다음 T4 · 5.2 팩 상한 · 8장), 15 7장, 13,
  adr/README.

**왜**

- 접속한 클라이언트가 어떤 게임인지 몰랐다 (월드 이름은 64 바이트로 잘림). 프로젝트 팩은 저장소 content/ 에 없어 원격 접속이
  "팩을 찾을 수 없다"로만 끝났다 — 무엇을 어디서 읽어야 하는지 알려 준다. 자동 탐색 · 다운로드는 무결성 문제로 뒤로 (v0.5 · v0.7).

**검증**

- Linux clang 19 Debug -Werror: ctest 59/59 (+1). unit_network · unit_client · unit_project 통과.
- 실행: `SandboxClient --headless --world tests/data/projects/minimal` → "game Minimal — sandbox.minimal 0.1.0".
  probe (실제 UDP) → 게임 sandbox.minimal 0.1.0 "Minimal".
- 프로토콜 7 클라이언트는 VersionMismatch 로 거절된다 (정확 일치 규칙 그대로). 세이브 · 시뮬레이션 형식 변경 없음.

**남은 일**

- M1-T4: 새 프로젝트 만들기 (템플릿 → 매니페스트 + 빈 월드 + 팩 뼈대), 에디터 저장을 프로젝트 worlds/ 로.

---

## 2026-10-19 — v0.2 M1-T2: 서버 · 클라이언트가 게임 프로젝트를 연다 (ADR-0041)

**무엇을**

- `project::openProject` — validateProject 와 같은 검사(공용 함수 checkProject 로 나눔)를 하고, 오류가 없으면 콘텐츠와 시작
  월드를 넘긴다 (검사 때 연 월드를 그대로 — 두 번 열지 않는다). 오류면 ValidationFailed (처음 5 개 문제). `isProjectFolder`.
- `scenario::openWorldSource`: 시나리오 → 게임 프로젝트(sandbox.project.json) → 세이브 폴더 순서. 프로젝트는 이 엔진 버전
  (BuildInfo)으로 검사, 팩 = <프로젝트>/content (--content-root 를 쓰지 않음), 월드 이름 = 매니페스트 name,
  `WorldSource.game` = 매니페스트. 그래서 `SandboxServer --world <프로젝트>` 와 `SandboxClient --world <프로젝트>`
  (LocalServerHost) 가 같은 길로 연다. 서버는 게임 정보를 로그에, 도움말 · NotFound 문구에 프로젝트.
- 시험: unit_project 5 케이스 더 (열기 · 직접 로드와 같은 해시 · 엔진 범위 · gameId 오류 문구 · 경고 · WorldSource 세 갈래),
  CTest server_project_exit · server_project_invalid · client_local_project_headless. tests/data/projects/minimal 의 월드는
  일시정지를 풀었다 (서버가 60 틱까지 돈다).
- 문서: ADR-0041, 18 (0 · 2 · 3 · 5.1 — T2 ✅, T3 = 핸드셰이크 게임 정체성, T4 = 새 프로젝트 · 저장 · 7 · 8장), 15 7장,
  08 (WorldSource), 17, 13, adr/README.

**왜**

- M1-T1 의 프로젝트를 실제로 돌릴 길. 서버와 싱글플레이가 한 함수로 월드를 여는 구조(ADR-0026)라 거기에 더했다.
- 검사 오류가 있으면 시작하지 않는다 — 접속자가 깨진 콘텐츠 · 월드를 받지 않게. 경고(다른 콘텐츠로 저장된 월드 등)는 열린다.

**검증**

- Linux clang 19 Debug -Werror: ctest 58/58 (+3). unit_project 20 케이스 · 225 단언.
- 실행: `SandboxServer --world tests/data/projects/minimal --port 0 --ticks 60 --exit` → "게임 프로젝트 sandbox.minimal 0.1.0
  Minimal", "listening udp … world Minimal", "tick 60 hash …". bad_manifest → P2 · P3 · P4 세 줄과 종료 코드 2.
  `SandboxClient --headless --world tests/data/projects/minimal --frames 60` → 스냅숏 받음, 다시 맞춤 0.
- 와이어 · 세이브 · 시뮬레이션 형식 변경 없음 (프로토콜 7 · kSimVersion 그대로, det_* 통과).

**남은 일**

- M1-T3: Welcome · Reject 에 게임 정체성 (프로토콜 8) — 원격 클라이언트는 지금 `--content <프로젝트>/content` 로 접속한다.
- M1-T4: 새 프로젝트 만들기, 에디터 저장을 프로젝트 worlds/ 로.

---

## 2026-10-19 — v0.2 M1-T1: GameProject 매니페스트 · SemVer · 프로젝트 검사 (ADR-0040)

**무엇을**

- `foundation/version/SemVer`: SemVer 2.0.0 엄격 파서 (세 숫자 · 앞자리 0 금지 · 프리릴리스 · 빌드 · u32), 11장 우선순위,
  `VersionRange` (>= > <= < = ^, 부분 버전은 npm 과 같은 뜻, 공백 = AND). `||` · `~` · `*` · 하이픈은 Unsupported (D-05).
- `core/project/GameProject`: `sandbox.project.json` schemaVersion 1 (gameId · name · version · engine · description? ·
  content.packs? · entryWorld) 읽기 · 정규형 쓰기 · 원자적 저장, 경로 검사 (상대 · '/' · '..' · Windows 예약 이름),
  `validateProject` — 매니페스트 → 엔진 범위 → `<프로젝트>/content` 팩 (ContentLoader V1~V7) → 시작 월드 (요약 · 팩 ·
  contentHash · persist::loadWorld 로 실제로 열기). 문제는 ContentIssue (P1~P5) 로 모두 모은다.
- `sbx_sim_check --validate-project <폴더>` (다른 실행 · 검사 옵션과 함께 쓰면 거절).
- 시험: unit_foundation (test_semver), 새 스위트 unit_project (test_game_project 15 케이스 — eco 팩을 복사하고
  ecosystem_small 3 틱을 저장한 프로젝트를 실제로 연다), unit_tools (옵션), CTest project_minimal_validate ·
  project_bad_manifest_rejected · project_bad_manifest_message, 데이터 tests/data/projects/{minimal,bad_manifest}.
- 문서: ADR-0040, 18 (현황 표 · 5.1 T1 ✅ · 5.2 규범 · 8장), 17, 13, 11 1장, 09 3장, adr/README.

**왜**

- v0.2 의 첫 조각: 게임 하나를 담는 단위가 없었다 (서버는 시나리오 이름 · 세이브 폴더 + 저장소 content/ 만 안다).
  새 맵 형식을 만들지 않고 세이브 폴더를 월드로, 기존 팩을 프로젝트 안에 — 저장 · 마이그레이션 · 해시 · 에디터 경로 재사용.
- 검사가 월드를 실제로 여는 이유: "형식은 맞는데 안 열린다"를 서버 시작 전에 잡는다.

**검증**

- Linux clang 19 Debug -Werror: ctest -LE "balance|bench" 55/55 (기준 51 + 4). unit_project 15 케이스 · 167 단언.
- 실행: `sbx_sim_check --validate-project tests/data/projects/minimal` → 오류 0 · 경고 0, 시작 월드 열림 (tick 3, 엔티티 3);
  bad_manifest → P2 · P3 · P4 세 줄, 종료 코드 1.
- 시뮬레이션 · 세이브 · 와이어 형식은 바뀌지 않았다 (kSimVersion · 골든 그대로, det_* 통과).

**남은 일**

- M1-T2: WorldSource 가 프로젝트를 연다 → SandboxServer · SandboxClient `--world <프로젝트>`, 핸드셰이크에 게임 정체성.
- M1-T3: 새 프로젝트 만들기 · 에디터 저장을 프로젝트 worlds/ 로.

---

## 2026-10-19 — 플랫폼 범위 · 버전 줄 (ADR-0039), 18-PLATFORM 통합 계획, 설계 초안 원본 (docs/platform/)

**무엇을**

- 사용자의 플랫폼 설계 초안(2026-10-13, 28 문서)을 `docs/platform/` 에 원본 그대로 (동결 · 비규범).
- ADR-0039: 제품 = Sandbox Platform (Engine + Game Creator + SandboxScript + Package Manager + Hub + Cloud + Marketplace).
  Phase 0~15 = v0.1 Engine Foundation, 이후 작업 단위는 "v0.N Mk-Tn". 스크립팅은 v0.3 TypeScript 부분집합 → 자체 VM (Lua · WASM
  후보에서 뺌), 공개 인터넷은 v0.7 전 보안 전송 ADR, 결제 · 호스팅은 v0.8. 바뀌지 않는 것(서버 권위 · 30 TPS · 결정론 · 경계 ·
  3 백엔드)을 명시.
- 18-PLATFORM (새 계획 문서): 버전 줄 · v0.1 의 열린 항목 · 초안 대비 현황 표(Implemented / Partial / Planned — 소스 · 시험
  근거) · 모듈 계획 · 버전 규칙 · v0.2 마일스톤 M1~M6 과 태스크 · 열린 결정 D-01~D-11 · 문서/ADR 계획 · 기준선.
- 00 (1장 플랫폼 문단, 7장 스크립팅 비목표), 03 6.4 (언어 후보), 16 (머리말 · 2장 버전 줄 · 6장 범위 밖 · R14), docs/README
  (지도 · 읽는 순서), adr/README, README.

**왜**

- 새 방향이 기존 규범과 6 군데 부딪혔다 (00 비목표 · 16 범위 밖 · 03 6.4 언어 후보 · ADR-0011 명시 등록 · R6 평문 ENet ·
  Phase/버전 번호). 한쪽을 조용히 무시하지 않고 ADR 로 처리 방법과 시점을 정했다.
- 초안의 "현행 구현"은 2026-10-13 기준이라 그 뒤의 13A~15 가 빠져 있다 — 현황 표는 2026-10-19 소스 · 시험으로 다시 썼다.

**검증**

- 문서만 바꿨다. 기준선: master f4a72c1 (= GitHub master, git ls-remote) Linux clang 19 Debug -Werror ctest 51/51 통과.
- 표 정렬 (열 폭 유지), 링크 대상 파일 존재 확인.

**남은 일**

- v0.2 M1-T1 (다음 커밋): GameProject 매니페스트 · SemVer · 프로젝트 검사.
- v0.1 열린 항목: 사용자 PC 실기 QA · 50k 판정, CI 결과, macOS.

---

## 2026-10-19 — Phase 15A · 15B: 50k 시나리오, 결과가 같은 최적화 — 태그 종류 색인 · 행동 병렬 · Prefab 템플릿 · 스냅숏 공유

**무엇을**

- 측정: `ecosystem_50k` (448 × 448, 50,000 개체, 1,000 틱 — 10k 와 같은 밀도). sbx_bench sim.ecosystem 이 10k · 50k 를 함께.
- 15A 시뮬레이션:
  - 공간 색인: 위치 SoA, u32 순열 정렬, 태그 집합 → 종류 번호(intern), 종류별 고운 색인 (4 × 4 타일, 행마다 원의 현 안 칸만 —
    순서 S1 아님, 순서와 무관한 질의만). 셀 계산 인라인 (floor 호출 없이).
  - 감지: 그래프 × 태그 종류 일치 표, 일치하는 종류의 색인만 (늑대가 풀을 건너뛴다).
  - 충돌: 후보의 콜라이더 · 위치를 항목 표(엔티티 index → 항목)에서, 후보 반경의 상대 반지름 = 이번 틱 최대 반지름(전에는
    상한 2), 지형 밀어내기 계산을 Worker 와. `Registry::slotCount`.
  - 행동: 평가를 Worker 와 나눈다 — 자기 Behavior · Movement · Path 만 쓰고 남의 Transform 만 읽는다 (04 7장에 Worker 쓰기
    조건 (a)~(e)).
  - 번식: 이웃의 Prefab 비교를 엔티티 index → Prefab 번호 표로. `WorldGrid::moveCostAt` 인라인 (청크 찾기 · floorDiv 없이).
  - Prefab 템플릿: 덮어쓰기 없는 생성은 첫 JSON 생성의 컴포넌트 값을 BinaryCodec 바이트로 기억해 다음부터 읽는다
    (contentRevision 이 바뀌면 버린다).
- 15B 스냅숏: (엔티티, 컴포넌트) 인코딩을 build 마다 한 번 · 클라이언트끼리 공유, 관심 엔티티는 청크별 목록에서, netId 기수
  정렬, 복제 mask 를 풀마다 훑어 index 표로.
- 문서: ADR-0038, 14(상태 · 6장 판정 · 7.12), 04(7장 Worker 쓰기 조건), 03(행동 병렬), 05(4.4 색인), 08(6.3 15B),
  13(시나리오 표), 16(Phase 15 — 15A · 15B ◐, 판정 대기), README.

**왜**

- Phase 15 완료 기준(50k 평균 < 10 ms · p99 < 25 ms)을 잴 시나리오가 없었다. 2코어 측정: 50k Worker 1 평균 46.6 ms — 감지 20 ·
  충돌 9.3 · 행동 6.6 ms. 비용은 이웃 수를 따른다 (14 7.5): 감지 후보의 84 % 가 풀인데 늑대는 풀을 보지 않는다.
- 결과가 비트 단위로 같은 최적화만 했다 — 감지 주기 같은 동작 변경은 밸런스와 함께 따로 (ADR-0038).

**검증**

- 해시: 바꾸기 전 sbx_sim_check 로 ecosystem_small(900) · eco_lifecycle(600) · ecosystem_10k(600) · ecosystem_50k(300) 를 50 틱마다
  기록해 두고 단계마다 비교 — 모두 같음, --threads 0,1,4 같음 (D5). TSan (ecosystem_10k 60 틱 · Worker 3) 경고 0.
  스냅숏: 바이트 · 관심 개체 수가 6 경우 모두 같음. ctest 51/51 (골든 · D2 · D3 · net 수렴 포함).
- 성능 (2코어 클라우드, master + 50k 시나리오만 넣은 빌드와 번갈아 두 번씩 — 14 7.12): 50k Worker 0 63.7 → 38.5 ms,
  Worker 1 46.6 → 29.6 ms (p99 81 → 53), 10k Worker 0 9.4 → 5.5 ms. 50k 첫 틱(배치) 650 → 455 ms. 스냅숏 클라 16 · 월드 전체
  219 → 127 ms, 클라 4 · 월드 전체 64 → 34 ms, 클라 16 · 화면 25 → 20 ms.
- 시도했지만 이득이 없던 것: 색인 SoA 만으로는 감지가 그대로였다 (범위 안 후보의 갱신이 비용) — 종류별 색인에서 줄었다.
  색인 재구성은 SoA · 종류 작업으로 0.2 ms 쯤 늘었다 (질의는 25 % 빨라짐).
- Linux clang (-Werror) · gcc Debug ctest 51/51, MinGW 경고 0 (커밋마다).

**남은 일**

- 사용자 PC 측정: `sbx_bench --only sim.ecosystem --threads <코어 수 − 1>` → 50k 평균 < 10 ms · p99 < 25 ms 면 Phase 15 ✅.
  넘으면 남은 직렬 몫(번식 · 재구성 · 이동 · 충돌 모으기)을 15C 로.
- [계획] 스냅숏 Net 스레드 · 메시지 아레나 (클라 16 < 3 ms), 색인 증분 갱신, 시스템 간 병렬, 감지 원 안 칸 합산.

---

## 2026-10-19 — Phase 14C: Metal 백엔드 — 공유 이벤트 타임라인, 그릴 때 묶는 번호 표, CAMetalLayer 스왑체인 (구현 완료 · macOS 검증 대기)

**무엇을**

- render/metal (Objective-C++ · ARC): `MetalDevice.hpp` (내부 헤더 — ObjC++ 밖에서 include 하면 #error), `MetalDevice.mm`
  (Metal 3 장치 · MTLCommandQueue · MTLSharedEvent 타임라인 · 명령 버퍼 오류 수집 · Private/Shared 버퍼 · Private 2D/2DArray
  텍스처와 뷰 · 업로드 링 · 지연 해제 · 프레임), `MetalPipeline.mm` (MSL newLibraryWithSource · 샘플러 · 레이아웃 서명 ·
  바인드 그룹 리소스 목록 · 정점 서술자 · 블렌드 · 리플렉션 번호 표), `MetalCommandList.mm` (렌더 · 블리트 인코더, 기억한
  상태를 draw 때 묶기, push = buffer 0 setBytes, 시저 자르기, 디버그 그룹), `MetalSwapChain.mm` (CAMetalLayer · nextDrawable ·
  별도 명령 버퍼 presentDrawable · @autoreleasepool).
- RHI: `Backends.hpp` 에 metal::createDevice, 팩토리 Auto = D3D12 → Vulkan → Metal, `backendCompiled(Metal)`.
  CMake: `SBX_ENABLE_METAL` (macOS 기본 ON), OBJCXX · -fobjc-arc · Metal · QuartzCore · Foundation, `SBX_HAS_METAL`.
  sbx_render_tests 는 Metal 빌드에도 (루트 CMakeLists 조건).
- 시험: CTest `render_tests_metal` (`--rhi metal --debug --skip-unsupported` — 새 옵션: 디바이스가 Unsupported 면 77 →
  건너뜀). 팩토리 시험 · `--rhi` 시험이 Metal 이 든 빌드를 다룬다. client: `--rhi metal`, `--version` 의 render 목록,
  client_version 정규식에 Metal. Vulkan 의 Cocoa 표면 메시지는 "--rhi metal" 을 가리킨다.
- build: macOS 배포 목표 13.0 → 13.4 (따로 커밋). Apple libc++ 의 std::format 은 std::to_chars(double) 을 쓰는데 그 시스템
  라이브러리가 13.3/13.4 부터라 13.0 이면 모든 std::format TU 가 "unavailable" 로 컴파일 오류 — 14B 를 macOS 에서 빌드해 보지
  않아 놓쳤다 (리뷰 에이전트가 Apple 포럼 사례로 찾음).
- 문서: ADR-0037, 06(상태 · 2 · 3.4 · 3.5 · 5.3 · 6.1 · 13 · 14장), 13, 15(1 · 3 · 4 · 6 · 9장), 16(Phase 14 = 구현 완료 ·
  macOS 검증 대기 — ◐ 의 뜻을 적었다), 17, MANUAL-QA 14C (14B 의 "render none" 을 고쳤다), README.

**왜**

- Phase 14 의 마지막 단계. 사용자: "완료는 Test 후이고 Mac 이 없으니 완료는 대기 상태로" — 코드 · 빌드 · 시험 · 문서를 끝내고
  상태를 "구현 완료 · macOS 검증 대기"로 둔다 (✅ 는 macOS 결과가 온 뒤).
- 펜스를 MTLSharedEvent 로 하면 프레임 슬롯 · 업로드 링 · 지연 해제가 다른 백엔드와 같은 코드다. Argument Buffer 대신 그릴 때
  setBuffer — 지금 규모에서 단순하고, MSL 쪽(ADR-0035 의 고정 번호)을 바꾸지 않는다.

**검증**

- macOS 가 없어 **빌드 · 실행하지 못했다**. render/metal 의 .mm 넷: GNUstep 헤더 + 검사용 Apple 선언(14B 의 머리 파일에 Metal
  프로토콜 · 서술자 · 열거를 더함, -fblocks)으로 `clang++-19 -fsyntax-only -fobjc-arc -Wall -Wextra -Wpedantic -Wshadow` —
  우리 코드의 오류 · 경고 0. Metal API 의 제약은 Apple 문서로 확인한 것만 검사에 넣었다 (copyBuffer 4 의 배수 · 인덱스 오프셋 =
  인덱스 크기의 배수).
- 독립 리뷰(서브 에이전트가 Apple 문서 JSON 으로 셀렉터 · 열거 · 가용 버전을 대조): 이름 · 시그니처 오류 없음. 고친 것 —
  패스 밖에서 연 디버그 그룹을 패스 안에서 닫으면 짝이 어긋남, 빠진 색 첨부가 번호를 당김 · 파이프라인 형식과 패스 첨부가
  다른 draw, 묶지 않은 정점 버퍼, 2D 복사의 bytesPerImage(0 이어야), 깊이 텍스처 복사, 빈 제출의 명령 버퍼가 nil 이면 펜스만
  오름, 블리트 인코더 안의 쓰기-쓰기 · 읽기-쓰기, 정수 형식 블렌딩, 버퍼 밖 오프셋의 크기 계산, METAL_DEVICE_WRAPPER_TYPE.
- Linux clang 19 (-Werror, MSL 켬) · gcc 13 Debug: ctest 51/51. MinGW 경고 0 (커밋마다).

**남은 일**

- CI 의 macOS 잡 첫 실행 (빌드 · platform_cocoa · render_tests_metal — 실행기의 GPU 가 Metal 3 가 아니면 건너뛴다, dxc 가 없으면
  그리기 케이스 없음) → 로그를 받아 고친다. Mac 이 있으면 MANUAL-QA 14B · 14C. 통과하면 Phase 14 ✅.
- [계획] Metal 타임스탬프 · 깊이 · metallib · Argument Buffer · --rhi-capture, Slang 재평가 스파이크(14.0).

---

## 2026-10-19 — Phase 14B: macOS Cocoa 창 — AppKit (Objective-C++ · ARC), 가상 키 코드 표, NSTextInputClient (미검증)

**무엇을**

- platform/macos: `CocoaWindow.mm` — NSApplication(Regular · 앱 메뉴 ⌘Q = 닫기 요청), NSWindow + flipped 내용 뷰
  (layer = CAMetalLayer → NativeWindowHandle{Cocoa}), 위임으로 닫기 요청 · 크기 · 배율 · 최소화 · 포커스, 키(keyCode →
  `MacKeyMap`, 반복 = isARepeat, flagsChanged 의 장치별 비트로 왼 · 오른 수정자, Caps Lock 누름 + 뗌, ⌘ keyUp 직접 전달),
  글자(켜져 있을 때만 interpretKeyEvents → insertText, 조합 중 글자는 버림), 마우스(other 버튼 2 · 3 · 4, clickCount, 휠 =
  scrollingDelta, 정밀 ÷ 10), 캡처(CGAssociate + 델타), NSCursor, NSPasteboard, poll/wait = nextEventMatchingMask.
  `MacKeyMap` (kVK 표 — AppKit 없음, 모든 OS 컴파일), `MacPlatform.cpp` ("Cocoa"), `MacWindows.hpp`.
- CMake: APPLE 이면 OBJCXX · CocoaWindow.mm 만 -fobjc-arc · AppKit · QuartzCore · CoreGraphics, SBX_HAS_COCOA. 배포 목표 13.0.
  stub 창은 이제 "X11 · Wayland 를 모두 끈 Linux" 뿐.
- client: macOS 한글 UI 글꼴 (Apple SD 산돌고딕 Neo → 나눔고딕 → AppleGothic). client_version 정규식에 Cocoa.
- 시험: unit_platform Mac 키 표 (모든 OS), 스위트 `cocoa` (test_cocoa_window.mm, macOS 만) · CTest `platform_cocoa`.
- 문서: ADR-0036, 07(상태 · 1 · 2 · 6.3 · 9장), 13, 15(1 · 4 · 9장), 16(14B ◐ · 14C), 17, MANUAL-QA 14B, README.

**왜**

- Phase 14 의 두 번째 단계. Metal 백엔드(14C)가 받을 CAMetalLayer 를 창이 만든다.
- 가상 키 코드는 물리 위치 — 세 OS 가 같은 Key 를 낸다. 엔진이 이벤트 루프를 쥐어 다른 OS 와 같은 poll/wait 계약.

**검증**

- macOS 가 없어 **빌드 · 실행하지 못했다**. 대신: GNUstep 헤더(libgnustep-gui-dev) + 없는 Apple API 를 적은 검사용 머리 파일로
  `clang++-19 -fsyntax-only -fobjc-arc -fobjc-runtime=gnustep-2.0` — CocoaWindow.mm · test_cocoa_window.mm 오류 · 경고 0
  (문법 · C++ 형 · ARC 만 — Apple API 이름 · 시그니처는 확인 못 함).
- Linux clang 19 · gcc 13 Debug: ctest 51/51 (Mac 키 표 시험 151 단언). MinGW 경고 0 (커밋마다).
- 사고 하나: .clang-format 이 Objective-C 를 지원하지 않아 clang-format 이 .mm 에 빈 출력을 냈고 그것으로 파일을 덮어썼다 —
  대화 기록에서 되살렸다. 이제 서식 도구는 실패하면 건너뛴다 (클라우드 스크립트), 15-BUILD 9장에 적었다.

**남은 일**

- 첫 확인: push 후 CI 의 macOS 잡 (빌드 · platform_cocoa), Mac 이 있으면 MANUAL-QA 14B. 오류 로그를 받아 고친다.
- [계획] 조합 중 글자 UI, GCMouse 가속 없는 캡처, 대각선 크기 커서, 전체 화면.
- 다음: 14C — Metal 백엔드 (내장 MSL 실행 중 컴파일 · 번호 표 · MTLSharedEvent 타임라인).

---

## 2026-10-14 — Phase 14A: 셰이더 MSL 경로 — 벤더링한 SPIRV-Cross 호스트 도구, Metal 번호 규칙, MSL 소스 내장

**무엇을**

- external: SPIRV-Cross 1.4.304.1 (Debian orig 묶음, 서명된 .dsc 의 SHA256 7c9ffa18…, 수정 없음)의 코어 · GLSL · MSL 소스와
  라이선스만 → `sbx_spirv_cross` (경고 끔).
- tools/shader: `sbx_msl` (SPIR-V 한 단계 → .metal, MSL 3.0 · macOS · 인자 버퍼 없음). 번호는 정하지 않고 `--bind` 로 받는다
  — 셰이더가 쓰는 리소스가 번호를 받지 못했으면 실패.
- `sbx_shader_gen.py`: 프로그램의 바인딩을 (group, binding) 순서로 종류마다 Metal 번호 (버퍼 1.. · 텍스처 0.. · 샘플러 0..,
  push constant = buffer 0, 정점 버퍼 = 30 - slot, 한계 26 · 31 · 16) → `ReflectedBinding::metalIndex` · reflect.json, `--msl-tool`
  이면 sbx_msl 을 불러 `ShaderBytecode::msl` · `mslEntry` 로 내장. 자체 시험에 번호 · 한계.
- `render/rhi/ShaderTypes.hpp`: `kMetal*` 상수 · `metalVertexBufferIndex`, 필드 셋. 옵션 `SBX_SHADER_MSL` (macOS + 셰이더면 기본
  ON, 교차 빌드 불가), 정의 `SBX_HAS_MSL`. macOS 에서도 PATH 의 dxc 로 셰이더를 켠다.
- 시험: unit_render `test_shader_msl` (상수, 내장 셰이더 7 개의 번호, MSL 의 진입점 · 한정자 · "<이름> [[texture(n)]]" ·
  push constant buffer 0).
- 덤 (버그): `UploadRing` 이 비면 처음으로 돌아가던 것을 "끝에 자리가 없을 때만" 으로. GPU 가 프레임마다 다 끝내면(빈 링)
  위치가 늘 0 이라 render_tests_lavapipe 의 "링 감기" 검사가 부하(ctest -j8)에 따라 실패했다 (13B 부터 가끔 보였다).
  unit 시험 하나 추가.
- 문서: ADR-0035, 06(상태 · 5.3 · 6.1 · 6.3), 13, 15(3 · 4 · 5 · 9장), 16(Phase 14 를 14A · 14B 로), 17, external/README, README.

**왜**

- 개발 · 검증 환경에 macOS 가 없다 — Metal 을 돌릴 수 없으니 여기서 시험할 수 있는 셰이더 경로부터 (사용자 선택).
- 배포판 spirv-cross 는 판이 제각각(Ubuntu 24.04 = 2021 판)이고 Windows 에 없다 → 소스를 고정해 세 OS 가 같은 MSL 을 만든다.
- 번호를 생성기 한 곳에서 정하면 백엔드(리플렉션)와 셰이더(MSL)가 어긋날 수 없다.
- metallib 는 Xcode 전체가 필요하고 Linux 에서 시험할 수 없다 → MSL 소스를 내장해 Metal 이 실행 중에 컴파일 (14B).

**검증**

- Linux clang 19 Debug, `-D SBX_SHADER_MSL=ON` (셰이더 = Wine 의 dxc): sbx_msl 빌드 · 셰이더 7 개 × 2 단계 MSL 생성, ctest 51/51
  세 번 연속 (-j8), unit_render 31 케이스. 생성 MSL 을 눈으로: sprite VS `[[stage_in]]` + `push [[buffer(0)]]` + `[[vertex_id]]`,
  PS `atlas [[texture(0)]]` · `atlasSampler [[sampler(0)]]`, terrain `tiles [[texture(0)]]` · `palette [[texture(1)]]`.
  sbx_msl 에 샘플러 번호를 빼면 "Metal 번호를 받지 못한 리소스: atlasSampler(2:1)" 로 실패.
- MinGW (MSL 끔) 경고 0, gcc 13 Debug (MSL 켬) — 커밋마다.

**남은 일**

- 14B: Cocoa 창 · Metal 백엔드 (MSL 실행 중 컴파일 · 번호 표), 내장 MSL 을 Metal 로 컴파일하는 macOS 시험, 기준 이미지.
- [계획] metallib, 인자 버퍼, 14.0 Slang 스파이크 (14B 이후), CI 에서 MSL 생성 (Linux CI 에 dxc 고정이 먼저).

---

## 2026-10-13 — Phase 13C: Linux Wayland 창 — libdecor · xkbcommon · text-input-v3, 실행 중 창 시스템 선택, Linux CI

**무엇을**

- platform/linux: `WaylandWindow` (libwayland + libdecor 틀 — 서버 장식이 있으면 서버 것, 없으면 플러그인. configure 크기 ·
  최대화를 풀면 떠 있던 크기 · SUSPENDED = 최소화 · close → CloseRequested, 렌더러 전 wl_shm 어두운 버퍼), 키 = 같은 evdev 표,
  `WaylandInput` (xkbcommon 키맵 · 수정자 · 글자 · compose 데드 키 · repeat_info 로 클라이언트 자동 반복 · 버튼 코드 · 더블클릭),
  text-input-v3 입력기, pointer-constraints lock + relative-pointer 캡처, wl_data_device 클립보드, wl_cursor 테마, 정수 배율.
  확장 프로토콜 C 는 빌드 때 wayland-scanner 로 (`cmake/SbxWayland.cmake`). 옵션 `SBX_ENABLE_WAYLAND` (Linux 기본 ON).
- `WindowDesc.system` (Auto · X11 · Wayland) · `IWindow::backendName()` · `LinuxPlatform` 의 선택 (Auto = WAYLAND_DISPLAY →
  Wayland, 안 되면 X11 · 강제한 쪽은 대신하지 않음 · 한쪽만 든 빌드는 그쪽만).
- render/vulkan: Wayland 표면 (vkCreateWaylandSurfaceKHR), Linux 인스턴스 확장 = surface + 있는 것.
- client: `--platform auto|x11|wayland`, 로그 "창 <실제 백엔드>" (--headless 는 "Headless").
- 시험: unit_platform 에 xkb 키맵 이름으로 Wayland 입력(us · fr · us intl, 반복 시각, 수정자 키 반복 없음, 키맵 없음, 버튼,
  더블클릭), 강제 Wayland, 창 시스템 이름 · unit_client --platform. 스위트 `wayland` (실제 컴포지터 — 핸들 · 크기 · 제목 ·
  클립보드 · 커서 · 캡처 · 두 연결). CTest `platform_wayland` · `client_wayland_world_smoke` — `tools/wayland/run_headless.sh`
  (weston headless, 자기 XDG_RUNTIME_DIR), ASan 은 `tools/wayland/lsan.supp` (libdecor cairo 플러그인의 fontconfig · pango 캐시).
- CI: `.github/workflows/ci.yml` (문서에는 있었지만 커밋 기록에 없던 파일을 이번에 넣는다) — Linux 잡에 X11 · Wayland 개발 파일 · Xvfb · weston,
  push 트리거를 main → master (이 저장소의 브랜치).
- 문서: ADR-0034, 07(상태 · 1 · 2 · 6.2 · 9장), 06(상태 · 5.2), 13(CTest · CI 표), 15(1 · 4 · 7 · 8 · 9장), 16(13C ✅), 17,
  MANUAL-QA 13C, README.

**왜**

- 지금 데스크톱의 기본 세션은 Wayland 다. Xwayland 위의 X11 창은 배율이 흐리고 입력기가 XIM 을 거친다.
- GNOME 은 서버 장식이 없다 — libdecor 가 GNOME 에선 틀을 그리고 다른 서버에선 서버 틀을 쓴다 (SDL · GLFW 와 같은 선택).
- Wayland 키 코드 = evdev → 13B 의 표 그대로, 두 창 시스템이 같은 Key 를 낸다. 반복은 Wayland 규약상 클라이언트 몫이다.
- 한 바이너리가 X11 · Wayland 세션 모두에서 돌아야 한다 (배포판 패키지 하나).

**검증**

- Linux clang 19 Debug (-Werror) · gcc 13 Debug: ctest 51/51 (platform_wayland · client_wayland_world_smoke 추가).
  ASan · UBSan: platform_x11 · platform_wayland · unit_platform · client 창 스모크 둘 — 보고 없음 (억제 파일 적용 뒤).
  Wayland 만 · X11 만 · 둘 다 끈 구성: SandboxClient · SandboxTests 경고 0, --version "window Wayland" · "window X11",
  Wayland 만 든 빌드는 WAYLAND_DISPLAY 가 없어도 기본 소켓을 해 본다, X11 만 든 빌드의 --platform wayland = 오류.
- Xvfb 위 weston(x11 백엔드) + xdotool QA: libdecor cairo 틀(한글 제목 · 단추)에 월드 · 패널 (lavapipe FIFO 4장), 로그
  "Wayland 창 … · libdecor · 상대 이동 있음". 최대화 단추 → Resized 1300×804 → 복원 → 900×560, 모서리 끌기 → 820×496
  까지 매 단계 Resized, 닫기 단추 → CloseRequested → Shutdown. --no-render --log-input: 클릭 · F2 글자 a Z 9 ! · 키 반복
  (1 초에 약 25 번) · Ctrl+C → wl-paste "aZ9!xxx…" · wl-copy "붙여넣기" → Ctrl+V → Ctrl+C → wl-paste "붙여넣기" ·
  F3 캡처 중 MouseMove 위치 (500, 368) 그대로 Δ(15, -5) 셋 · 더블클릭 ×2 · 휠 (0, -1) · 가로 (-1, 0) · Ctrl+Q.
- 처음 잡은 것: 버퍼가 없는 표면은 지도에 오르지 않아 --no-render 창이 보이지도 입력을 받지도 않았다 (wl_shm 버퍼),
  libdecor 제목 줄 · 단추 클릭이 우리 wl_pointer 로도 와 게임 클릭으로 샜다 (표면 비교), 최대화를 풀 때 configure 에
  크기가 없어 최대화 크기에 남았다 (떠 있던 크기 기억), weston x11 백엔드에서 잠금이 늦게 켜져 캡처 중 절대 위치가
  움직였다 (캡처 중 위치 고정), 입력이 없는 창의 set_selection 은 weston 이 거절한다 (규약 — 시험을 그에 맞춤).
- MinGW (-D SBX_ENABLE_VULKAN=ON) 경고 0 (커밋마다).

**남은 일**

- 사용자 Linux PC: MANUAL-QA Phase 13C (GNOME · KDE · 한글 입력기 text-input-v3 · 배율 2 · 실제 GPU). CI 첫 실행 확인.
- [계획] 분수 배율 (wp-fractional-scale), 조합 중 글자 UI, wp-cursor-shape, 전체 화면, 닫은 뒤 클립보드, DnD, Linux CI dxc 고정.
- 다음: Phase 14 — macOS (Cocoa · Metal).

---

## 2026-10-13 — Phase 13B: Linux X11 창 — Xlib · evdev 키 표 · XIM · XInput2 Raw · 클립보드, Xlib 표면으로 Linux 클라이언트

**무엇을**

- platform/linux: `X11Window` (Xlib — WM_DELETE_WINDOW · ConfigureNotify 크기 · Unmap/_NET_WM_STATE_HIDDEN 최소화 · 포커스(키보드
  잡기 때문의 가짜 포커스는 뺀다) · Xft.dpi 배율 · _NET_WM_NAME 제목 · cursorfont 커서 · 빈 커서), 키 = keycode(evdev + 8) →
  `X11KeyMap` (순수 표, 모든 OS 에서 컴파일 · 시험), XKB detectable auto-repeat (없으면 같은 시각 Release + Press 쌍), XIM + XIC 텍스트
  입력 (Xutf8LookupString, 끄면 XUnsetICFocus + 조합 버림), 더블클릭 직접 판정, 휠 4 ~ 7 · X1/X2 8 · 9, 캡처 = XGrabPointer +
  XInput2 RawMotion (없으면 가운데로 되돌리기), CLIPBOARD UTF8_STRING (SelectionRequest 응답 · XConvertSelection 읽기),
  poll 기다림. `LinuxPlatform` (백엔드 이름 "X11", 잠자기). 옵션 `SBX_ENABLE_X11` (Linux 기본 ON — libx11-dev, libxi-dev 는 있으면).
- render/vulkan: Xlib 표면 — VulkanSwapChain.cpp 만 Xlib.h · vulkan_xlib.h 를 보고 vkCreateXlibSurfaceKHR 은 vkGetInstanceProcAddr 로,
  인스턴스 확장 VK_KHR_xlib_surface.
- client: Linux 한글 UI 글꼴 (Noto Sans CJK · 나눔고딕을 찾는다), --version "window X11".
- 시험: unit_platform X11 키 표 (모든 Key 왕복 · 한/영 · 한자), SandboxTests 스위트 `x11` (실제 X 서버 + XSendEvent 합성 이벤트 —
  닫기 · 키 · 반복 · 수정자 · 글자 켬/끔 · 버튼 · 더블클릭 · 휠 · X1 · 이동 · 크기 · 포커스 · 클립보드 · UTF-8 제목 · 커서 · 캡처),
  CTest `platform_x11` · `client_x11_world_smoke` (xvfb-run -noreset 이 있으면 가상 화면, 클라이언트는 lavapipe 로 월드를 그린다).
- 문서: ADR-0033, 07(상태 · 1 · 6.2 · 9장), 06(상태 · 5.2), 13, 15(1 · 3 · 4 · 7장), 16(13B ✅), 17, MANUAL-QA 13B, README.

**왜**

- Xlib 은 XIM · 글자 변환 · 클립보드가 있고 Vulkan 의 VK_KHR_xlib_surface 가 그대로 받는다 (XCB 만으로는 IME 가 없다).
- evdev keycode 는 물리 위치 그대로라 ADR-0017 의 Key 와 뜻이 같다 — Wayland(13C)도 같은 표를 쓴다.
- XInput2 Raw 가 Win32 Raw Input 과 같은 "가속 없는 상대 이동" 이다.
- 합성 이벤트는 같은 처리 함수를 지나므로 표 · 상태 기계를 실제 X 서버에서 자동 시험할 수 있다.

**검증**

- Linux clang 19 · gcc 13 Debug: ctest 49/49. ASan · UBSan 빌드: x11 · platform · client 스위트 56 케이스, 클라이언트 창(lavapipe)
  120 프레임 — 보고 없음.
- Xvfb + xdotool(XTEST — 실제 입력 경로): 월드 창(FIFO 3장, 제목 줄 "Vulkan (소프트웨어) 88 fps"), 한글 패널 글자, 휠 확대(줌 8.5 →
  22.4 px/칸) · 가운데 끌기 · D 키 이동 · 박스 선택("선택 eco.rabbit #275 …"), ImGui 글자 칸 입력 · xclip 붙여넣기("붙여넣기") ·
  Ctrl+C → xclip -o, G 격자 · F1, 창 크기 1200×760 · 500×300 · unmap/map · 1000×640 뒤 창을 채운다. --log-input: F3 캡처 중
  mousemove_relative 15,-5 → MouseMove Δ(15, -5) (XInput2 Raw), F2 글자 'a' 'Z' '9' '!' · Backspace 는 글자 아님, 더블클릭 ×2,
  휠 아래 · 왼쪽, Ctrl+Q.
- 처음 잡은 것: X.h 의 `CursorShape` 매크로가 우리 enum 을 가렸다 (#undef), Xvfb 가 마지막 클라이언트가 나갈 때 리셋해 다음 창이
  2 초 늦거나 연결이 거절됐다 (CTest 에 -noreset).
- MinGW (-D SBX_ENABLE_VULKAN=ON) 경고 0.

**남은 일**

- 사용자 Linux PC (실제 창 관리자 · 한글 입력기 · GPU): MANUAL-QA Phase 13B. Windows 는 다시 빌드 → ctest (바뀐 것은 X11 키 표뿐).
- [계획] XSETTINGS 로 실행 중 배율, 클립보드 INCR · 닫은 뒤 남기기, on-the-spot 조합 글자 그리기, Xcursor 테마, 전체 화면.
- 다음: 13C — Wayland (libdecor · xkbcommon · text-input-v3) + --platform + Linux CI.

---

## 2026-10-13 — Phase 13A: Vulkan 백엔드 — volk + 벤더링한 헤더, 타임라인 세마포어, lavapipe 로 D3D12 와 같은 기준 이미지

**무엇을**

- external: Vulkan-Headers 1.4.309 (C 헤더만) · volk 1.4.304 — Debian orig 묶음에서 (서명된 .dsc 의 SHA256), 수정 없음.
  `sbx_vulkan_headers` (VK_NO_PROTOTYPES, Windows 는 VK_USE_PLATFORM_WIN32_KHR) · `sbx_volk`. 옵션 `SBX_ENABLE_VULKAN` (Linux 기본 ON,
  Windows · macOS 기본 OFF — 계획의 SBX_ENABLE_VULKAN_ON_WINDOWS 를 대신한다).
- render/vulkan: VulkanDevice (인스턴스 1.3 · Validation · debug utils, 장치 고르기 — --rhi-warp = CPU 장치, 타임라인 큐, 리소스마다
  vkAllocateMemory, 업로드 링 · 지연 해제 · 타임스탬프 = D3D12 와 같은 프로토콜), VulkanPipeline (셰이더 · 샘플러 · 세트/파이프라인
  레이아웃 캐시 · 바인드 그룹 = 디스크립터 세트 · dynamic rendering 파이프라인), VulkanCommandList (프레임 슬롯마다 커맨드 풀,
  텍스처 전이 + 보수적인 전역 메모리 배리어, 음수 높이 뷰포트), VulkanSwapChain (Win32 표면, acquire · present binary 세마포어를
  큐가 맡는다, OUT_OF_DATE · VSync 바꿈 → 다시 만들기). DeviceCaps 정렬은 D3D12 값 이상.
- render/rhi/RenderDeviceFactory: createRenderDevice · defaultBackend · backendCompiled · parseBackend, SBX_HAS_D3D12 ·
  SBX_HAS_VULKAN. render/stub 을 지웠다.
- 셰이더: Linux 는 PATH 의 dxc (또는 SBX_DXC) 가 있으면 내장 셰이더를 켠다. sbx_shader_gen 이 내장 SPIR-V 에서 -fspv-reflect 의
  SPV_GOOGLE_* 확장 · 장식을 뺀다 (리플렉션은 원본으로 — self-test 추가).
- 시험: sbx_render_tests `--rhi auto|dx12|vulkan`, Vulkan 빌드면 Linux 에서도. CTest `render_tests_lavapipe`. SandboxTests: 백엔드
  팩토리 (이름 · 빌드에 든 것 · 없는 백엔드 = Unsupported), 클라이언트 --rhi. 고친 시험: 16 바이트 변수에서 256 바이트를 읽던 상수
  버퍼 업로드 (ASan 이 잡았다), 대소문자를 가리는 파일 시스템의 에셋 이름 (Green.png → green.png).
- client: `--rhi auto|dx12|vulkan`, --version 의 render 에 빌드에 든 다른 백엔드 ("D3D12 (+Vulkan)"), 상태 줄 · 디버그 패널의 검증
  경고 문구를 백엔드 이름으로, 소프트웨어 장치 표시 "(소프트웨어)".
- 문서: ADR-0032, 06(상태 · 2 · 3.2 · 5.2 · 12 · 13 · 14장), 07 6.2, 13, 15(1 · 3 · 4 · 5 · 7 · 9장), 16(Phase 13 = 13A · 13B · 13C,
  13A ✅), 17, external/README, MANUAL-QA 13A, README.

**왜**

- 창(13B)을 기다리면 백엔드 버그와 창 버그가 섞인다. 헤드리스 렌더 시험이 백엔드의 거의 전부를 덮고, 스왑체인은 이미 있는
  Win32 창으로 먼저 시험한다 (로드맵의 "Vulkan 을 Windows 에서 먼저").
- volk + 벤더링한 헤더: SDK · import 라이브러리 없이 재현 가능한 빌드, 로더가 없는 PC 에서도 D3D12 클라이언트가 뜬다.
- 타임라인 세마포어는 D3D12 펜스와 같은 모양이라 FrameContext · 업로드 링 · 지연 해제가 그대로다. binary 세마포어는 큐 안에
  숨겨 ISwapChain · ICommandQueue 를 바꾸지 않았다 (ADR-0032 결정 3).
- 정렬 · NDC Y · 기준 이미지를 D3D12 에 맞춰 "상위 코드 diff 0 줄" 을 기준 이미지로 증명한다 — 백엔드별 기준을 두지 않았다.

**검증**

- Linux clang 19 Debug (커밋마다 -Werror — 셰이더 없이 7A 케이스) · gcc 13 Debug: ctest 47/47. 셰이더(tools/wine/dxc.sh)를 켠
  빌드의 sbx_render_tests --rhi vulkan --warp: 커밋된 기준 이미지로 28 케이스 · 누수 0 · Validation 경고 0 · 오류 = 예상 8,
  --gbv (동기화 검증) 도 0. ASan · UBSan 빌드의 sbx_render_tests 0.
- MinGW Release (-D SBX_ENABLE_VULKAN=ON) 경고 0 (커밋마다) + Wine 11: sbx_render_tests --rhi vulkan (winevulkan → lavapipe) ·
  기본 dx12 (vkd3d) 모두 28 케이스, SandboxTests 348 케이스. 창 ecosystem_small --rhi vulkan: 스왑체인 FIFO 3장, 제목 줄
  "Vulkan (소프트웨어) 88 fps", 지형 · 스프라이트 · 패널, SetWindowPos 로 1200×800 · 0×0 · 1100×760 (다시 만들기 · 최소화) 뒤에도
  창을 채운다, Ctrl+Q.
- 처음 돌렸을 때 잡은 것: 스왑체인 확장을 표면 없는 인스턴스에서 켜던 것 (VUID-…-01387), SPIR-V 의 GOOGLE 확장 (08742), 복사 전용
  텍스처의 이미지 뷰 (04441), 디바이스 detach 순서 (시험 끝 SIGSEGV).

**남은 일**

- 사용자 PC (MSVC): `render/stub/StubRenderDevice.cpp` 지우기 → 다시 빌드 → ctest (render_tests_warp), `-DSBX_ENABLE_VULKAN=ON` 으로
  실제 GPU 의 --rhi vulkan (MANUAL-QA Phase 13A).
- [계획] VMA · 서브 할당, 디스크립터 프레임 링 · bindless, VkPipelineCache, 세밀한 버퍼 배리어, 전송 큐, 깊이 첨부.
- 다음: 13B — X11 창 · 입력 · Xlib 표면 → Linux 클라이언트. 13C — Wayland · Linux CI.

---

## 2026-10-12 — Phase 12D: 세이브 · 로드 · 리플레이 UI + 완료 기준 — 서버가 틱 사이에 하고, 월드를 바꾸면 generation (프로토콜 7). Phase 12 완료

**무엇을**

- core: `replay/ReplayPlayer` — 시작 틱 · 해시 확인 뒤 step() 마다 call 하나 (그 call 의 명령 + tick(), 해시 레코드 · 끝 해시
  비교 — 불일치는 처음 것을 기억하고 끝까지). playReplay 는 이것으로. 시나리오 `edit_session` (두 사람의 편집 명령 로그 600틱 —
  이동 · 값 · 컴포넌트 더하기/빼기 · 지형 · Rule · Prefab · Behavior 오버레이, 일시정지 편집 · 한 틱 · 속도, 일부러 거절 하나).
  `WorldSource`: openSave (이미 읽은 콘텐츠로 세이브 열기 — contentHash 가 다르면 거절) · readSaveSummary.
- network: 프로토콜 7 — WorldOp(65) · WorldOpResult(66) · WorldList(67) · WorldChanged(68, Bulk), Welcome · Snapshot 의
  generation, ServerStats 의 기록 · 재생 상태. ServerHost: Net 절반이 권한으로 거르고(목록 = 모두, 나머지 = admin 부터) Sim
  절반이 틱 사이에 저장 · 불러오기 · 기록 시작/끝 · 재생 (<savesRoot>/<이름>, 이름 = [A-Za-z0-9_-]{1,48}). 월드 바꾸기 =
  generation++ · ReplicationWriter::resetWorld(새 epoch) · WorldChanged · 오버레이 전체 · 그 배치의 명령 거절. 재생 중 편집은
  거절, Pause · Resume · Step · Speed 는 재생 제어, 끝나면 모두에게 결과 · 일시정지. 멈출 때 기록 중인 리플레이를 쓴다.
  ClientSession: requestWorldOp · 결과 · 목록 · generation (새 generation 스냅숏은 WorldChanged 까지 쥐고, 옛 것은 버리고,
  Welcome 보다 먼저 온 WorldChanged 로 시작). SandboxServer `--saves`, LocalServerDesc::savesRoot.
- editor: IEditorHost::worldGeneration — 바뀌면 되돌리기 · 다시 하기 · 기다리던 편집 · EditPreview 를 비운다.
- client: NetworkSession — worldOps · worldOp (접속하면 목록, 받아들여진 결과마다 다시), 재생 중 Space · . · = / - 는 재생 제어,
  상태 줄에 기록 · 재생. `ui/WorldPanel` ("월드"), Ctrl+S (`editor.save`) = 그 이름으로 저장, SandboxClient `--saves`.
- 테스트 (Phase 12 완료 기준): network test_world_ops (메시지 · 저장 · 불러오기 · 같은 배치 거절 · 늦게 들어옴 · 콘텐츠 다름 ·
  기록 → 재생 · 재생 제어 · 멈출 때 쓰기 · 스냅숏 쥐기 · 이른 WorldChanged), test_multi_client (5 역할 × 명령 12 종 + 월드 요청 5 종,
  두 editor 동시 편집 수렴), core edit_session (+ CTest det_edit_session · det_replay_edit_session), editor
  test_editor_world_change, client test_world_panel, 옵션 시험.
- 문서: ADR-0031, 10-EDITOR(상태 · 2 · 7 · 9 · 10장), 08(상태 · 4 · 5 · 12 · 13장, 프로토콜 7), 09 4.1, 01 4장, 13, 15, 16(12D ✅ ·
  Phase 12 완료), 17, MANUAL-QA 12D, README.

**왜**

- 클라이언트는 월드를 갖지 않는다 (ADR-0026) — 저장 · 불러오기는 서버의 일이다. 원격 클라이언트가 서버 파일을 고르는 것은
  "저장 폴더 안의 이름" 으로만 (경로 · .. 금지).
- 월드를 통째로 바꾸는 것은 epoch 재동기화(ADR-0025)와 같은 길에 얹고, 경계가 바뀔 수 있어 복제 월드를 다시 만든다. 채널 사이
  순서가 보장되지 않아 "옛 스냅숏이 새 월드에 붙는" 경우를 generation 으로 막는다 (ADR-0031 결정 3).
- 재생을 서버 월드로 하면 모두가 같은 재생을 보고, 끝난 자리에서 그대로 이어 편집한다. call 단위라 일시정지 편집까지 같다 (D3).
- 되돌리기 기록은 netId 로 대상을 가리킨다 — 다른 월드에서는 엉뚱한 개체를 바꾸므로 비운다.

**검증**

- Linux clang Debug (커밋마다) · gcc Debug: ctest 46/46 (골든 그대로 — det_edit_session · det_replay_edit_session 추가).
  TSan network · net · client · editor · content 129 케이스 경고 0.
- MinGW Release 빌드 경고 0 (커밋마다) + Wine 230 → 238 → 243 케이스. Wine 창 ecosystem_small --saves (sbx_sim_check 로 만든
  리플레이 · 세이브): 접힌 "월드 — 보통" 제목 줄, 펼친 패널의 목록([리플레이] · tick · 개체) · Ctrl+S → 목록에 quicksave · 결과
  줄. 처음 둔 펼친 패널은 인스펙터 · 편집 패널과 겹쳐 접어 두고 제목 줄에 상태를 보이게 바꿨다. Ctrl+S 가 처음엔 에디터 없는
  선택 경로에만 있어 듣지 않았다 — 월드 입력 공통 자리로 옮기고 앱 시험(Ctrl+S → 세이브 폴더)을 더했다.
- Wine · xdotool 의 클릭이 ImGui 단추에 닿지 않아 불러오기 · 재생 단추는 단위 시험(로컬 서버 · 패널 그리기)으로 확인했다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드(프로토콜 7 — 서버 · 클라이언트 둘 다) → ctest, MANUAL-QA Phase 12D.
- [계획] 세이브 지우기 · 이름 바꾸기 · 썸네일 · 자동 저장, 클라이언트로 내려받기 · 올리기, 재생 되감기 · 시크, 저장을 Worker 로.
- 다음: Phase 13 (Linux — X11 → Vulkan → Wayland).

---

## 2026-10-12 — Phase 12C: 역할 · 접속자 · 콘텐츠 편집 — 역할은 Net 절반의 접속자 표로, 콘텐츠는 EditContent → 월드 오버레이 (프로토콜 6)

**무엇을**

- core: `content/ContentOverlay` (종류 · id → JSON, 지문). 로더가 읽은 파일 텍스트를 DB 에 남기고(ContentSources)
  `ContentLoader::applyOverlay` 가 팩 원본 + 오버레이로 다시 만든다 (팩 항목을 바꾸면 그 자리를 건너뛰고 오버레이 쪽을 읽는다,
  Rule 은 원래 정의 순서). 항목마다 raw JSON · overlay 표시, findRule. 명령 `EditContent` (CommandJson "content").
  SimulationWorld = 팩 DB + 오버레이 DB, content() 는 지금 것, contentRevision · setContentOverlay · 행동 상태를 상태 id 로 옮기기.
  WorldHash 끝에 오버레이 지문 (비면 없음), 세이브 overlay/<종류>/<id>.json · 로드(실패면 경고 + 팩 콘텐츠).
- network: 프로토콜 6 — 명령 태그 8, ContentOverlay(42) · PlayerList(61) · SetRole(63) · SetRoleResult(64), Auth.ownerKey.
  ServerHost: 접속자 표 알림 · 역할 바꾸기(net::roleCanAssign) · 토큰 표에 역할 · 주인 열쇠, Sim 절반이 오버레이 차이 · 들어온
  클라이언트 전체를 보낸다. ClientSession: players · role · requestRole · overlay. CommandValidator: EditContent = ContentEdit.
  SandboxServer `--owner-key`.
- editor: EditPermissions (IEditorHost::permissions) — 막힌 툴 숨김 · 선택 툴로, Delete · Inspector · 되돌리기 막기.
  `Editor::editContent` (되돌리기 한 칸 — 이전 오버레이 값). `ui/ContentPanel` ("콘텐츠" 창 — 목록 · JSON · 검사 · 적용 · 팩 값으로 ·
  새 항목 · 요약), 편집 패널의 "콘텐츠 창" 단추 · 막힌 툴 숨김, 인스펙터 "보기만".
- client: NetworkSession — 오버레이를 팩 콘텐츠에 얹어 content(), permissions, players · requestRole, `--owner-key`.
  `ui/PlayersPanel` ("접속자"), 시뮬레이션 패널의 실행 제어를 역할로 끈다.
- 테스트: content/core/persist test_content_overlay, network test_roles_overlay (+ test_messages · test_server_host), editor
  test_editor_roles (FakeEditorHost 에 perms · contentDb), client test_roles_content, 옵션 시험.
- 문서: ADR-0030, 10-EDITOR(상태 · 2 · 7 · 8 · 9 · 10장), 08(상태 · 4 · 5장, 프로토콜 6), 03 4장, 04 5장, 09 3 · 7장, 11 0 · 1.2장,
  13, 15, 16(12C ✅), 17, MANUAL-QA 12C, README.

**왜**

- 역할은 시뮬레이션 상태가 아니다 — 명령이 아니라 Net 절반의 메시지로 두면 Sim 스레드를 거치지 않고 판단 · 알린다. 표 전체를
  보내 접속자 패널이 그대로 쓴다 (ADR-0030 결정 1 · 2).
- 콘텐츠 DB 는 색인 · action 번호 · 감지 질의 · 교차 참조가 얽혀 있다 — 부분 패치 대신 로더 한 길로 다시 만들어 팩과 같은 검증 ·
  같은 결과를 보장한다. 불변 DB 를 갈아 끼우면 System 코드는 그대로다 (결정 3).

**검증**

- Linux clang · gcc Debug: ctest 44/44 (골든 그대로). TSan network · net · client · editor · content 116 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine 229 케이스. Wine 창: 원격 서버(--default-role player --owner-key) + 두 클라이언트 — 손님 창의
  접속자 표 · 선택 툴만 · 2 키 거절 문구 · 실행 제어 꺼짐. 로컬 "콘텐츠" 창(규칙 JSON · 요약). 처음 둔 접힌 콘텐츠 창이 목록
  패널 제목과 겹쳐 "편집" 패널의 단추로 여닫는 창으로 바꿨다 (열 때 앞으로). Wine · xdotool 의 클릭이 ImGui 단추에 닿지 않아
  창 안의 조작은 단위 시험(패널 그리기)과 기본값을 바꾼 임시 빌드의 화면으로 확인했다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드(프로토콜 6) → ctest, MANUAL-QA Phase 12C.
- [계획] 폼 · 노드 그래프 편집, 팩 항목 끄기 · 오버레이를 팩으로 내보내기, 태그 · 머티리얼 편집, 강퇴 · 차단, 소유 개체.
- 다음: 12D 세이브 · 로드 · 리플레이 UI + 완료 기준 (edit_session D3 · 두 클라이언트 동시 편집 · 역할 × 명령 매트릭스).

---

## 2026-10-12 — Phase 12B: 인스펙터 · 목록 · 되돌리기 — 서버 전용 값은 선택 상세로 (프로토콜 5), 받아들여진 편집 한 번 = 한 칸

**무엇을**

- network: 프로토콜 5 — InspectEntry.components[] {stableId, json ≤ 4 KB} × ≤ 16. buildInspect 가 복제되지 않는 EditorVisible
  컴포넌트(core.movement · life.age · ai.sensor …)의 리플렉션 JSON 을 담는다.
- editor: `Undo.{hpp,cpp}` (UndoEntry · IdRemap · paintInverse · recreateCommand · withoutEntityRefs). Editor 에 기록(편집 한 번 =
  한 칸, 결과가 다 오면 받아들여진 명령만) · undo · redo (기다리는 동안 막기, 거절이면 칸 빼기) · setField · addComponent ·
  removeComponent · 카메라 맞추기 요청. 툴이 보내는 명령마다 역명령 (이동 -delta, 배치 → 새 netId 지우기, 지우기 → 다시 만들기,
  지형 → 이전 머티리얼). `ui/InspectorPanel` ("인스펙터" — 리플렉션 필드 위젯, 이름 순, "(서버)", 더하기 · 빼기 / "목록"),
  편집 패널에 되돌리기 · 다시 단추. IEditorHost 에 catalog · components · listEntities · positionOf.
- client: NetworkSession 이 위 넷을 구현 (복제 월드 + 선택 상세). Application: Ctrl+Z · Ctrl+Y · F(editor.focus), 두 패널.
- 테스트: editor test_undo (FakeEditorHost.hpp 로 가짜 host 공유), network (선택 상세 컴포넌트), client (로컬 서버에서 고치기 →
  되돌리기 · 지운 개체 되살리기 · 앱 Ctrl+Z).
- 문서: ADR-0029, 10-EDITOR(상태 · 2 · 4 · 6.2 · 9 · 10장), 08(상태 · 5장, 프로토콜 5), 13, 15, 16(12B ✅), 17, MANUAL-QA 12B, README.

**왜**

- 복제 월드에는 복제 컴포넌트뿐이라 사용자가 고칠 값 대부분(maxSpeed · 감지 반경 · 수명)이 안 보였다. 전부 복제하면 모든 개체의
  대역폭이 늘어난다 — 고른 개체만 오가는 선택 상세 길에 얹었다 (ADR-0029).
- 되돌리기는 10-EDITOR 6.2 그대로 — 서버가 받아들인 것만, 역명령도 보통 명령. 지운 개체는 새 netId 로 돌아오므로 기록을 잇는 표가
  필요했다.

**검증**

- Linux clang · gcc Debug: ctest 44/44. TSan client · editor · network 88 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine: 187 케이스. Wine 창: 박스 선택 → 인스펙터(복제 · 서버 컴포넌트) · 목록, 배치 두 번 → Ctrl+Z
  (서버 수락 4 · 거절 0). 컴포넌트가 stableId 순이라 뒤섞여 보여 이름 순으로, 목록 패널을 네트워크 패널과 겹치지 않는 자리로 옮겼다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 12B.
- [계획] 덮어쓰기 경고 · 잠금, 되살린 개체의 saveId · 관계, enum 이름 · 태그 칩 · EntityRef 고르기 · "뷰에서 선택", batchId.
- 다음: 12C 역할 · Players · 콘텐츠 편집, 12D 세이브 · 리플레이 UI · 완료 기준 시험.

---

## 2026-10-09 — Phase 12A: 에디터 툴 — SandboxEditor · IEditorHost · 선택 · 이동 · 배치 · 지형 · 지우기 · EditPreview

**무엇을**

- editor/ = SandboxEditor (새 라이브러리, Core + Render + imgui, Network 금지): `EditorHost.hpp` (ICommandSink · IEditorHost),
  `Editor` (툴 다섯 · 설정 · EditPreview · 결과 처리 · 덧그림 · 상태 줄), `ui/EditorPanel` ("편집" 패널 — 툴 · Prefab 목록 ·
  거르기 · 끌기 간격 · 머티리얼 · 브러시 반지름 · 모양 · 격자 맞춤 · 명령 수 · 거절 사유). 루트 CMake 가 늘 만든다.
- client: NetworkSession 이 IEditorHost — submit(=명령 보내기) · 결과를 에디터 몫으로 · 콘텐츠 · 복제 지형 · 스냅숏 고르기 ·
  setSelection · setPreview(그릴 때 스냅숏 위치에 더한다) · 연결이 바뀌면 기다리던 결과를 거절로. IWorldSession::editorHost.
  Application: 세션이 host 를 주면 왼쪽 · 오른쪽 · Delete · Esc · 숫자 키(editor.tool.* = Digit1 ~ 5)를 에디터로, 덧그림 ·
  편집 패널 · 제목 줄 "툴 …". host 가 없으면(가짜 세션) 8B 선택 그대로.
- 테스트: editor 스위트 (test_editor — 가짜 host), client (test_editor_session — 로컬 서버까지, 앱 숫자 키 · 클릭). CTest unit_editor.
- 문서: ADR-0028, 10-EDITOR(상태 · 2 · 3 · 6.1 · 9 · 10장), 06 10장, 13, 15(타깃 · 실행), 16(Phase 12 를 12A ~ 12D 로, 12A ✅), 17,
  MANUAL-QA 12A, README.

**왜**

- Phase 12 는 1차 목표선이지만 일곱 묶음이라 넷으로 나눈다 (ADR-0028). 12A 로 "놓고 · 옮기고 · 칠하고 · 지운다" 가 서버 권한
  그대로(싱글플레이도) 돈다 — Inspector · Undo · 역할 · 콘텐츠 편집은 그 위에.
- 10-EDITOR E2: 에디터는 Network 를 모른다 — 복제 월드 · 선택 · 명령 결과를 가진 NetworkSession 이 host.
- 원안 단축키 Q W E B X 는 카메라 W A S D 와 겹친다 → 숫자 키.

**검증**

- Linux clang · gcc Debug: ctest 44/44 (unit_editor 7 케이스, client 28 케이스). TSan client · editor 35 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine: network · net · core · foundation · client · editor 179 케이스.
- Wine 창 (ecosystem_small, 일시정지): 3 배치 클릭 · 4 지형 끌기(바위 띠 · 브러시 원) · 2 이동(끄는 동안 preview 자리 = 놓은 뒤 서버
  자리, 깜빡임 없음), 서버 "수락한 명령 12 · 거절 0". 편집 패널이 오른쪽 아래에서 잘려 크기를 고정했다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 12A (원격 편집 · player 역할 거절).
- 12B Inspector · Hierarchy · Undo/Redo, 12C 역할 · Players · 콘텐츠 편집, 12D 세이브 · 리플레이 UI · 완료 기준 시험.
- [계획] 배치 미리보기 그림, 툴 커서 모양, 패널을 SandboxEditor 로 옮기고 도킹 레이아웃.

---

## 2026-10-08 — 측정: 사용자 PC (MSVC Release) sbx_bench --only net.

**무엇을** 사용자가 보낸 결과를 14-PERFORMANCE 7.11 · 16-ROADMAP Phase 11 에 기록. 바이트 · 관심 개체 · late_join 스냅숏 수가
컨테이너(clang)와 정확히 같고, 만들기 시간은 클라 16 · 화면 13.5 ms (컨테이너 37 ms), 훑기 약 2 ms.

**남은 일** 14 1.4 의 3 ms 목표는 여전히 [계획 15]. MANUAL-QA 11 의 나머지 (확대 · 다시 접속).

---

## 2026-10-08 — 문서: sbx_bench 는 SBX_BUILD_BENCH=ON 으로 구성해야 생긴다

**무엇을** MANUAL-QA Phase 11 · 15-BUILD 에 sbx_bench 를 켜는 방법(CLion 프로필 CMake options 에 `-DSBX_BUILD_BENCH=ON`)을
적었다. 15-BUILD 4장의 sbx_bench 링크 목록을 실제대로 (Core Network Render).

**왜** 사용자 보고 "sbx_bench 빌드가 안 된다" — cmake-build-release 의 캐시가 `SBX_BUILD_BENCH=OFF`(기본값)라 타깃이 없었다.
MANUAL-QA 11 이 켜는 단계 없이 실행 명령만 적었다.

**검증** MinGW Release 에서 SBX_BUILD_BENCH=ON 으로 sbx_bench 빌드 경고 0, Wine 에서 `sbx_bench --quick` 끝까지 (net.* 포함).
MSVC 로는 아직 빌드한 적 없다 — 사용자 PC 에서 처음.

---

## 2026-10-08 — Phase 11: 관심 영역 — Subscribe 청크 사각형 · 지연 해제 · 예산 우선순위 · 관심 지형 · 세션 토큰 다시 접속

**무엇을**

- network: 프로토콜 4 — `Subscribe` (청크 사각형 또는 all). `ReplicationWriter`: 스냅숏마다 위치 → 청크를 한 번 훑고,
  클라이언트마다 관심 = 구독 청크 비트맵 ∪ 늘 보낼 것(`setAlwaysRelevant` — Inspect 로 선택한 개체 · 위치 없는 개체).
  빠진 청크는 30 스냅숏 지연 해제, 관심 밖 = despawn · 들어오면 spawn. 예산이 있으면 늘 보낼 것 → spawn → update, 같은
  단계는 중심 거리 − 미룬 틱 × 0.5 칸 (round-robin 삭제). 지형은 관심 청크만 · 가까운 것부터 · 클라이언트가 들고 있다.
  ServerHost: Subscribe → setInterest, Inspect 집합 → setAlwaysRelevant. 세션 토큰 표 (60 초, ClientQuit 면 지움, 살아 있는
  연결의 토큰이면 옛 연결 Kicked). ClientSession::subscribe · desc.token. LoopbackTransport::severAll (시험).
- client: `IWorldSession::setView` (Application 이 카메라 visibleRect), `NetworkSession::interestFor` (+ 1 청크 · 경계로 자름 ·
  전체면 all, 초당 2 번). --connect 가 시간 초과로 끊기면 같은 토큰으로 2 초마다 다시 (60 초), 그동안 옛 복제본. 네트워크
  패널 "관심 영역" · "다시 접속".
- bench: `net.snapshot` · `net.late_join` (bench/NetBench.cpp, sbx_bench 가 SandboxNetwork 링크).
- 테스트: test_interest (새 파일), test_server_host (토큰), test_network_session (interestFor · 원격 다시 접속), test_messages.
- 문서: ADR-0027, 08(상태 · 4 · 5 · 6.1 · 6.3 · 8 · 12 · 13장, 프로토콜 4), 13, 14(1.4 · 2 · 6 · 7.11), 15, 16(Phase 11 ✅), 17,
  MANUAL-QA 11, README.

**왜**

- Phase 10 은 모든 엔티티를 모든 클라이언트에 보냈다 — 50k 면 클라이언트당 스냅숏 1 MB (14 MB/s). 256 KB/s 예산에서는
  화면 안 개체도 늦게 왔다 (14 7.9). 완료 기준: 바이트 ∝ 가시 엔티티, Late Join < 2 초.
- 처음 구현(거리 순 우선순위만)은 50k 화면 새 접속이 77 스냅숏(5.1 초) — 가까운 개체의 매 틱 갱신이 예산을 먹었다 →
  spawn 을 update 앞에 두어 5 스냅숏(0.33 초).
- 다시 접속: 16-ROADMAP 11.4. 무선 · 잠깐의 끊김에 월드를 잃지 않게.

**검증**

- Linux clang · gcc Debug: ctest 43/43. TSan network · net · client 73 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine: network · net · core · foundation · client 170 케이스.
- Wine 창 (ecosystem_survival, 서버 스레드): Home(월드 전체) 2,316 개체 → 휠 확대(22.6 px/칸) 뒤 관심 "청크 -1..2 × -2..1",
  클라이언트 개체 692 · 받은 KB/s 425 → 145.
- 측정 (14 7.11, Release `sbx_bench --only net.`): 바이트 ≈ 관심 개체 × 19.6 B (월드 전체 978 KB · 화면 49 KB), 새 접속 화면
  0.33 초 · 월드 전체 6.07 초, 클라 16 화면 만들기 37 ms.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 11 (확대 · 다시 접속 — resmon 으로 서버 일시 중단).
- [계획 15] 클라이언트 16 · 50k 스냅숏 만들기 < 3 ms (14 1.4) — 같은 개체 인코딩 바이트 재사용 · Net 스레드 인코딩.
  위치 양자화 · 필드 마스크 (예산 없이 화면 하나 600 ~ 720 KB/s). 소유 개체 alwaysRelevant · 역할 바꾸기 (Phase 12).
- 다음: Phase 12 — Multiplayer Editor.

---

## 2026-10-08 — Phase 10B: SandboxClient 가 서버의 복제본을 그린다 — LocalServerHost · NetworkSession · 보간 · Inspect · --direct-sim 삭제

**무엇을**

- core: `scenarios/WorldSource` (`openWorldSource` — 시나리오 이름 · 세이브 폴더 → 월드). SandboxServer --world 의 코드를 옮겨
  클라이언트 --world 와 같이 쓴다.
- network: 프로토콜 3 — `Inspect`(볼 netId ≤ 32) · `InspectResult`(행동 상태 · 감지 반경 · 남은 경로 · 목표 · 대상 netId, Snapshot
  채널). ServerHost: 클라이언트별 선택 상세 집합, 스냅숏 간격을 실제 시간으로 (×8 에서도 15 Hz). `LocalServerHost`
  (ServerHost + Loopback 둘, 역할 owner · 예산 없음, Threaded | Inline). `InterpolationClock` (서버 틱 추정 · renderTick,
  지연 0.1 초 · 앞으로만 · 일시정지 · 재개 · 다시 맞춤). ClientWorld: `sampleTransform` (선형 · 최단 회전 · 순간이동 · 같은 틱),
  직전 표본 틱 ≥ 직전 스냅숏 틱. ClientSession: `setInspect` · `inspect`.
- client: `NetworkSession` (IWorldSession — 로컬 · 원격 접속, 콘텐츠가 다르면 서버 팩을 읽어 다시, 첫 스냅숏까지 Connecting,
  거절 · 끊김이면 실패, 일시정지 · 한 틱 · 속도 = 서버 명령, 선택 netId + Inspect, 받은 KB/s · 적용 ms). SpriteExtraction 은
  ClientWorld + renderTick 에서 (render.sprite Opaque 캐시 netId 별, 지형은 바뀐 청크만). Application: Connecting 에서 update ·
  ready 대기 · failure 면 종료 코드 1. "네트워크" 패널. 옵션 `--world` · `--connect` · `--name` (--direct-sim · DirectSim 삭제).
- foundation: `console::utf8Arguments` — Windows 에서 명령줄을 UTF-16 에서 UTF-8 로 (Wine 원격 QA 에서 `--name 원격` 이 "??" 로
  보여 찾았다). SandboxClient · SandboxServer · sbx_net_probe · sbx_sim_check 에 적용, shell32 링크.
- 테스트: unit_network (Inspect 메시지 · 보간 시계 · 표본 · 선택 상세 · LocalServerHost ×8 15 Hz · 없는 월드), unit_client
  (test_network_session — test_direct_sim 대신), foundation (명령줄 UTF-8). CTest `client_local_world_headless` (tick 28 ·
  다시 맞춤 0) · `client_world_bad_name` (client_direct_sim_headless 대신).
- 문서: ADR-0026 (ADR-0021 Superseded, 0020 결정 5 입력 · 6 대체), 08(상태 · 4 · 5 · 6.3 · 6.4 · 9 · 12 · 13장, 프로토콜 3), 06 9장,
  01, 07 6.1, 13, 14 7.10, 15, 16(Phase 10 ✅), 17, MANUAL-QA 10B (8A ~ 8C 명령을 --world 로), README.

**왜**

- 16-ROADMAP 2장 · 10.5: "--direct-sim 은 Phase 10 완료 기준에서 삭제를 강제한다" — 싱글플레이도 명령 · 권한 · 복제를 같은 길로.
- 선택 상세(8B 의 감지 반경 · 경로 · 대상)는 서버 전용 컴포넌트라 복제되지 않는다 → 고른 개체만 Inspect 로.
- 10A 의 "Simulation 단계 2 번마다" 스냅숏은 ×8 에서 120 Hz 가 되어 대역폭 · 클라이언트 적용이 속도에 비례했다 → 실제 시간.

**검증**

- Linux clang Debug: ctest 43/43. 헤드리스 `--world ecosystem_small --frames 60` 이 두 번 모두 tick 28 (결정적).
- 네이티브 원격: Release SandboxServer ecosystem_survival · 10k + 헤드리스 SandboxClient --connect (15 Hz, 다시 맞춤 0).
- Wine (MinGW Release): SandboxClient --world ecosystem_small 창 (55 fps, 네트워크 패널, 패널 버튼 일시정지 = 서버 명령),
  SandboxServer + SandboxClient --connect --name 원격 (콘텐츠 다시 맞추기, 4.3k 개체, 한글 이름).
- 측정 (14 7.10): 10k Release 추출 1.9 ms · 스냅숏 적용 약 8 ms (15 Hz), Debug 적용 약 45 ms.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 10B (8A ~ 8C 의 남은 항목도 이제 --world 로).
- [계획] 스냅숏 디코드 · 적용을 클라이언트 Net 스레드로 (10k 에서 66 ms 마다 튐), 메뉴 · 다시 접속 UI, 외삽, Phase 11 Interest ·
  재접속 토큰, Phase 12 인스펙터.

---

## 2026-10-08 — Phase 10A: 복제 — 변경 순번 · ReplicationWriter · ClientWorld · 프로토콜 2 · net_convergence

**무엇을**

- core: `SimulationWorld::changeStamp` — tick() 마다 `max(이전 + 1, 틱)` 로 오르는 변경 순번을 레지스트리 currentTick 으로 쓴다
  (일시정지 편집 단계도 다른 값). `core/serialization/BinaryCodec` (리플렉션 ↔ 바이트: LEB128 · zigzag · f32 비트 그대로 ·
  EntityRef 는 EntityRefCodec), `ComponentInfo::writeBinary/readBinary`.
- network: 프로토콜 2 (Welcome.replicated · Snapshot · SnapshotAck · TerrainChunk 길이 부호화, 스트림 메시지 16 MB).
  `ReplicationWriter` — 클라이언트별 스냅숏 기록 32 개, ack 된 기준 + inflight 합으로 spawn · update · despawn, 바이트 예산
  round-robin, 기준을 잃으면 epoch 으로 다시 맞추기, 바뀐 지형 청크 (스냅숏당 64). `ClientWorld` — 시뮬레이션 없는 복제 월드
  (Registry · WorldGrid · netId 표 · Opaque · transform 표본). ServerHost: 단계 2 번마다 스냅숏(일시정지 중에도), 클라이언트당
  예산, roster · ack 큐, replicationMs. ClientSession: Welcome 뒤 ClientWorld 를 만들고 적용 · ack, Welcome 보다 먼저 온 지형을
  모아 둔다.
- SandboxServer `--snapshot-kbps` (기본 256, 0 = 제한 없음), 끝 줄 `replication X ms`. sbx_net_probe 가 복제 개체 수를 보인다.
- 테스트: unit_network 에 복제 6 케이스, CTest `net_convergence` (서버 + 클라이언트, Loopback · 100 ms ± 20 · 5 % 손실 양방향 ·
  64 KB/s — 복제 컴포넌트 · Opaque · 지형이 바이트 단위로 같다). `unit_*` · `net_convergence` 는 케이스가 0 개면 실패.
- 문서: ADR-0025, 08(상태 · 4 · 5 · 6 · 7 · 9 · 12 · 13장), 02 8장, 03, 09 5장, 13, 14 7.9, 15, 16(Phase 10 → 10A ✅ · 10B),
  17, MANUAL-QA 10A, README.

**왜**

- 16-ROADMAP Phase 10 — LocalServerHost 와 --direct-sim 삭제(10B)는 클라이언트가 서버 월드를 받아 볼 수 있어야 시작할 수 있다.
  Phase 10 을 10A(복제 자체 · 수렴 시험)와 10B(SandboxClient 연결 · 보간)로 나눠 복제를 그리기 없이 바이트 비교로 먼저 확정한다.
- 틱 번호만으로는 일시정지 편집을 놓쳤고, 편집 순번을 기준에 섞으면 같은 틱에 움직인 엔티티를 모두 다시 보냈다 (29 개 대
  2 개) → 변경 순번 (ADR-0025).
- 사용자 PC 에서 다시 빌드하지 않은 ctest 가 unit_network 0 케이스로 "통과" 했다 → 0 케이스 실패 규칙.

**검증**

- Linux clang Debug · gcc Debug: ctest 42/42 (새: net_convergence — Loopback 249 개체, 나쁜 링크 330 개체 수렴). 결정론 골든
  그대로 (변경 순번은 해시와 무관).
- TSan: SandboxTests network · net 36 케이스, 경고 0.
- MinGW 전체 빌드 경고 0. Wine 11: SandboxTests network · net · core · foundation 132 케이스.
- Release, ecosystem_10k + probe (ENet 127.0.0.1): 스냅숏 만들기 제한 없음 5.4 ~ 5.7 ms · 256 KB/s 1.5 ms, 틱 7 ms 안팎 (14 7.9).

**남은 일**

- 사용자 PC (MSVC): **다시 빌드한 뒤** ctest (net_convergence 포함), MANUAL-QA Phase 10A.
- [계획 10B] NetworkSession · `SandboxClient --connect` · LocalServerHost, 그리기를 ClientWorld 에서, 보간(SnapshotBuffer),
  선택 상세(Inspect), --direct-sim 삭제, Network 패널. [계획] 양자화 · 필드 마스크 · Event 복제 · EntityRef 대기 목록 ·
  Opaque 갱신 · 클라이언트 간 바이트 재사용. [계획 11] Interest · 우선순위.

---

## 2026-10-08 — Phase 9: 네트워크 기초 — ENet · 핸드셰이크 · 명령 · ServerHost · SandboxServer --world · sbx_net_probe

**무엇을**

- `external/enet`: ENet v1.3.18 C 소스를 수정 없이 (Debian `enet_1.3.18+ds.orig.tar.xz`, SHA256 = 서명된 .dsc 값). `sbx_enet`
  정적 라이브러리 — 루트가 C 언어를 켠다, Linux 는 업스트림과 같은 configure 검사, Windows 는 ws2_32 · winmm.
- `network/` (SandboxNetwork, PUBLIC Core): Transport 계약(`INetworkTransport` — wait · drain · boundPort, ConnectionId 재사용 없음),
  LoopbackTransport(+ 허브, 스레드 사이) · SimulatedTransport(지연 · 지터 · 대역폭 · Snapshot 손실 · 옛것 버림 · 신뢰 채널 재전송
  지연, seed · 주입 시계) · EnetTransport(채널 3, disconnect_later, 15 초). BitStream(LSB 우선 · LEB128 · zigzag · 상한 · 엄격한
  UTF-8 · 오류 플래그). Messages(kProtocolVersion 1, Hello · Challenge · Auth · Welcome · Reject · Command · CommandResult ·
  ServerStats · Disconnect, 나머지 id 는 예약) · CommandCodec(명령 11 종, JSON 값은 텍스트).
- ServerHost: Net 절반(핸드셰이크 · 거절 6 가지 · 10 초 시간 초과 · ProtocolError 끊기 · CommandValidator) + Sim 절반(ScenarioRunner,
  executeTick = 다음 틱, 네트워크 명령의 결과만 돌려줌, 1 초마다 ServerStats), inbox · outbox · status 큐. Inline · Threaded.
  CommandValidator: 순번 → 속도 제한(초당 120) → 권한(10 7장 표 — 편집 Editor, 시뮬레이션 제어 Admin). ErrorCode::RateLimited.
- ClientSession: 핸드셰이크 · 명령 · 결과 · 통계 · 끊기 이유.
- SandboxServer `--world <시나리오|세이브 폴더> --port --bind --max-clients --default-role --threads --ticks N --exit`, Ctrl+C 로
  서버 종료 알림, 실제 포트 출력, SIGPIPE 무시. `sbx_net_probe`(tools/net_probe): 접속 · 명령 · 결과 · 통계, 콘텐츠가 다르면
  서버가 알려 준 팩을 읽어 다시 접속.
- 문서: ADR-0024, 08(상태 · 2 · 4 · 5 · 5.1 · 10 · 12 · 13장), 01 · 03 · 09 · 10 상태, 13, 15, 16(Phase 9 ✅), 17, MANUAL-QA 9,
  external/README, README.

**왜**

- 16-ROADMAP Phase 9 — 에디터(12)가 처음부터 명령을 서버로 보내는 구조로 태어나야 하고(2장), Phase 10 복제 · LocalServerHost 가
  이 위에 선다. --direct-sim 을 지우려면(10.5) 네트워크 경로가 먼저 있어야 한다.
- 검증을 Net 쪽에 두어 거절될 명령이 Simulation 스레드에 닿지 않게, Inline 모드로 핸드셰이크 · 시간 초과 · 속도 제한을 실제
  시간 없이 결정적으로 시험한다 (ADR-0024).

**검증**

- Linux clang Debug: ctest 41/41 (새: unit_network 28 케이스 · server_world_exit · server_world_bad_name · net_server_probe_smoke).
  `SandboxServer --world random_walk_1k --ticks 60 --exit` 의 해시가 헤드리스 `--scenario random_walk_1k --ticks 60` 과 같다.
- MinGW 전체 빌드 경고 0. Wine 11: SandboxTests network · server · tools · foundation 76 케이스 (ENet 이 winsock 위에서),
  SandboxServer.exe + sbx_net_probe.exe 두 프로세스 (콘텐츠 해시 다시 맞추기 · 명령 3 개 수락 · 나감 로그).
- 수동: 서버 + probe 로 MANUAL-QA 9 의 흐름 (Pause/Step/Resume/Speed/Create 수락, editor 의 Pause 거절 → 종료 코드 3, Ctrl+C →
  probe 에 "서버 종료", 서버 없음 → 10 초 뒤 접속 실패).

**남은 일**

- 사용자 PC (MSVC): ctest, MANUAL-QA Phase 9 (방화벽 · 다른 PC 접속).
- [계획 Phase 10] 복제(NetEntityId · Snapshot · Bulk · 보간), SandboxClient 의 네트워크 세션 · LocalServerHost, --direct-sim 삭제.
  [계획 11] Subscribe · Interest · 재접속 토큰. [계획 12] 역할 바꾸기 · Chat · ContentOverlay. 서버 --record-replay · --autosave.

---

## 2026-10-07 — Phase 8C: ImGui (1.92 docking) · 기본 패널 — Phase 8 완료

**무엇을**

- `external/imgui`: Dear ImGui v1.92.9b docking 을 수정 없이 벤더링 (crates.io dear-imgui-sys 0.18.0 의 cimgui 서브모듈 사본,
  crate SHA256 external/README 에). 정적 라이브러리 `sbx_imgui` (IMGUI_DISABLE_OBSOLETE_FUNCTIONS, 경고 끔).
- `render/imgui/ImGuiRenderer` + `shaders/imgui.hlsl`: 1.92 동적 텍스처 프로토콜(RendererHasTextures) — 만들기 · 바뀐 사각형
  갱신 · 지연 해제, ImTextureID = 칸 + 1, Alpha8 → 흰색 + 알파. 업로드 링 정점 · uint16 인덱스, 명령마다 scissor +
  drawIndexed(VtxOffset), ResetRenderState 는 platformIo 콜백. `Renderer::record` 에 UI 콜백 + 타임스탬프 kMarkUi →
  `GpuPassTimes.uiMs`.
- `apps/client/ui/ImGuiLayer`: PlatformEvent → ImGuiIO (수정자는 좌우 키 상태), WantCaptureMouse/Keyboard →
  `InputSystem::setCapture` (I1), 커서 모양 · 클립보드 · 글자 칸에서 IME 켜기/끄기, 키보드 내비게이션 끔, imgui.ini 안 씀.
  폰트: `--font` → 맑은 고딕 → 내장 벡터 폰트 (동적 아틀라스 — 쓴 글자만).
- `apps/client/ui/DebugPanels`: "시뮬레이션"(tick · TPS · 일시정지 · 한 틱 · 속도 · 격자 · 자세히 · 맞춤 · 선택 설명 · 해제),
  "통계"(fps 그래프 · CPU 구간 · GPU 패스 · Draw · UI · 디바이스 · ImGui 데모). 패널은 `PanelActions` 를 돌려주고
  Application 이 단축키와 같은 함수로 적용. F1 숨기기. `IFrameRenderer::info()` · `IWorldSession::info()` 가 수치를 준다.
- 옵션 `--font <파일>`, `--no-ui`. ADR-0023. 06 10 · 14장, 07 (I1), 13, 15, 16 (Phase 8 완료), 17, MANUAL-QA 8C.

**왜**

- 16-ROADMAP 8.4 — Phase 8 의 마지막 항목. 에디터(Phase 12)와 이후 디버깅이 화면 패널을 전제한다.
- 1.92 동적 텍스처: 한글 UI 에서 정적 아틀라스는 수 MB 를 굽는다. 요청 처리는 RHI 에 이미 있는 텍스처 생성 · 영역 복사뿐.
- 패널 → 액션: 동작이 키보드 단축키와 한 곳에서 정해지고, 단위 테스트가 그리지 않고 액션을 본다.

**검증**

- 단위 (SandboxTests, client 스위트): test_imgui_layer 5 케이스 — 키 · 커서 변환 전부, 이벤트 → io, 가로채기 · IME 켜기/끄기,
  Application + UI (I1: 패널 위 클릭이 선택을 만들지 않는다, F1), 패널 액션.
- GPU (sbx_render_tests, Wine + lavapipe): test_imgui 2 케이스 — 기준 이미지 imgui_basic (내장 비트맵 폰트, 허용 8 · 2%),
  창 밖은 지운 색 그대로, 두 번째 프레임에 텍스처를 다시 만들지 않음, 큰 폰트 → 갱신, 화면 밖 창 = Draw 0, 끝나면 텍스처 수 원래대로.
  render 28 케이스 통과, 끝 로그 살아 있는 리소스 0.
- 빌드 · ctest: Linux clang Debug · RelWithDebInfo, gcc Debug 37/37 (경고 0), MinGW 전체 빌드 경고 0. 헤드리스 클라이언트 tick 28 유지.
- Wine 창 QA: 한글 패널(Noto CJK 를 --font 로), 일시정지 버튼 클릭 → 멈춤.

**남은 일**

- 사용자 PC: `ctest -L render` (imgui_basic), MANUAL-QA 8C (맑은 고딕 · 패널 · F1 · 입력 가로채기 · 데모 창 한글 입력), UI GPU 시간.
- [계획] imgui.ini · 도킹 레이아웃 · 인스펙터는 Phase 12 에디터와 함께 (SandboxEditor 로 옮긴다).

---

## 2026-10-07 — Phase 8B 측정: 사용자 PC 의 실제 GPU

**무엇을**

- 사용자 PC (MSVC Release, `--direct-sim ecosystem_10k --vsync off`, 4.3 px/칸) 제목 줄 수치를 14-PERFORMANCE 7.8 · 16-ROADMAP 에 적었다:
  1,628 fps · 렌더 CPU 0.4 ms · 추출 0.1 ms · Draw 2 · GPU 0.05 ms (지형 0.04 · 스프라이트 0.01 · 격자 0.00 · 선 0.00) ·
  30.0/30 TPS · 틱 5.2 ms (개체 9,652). 그 전의 8A 빌드는 VSync 켬에서 144 fps (모니터 상한).

**왜**

- Wine + lavapipe 수치(지형 3 ~ 5 ms)는 소프트웨어 래스터라 실제 비용을 말하지 않는다 — 실제 GPU 로 확인했다.

**검증**

- 사용자 스크린숏의 제목 줄. Phase 8 완료 기준 "10k 스프라이트 60 FPS · Draw ≤ 16" 충족.

**남은 일**

- MANUAL-QA 8B 의 선택 · 격자 · --rhi-debug 항목, ctest -L render.

---

## 2026-10-07 — Phase 8B: 지형 · 격자 · 선택 · 디버그 선 · 패스별 GPU 시간

**무엇을**

- RHI: GPU 타임스탬프 — `ICommandList::writeTimestamp(i)` · `resolveTimestamps(n)`, `IRenderDevice::completedTimestamps()`
  (프레임 슬롯마다 32 칸, beginFrame 이 슬롯을 다시 쓰기 전에 옮긴다 — GPU 를 기다리지 않음). D3D12 쿼리 힙 + Readback 버퍼.
  `Format::R16Uint`.
- TerrainPass: 타일 머티리얼 번호 텍스처(R16 UINT) + 팔레트(RGBA8 1 행), 월드를 덮는 사각형 하나 · Draw 1개, revision 이
  바뀐 청크만 영역 업로드(프레임 4 MB), TerrainCache(순수 로직). 확대하면 타일마다 밝기 결, 없는 번호 = 마젠타.
- GridPass(타일 · 청크 선 · 월드 경계를 픽셀 셰이더로), LinePass(선마다 인스턴스, 화면 픽셀 두께) — SelectionPass · DebugPass.
  DebugDrawList(line · rect · box · circle · arrow · polyline). Renderer 순서: Clear → Terrain → Sprite → Grid → Selection →
  Debug, 각 뒤에 타임스탬프 → RendererStats.gpu (패스별 ms). 셰이더 terrain · grid · lines.
- 클라이언트: capture 가 지형 청크를 revision 이 바뀔 때만 복사(스냅숏끼리 공유) · 팔레트 = "terrain/<id>" 색, 선택한 개체
  (최대 32)의 프리팹 · 상태 · 에너지 · 체력 · 감지 반경 · 경로 · 목표 · 대상 · 속도. presentation/SelectionOverlay(고르기 ·
  외곽선 · 자세히 · 제목 줄). IWorldSession 에 선택 API. 조작: 왼쪽 클릭 · 끌기 박스 · Shift 더하기/빼기 · Esc · G 격자 · V 자세히,
  카메라 끌기는 가운데 버튼만. 일시정지 중 선택이 바뀌면 진행 없이 다시 capture. 배경 스프라이트는 지형이 대신한다.
- 제목 줄: "선택 eco.rabbit #… · flee · 에너지 … · 체력 … · 경로 …", "GPU … ms (지형 · 스프라이트 · 격자 · 선)".
- assets/ecosystem/materials.json: 흙 · 물 · core.* 지형 색.
- 문서: ADR-0022, 06(상태 · 8.1 · 8.3 · 8.4 · 8.5 · 8.6 · 9 · 14장), 11(지형 색), 13, 14(7.8), 15, 16(8.3 · 8.6 ✅), 17,
  MANUAL-QA 8B, README.

**왜**

- 8A 의 월드는 단색 사각형 위의 개체뿐이었다 — 호수 · 흙이 안 보이고, 무엇이 왜 움직이는지 볼 방법이 없었다.
- 타일 하나 = 텍셀 하나면 정점 메시 없이 청크 수와 무관하게 Draw 1개, 구현마다 같은 픽셀 (ADR-0022).
- 줌 범위(0.25 ~ 512 px/칸)에서 선이 보이려면 화면 픽셀 두께여야 한다.

**검증**

- Linux: clang Debug/RWD · gcc Debug — 경고 0, CTest 37/37. TSan client · render · foundation 2회 경고 0.
- Wine 11.19 + lavapipe: sbx_render_tests 26 케이스 통과 (terrain · overlay 기준 이미지를 확대해 검토, vkd3d 도 타임스탬프를
  준다), 누수 0. SandboxClient ecosystem_survival: 지형 · 격자 · 박스 선택 21개 · 감지 반경 · 속도 화살표 스크린숏.
- ecosystem_10k 헤드리스 RWD: 틱 13.9 ~ 14.3 ms (8B 전 14.4 — 지형 capture 비용이 보이지 않는다), emit 0.10 ms (14 7.8).

**남은 일**

- 사용자 PC: ctest -L render (terrain · overlay 를 WARP 로), MANUAL-QA 8B (실제 GPU 의 패스별 ms).
- `[계획]` 지형 타일 그림 · 경계 섞기 · 밉맵, 화면 글자 지표(8C ImGui), 스트리밍 월드의 지형 고리 텍스처.

---

## 2026-10-07 — Phase 8A 후속: --direct-sim 을 Simulation 스레드로 (Debug 3 ~ 6 fps)

**무엇을**

- 사용자 보고: `--direct-sim ecosystem_10k --vsync off` 가 3 ~ 6 fps (MSVC Debug), Release 로 바꾸니 올랐다.
- 원인: DirectSim 이 렌더 스레드에서 ScenarioRunner.step 을 부르고 밀리면 한 프레임에 4 틱까지 따라잡았다. Debug 의 틱
  (clang -O0 82.6 ms, MSVC Debug 는 더 느림)이 틱 간격 33 ms 보다 길어 늘 4 틱 → 프레임 ≈ 틱 × 4.
- DirectSim: 창 = **Threaded** (Simulation 스레드가 월드 소유, 30 TPS × 속도 시각표, 0.25 초 넘게 밀리면 버림, 일시정지 ·
  한 틱 · 속도는 mutex + condition_variable), 헤드리스 · 테스트 = **Inline** (그대로). 시뮬레이션 Worker 를 DirectSim 이 소유
  (`--threads n`, 기본 코어 수 - 2 를 1 ~ 4, 헤드리스 0).
- SpriteExtraction: `capture`(Simulation 스레드, 틱마다 → 불변 WorldSnapshot — 직전 · 지금 위치, 크기, 회전, 스프라이트, 색,
  레이어, 배경) + `emit`(Main 스레드, 프레임마다 보간). 스냅숏은 shared_ptr<const> 로 내놓고 이전 버퍼를 다시 쓴다.
- 제목 줄: 시뮬레이션 "29.8/30 TPS · 틱 12.3 ms", Application "144 fps · 월드 · 추출 · 렌더 ms" (0.5 초 평균).
  끝날 때 프레임 · 틱 평균을 출력.
- 문서: ADR-0021 (ADR-0020 결정 5 · 6 대체), 01(상태), 06 9장, 13, 14 7.7, 15, 16, 17, MANUAL-QA 8A(fps 는 Release 로).

**왜**

- 느린 틱이 화면 · 카메라까지 막았다. 01 5장의 스레드 모델(Simulation 스레드, T1 · T2)대로 나누면 틱 비용과 무관하게 그린다.
- 헤드리스는 Inline 으로 남겨 CTest client_direct_sim_headless 의 틱 수(28)가 시간과 무관하게 고정된다.

**검증**

- Wine 11.19 + lavapipe(소프트웨어, 2코어), ecosystem_10k --vsync off 40 초: MinGW Debug **4 → 52 fps**(평균 69.8, 시뮬레이션
  9.3/30 TPS · 틱 105 ms 로 표시), Release 60 → 57 fps(평균 68.9, 29.8/30 TPS) — 2코어 소프트웨어 렌더라 Release 차이는 작다.
- 단위 테스트: Threaded(Worker 2) 진행 · 일시정지 · 한 틱 정확히 하나 · 같은 틱에서 Inline(Worker 0) 과 위치가 같다 (D5).
  TSan(linux-clang-tsan, clang 19) client · foundation 3회 경고 0. Linux clang · gcc Debug/RWD CTest, MinGW 경고 0.
- 헤드리스 끝 요약으로 틱 · 추출을 Debug/RWD 비교 (14-PERFORMANCE 7.7).

**남은 일**

- 사용자 PC: Release ecosystem_10k 의 fps · TPS · 렌더 ms, Debug 에서 카메라가 부드러운지 (MANUAL-QA 8A).
- `[계획]` Debug 의 틱 자체(MSVC 반복자 검사)는 그대로 느리다. Phase 10 에서 --direct-sim 이 LocalServerHost 로 바뀐다.

---

## 2026-10-07 — Phase 8A: 스프라이트 렌더러 · AssetManager · --direct-sim 관찰

**무엇을**

- Phase 8 을 셋으로 나눴다 (ADR-0020): 8A 에셋 · 스프라이트 · Renderer · --direct-sim → 8B 지형 · 디버그 패스 · 지표 → 8C ImGui.
- `render/renderer`: Camera2D(월드 y 위, 줌 = px/칸, 커서 기준 줌 · 끌기 · 맞춤), RenderWorld · SpriteDraw, 정렬 키
  [pass|layer|pipeline|material|depth], SpriteBatcher(컬링 → 안정 LSD 기수 정렬 → 아틀라스 · 상한별 묶음), Renderer
  (AssetManager.update → 배치 → 인스턴스를 업로드 링에 → Clear + WorldSpritePass, 묶음마다 draw(4, n)).
- `shaders/sprite.hlsl`: 정점 버퍼 없이 SV_VertexID 사각형 + 48 바이트 인스턴스 정점(위치 · 크기 · 회전 · 층 · uv · 색),
  카메라 push constant, Texture2DArray 아틀라스.
- `render/asset`: AssetManager(경로 정규화 id, JobSystem Worker 디코드, 요청 순 선반 패킹 + 1 텍셀 테두리, 프레임 예산 8 MB 의
  영역 업로드, 흰색 자리 · 마젠타 실패), ShelfPacker, MaterialLibrary(`assets/<팩>/materials.json`, 대체 색).
- `assets/ecosystem`: 풀 · 토끼 · 늑대 32² 자리 표시 그림(새로 그린 단순 도형)과 머티리얼 표.
- SandboxClient: ClientRenderer(ClearRenderer 를 바꿈 — 월드면 Renderer, 메뉴면 Clear + 7B 삼각형, AssetManager 소유),
  IWorldSession · DirectSim(`--direct-sim <시나리오>` — ScenarioRunner 30 TPS × ¼ ~ 8, 일시정지 · 한 틱, 따라잡기 4 틱),
  presentation/SpriteExtraction(render.sprite Opaque → 머티리얼 · 크기 · 레이어, saveId 캐시, 틱 사이 보간, depth = -y,
  월드 배경). Application: 세션이 있으면 InWorld 로, WASD · 휠 · 끌기 · Home · Space · . · = · -. 옵션 --seed · --content · --assets.
  SandboxClient 가 SandboxCore 를 링크한다 (01 의 계획대로).
- 테스트: sbx_render_tests 6 케이스(sprite · camera · order · batch_1k · assets · materials) + 기준 이미지 2장, SandboxTests
  render(카메라 · 키 · 배치 · 기수 = 기준 stable_sort · 패커 · id) · client(DirectSim · Extraction · 앱 InWorld · 옵션),
  CTest client_direct_sim_headless. 벤치 render.sprite_batch.
- 문서: 06(상태 · 7 · 8 · 9 · 14장), 11(material 의미), 13, 14(7.6), 15, 16(Phase 8 표), 17, MANUAL-QA 8A, README, ADR-0020.

**왜**

- Phase 8 의 "화면에 보인다" 를 지형 · UI 보다 먼저 스프라이트로 — 시뮬레이션(Phase 5)을 처음으로 눈으로 본다.
- 인스턴스 정점 버퍼 · 고정 아틀라스 · push constant 카메라는 프레임마다 바인드 그룹을 다시 쓰지 않는 가장 단순한 조합 (ADR-0020).

**검증**

- Linux: clang · gcc Debug/RelWithDebInfo, clang ASan — 경고 0, CTest 전부 통과 (37~41개). include 경계 0.
- MinGW + Wine 11.19 + lavapipe: sbx_render_tests 22 케이스 · 313 단언 통과, 누수 0. 7A · 7B 기준 이미지 7장은 그대로.
  sprite · batch_1k 는 확대해 검토. SandboxClient --direct-sim ecosystem_small 스크린숏(풀 · 토끼 · 늑대, 휠 줌 ×2.3 이 커서 기준),
  ecosystem_10k 12,877 스프라이트 · Draw 1 · 54 fps (소프트웨어 렌더 — 하드웨어 수치 아님).
- 벤치(RWD, 2코어 컨테이너): 50k 배치 4.85 ms(비교 정렬) → 2.19 ms(기수 정렬), 10k 0.44 ms. 14-PERFORMANCE 7.6.
- 고친 것: batch_1k 는 처음에 회전한 3.6 px 스프라이트 + 날카로운 텍스처라 구현마다 다를 수 있어 픽셀 정렬 · 무회전 · 완만한
  텍스처로 바꿨다. DirectSim 따라잡기에서 fmod 나머지가 다음 프레임에 틱을 하나 더 넣던 것을 밀린 시간 통째 버리기로.

**남은 일**

- 사용자 PC: ctest -L render (sprite · batch_1k 를 WARP 로), MANUAL-QA 8A (실제 GPU 에서 ecosystem_10k fps).
- `[계획]` 8B TerrainPass(호수 · 지형) · Grid · Selection · DebugDraw · 타임스탬프, 8C ImGui, 아틀라스 해제 · 밉 · 핫 리로드.

---

## 2026-10-07 — 규칙: 수정 작업은 커밋 · push 까지

**무엇을**

- AGENTS.md 5장을 "커밋 · Push 규칙" 으로 바꿨다: 파일을 고친 작업은 커밋과 push 까지 해야 완료. 순서(빌드 · 테스트 → 범위 확인 →
  경로 지정 add → 작업 단위별 커밋 → push → 해시 · 결과 보고), 무관한 변경 제외, 강제 push 금지, git 을 직접 못 쓰는 환경의
  bundle 전달 절차. 2장 절차에 11번 Commit & Push. `.gitignore` 에 `*.bundle`.

**왜**

- 사용자 요청. Phase 2~7B 가 초기 커밋 뒤로 한 번도 커밋되지 않은 채 쌓여 있었다 (2026-10-07 에 항목별 8 커밋으로 정리).

**검증**

- 문서 변경만. 원격 세션은 `.git` 에 쓸 수 없어(기기 브리지 제한) 이 커밋도 bundle 로 전달했다.

---

## 2026-10-07 — Phase 7B: 셰이더 빌드 · 파이프라인 · BindGroup · Triangle · Texture

**무엇을**

- 셰이더 빌드 (ADR-0019): `cmake/SbxShaders.cmake` — DXC 를 NuGet `Microsoft.Direct3D.DXC` 1.9.2609.5 (SHA256 고정)로 처음 구성 때
  `.cache/dxc/` 에 내려받는다 (Windows). `SBX_BUILD_SHADERS`(기본 Windows ON) · `SBX_DXC` · `sbx_add_shader()`. HLSL → DXIL + SPIR-V →
  `tools/shader/sbx_shader_gen.py`(자체 SPIR-V 리플렉션, Python 표준 라이브러리만) → `<Pascal>Shader.{hpp,cpp}`(내장 바이트코드 ·
  리플렉션 표 · cbuffer/push 구조체 + offsetof/sizeof static_assert) + `.reflect.json`. 생성기가 DXIL 서명(dxil.dll)을 검사한다.
- 셰이더: `shaders/basic_color.hlsl`(정점 색 · Frame tint · push 변환), `basic_texture.hlsl`(Texture2D + Sampler, 그룹 2),
  `common/Common.hlsli`. 바인딩 규칙 register(xN, spaceG) — G = 그룹, N = 그룹 안 고유 binding, push = b0 space7.
- RHI: `ShaderTypes`(단계 · 바인딩 종류 · ShaderReflection · ShaderBytecode), Sampler · BindGroupLayout · BindGroup · GraphicsPipeline Desc,
  `layoutFromReflection`, `RangeAllocator`, `PipelineValidation`(레이아웃 · 파이프라인 ↔ 리플렉션 · 바인드 그룹 검사 — 그래픽 API 전에),
  ICommandList 의 setPipeline · setBindGroup · pushConstants · setVertexBuffer · setIndexBuffer · draw · drawIndexed, DeviceStats 의
  livePipelines · descriptorsUsed.
- D3D12 (`Dx12Pipeline.cpp`): shader-visible CBV/SRV/UAV 65,536 · Sampler 2,048 힙, 바인드 그룹 = 연속 구간(지연 반납), 루트 시그니처를
  레이아웃 모양 + push 크기로 만들어 캐시([루트 상수] → 슬롯마다 리소스 표 + 샘플러 표), PSO(CCW 앞면 · 블렌드 4종 · 깊이 끔).
  Garbage 를 일반화(객체 + 해제 콜백). `Com<T>` 변환 이동 생성자.
- 테스트: `tests/render/test_rhi_draw.cpp` 8케이스 — triangle · coord_convention(래스터 사분면 == 업로드 사분면, 비트 단위) ·
  culling_ccw · push/상수 버퍼 · drawIndexed · texture(nearest == 사분면, linear) · 검증 오류 5종 · 수명. 기준 이미지 4장 추가.
  끝의 누수 검사에 파이프라인 객체 · 디스크립터. 실패해도 이미 만든 것을 해제하도록 ColorRig 를 정리.
  SandboxTests `render`: `test_pipeline_validation.cpp`(RangeAllocator 참조 모델 · 검사 · 생성 셰이더). CTest `shader_gen_selftest`.
- SandboxClient: 셰이더가 내장된 빌드는 Clear 위에 도는 삼각형(가로세로비 보정 · 뒷면 컬링 — 규약 확인용).
- 시험 도구: `tools/wine/dxc.sh`(Windows dxc.exe 를 Wine 으로 — 교차 빌드), `run.sh` 에 `WINE` · Mono/Gecko 끔.
- 문서: 06(상태 · 3.2 · 3.4 · 5.1 · 6장 · 8.6 · 14장), 13, 15(옵션 · 함정), 16(7.4~7.6 ✅), 17, MANUAL-QA Phase 7B, README, ADR-0019.

**왜**

- ADR-0007 의 "도구 버전 고정 · SPIR-V 리플렉션 · 오프라인 컴파일" 을 사용자 PC 와 클라우드 세션 양쪽에서 같은 바이트로 하려면
  NuGet 이 맞았다 (GitHub 릴리스는 세션에서 막힘). 리플렉션에 필요한 정보는 SPIR-V 장식에 다 있어 SPIRV-Cross 빌드를 미뤘다.
- 바인딩 오류를 D3D12 런타임 오류(또는 조용한 잘못된 바인딩)가 아니라 원인 문장으로 받으려고 검사를 백엔드 앞에 뒀다.

**검증**

- Linux (셰이더 OFF): clang-19 · gcc-13 Debug/RelWithDebInfo, clang ASan — 빌드 경고 0, CTest 전부 통과 (36~40개).
- MinGW 교차 빌드 + `tools/wine/dxc.sh`(Wine 9 의 dxc.exe)로 셰이더까지 빌드 → **Wine 11.19 (WineHQ devel, dpkg -x)** d3d12(vkd3d) +
  lavapipe: `sbx_render_tests --fl11` 16케이스 · 222 단언 통과, 끝 "버퍼 0 · 텍스처 0 · 파이프라인 객체 0 · 디스크립터 0 · 경고 0 ·
  오류 6 (예상 6)". 7A 기준 이미지 3장은 바이트 단위로 그대로, 새 4장은 확대해 눈으로 검토. SandboxTests render 16케이스 통과.
  SandboxClient 실제 창: 삼각형이 돌고 가로세로비가 맞다 (스크린숏 2장).
- Ubuntu 의 Wine 9.0(vkd3d 1.10)은 PSO 생성에서 "Failed to compile shader, vkd3d result -4" — DXIL 미지원이라 시험 환경을 올렸다.
- GCC 13 -Warray-bounds 오진(RangeAllocator::allocate 의 vector::erase) → 반복자 루프로 바꿔 없앴다.

**남은 일**

- 사용자 PC: 첫 구성의 DXC 내려받기 · `ctest -L render`(WARP + Debug Layer 에서 기준 이미지 4장 재확인) · MANUAL-QA Phase 7B.
- `[계획]` Compute 파이프라인 · dispatch, 깊이 버퍼, PSO 캐시 (Phase 8). D3D12MA (Phase 8).

---

## 2026-10-06 — Phase 7A 후속: 사용자 PC 의 Debug Layer 성능 경고

**무엇을**

- 사용자 PC (MSVC, WARP, Debug Layer 켬): `render_tests_warp` 8 케이스는 모두 통과했지만 끝 검사에서 실패 — Debug Layer 경고 5건
  "ClearRenderTargetView: The application did not pass any clear value to resource creation". 정확성 문제가 아니라 성능 안내다.
- `Dx12Device` 의 InfoQueue 저장 필터에 `D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE` ·
  `CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE` 를 뺐다 (스왑체인 백버퍼는 최적 Clear 값을 가질 수 없어 매 프레임 났을 것).
- 사용 오류 테스트가 일부러 낸 `[error]` 로그 앞에 안내 MESSAGE.

**검증**

- 사용자 PC 관찰: FL 12_1 · Microsoft Basic Render Driver · Debug Layer 켬 — 기준 이미지 3장 · 수명 · 링 감기 모두 통과 (Debug Layer 메시지 외).
- Wine 재실행 통과 (Wine 에는 Debug Layer 가 없어 필터 자체는 사용자 PC 에서 다시 확인).

---

## 2026-10-06 — Phase 7A: RHI 골격 · D3D12 디바이스 · 스왑체인 Clear · 프레임 자원 · 기준 이미지 테스트

**무엇을**

- Phase 7 을 둘로 나눴다 (ADR-0018): 7A 디바이스·Clear·프레임 자원·기준 이미지 틀 → 7B 셰이더·파이프라인·Triangle·Texture.
- `SandboxRender` (Platform 만 의존, 항상 빌드):
  - `render/rhi`: `RhiTypes`(핸들·Format·MemoryType·Usage·ResourceState·Desc·Caps·DeviceStats), `RenderDevice`(IRenderDevice ·
    ICommandList · ICommandQueue · ISwapChain · createRenderDevice), `HandlePool`, `DeferredDestructionQueue`, `UploadRing`
  - `render/dx12`: `Dx12Device`(어댑터 선택 — WARP · 고성능 하드웨어 · 소프트웨어, FL 12_0 기본, committed 리소스, RTV 힙,
    업로드 링, 프레임 슬롯, 지연 해제, InfoQueue 경고·오류 집계, DRED, 디바이스 제거 원인), `Dx12CommandList`(배리어 · Clear ·
    MRT · 버퍼/텍스처 복사 + 배치 검증 · PIX 마커), `Dx12SwapChain`(FLIP_DISCARD · tearing · resize · 최소화 skip), `Com<T>`
  - `render/asset/Image`(RGBA8 · PNG 읽기/쓰기 · 비교 · 차이 그림), stb 벤더링(`external/stb`, f58f558)
  - `render/stub` — 백엔드가 없는 OS 의 `createRenderDevice` = Unsupported
- RHI 추가 (06 3.2 초안 대비): beginFrame/endFrame/frameIndex/waitIdle, map, allocateUpload, copyTextureToBuffer, destroy 오버로드,
  alive · textureDesc · stats, lastSubmittedValue, DeviceDesc.allowFeatureLevel11 (시험 전용)
- `sbx_render_tests` (Windows, `tests/render`): clear · 두 색 첨부 · upload_quadrants · region_copy · 버퍼 왕복 + 링 감기 · 수명 ·
  사용 오류. 기준 이미지 3장. 끝에 누수 0 · Debug Layer 경고 0 · 예상한 오류 수. CTest `render_tests_warp`
- SandboxClient: `IFrameRenderer` 경계 + `ClearRenderer`(실제 시간으로 천천히 색이 바뀌는 Clear, 제목 줄에 fps), 옵션
  `--no-render --rhi-debug --rhi-gbv --rhi-warp --rhi-fl11 --vsync on|off --frames-in-flight 2|3`. 렌더러가 있으면 페이싱은 VSync
- `tools/wine/`: Wine(vkd3d) + lavapipe 실행 래퍼와 시험용 Vulkan 레이어
- 단위 테스트 스위트 `render` (모든 OS): 핸들 풀 · 지연 해제 · 업로드 링(참조 모델 2,000프레임) · 포맷 · PNG 왕복(한글 경로) · 비교.
  `client`: 가짜 렌더러 · 새 옵션

**왜**

- Phase 7 (16): 렌더러(8)가 올라설 RHI 와 첫 백엔드. 06 8.6 순서 "Window → Clear → Triangle → Texture" 의 Clear 까지.

**도중에 바꾼 것 / 발견한 것**

- MinGW 의 d3d12.h 는 d3d12sdklayers.h 를 포함하지 않는다 → 직접 include. D3D_FEATURE_LEVEL_12_2 도 없다 → 값으로.
- Wine 의 d3d12(vkd3d)가 lavapipe 에서 FL 11_0 도 거절: viewportSubPixelBits 가 0 → 시험용 Vulkan 레이어로 8. 그래도 타일드
  리소스가 없어 FL 12_0 은 안 된다 → allowFeatureLevel11 (sbx_render_tests --fl11, SandboxClient --rhi-fl11).
- 지연 해제를 "마지막 제출 값" 으로 기록하면 기록 중 파괴에서 사용 후 해제가 된다 → "다음 제출 값".
- waitIdle 이 업로드 링의 이번 프레임 구간까지 반납하면, 아직 제출 전인 업로드를 덮어쓸 수 있다 → 끝난 프레임만 반납.
- VSync 가 없는 Xvfb 에서 클라이언트가 1,500 fps 로 돌아 프레임 기반 시간의 색 변화가 깜빡였다 → 렌더러에 실제 시간을 넘긴다.
- stb 의 stbi_write_png_to_mem 은 공개 선언이 없다 → stbi_write_png_to_func. 파일 IO 는 foundation/io (Windows 한글 경로).

**검증**

```text
linux-clang(19.1.1): Debug 36/36 (bench_quick 포함), RelWithDebInfo 39/39 (balance 포함). linux-gcc(13.3): 35/35 · 38/38. 경고 0
linux-clang-asan Debug 35/35 (bench·balance 제외). doctest Linux 203 케이스 · Windows(Wine) 202 케이스
MinGW GCC 13 교차 빌드 → Wine 9 (d3d12 = vkd3d → lavapipe, Xvfb):
  sbx_render_tests --fl11: 8 케이스 통과, 누수 0, 해제 13, 경고 0 (Wine 에는 Debug Layer 가 없다 — 안내 후 계속 확인)
  SandboxClient --rhi-fl11: 스왑체인 640×400 BGRA8 · 버퍼 3, Clear 색이 시간에 따라 바뀜(스크린샷), 900×500 으로 크기 변경,
  제목 줄 "D3D12 … fps VSync 켬", Ctrl+Q 종료
  SandboxTests.exe 202 케이스 통과 (Wine 은 LANG=C.UTF-8 이어야 한글 경로를 만든다 — tools/wine/run.sh)
기준 이미지 3장의 픽셀 값을 스크립트로 확인 (clear (51,102,204) · 사분면 빨강/초록/파랑/흰색 · 노란 패치 위치)
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- 사용자 PC: MSVC 빌드, `ctest -L render` (WARP + Debug Layer — "그래픽 도구" 설치 시), qa/MANUAL-QA Phase 7A
  (실제 VSync · DPI · 최소화 · --rhi-debug 경고 0). Wine 은 Debug Layer · Microsoft 런타임 동작을 대신하지 못한다.
- Windows 골든 4개 재기록(5C) · Phase 6 수동 QA 는 그대로 남아 있다.
- [계획] 7B: DXC 셰이더 빌드(sbx_add_shader · 리플렉션 JSON · 헤더 생성), 루트 시그니처 · PSO · BindGroup, Triangle · Texture ·
  좌표 규약 기준 이미지. Phase 8: D3D12MA, Copy 큐, 업로드 예산, 타임스탬프.

---

## 2026-10-06 — Phase 6: Windows 플랫폼 (창 · 입력 · ActionMap · Null 오디오 · SandboxClient 빈 창)

**무엇을**

- `SandboxPlatform` (Foundation 만 의존, ActionMap 파싱에 nlohmann/json PRIVATE). 항상 빌드한다 — 입력·오디오 단위 테스트가 쓴다.
  - `platform/common`: `Key`(물리 위치 · 이름 표), `PlatformEvent`/`PlatformEventQueue`, `IWindow`/`WindowDesc`/`NativeWindowHandle`,
    `HeadlessWindow`(OS 창 없는 IWindow — inject), `InputSystem`(I1 setCapture/downstream · I3 · I4 · 눌린 순간 수정자),
    `ActionMap`(settings/input.json · 정확 수정자 · 충돌 · 병합 · toJson) + `ActionState`(가장자리), `Gamepad` 인터페이스(I5),
    `IAudioBackend`, `FramePacer`/`PreciseSleeper`, `attachConsole`
  - `platform/windows`: `Win32Window`(Per-Monitor DPI v2 · WM_DPICHANGED · Raw Input 캡처 · IME 컨텍스트 분리 · WM_CHAR 서로게이트 ·
    SetCapture · 더블클릭 · 휠 · 클립보드 · 커서 · Alt 메뉴/경고음 차단 · AltGr·Shift·PrintScreen 보정), `Win32Platform`
    (고해상도 대기 타이머 · 콘솔), `Win32KeyMap`(windows.h 없는 순수 표 — 모든 OS 에서 테스트)
  - `platform/audio/NullAudioBackend`, `platform/stub`(창이 없는 OS: createWindow = Unsupported)
- `SandboxClient` (`apps/client`): 앱 상태기계(Boot → MainMenu → Connecting → InWorld(Play|Edit) → Shutdown, 전이 표, 프레임 끝 적용),
  프레임 루프(이벤트 → 입력 → 액션 → 디버그 동작 → 오디오 → 제목 줄), 제목 줄 입력 모니터(수동 QA), 기본 바인딩 JSON 내장 +
  `--input`, `--headless --frames --fps --width --height --console --log-input`. Windows: GUI 서브시스템 + main, DPI 매니페스트
- `foundation/text/Utf8.hpp`. 기본 로그 싱크가 줄마다 stderr flush
- 옵션 `SBX_BUILD_CLIENT`(ON). CTest `unit_platform` · `unit_client` · `client_version` · `client_rejects_unknown_option` ·
  `client_headless_smoke`
- ADR-0017 (물리 키 · 텍스트 입력 기본 끔 · 정확 수정자 + 눌린 순간 수정자 · HeadlessWindow · 오디오 update(dt))

**왜**

- Phase 6 (16): 렌더러(7·8)가 그릴 창과, 에디터(12)가 쓸 입력 경로를 먼저 확정한다. 실제 창 없이도 앱 루프를 시험할 수 있어야
  CI 와 Linux 에서 클라이언트 코드가 회귀하지 않는다.

**도중에 바꾼 것 / 발견한 것**

- 07 초안의 "scancode 기준 바인딩" 은 OS 마다 값이 달라 설정 파일에 적을 수 없다 → Key 자체를 물리 위치로 정의 (ADR-0017).
- 같은 프레임에 연속 두 번 탭하면 ActionState 가 두 번째를 놓쳤다 (지난 프레임도 켜짐) → 바인딩 키가 새로 눌린 프레임은 pressed.
- Wine + xdotool 시험에서 `Ctrl+Q` 가 안 먹었다: 한 프레임 안에 Ctrl↓ Q↓ Ctrl↑ Q↑ 가 모두 와서 프레임 끝 수정자가 비어 있었다
  → 키가 눌린 순간의 수정자를 InputState 에 기록 (keyPressMods).
- 두 Shift 보정이 누르지 않은 쪽 KeyUp 까지 냈다 → 알린 Shift 만 뗀다.
- Wine 에서 강제 종료하면 로그가 사라졌다 (Windows CRT 가 파이프로 넘긴 stderr 를 버퍼링) → 로그 싱크 flush.
- 07 의 `IAudioBackend::update()` → `update(dtSeconds)` (Null 이 목소리 수명을 정하려면 시간이 필요).

**검증**

```text
linux-clang(19.1.1): Debug 35/35 (bench_quick 포함), RelWithDebInfo 38/38 (balance 포함). linux-gcc(13.3): 34/34 · 37/37. 경고 0
linux-clang-asan Debug 34/34 (bench·balance 제외), doctest 193 케이스
MinGW GCC 13 (posix) 교차 빌드 → Wine 9: SandboxTests.exe 193 케이스 전부 통과, SandboxClient --version · --headless 통과
Wine 9 + Xvfb + xdotool 로 실제 Win32Window: 키(스캔 코드 → Key)·Shift·F2 글자 "hi"·클릭·더블클릭 ×2·휠·가운데 버튼·
  F3 캡처 중 Raw Input Δ(30, 10)·Esc 해제·창 크기 변경 → Resized 1000×700·제목 줄(한국어)·Ctrl+Q 종료
include lint 0건 (195 파일), clang-format 적용
```

**미검증 / 남은 일**

- MSVC 빌드와 qa/MANUAL-QA Phase 6 체크리스트 (사용자 PC): DPI 모니터 이동, 한글 IME 조합, Alt+Tab, 최소화 CPU, 클립보드.
  Wine 은 Per-Monitor DPI 를 지원하지 않는다 ("DPI 를 켜지 못했습니다" 경고는 Wine 에서만 나와야 한다).
- Windows 골든 4개 재기록(Phase 5C 에서 남은 일)은 그대로 남아 있다.
- [계획] 사용자 설정 경로(platformPaths) 자동 로드, IME 캐럿 위치·현지 배열 키 이름(Phase 8), 게임패드 구현, miniaudio.
- 다음: Phase 7 — DirectX 12 RHI (Device · SwapChain Clear → Triangle → Texture, 셰이더 빌드, WARP 기준 이미지).

---

## 2026-10-06 — Phase 5C: 리플레이(D3) · 생태계 시나리오 · 밸런스 · 10k 성능 (Phase 5 완료)

**무엇을**

- 리플레이 (D3, ADR-0016): `core/command/CommandJson`(명령 ↔ JSON, 엔티티 참조 변환), `SimulationWorld::setCommandObserver`
  (수락된 명령을 적용 전에 netId → saveId 로 바꿔 넘김), `core/replay/Replay`(ReplayRecorder · JSON Lines 파일 · playReplay).
  명령의 위치 = tick() 호출 번호(call). `sbx_sim_check --replay-roundtrip [--replay-dir]` · `--replay <file>`
- 시나리오 `ecosystem_small`(64×64, 300) · `ecosystem_survival`(256×256, 1,500, 18,000틱) · `ecosystem_10k`(192×192, 10,000) —
  풀·토끼·늑대 70/25/5 %, 호수·흙, **보충 없음**
- 밸런스: `life.reproduce` 밀도 제한(crowdRadius · crowdMax), 초식 행동(배고프고 풀이 보일 때만 찾기, 대상을 잃으면 배회),
  육식(energy < 80 일 때 사냥, 15초 휴식), 수치 조정 (11 7장 — 초안 대비 표)
- 성능: 색인 항목에 core.tags 사본, flee 가 통행 가능한 목표를 고름, 감지·충돌 읽기 패스를 Worker 와 나눔(`parallelFor`,
  `SystemContext.jobs`)
- `sbx_bench --only <접두사> --threads n` + `sim.ecosystem` (평균·p95·p99·최대·System 별·종별 수)
- `sbx_sim_check --require-prefabs` (끝에 0 이면 실패) + 종별 개체 수 출력
- kSimVersion 3 → 4, 골든 4개 (ecosystem_small 신규)
- 테스트: 명령 JSON 왕복, 리플레이 왕복(일시정지 편집·Step 포함)·변조 검출·시작 월드 불일치·파일 오류(잘림·magic·CRLF),
  옵션. CTest `det_replay_*` 4개, `det_ecosystem_small(_golden)`, `balance_ecosystem_survival_{1,2,3}`(최적화 빌드만)

**왜**

- Phase 5 완료 조건(16): 시드 3개 18,000틱 공존, D1~D5, 10k 평균 < 10 ms · p99 < 25 ms. 리플레이는 Phase 9 오토세이브·
  Phase 12 에디터 명령 로그의 바탕이다.

**도중에 바꾼 것 / 발견한 것**

- 리플레이 위치를 (tick, editSequence) 대신 tick() 호출 번호로 — 미리 큐에 넣으면 실행 틱이 편집 단계 몫까지 가져간다.
  대상은 saveId 로 — 시작 세이브를 로드한 월드는 netId 를 다시 매긴다 (ADR-0016).
- 첫 survival 실행이 10분 넘게 걸렸다: 풀이 지도를 다 덮을 때까지 지수적으로 늘었다 → 밀도 제한. 초안 수치에서는 토끼가
  "풀이 안 보이면 멈춘다" 행동 때문에 굶었고, 늑대가 토끼를 다 잡은 뒤 굶었다. 풀 crowdMax 6 은 시드 3 에서 붕괴, 8 에서 공존.
- 512×512 의 "10k" 는 3,000틱 뒤 27,000 개체가 된다 → 평형 밀도에서 10,000 근처를 유지하는 192×192 로. 비용은 밀도를 따른다.
- 물 위 flee 목표가 A* 를 확장 상한까지 헤매게 해 경로 비용 4.5 ms/틱 → 통행 가능한 점을 고르게 해 0.08 ms.
- survival 맵을 문서 초안 512×512 에서 256×256 으로 (시드 3개를 수 분 안에 — 13 6장).
- Windows 골든 항목(simVersion 3)은 더 이상 맞지 않아 지웠다 (남겨 두면 SKIP 이 아니라 "갱신 필요" 실패) → 다시 기록 필요.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3): Debug 30/30, RelWithDebInfo 33/33 (balance 3개 포함), -Werror
linux-clang-asan Debug 29/29 (bench 제외), doctest 168 케이스
D1·D2·D5 ecosystem_small --repeat 2 --save-at 450 --threads 0,1,8 일치
D3  det_replay_{random_walk_1k, world_save_load, eco_lifecycle, ecosystem_small} 일치. 리플레이 한 줄을 고치면
    다음 해시 레코드에서 "불일치 (D3) … 버그 (같은 simVersion)" (수동 + 단위 테스트)
D4  골든 4개 kSimVersion 4 — Clang 19 · GCC 13 같은 해시
공존 ecosystem_survival 18,000틱 시드 1·2·3: 풀 7,218/7,868/7,535 · 토끼 1,005/981/987 · 늑대 262/251/255
성능 sim.ecosystem (ecosystem_10k → 14,710 개체): Worker 1 평균 7.83 · p99 13.56 ms, Worker 0 평균 10.48 · p99 15.95 ms
MinGW GCC 13 (posix) 교차 빌드 성공, include lint 0건 (161 파일), clang-format 적용
```

**미검증 / 남은 일**

- Windows: 골든 4개 다시 기록 (`--record-golden`, ecosystem_small 포함), ctest 전체 (balance 는 Release/RelWithDebInfo 구성에서).
- Worker 0 에서 ecosystem_10k 평균 10.5 ms (기준 10 ms 근소 초과) — 감지 TagMask 색인(S5)·반경 조정 후보 (14 7.5).
- 경로 캐시·HPA*, 바이너리 리플레이(Phase 9), 에디터 명령 로그 시나리오 edit_session(Phase 12).
- 다음: Phase 6 — Windows 플랫폼 (IWindow · Win32Window · InputSystem · SandboxClient 빈 창).

---

## 2026-10-05 — Phase 5B: 감지 · 행동 · 상호작용 · 경로 · 충돌 · JobSystem

**무엇을**

- 컴포넌트 5개 (카탈로그 18개): `core.movement`(maxSpeed·accel·arriveRadius·goal·hasGoal), `core.collider`(radius·layer·mask),
  `ai.sensor`(radius), `ai.behavior`(graph·state·enteredTick·target(saveId)·blackboard[8]),
  `ai.path`(start·goal·state·submittedTick·partial·waypoints ≤16·cursor). 틱 캐시 필드는 reflect 하지 않는다 (ADR-0015)
- 콘텐츠 link 단계: action 번호·action 별 Rule 색인(priority 내림, 정의 순)·Behavior 감지 질의(sensed·seek·flee 의 TagExpr,
  그래프당 ≤ 8, 넘으면 V7). `ContentDatabase::actions/findAction/rulesForAction`
- System 7개 + Movement 조향 (`core/systems/`, 등록 `DefaultSystems.cpp`):
  Stage 5 PathCollect · 6 Sensor · 7 Behavior(FSM, 노드 어휘 전부) · 8 PathRequest · 9 Movement(조향) · 10 Interaction ·
  11 ResolveIntents(Effect op 전부: field.add/set · destroy · spawn · tag.add/remove · event) · 16 Collision
- 경로 (`core/path/`): `PathGridSnapshot`(불변, 바뀐 청크만 다시 복사), `tileLineClear`(Amanatides–Woo, 코너 컷 금지),
  A*(8방향, 정수 비용, (f, h, 순번) 전순서, 확장 상한·부분 경로, 직선 검사로 경유점 줄이기), `PathfindingService`
  (Job 계약 T→T+1, 로드 후 Submitted 재제출)
- `foundation/job/JobSystem`(Worker 풀, 0 = 그 자리 실행, `JobGroup::wait`). `SimulationWorld::setJobSystem`
- `SimulationWorld`: `SaveIndex`(saveId → EntityId), `IntentBuffer`, `PathfindingService`, `resolveSave`,
  `SystemContext` 에 catalog · saves · intents · paths. `RandomService::Purpose::Interaction`
- `sbx_sim_check --threads n[,n..]` (D5) — 실행마다 자기 JobSystem, 나란히 비교. CTest `det_eco_lifecycle` 에 `--threads 0,1,8`
- eco 팩: rabbit·wolf 에 이동·충돌·감지·행동·경로. 늑대 targetInRange 0.9 · Rule range 1.0 (충돌 반지름 합 0.7 보다 넉넉히)
- kSimVersion 2 → 3, 골든 3개 재기록
- 테스트: JobSystem, 경로(스냅샷 불변·직선 검사·우회·부분 경로·서비스 D5), 콘텐츠 link·V7, AI(사냥→먹기·이벤트·파괴,
  배타 Rule, "*" 자기 전이·stateTime, 경로 T→T+1 우회·Worker 0/4 동일, **진행 중 Job 저장→로드 D2**, 조향, 충돌)

**왜**

- 생태계가 "먹고 먹히는" 상태가 되어야 5C 의 밸런스·리플레이·성능 측정이 의미가 있다. 경로 Job 은 첫 Worker 사용처라
  D5 검사를 여기서 세운다 (Phase 3 에서 이월).

**도중에 바꾼 것 / 발견한 것**

- 문서의 ai.path_request + ai.path_follow → ai.path 하나 (구조 변경 없이 상태 전이). ai.sensor 의 mask → 그래프 질의.
  대상 참조는 saveId. 진행 중 Job 은 저장하지 않고 로드 때 같은 입력으로 다시 제출 (ADR-0015).
- 처음엔 Behavior 그래프 캐시를 포인터로 두었다가 02 C2(컴포넌트에 포인터 금지)에 맞춰 인덱스 + id 확인으로 바꿨다.
- "*" → 지금 상태 자신으로의 전이는 건너뛴다 — 그렇지 않으면 eco.herbivore 의 flee 가 매 틱 다시 들어가 stateTime 이 0.
- 중심 거리 사거리와 충돌 분리: 분리가 반지름 합까지 밀어내므로 targetInRange·range 를 그보다 크게 둬야 먹을 수 있다.
- 클라우드 컨테이너가 2코어·8 GB 인데 -j16 으로 빌드하다 cc1plus 가 OOM 으로 죽었다 → -j2. 빌드 도중 헤더를 고쳐
  서로 다른 레이아웃으로 컴파일된 목적 파일이 섞인 바이너리가 D2 테스트를 깨뜨린 일이 있었다 (다시 빌드하니 통과) —
  검증 결과는 소스를 고정한 뒤 처음부터 다시 돌린 것만 적는다.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 24/24 (소스를 고정한 뒤 처음부터 다시 돌린 결과)
linux-clang-asan Debug → 23/23 (bench 제외)
doctest 162 케이스
D1·D2·D5 eco_lifecycle --repeat 2 --save-at 900 --threads 0,1,8 일치. 저장 시점 101~110·211·333·517·700 에서도 D2 일치
D2  단위 테스트: 경로 Job 이 진행 중(Submitted)일 때 저장 → 로드(해시 검증 일치) → 400틱 매 틱 같은 해시
D4  골든 3개 kSimVersion 3 으로 재기록 — Clang 19 · GCC 13 같은 해시
D5  sbx_sim_check --threads 0,1,8 · 경로 서비스 단위 테스트(Worker 0·1·4) 같은 결과
실행 SandboxServer --scenario eco_lifecycle --ticks 300 정상 종료 (overruns 0)
성능 (RelWithDebInfo) eco_lifecycle 평균 tick 0.11 ms (~500 개체), random_walk_10k 2.0 ms (Phase 4: 1.9) — 14-PERFORMANCE 7.4
MinGW GCC 13 (posix) 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건 (152 파일), clang-format 적용 (사용자가 고친 test_error.cpp 는 그대로 둠)
```

**후속 (2026-10-06, 사용자 PC 피드백)**

- 사용자가 `cmake-build-debug\bin` 에서 `--record-golden tests/golden/...` 을 실행 → 상대 경로라 파일이 없었고, 도구는 골든 파일이
  없으면 기본 시나리오(random_walk_1k)를 돌린 뒤 쓰기 단계에서야 실패했다 (세 명령 모두 random_walk_1k 로 돌았다).
  → 골든 파일이 없으면 시뮬레이션 전에 절대 경로·현재 폴더와 함께 거절, 새 골든은 `--scenario` 필수, 쓸 폴더가 없으면 거절.
- Windows 콘솔(cp949)에서 한국어 출력이 깨졌다 → `foundation/io/Console`(SetConsoleOutputCP 65001, windows.h 없이 선언 하나)을
  sbx_sim_check · SandboxServer 시작 때 부른다.
- MSVC 19.44 에서 Phase 5B 코드가 빌드·실행됐다 (사용자 출력: random_walk_1k simVersion 3 최종 해시 0x3c9524ea60fc4ef7 —
  Linux 골든과 같다). 검증: linux-clang Debug 24/24, MinGW 교차 빌드, include lint 0건.
- 사용자가 저장소 루트에서 골든 3개에 Windows-x86_64-MSVC-19.44 항목을 기록했다 (simVersion 3). 세 파일 모두 Clang 19 · GCC 13 ·
  MSVC 19.44 의 **모든 체크포인트 해시가 같다** — 생태계(감지·FSM·A*·충돌, sqrt 포함)까지 세 컴파일러 일치. 콘솔 한국어 정상 출력.

**미검증 / 남은 일**

- Windows ctest 전체 (Phase 5A·5B 코드). 사용자 PC: 골든 3개에 `--record-golden` (simVersion 3 — 지금 Windows 항목 없음).
- 밸런스: 1800틱 eco_lifecycle 에서 토끼가 늘고 풀이 줄어드는 쪽 — 7장 수치 조정·18,000틱 3시드 공존은 5C.
- 경로 캐시·HPA*·그룹 경로, 감지 TagMask(S5), 10k 에서 경로 예산·감지 비용 측정 (5C 의 sbx_bench sim.ecosystem).
- 다음: 5C — Replay 기록·재생(D3), ecosystem_small/ecosystem_survival, sbx_bench sim.ecosystem 10k, 밸런스.

---

## 2026-10-05 — Phase 5A: 콘텐츠 · 생명 주기

**무엇을**

- Phase 5 를 셋으로 나눴다: **5A** 콘텐츠·생명 주기 → 5B 행동·상호작용·경로 → 5C 리플레이·밸런스·성능 (16-ROADMAP).
- 필드 타입 `FixedString<N>` / `ContentId`(47 바이트) — JSON 문자열, 해시는 길이+바이트. `Hint::PrefabRef`·`BehaviorRef`.
  `FieldAccess` 방문자로 `ComponentInfo::fields`·`findField`·`getNumber`·`applyNumber`(FieldMeta 범위로 자르기) — Rule 조건·효과용
- 콘텐츠 (`core/content/`): `TagSet`(256 비트)·`TagExpr{all, any, none}`, `ContentModel`(Prefab·Rule·BehaviorGraph·PackInfo),
  `ContentLoader` — pack.json 탐색·requires 위상 정렬·태그·머티리얼·Prefab·Rule·Behavior 파싱, 검증기 V1~V7(모든 문제를 모아
  "<파일>:<JSON 포인터>" 로 보고), contentHash(CRLF 정규화, presentation/ 제외). 내장 "core" 팩
- 컴포넌트: `core.tags`, `core.prefab`(출처), `life.energy`·`life.health`·`life.growth`·`life.reproduce` (카탈로그 13개)
- 생성: `CreateEntity{prefab}` 명령과 System 의 `SpawnQueue` 가 같은 `SimulationWorld::instantiate` 를 쓴다
  (Prefab + 덮어쓰기 키 단위 병합, 위치 우선순위, 검증, 경계). SpawnQueue 는 (parent saveId, seq) 정렬, uniqueTile
- `LifecycleSystem` 재작성: 에너지 소모·체력·나이·수명 → 사망(saveId 순, `Died` 이벤트 + `DeathCause`), 성장, 번식
  (cooldown·minEnergy·minStage·chance, `stream(Spawn, saveId)`, AdjacentEmptyTile/Nearby)
- 팩 `content/ecosystem` ("eco"): 태그 7, 머티리얼 3, Prefab grass·rabbit·wolf(11 7장 수치 + Opaque render.sprite),
  Rule 3(먹기), Behavior 2(초식·육식) — Rule·Behavior 실행은 5B
- 세이브: world.json 에 content{packs, contentHash}·tags[]·opaqueComponents[], 로드 때 태그 비트 재매핑·콘텐츠 차이 경고,
  로드 해시 검증 조건을 Opaque 이름 집합 비교로 (ADR-0014)
- 시나리오 `eco_lifecycle` (128×128, 풀 400·토끼 60·늑대 10, 300틱마다 토끼 +10, 1800틱), `IScenario::requiredPacks`
- 도구: `sbx_sim_check --validate-content <팩…>`·`--content-root`, `SandboxServer --content-root`, 기본 콘텐츠 경로
  `SBX_DEFAULT_CONTENT_DIR`
- 테스트: 콘텐츠 로더(정상 팩 + 규칙별 틀린 팩), 필드 접근, Prefab 생성·생명 주기·번식·SpawnQueue 순서, 세이브 태그 재매핑.
  CTest `det_eco_lifecycle`, `det_eco_lifecycle_golden`, `content_eco_validate`, `unit_content`. 골든 `eco_lifecycle`

**왜**

- 5B 의 Behavior·Rule 실행은 콘텐츠 모델과 Prefab 생성이 먼저 있어야 시험할 수 있다. 9개 티켓을 한 번에 검증하면
  결정론 위반의 원인을 좁히기 어렵다 — 행동 없이 생명 주기만 도는 시나리오로 먼저 D1·D2·D4 를 고정한다.

**도중에 바꾼 것 / 발견한 것**

- Prefab 생성은 ECB 가 아닌 SpawnQueue (ADR-0014 1번) — 5B 에서 System 을 Job 으로 나눠도 saveId 할당 순서가 그대로다.
- Phase 4 의 opaqueEntities("Opaque 가 있으면 해시 비교 건너뜀")로는 render.sprite 를 가진 생태계 세이브가 검증되지 않는다
  → 저장·로드 프로세스의 Opaque 이름 집합이 같으면 비교 (ADR-0014 3번, ADR-0013 2번 일부 대체).
- nlohmann::json `*j.items().begin()` 을 구조적 바인딩으로 잡으면 임시 프록시에 매달린다 — Debug 는 통과, RelWithDebInfo 에서
  invalid_iterator 예외. `j.begin()` 반복자로 고쳤다. 최적화 빌드 테스트가 잡았다.
- `requires` 는 C++20 키워드라 `PackInfo::dependencies` 로. 11 5장 예시의 flags "water" → 구현은 "Water"(대문자) — 11 0장에 기록.
- 기존 골든(random_walk_1k, world_save_load)이 변하지 않았다 → kSimVersion 은 2 그대로.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 24/24
linux-clang-asan Debug → 23/23 (bench 제외)
doctest 144 케이스
콘텐츠 content/ecosystem 오류·경고 0 (content_eco_validate)
D1  eco_lifecycle --repeat 2 일치
D2  eco_lifecycle 900틱 저장 → 1800틱까지 동일, 로드 해시 검증 일치 (태그 표 · Opaque render.sprite 를 지나는 왕복)
D4  eco_lifecycle 골든 신규 — Clang 19 · GCC 13 기록, 두 컴파일러 같은 해시. 기존 골든 2개 변화 없음
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건, clang-format 적용
```

**사용자 PC 결과 반영 (Windows · MSVC 19.44 · CLion Debug, Phase 4 코드 — 2026-10-05 19:30 의 CTest 로그)**

- 19개 중 17개 통과. D1·D2(폴더 교체 rename 포함) 통과 — Windows 실행 첫 확인. `random_walk_1k`·`world_save_load` 의
  최종 해시(600틱)가 Linux 골든과 같다 (Clang·GCC·MSVC 세 컴파일러 일치). 골든 2개는 Windows 항목이 없어 SKIP.
- 실패 1 `arch_include_lint_selftest`: 린터가 위반 메시지의 '—' 를 cp949 콘솔에 쓰다 UnicodeEncodeError.
  → 스크립트가 stdout/stderr 를 UTF-8 로 고정. 사용자가 `-X utf8` 을 붙인 수정은 유지하되, 그 수정이 들어간 쪽이
  `arch_include_lint`(저장소 전체 검사)여서 이 테스트가 픽스처를 검사하게 바뀌어 있었다 → `--root` 를 저장소로 되돌리고
  `-X utf8` 은 두 테스트 모두에 적용.
- 실패 2 `arch_link_boundary_selftest`: 중첩 CMake 구성의 링크가 LNK1104(kernel32.lib) — IDE 가 vcvars 환경을 CTest 에
  주지 않는다. → MSVC 면 구성 시점의 LIB·INCLUDE·LIBPATH 를 테스트 환경으로 넘기고, 컴파일러 자체를 못 쓰면 SKIP.
- 사용자가 `test_error.cpp` 에 `<ostream>`·`<string_view>` 를 넣은 MSVC 컴파일 수정은 유지. 같은 문제가 다른 테스트에서
  나지 않게 `sbx_doctest` 에 `DOCTEST_CONFIG_USE_STD_HEADERS` (doctest 가 std 를 앞선언하지 않고 헤더를 포함).
- IDE 가 표를 정렬한 ADR 0003~0011·external/README 는 기기 쪽을 그대로 가져왔다.

**미검증 / 남은 일**

- Phase 5A 코드의 MSVC 빌드 (MinGW 크로스만 확인). 사용자 PC: 골든 3개(`random_walk_1k`·`world_save_load`·`eco_lifecycle`)에
  `--record-golden` 으로 Windows 항목 기록 — 그 전까지 `det_*_golden` 은 SKIP 이 정상.
- Prefab 상속(`inherits`), enum 필드의 이름 표기, actions.json(Phase 12) — 11 0장 `[계획]`.
- SpawnQueue 상한 (5C 성능 측정에서), EntityRef 2-pass (5B 의 ai.* 대상 참조와 함께).
- 다음: 5B — SensorSystem·BehaviorSystem(FSM)·InteractionSystem·Rule 효과, 조향 이동·충돌, PathfindingService + JobSystem(D5).

---

## 2026-10-05 — Phase 4: World · Save/Load

**무엇을**

- 월드 (`core/world/`): `ChunkCoord`(floorDiv, y 우선 비교), 지형 레이어(SoA: material·flags·moveCost·height),
  `Chunk`(terrainRevision, revision 키 해시 캐시), `WorldGrid`(고정 경계, 기본 16×16 청크, paint, 점 포함·자르기)
- 콘텐츠 (`core/content/`): 최소 `ContentDatabase` — `TerrainMaterial` (id 정렬 인덱스, 검증, 내장 4종, contentHash)
- 명령: `PaintTerrain{materialId, cells | center·shape·radius}` — id 로 지정, 경계·상한 검증, 바뀐 청크만 revision +1
- 경계: Create/Move/AddComponent·ChangeComponent(core.transform)가 월드 밖이면 OutOfRange, Movement 는 경계로 자른다
- 공간 색인: 월드 경계 안 격자 + 카운팅 정렬, `forEachInChunk`·`countInChunk`
- WorldHash: 지형 추가 (머티리얼은 id 의 stableId). `kSimVersion` 1 → 2, 골든 재기록
- 세이브 (`core/persist/`): `saveWorld`/`loadWorld` — world.json · entities.jsonl · chunks/*.chunk, 머티리얼 재매핑,
  Opaque 보존, 컴포넌트 `MigrationRegistry`, 로드 시 worldHash 검증 (ADR-0013). `foundation/io/FileIo`
- 시나리오 `world_save_load`(128×128, 300 개체, 10틱마다 지형 칠하기). `ScenarioRunner` 가 로드한 월드로 이어 돌리기
- `sbx_sim_check --save-at T [--save-dir]` (D2: 저장→로드한 월드를 나란히 끝까지 비교)
- 테스트: 월드 격자·지형·콘텐츠·PaintTerrain·경계, 청크 파일, 마이그레이션, D2 왕복, 재매핑, Opaque, 손상 세이브, CRLF,
  커밋된 샘플 세이브 `tests/data/saves/v1_sample`. CTest `det_world_save_load`, `det_random_walk_save_paused`,
  `det_world_save_load_golden`, `unit_persist`
- 벤치: `save.world` 50k

**왜**

- 세이브 왕복(D2)이 되어야 리플레이(Phase 5)의 시작점과 에디터의 저장(Phase 12)이 성립한다. 청크는 저장·전송·경로·렌더의
  공통 단위라 (05 2.1) 먼저 고정 경계로 단순하게 세운다.

**도중에 바꾼 것 / 발견한 것**

- 원자성: 파일별 rename(초안) 대신 폴더 교체 — 세트 불일치와 지난 세이브의 청크 파일 잔재를 함께 막는다 (ADR-0013).
- 로드가 world.json 의 worldHash 와 스스로 비교한다. 첫 구현에서 "Opaque 를 가진 프로세스가 저장 → 그 컴포넌트를 아는
  프로세스가 로드" 경로가 해시 불일치로 실패하는 것을 테스트가 잡았다 → world.json 에 opaqueEntities 를 남기고 그때는 비교를 건너뛴다.
- 지형 해시를 머티리얼 인덱스가 아닌 id 로 — 콘텐츠 순서가 바뀐 재매핑 로드에서도 검증이 성립한다.
- Chunk 에 entityCount·state 를 두지 않았다 (공간 색인이 O(16) 으로 계산, state 는 스트리밍과 함께) — 05 0장.
- 공간 색인 카운팅 정렬로 50k 재구성 6.8 → 1.1 ms, 1k tick 0.29 → 0.18 ms, 10k 3.8 → 1.9 ms.
- 사용자 PC 의 IDE 가 docs 의 마크다운 표를 열 맞춤으로 바꿔 둔 것을 확인 (내용 변화 없음). 이번 문서 수정도 같은 형식으로 맞췄다.
  00·06·10·design 문서는 표 정렬 외의 차이도 있어 기기 쪽을 기준으로 가져왔다 (Phase 3 커밋 때).

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 20/20
linux-clang-asan Debug → 19/19 (bench 제외)
doctest 126 케이스
D1  det_random_walk_repeat · det_world_save_load(--repeat 2) 일치
D2  world_save_load 250틱 저장 → 600틱까지 동일, random_walk_1k 일시정지 300틱 저장 → 420틱까지 동일, 로드 해시 검증 일치
D4  random_walk_1k(simVersion 2), world_save_load — Clang 19 · GCC 13 기록, 두 컴파일러 같은 해시
성능 (RelWithDebInfo, 클라우드 컨테이너) 50k 저장 188 ms / 로드 504 ms (기준 저장 < 1 s)
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- Windows 실행·MSVC. Windows 에서 폴더 교체 rename 이 실제로 동작하는지 (탐색기·백신이 폴더를 잡는 경우) — 사용자 PC 확인 필요.
- 사용자 PC: `det_*_golden` 2개는 SKIP 이 정상 → 두 골든 파일에 `--record-golden` (simVersion 2 로 바뀌었으므로 Phase 3 에서
  기록한 Windows 항목이 있다면 그것도 다시).
- 증분 저장·zstd·Worker 직렬화 (Phase 9 오토세이브), EntityRef 2-pass (Phase 5), 스키마 마이그레이션 체인.
- 다음: Phase 5 Ecosystem (ContentDatabase 완성, Sensor·Behavior·Interaction·Rule, Pathfinding Job, Replay, D3·D5).

---

## 2026-10-05 — Phase 3: 헤드리스 시뮬레이션 · 결정론 하네스

**무엇을**

- 시뮬레이션 (`core/simulation/`): `SimulationClock`(일시정지·Step·속도·editSequence), `ISystem`/`SystemContext`/`Stage`,
  `SystemScheduler`(Stage → 등록 순서, System 별 ECB + 구조 잠금, `ISystemProfiler` 훅), `SimulationWorld`
  (Stage 0·2·3·4·5~16·17, 일시정지 편집 단계), `EventStream`, `kSimVersion = 1`
- 명령 (`core/command/`): `SimCommand` variant (Create/Delete/Move/Add/Remove/ChangeComponent, Pause/Resume/Step/Speed),
  `CommandQueue`((executeTick, issuer, sequence) 정렬), `CommandResult`. 적용기는 명령마다 원자적
- 정체성: `persist.persistence{saveId}`, `net.identity{netId}` — SimulationWorld 가 생성 직후 부여, 명령으로 변경 불가
- 난수 (`core/random/`): `CounterRng`(SplitMix64 카운터, 동결), `RandomService.stream(Purpose, saveId)`
- 공간 색인 (`core/world/SpatialIndex`): 정렬 배열 격자, radius/AABB/nearest, 결과 순서 (셀 y, 셀 x, saveId)
- System (`core/systems/`): RandomWalk(Stage 7, 진단용), Movement(9), Lifecycle(15: `core.lifetime`, `life.age`)
- WorldHash (`core/replay/`): saveId 순서, Hashed 컴포넌트 stableId 순서, 구조 오류는 Error. 엔티티별 해시·JSON 진단
- 시나리오 (`core/scenarios/`): `IScenario`, `ScenarioRunner`, `random_walk_1k`/`random_walk_10k` (명령만으로 월드를 바꾼다)
- 도구: `sbx_sim_check` (`tools/sim_check/`, `SBX_BUILD_TOOLS`) — `--repeat`(D1), `--golden`/`--record-golden`(D4),
  불일치 시 엔티티→컴포넌트→필드 진단, `--inject-divergence`(하네스 자체 시험)
- 서버: `SandboxServer --scenario <name> [--ticks] [--seed] [--realtime]` 헤드리스 실행 (최종 해시·시간 출력)
- 골든: `tests/golden/random_walk_1k.json` (Linux-x86_64-Clang-19, Linux-x86_64-GNU-13) — 툴체인별 (ADR-0012)
- 빌드 정보: `kToolchainKey` 추가
- 벤치: `sim.random_walk` 1k/10k, `sim.spatial` 50k
- 테스트: RNG(동결 값), 명령 큐, 공간 색인 단위 + 속성(전수 탐색 대조), SimulationWorld(명령 경로·원자성·일시정지·
  Lifecycle·ECB 정체성·스케줄 순서·해시 성질), 시나리오(D1 축소판), sim_check·서버 옵션.
  CTest `det_random_walk_repeat`, `det_random_walk_golden`(항목 없으면 SKIP), `det_harness_selftest`, `server_scenario_smoke`

**왜**

- 렌더러보다 먼저 헤드리스로 "정답"을 고정한다 (16-ROADMAP 순서의 이유). 이후 모든 Phase 의 회귀는 골든 해시와
  `--repeat` 로 잡는다. 시나리오가 사람(에디터)과 같은 명령 경로를 쓰므로 Phase 12 의 편집 경로가 지금부터 시험된다.

**도중에 바꾼 것 / 발견한 것**

- 사용자 Windows 실행 결과 (CLion, MinGW GCC 15.2, `cmake-build-debug`): 8개 중 7개 통과 (property 3.9 s 포함).
  `arch_link_boundary_selftest` 가 "unable to find a build program corresponding to Ninja" 로 실패 — CLion 은 Ninja 를
  PATH 가 아니라 CMAKE_MAKE_PROGRAM 으로 넘기는데 중첩 cmake 호출이 그것을 받지 못했다. → `CMAKE_MAKE_PROGRAM`·
  `CMAKE_TOOLCHAIN_FILE` 을 전달하도록 수정, PATH 에 ninja 가 없는 환경을 만들어 재현·검증. Python3 은 여전히 못 찾음
  (arch_include_lint 미등록 — 15-BUILD 9장에 안내 추가).
- WorldHash 엔티티 순서를 문서 초안의 `EntityId.index` 에서 **saveId** 로 바꿨다 (로드 후 index 가 달라져도 같은 해시 — D2).
- 골든을 툴체인별로 기록 (ADR-0012). 시나리오(입력) 변경은 simVersion 대상이 아니라는 예외를 04 6장에 명시.
- JSON 읽기 버그 (Phase 2 코드): 코드에서 만든 `Json(100)` 은 signed 정수로 저장되는데 부호 없는 필드가 이를 거절했다.
  파일에서 읽은 JSON 은 양수를 unsigned 로 만들기 때문에 Phase 2 테스트가 못 잡았다. 음수만 거절하도록 수정.
- SpatialIndex: AABB 가 int32 전 범위를 덮으면 폭×높이(2^32 × 2^32)가 u64 를 넘쳐 0 이 되고, 격자 순회가 사실상 무한 루프.
  단위 테스트("huge query")가 잡았다 → 폭·높이를 먼저 셀 수와 비교.
- `editSequence` 가 실행 중 적용된 PauseSimulation 에도 오르던 것 → "이번 tick() 이 편집 단계인가" 로 판정.
- System 시간 측정은 Core 밖에서 주입 (`ISystemProfiler`) — Core 에 `<chrono>` 를 들이지 않는다 (04 4.5).
- `--threads`(D5)는 JobSystem 이 없어 Unsupported 로 거절하고 Phase 5 로 옮겼다 (16-ROADMAP 5.9). 단일 스레드에서 D5 를
  "통과"로 표시하지 않기 위해서다.
- 측정: SpatialIndex 50k 재구성 6.8 ms — 05-WORLD 4.1 의 1 ms 조건 초과. Phase 4 카운팅 정렬로 대응 (05-WORLD 4.4).

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 16/16
linux-clang-asan Debug → 15/15 (bench 제외)
doctest 111 케이스 / 단언 약 152만 (대부분 속성 테스트)
D1  sbx_sim_check --repeat 2 --ticks 600: 일치.  하네스 자체 시험: tick 60 에서 saveId 1 의 core.transform.position 까지 좁힘
D4  Clang 19 · GCC 13 항목 기록, 두 컴파일러 × 두 빌드 설정 모두 같은 해시 (0x0a5ae056a0fb401b @600)
성능 (RelWithDebInfo, 클라우드 컨테이너) random_walk 1k 평균 0.29 ms (기준 < 2 ms), 10k 3.8 ms
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror): Server·Tests·sim_check·bench 빌드 성공 (실행은 못 함)
arch_link_boundary_selftest: PATH 에 ninja 없이 CMAKE_MAKE_PROGRAM 만 준 구성에서 통과
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- Windows 실행 (MSVC·MinGW): 사용자 PC 에서 `det_random_walk_golden` 은 SKIP 이 정상 →
  `sbx_sim_check --record-golden tests/golden/random_walk_1k.json` 으로 기록해 커밋. MSVC 빌드 자체도 아직 미검증.
- D5(`--threads`), p99 tick 측정, 공간 색인 카운팅 정렬, 명령 권한·속도 제한 — Phase 4·5·9~12.
- 다음: Phase 4 World (Chunk · Terrain · Save/Load, D2).

---

## 2026-10-05 — Phase 2: Core ECS

**무엇을**

- Foundation: `SmallVector<T,N>`, `Vec2`/`Vec2i`
- ECS (`core/ecs/`): `EntityId`, `EntityManager`(LIFO·퇴역 슬롯), `ComponentPool`(4096 페이지 sparse set, P1~P3 `validate()`),
  `SBX_COMPONENT`/`ComponentTraits`(이름 규칙 컴파일 타임 검사, Persistent→Hashed 자동, `NotHashed`),
  `Registry`(구조 잠금, emplaceOrReplace, 리소스, `forEachEntityByIndex`, `poolsByStableId`, `adoptPool`),
  `View<Read/Write/Exclude>`(최소 풀 드라이버, Write 시 changed 틱), `EntityCommandBuffer`(PendingEntity, 상쇄),
  `ComponentCatalog`(명시 등록 — ADR-0011)
- 직렬화: 리플렉션 계약(`reflect(V&, T&)`), `JsonWriter/Reader`(기본값 유지·타입/범위/모르는 키 오류), `HashVisitor`(H1)
- 컴포넌트: `core.transform`, `core.velocity` + `registerCoreComponents`
- 외부: nlohmann/json v3.12.0 vendored (`external/nlohmann_json`), `external/CMakeLists.txt` 로 타깃화
- 테스트: 단위(ecs 스위트 신설), 속성 테스트(Registry vs `std::map` 참조 모델, 시드 3개), 결정성(같은 시퀀스 → 같은 id·순서)
- 벤치: `sbx_bench`(`SBX_BUILD_BENCH`), `ecs.iterate`·`ecs.churn`, 기준선 `bench/baselines/cloud-sandbox-container.json`
- CMake: MinGW 로 구성하면 경고 (사용자 CLion 이 기본 MinGW 프로필로 구성한 것을 확인했기 때문)

**왜**

- Phase 3(시뮬레이션 루프)이 올라설 저장소·순회·구조 변경·해시의 기반. 해시·세이브가 리플렉션 하나를 공유해
  "해시에 빠진 필드"(RTS H3) 문제를 구조로 막는다.

**도중에 바꾼 것 / 발견한 것**

- 구조 잠금 중 emplace 는 Release 에서도 중단 (참조 반환이라 무시 불가) — 02-ECS 0장에 반영.
- ECB emplace = 있으면 교체. Registry 에 emplaceOrReplace 추가.
- 벤치 첫 실행에서 4-컴포넌트 시나리오가 빈 view 를 재고 있었다(0.13 ns) → 방문 수 검사를 넣어 무의미한 측정이면 중단하게 함.
- GCC 13 -O2 `-Warray-bounds` 오진 (SmallVector relocate 의 memmove) → 명시적 루프로 우회.
- doctest `REQUIRE_MESSAGE` 에 삼항식을 넣으면 컴파일 실패 → `INFO` + `REQUIRE` 로.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) · linux-clang-asan  ×  Debug · RelWithDebInfo, -Werror
→ 6개 조합 각 ctest 11/11 (doctest 76 케이스, 단언 약 147만 — 대부분 속성 테스트)
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror): Server·Tests·bench 빌드 성공 (실행은 못 함)
include lint 58 파일 0건, 링크 경계 3 타깃 통과
벤치 (RelWithDebInfo, 클라우드 컨테이너): 10k×4 컴포넌트 8.2 ns/entity, 50k×4 10.9 ns/entity
사용자 PC(D:\Game\Sandbox\cmake-build-debug) 확인: CLion 기본 MinGW GCC 15.2 로 Phase 1 SandboxServer 빌드 성공,
  테스트는 빌드·실행되지 않았고 Python 을 찾지 못함 (arch 테스트 미등록)
```

**미검증 / 남은 일**

- MSVC 빌드 (특히 `SBX_COMPONENT` 의 전역 네임스페이스 특수화, View 의 템플릿 람다) — 사용자 PC 또는 CI.
- EnTT 기준선 비교, Opaque 컴포넌트, Binary/Bit/ImGui Visitor, `removedLog<T>` — 해당 Phase.
- 다음: Phase 3 헤드리스 시뮬레이션.

---

## 2026-10-05 — Phase 1: 저장소 골격

**무엇을**

- 빌드: 루트 `CMakeLists.txt`, `CMakePresets.json`(7 configure 프리셋, Ninja Multi-Config), `cmake/Sbx*.cmake`
  - `sbx_warnings`(MSVC `/utf-8` 포함), `sbx_strict_conversions`, `sbx_simulation_flags`(Core 가 PUBLIC 전파)
  - 표준 라이브러리 기능 확인(`std::expected`/`format`/`source_location`) — 안 되면 원인 한 줄로 중단
  - `CMAKE_CXX_SCAN_FOR_MODULES OFF`, 실행 파일은 `build/<preset>/bin/<Config>/`
  - `BuildInfo.hpp` 생성(버전·커밋·컴파일러)
- 경계 강제: `sbx_check_link_boundaries()`(전이적 링크 그래프), `tools/check_includes.py`(모듈 방향·금지 헤더·SFML/OpenGL·`#import`),
  각각 자체 시험(`arch_link_boundary_selftest`, `arch_include_lint_selftest` + `tests/arch/fixtures`)
- Foundation: `Types`, `Error`/`Expected`, `SBX_ASSERT`/`SBX_VERIFY`(처리기 교체 가능), `log`, `Fnv1a64`(동결 상수 테스트), `Handle<Tag>`
- Core: `core/simulation/SimConstants`(30 TPS, 고정 dt, 스냅샷 주기 약수 규칙)
- SandboxServer: `--help`/`--version`/`--log-level`, 종료 코드 0/1/2
- SandboxTests: doctest v2.5.0 vendored, 29 케이스
- 저장소 설정: `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`(적용 완료), `.clang-tidy`
- CI: `.github/workflows/ci.yml` (Windows MSVC, Linux clang-19/gcc-13/asan, macOS 15)

**왜**

- Phase 0 에서 정한 경계(ADR-0005)를 문서가 아니라 빌드·테스트 실패로 강제하기 위해. RTS 에서 "core 는 SFML 을 모른다"가
  문서에만 있고 코드에서 깨져 있던 것을 반복하지 않는다.

**도중에 바꾼 것**

- Clang 18 + libstdc++ 13 은 `std::expected`를 비활성화한다 → Linux 최소 요구를 Clang 19 로 올리고 구성 단계 검사 추가.
- CMake 3.28 + C++23 은 모듈 스캔을 켜서 clang-scan-deps 가 없으면 전부 실패한다 → 모듈 스캔 끔.
- `toString(enum)` 자유 함수가 doctest 와 ADL 충돌 → `errorCodeName`/`levelName`으로 개명, 규칙을 12-CODING-STANDARDS 에 추가.
- `SmallVector`는 첫 사용처인 Phase 2 로 이동.

**검증** (Linux 클라우드 환경)

```text
linux-clang (clang 19.1.1) · linux-gcc (gcc 13.3) · linux-clang-asan   ×   Debug · RelWithDebInfo, -Werror
→ 6개 조합 모두 ctest 8/8 통과 (doctest 29 케이스 / 68 단언)
SandboxServer --version → "SandboxServer 0.1.0 (commit unknown, Linux, Clang 19.1.1) / tick 30 Hz, dt 0.0333333 s"
인자 없음 → 종료 코드 1, 잘못된 인자 → 2
boundary selftest: 중간 INTERFACE 타깃을 거친 Server→Platform 링크를 구성 단계에서 FATAL_ERROR
include lint: 실제 트리 22 파일 0건, 픽스처 8건 정확히 검출
clang-18 지정 시 구성 단계에서 원인 메시지와 함께 중단됨을 확인
```

**미검증 / 남은 일**

- Windows MSVC, macOS 빌드는 이 세션에서 실행할 수 없었다 → 사용자 PC 에서 `cmake --preset windows-msvc` 1회, CI 첫 실행.
- `.github/workflows/ci.yml` 은 원격 쓰기가 금지된 경로라 채팅으로 전달했다 → 사용자가 직접 배치.
- `git init`·원격 저장소 생성은 사용자 작업 (16-ROADMAP 1.1b). 커밋 해시가 생기면 BuildInfo 에 반영된다(재구성 시).
- LICENSE (열린 질문 Q1).
- 다음: Phase 2 Core ECS.

---

## 2026-10-05 — Phase 0: 문서 세트 작성

**무엇을**

- 저장소 루트: `README.md`, `AGENTS.md`, `DEVELOPMENT_LOG.md`
- `docs/00~17` 규범·설계·절차 문서, `docs/adr/0001~0010`
- `docs/design/SANDBOX_ARCHITECTURE.md`: RTS 저장소 분석과 전체 설계의 원본 (Phase 0 입력, 이후 수정하지 않음)

**왜**

- 새 프로젝트를 코드 없이 시작하면서, 구현 전에 경계·불변식·순서를 먼저 고정하기 위해.
- 원본 설계서는 한 파일 2,000줄이라 작업 중 참조하기 어렵다. 주제별 규범 문서로 나누고
  구현에 필요한 세부(API 모양, 와이어 포맷, 스키마, 체크리스트)를 보강했다.

**검증**

- 문서 간 상대 링크 전수 확인, 코드 블록 짝 확인.
- 원본 설계서의 30개 장과 결정 항목이 분할 문서 어디로 갔는지 `docs/README.md` 4장에 대응표로 남김.

**남은 일**

- Phase 1: 저장소 골격(CMake 프리셋, Foundation/Core/Tests 타깃, CI, include 린트). [16-ROADMAP](docs/16-ROADMAP.md)
- 부록의 열린 질문(저장소 이름·라이선스 등)은 결정되는 대로 ADR로.
