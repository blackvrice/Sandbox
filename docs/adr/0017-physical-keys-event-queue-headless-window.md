# ADR-0017. 키는 물리 위치로, 창은 이벤트 큐를 채우고, 창이 없는 실행은 HeadlessWindow 로 한다

- 상태: **Accepted** · 날짜: 2026-10-06
- 관련: [07-PLATFORM](../07-PLATFORM.md) 2~7장, [01-ARCHITECTURE](../01-ARCHITECTURE.md) 2장, [16-ROADMAP](../16-ROADMAP.md) Phase 6,
  [ADR-0005](0005-dependency-boundaries.md)

## 맥락

Phase 6 에서 Platform 계층(창 · 입력 · 오디오)과 SandboxClient 의 빈 창을 만들었다. 문서 초안이 열어 둔 것이 다섯이었다.

```text
1. Key 의 뜻: 07 은 "바인딩은 scancode 기준, UI 표시는 Key 이름" 이라고만 했다. OS 의 scancode 값은 OS 마다 다르다
   (Windows Set 1, X11 keycode = evdev + 8, macOS kVK_*) — 설정 파일에 그대로 적을 수 없다.
2. 글자 입력: 게임은 WASD 를 키로 받는데, 한글 IME 가 켜져 있으면 키가 VK_PROCESSKEY 로 바뀌고 조합 창이 뜬다.
3. 액션의 수정자: "Z" 바인딩이 Ctrl+Z 에서도 켜지는가. 한 프레임(16 ms) 안에 Ctrl↓ Q↓ Ctrl↑ Q↑ 가 다 오면
   프레임 끝의 수정자 상태로는 "Ctrl+Q" 를 알아볼 수 없다 (Wine + xdotool 시험에서 실제로 놓쳤다).
4. 창이 없는 곳: Linux·macOS 창은 Phase 13·14 다. CI 와 단위 테스트가 앱 루프·상태기계를 돌릴 방법이 필요하다.
5. 오디오의 update(): 07 초안은 인자가 없다. Null 백엔드가 "끝난 목소리" 를 정하려면 시간이 필요하다.
```

## 결정

```text
1. Key = 물리 키 위치 (USB HID / SDL scancode 와 같은 생각). 이름은 미국 QWERTY 의 글자를 빌린다 (AZERTY 의 'A' 자리도 Key::Q).
   설정 파일(settings/input.json)은 Key 이름을 적는다. 각 OS 구현이 자기 scancode → Key 표를 가진다
   (Windows: platform/windows/Win32KeyMap — windows.h 없는 순수 표라 모든 OS 의 단위 테스트가 확인한다).
   KeyDown.scancode 는 OS 원값 그대로 진단용으로만 싣는다.
2. 텍스트 입력은 창마다 켜고 끈다 (IWindow::setTextInputActive). 기본은 끔: IME 컨텍스트를 떼어 키가 그대로 오고,
   TextInput 이벤트도 오지 않는다. 텍스트 필드(ImGui, Phase 8)가 포커스를 얻을 때 켠다.
3. 액션 바인딩의 수정자는 **정확히** 맞아야 한다 ("Z" 는 Ctrl+Z 에서 켜지지 않는다). 바인딩된 키가 수정자 키 자신이면
   그 비트는 뺀다. 수정자는 키가 **눌린 순간**의 것으로 본다 (InputState.keyPressMods) — 계속 누르고 있는 동안은 지금의 것.
   ActionState 는 프레임마다 켜짐/꺼짐 가장자리를 계산하고, 바인딩 키가 이번 프레임에 새로 눌렸으면 지난 프레임에 켜져
   있었어도 pressed 다 (연속 탭).
4. HeadlessWindow: OS 창 없이 IWindow 를 흉내 낸다. inject() 한 이벤트를 다음 pollEvents 에서 내고, 크기·배율·포커스·닫기
   이벤트는 창 상태에도 반영한다. SandboxClient --headless 와 클라이언트 단위 테스트가 쓴다. 창 구현이 없는 OS 의
   createWindow 는 Unsupported 를 돌려준다 (platform/stub).
5. IAudioBackend::update(dtSeconds). 핸들은 세대 핸들(SoundHandle · VoiceId), 목소리 상한을 넘으면 무효 VoiceId.
   NullAudioBackend 가 이 규칙의 기준 구현이다.
```

그 밖에 작게 정한 것:

```text
- 창은 콜백을 받지 않는다. WndProc 는 내부 큐에 쌓고 pollEvents 가 호출자 큐로 옮긴다 (크기 조절 모달 루프 중에도 쌓기만).
- WM_CLOSE 는 창을 닫지 않는다: shouldClose = true + CloseRequested. 창은 소유자가 파괴할 때 닫힌다.
- Windows: GUI 서브시스템 + main (/ENTRY:mainCRTStartup). 로그는 --console (부모 콘솔에 붙거나 새 콘솔),
  출력이 이미 파이프·파일이면(CTest, CLion) 그대로 쓴다. Per-Monitor DPI v2 는 매니페스트(MSVC) + 런타임 보강.
- 기본 키 바인딩은 실행 파일 안의 JSON 이다 (경로 문제 없음). --input <file> 이 액션 단위로 덮어쓴다.
- 빈 창 단계의 프레임 페이싱: FramePacer (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, timeBeginPeriod 안 씀). 최소화 중에는 이벤트 대기.
- 기본 로그 싱크는 줄마다 stderr 를 flush 한다 (Windows CRT 는 파이프로 넘긴 stderr 를 버퍼링한다).
```

## 근거

```text
- 물리 위치는 SDL·GLFW·브라우저(KeyboardEvent.code)가 모두 고른 답이다. 배열마다 다른 문자로 바인딩하면 AZERTY 사용자의
  WASD 가 흩어진다. 글자 자체가 필요한 곳(텍스트 필드)은 TextInput 으로 받으므로 잃는 것이 없다.
- 텍스트 입력을 기본으로 끄는 것은 SDL(SDL_StartTextInput) 과 같다. 한글 사용자에게 "한/영 상태에 따라 단축키가
  안 먹는" 문제가 생기지 않는다.
- 눌린 순간의 수정자: 사람 손으로도 Ctrl 을 16 ms 안에 떼는 일이 있고, 매크로·원격 데스크톱·xdotool 은 늘 그렇다.
  OS 의 KeyDown.mods 대신 InputSystem 의 자기 키 상태로 계산한다 — 이벤트 순서만으로 정해지고 테스트로 재현된다.
- HeadlessWindow 로 클라이언트 단위 테스트 8개가 OS 와 무관하게 돈다. 실제 Win32Window 는 MinGW 교차 빌드 + Wine/Xvfb +
  xdotool 로 키·글자·마우스·휠·더블클릭·Raw Input 캡처·크기 변경·Ctrl+Q 종료까지 이 세션에서 확인했다 (사용자 PC QA 전에).
```

## 결과

- 얻는 것: 배열과 무관한 바인딩, IME 가 게임 입력을 막지 않음, 빠른 조합키도 놓치지 않음, 창 없는 CI 에서 앱 루프 시험.
- 포기하는 것: UI 의 키 이름은 아직 미국 배열 이름이다 (현지 배열 이름 표시는 Phase 8 — GetKeyNameTextW, `scancodeFromKey`).
  IME 조합 중 문자열(preedit)은 이벤트로 오지 않는다 — 시스템 조합 창이 그린다.
- 위험: Win32 표에 없는 특수 키(일부 일본어 키 등)는 Key::Unknown → 바인딩할 수 없다. 필요해지면 Key 를 늘린다.
  Wine 은 Per-Monitor DPI 를 지원하지 않아 실제 DPI 동작은 사용자 PC 수동 QA 로만 확인된다.

## 대안

| 대안                                                      | 기각 사유                                                                  |
|-----------------------------------------------------------|----------------------------------------------------------------------------|
| 가상 키(VK_*)·문자로 바인딩                               | 배열마다 위치가 다르고, IME 가 켜지면 VK_PROCESSKEY 로 바뀐다              |
| OS scancode 값을 설정 파일에 그대로                       | OS 마다 값이 달라 같은 설정 파일을 쓸 수 없다                              |
| 프레임 끝 수정자로 판정                                   | 같은 프레임의 Ctrl↓ Q↓ Ctrl↑ 를 놓친다 (실제로 재현됨)                     |
| 수정자는 "포함" 일치 (Z 가 Ctrl+Z 에서도 켜짐)            | 단축키(Ctrl+Z)를 누를 때 일반 액션(Z)이 함께 실행된다                      |
| 창 콜백(이벤트 버스를 생성자로)                           | 07 2장의 결정 — 순서를 테스트로 재현하기 어렵고 모달 루프 중 재진입 위험   |
| Linux 에서 SandboxClient 를 빌드하지 않기 (Phase 13 까지) | 클라이언트 앱 코드가 Windows 에서만 컴파일·테스트된다 — 경계·회귀를 놓친다 |

## 재검토 조건

Phase 8 ImGui 통합(텍스트 필드 포커스 → setTextInputActive, IME 캐럿 위치 API), Phase 13·14 의 X11/Wayland/Cocoa 키 표,
게임패드 구현 (I5).
