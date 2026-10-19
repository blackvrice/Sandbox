# 07. 플랫폼 — 창 · 입력 · 오디오

> **규범 문서.** OS 계층 추상화를 정합니다. 상태: **Windows 창 · 입력 · ActionMap · Null 오디오 · HeadlessWindow 구현 (Phase 6)**.
> **Linux X11 창 · 입력 구현 (Phase 13B)** — Xlib · evdev 키 표 · XIM · XInput2 Raw · 클립보드 (6.2, [ADR-0033](adr/0033-x11-xlib-evdev-keys-xim-xinput2-raw.md)).
> **Linux Wayland 창 · 입력 구현 (Phase 13C)** — libdecor · xkbcommon · text-input-v3 · 실행 중 창 시스템 선택 (6.2, [ADR-0034](adr/0034-wayland-libdecor-xkbcommon-text-input-v3-runtime-select.md)).
> **macOS Cocoa 창 · 입력 (Phase 14B, 미검증 — macOS 에서 빌드 · 실행해 보지 않았다)** — AppKit · 가상 키 코드 표 · NSTextInputClient ·
> CAMetalLayer (6.3, [ADR-0036](adr/0036-cocoa-window-appkit-objcxx-arc-unverified-build.md)). miniaudio 는 Phase 8 이후 `[계획]`. 결정 근거는 [ADR-0017](adr/0017-physical-keys-event-queue-headless-window.md).

---

## 1. 경계

```text
SandboxPlatform 은 Foundation 에만 의존한다. ECS·Renderer·Network 를 모른다.
  (서드파티: nlohmann/json 을 ActionMap 파싱에만 PRIVATE 로. Windows: user32 · imm32. Linux: libX11 (+ libXi) — 13B,
   libwayland-client · wayland-cursor · xkbcommon · libdecor-0 — 13C. macOS: AppKit · QuartzCore · CoreGraphics — 14B)
플랫폼 헤더(windows.h, X11/*, wayland-*, Cocoa)는 platform/<os>/ 의 .cpp/.mm 안에만.
공개 헤더(platform/common/)는 플랫폼 독립 타입만 노출한다.
```

## 2. IWindow

```cpp
struct WindowDesc {
    std::uint32_t width = 1600, height = 900;
    std::string   title = "Sandbox";
    bool resizable = true, highDpi = true;
    WindowSystem system = WindowSystem::Auto;   // Linux: Auto · X11 · Wayland (13C, 6.2). 다른 OS 는 무시
};

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual void pollEvents(PlatformEventQueue& out) = 0;   // 큐에 쌓기만 한다. 콜백 없음
    virtual void waitEvents(std::chrono::milliseconds timeout) = 0;  // 최소화 시 CPU 절약
    virtual Extent2D framebufferSize() const = 0;          // 픽셀
    virtual Extent2D windowSize() const = 0;               // 논리 단위
    virtual float    contentScale() const = 0;             // DPI 배율
    virtual bool     shouldClose() const = 0;               // WM_CLOSE 뒤 true. 창은 소유자가 파괴할 때 닫힌다
    virtual bool     minimized() const = 0;
    virtual bool     focused() const = 0;
    virtual void     setTitle(std::string_view) = 0;
    virtual void     setCursor(CursorShape) = 0;
    virtual void     setCursorCaptured(bool) = 0;           // 숨김 + 창 안에 가둠 + Raw Input 상대 이동
    virtual bool     cursorCaptured() const = 0;
    virtual void     setTextInputActive(bool) = 0;          // IME. 기본 끔 (ADR-0017)
    virtual bool     textInputActive() const = 0;
    virtual std::string clipboardText() const = 0;
    virtual void     setClipboardText(std::string_view) = 0;
    virtual NativeWindowHandle nativeHandle() const = 0;    // RHI 만 사용
    virtual std::string_view backendName() const = 0;       // "Win32" · "X11" · "Wayland" · "Cocoa" · "Headless"
};
Expected<std::unique_ptr<IWindow>> createWindow(const WindowDesc&);   // platform/<os>/. 창이 없는 OS 는 Unsupported
```

창은 **렌더링을 하지 않습니다** (`clear/display` 없음). 버스·콜백을 생성자로 받지 않고 이벤트 큐를 채웁니다.
코드: `platform/common/Window.hpp`.

**HeadlessWindow** (`platform/common/HeadlessWindow`, ADR-0017): OS 창 없이 IWindow 를 흉내 냅니다. `inject()` 한 이벤트가
다음 `pollEvents` 에서 나오고, Resized · ContentScaleChanged · Focus* · CloseRequested 는 창 상태에도 반영됩니다.
`SandboxClient --headless`, 클라이언트 단위 테스트, 창이 아직 없는 OS 가 씁니다.

## 3. NativeWindowHandle

```cpp
struct NativeWindowHandle {
    enum class Kind : std::uint8_t { None, Win32, Xlib, Xcb, Wayland, Cocoa } kind = Kind::None;
    void* window  = nullptr;   // HWND | (void*)(uintptr_t)Window | xcb_window_t | wl_surface* | CAMetalLayer*
    void* display = nullptr;   // HINSTANCE | Display* | xcb_connection_t* | wl_display* | nullptr
};
```

macOS는 `NSWindow*`가 아니라 **`CAMetalLayer*`** 를 넘깁니다. 레이어는 CocoaWindow(.mm)가 만들어 붙입니다 —
Metal 백엔드가 AppKit을 몰라도 됩니다.

## 4. PlatformEvent

```cpp
using PlatformEvent = std::variant<
    KeyDown{Key key, std::uint32_t scancode, Modifiers mods, bool repeat},
    KeyUp{Key key, std::uint32_t scancode, Modifiers mods},
    TextInput{char32_t codepoint},
    MouseMove{Vec2 position, Vec2 delta},          // 논리 좌표
    MouseButtonDown{MouseButton, Vec2 position, std::uint8_t clicks},
    MouseButtonUp{MouseButton, Vec2 position},
    MouseWheel{Vec2 delta},
    FocusGained{}, FocusLost{},
    Resized{Extent2D framebuffer, Extent2D window},
    ContentScaleChanged{float},
    CloseRequested{}
>;   // (표기 단순화: 실제로는 구조체를 따로 선언)
```

`Key`는 플랫폼 독립 enum이고 **물리 키 위치**입니다 (ADR-0017 — USB HID·SDL scancode 와 같은 생각, 이름은 미국 QWERTY 글자:
`W`, `Digit1`, `LeftCtrl`, `Kp0`, `Lang1`(한/영), `Lang2`(한자) …). AZERTY 의 'A' 자리를 눌러도 `Key::Q` 입니다.
바인딩은 이 이름으로 저장하므로 배열과 무관합니다. 각 OS 가 자기 scancode → Key 표를 가집니다 (Windows: `Win32KeyMap`,
windows.h 없는 순수 표 — 모든 OS 에서 단위 테스트). `KeyDown.scancode`는 OS 원값(진단용, 확장 키는 `0xE0xx`).
`TextInput`은 `setTextInputActive(true)` 동안에만 오고 제어 문자는 오지 않습니다. 좌표는 논리 단위(픽셀 ÷ contentScale).
코드: `platform/common/Key.hpp`, `PlatformEvent.hpp`. 이벤트 큐 `PlatformEventQueue`는 도착 순서를 보존합니다.

## 5. Input System

```text
PlatformEventQueue → InputSystem::beginFrame/consume → InputState
InputState: keys[down/pressed/released/repeated], keyPressMods(눌린 순간의 수정자), mouse{pos, delta, buttons, clicks, wheel},
            text(UTF-8, 이 프레임), focus, gamepads[4] ([계획])
InputSystem::raw()          모든 입력 (ImGui 가 본다)
InputSystem::setCapture(m,k) → downstream()   I1: 가져간 장치를 비운 입력 (하위 소비자가 본다). beginFrame 이 푼다
ActionMap (settings/input.json) + ActionState(프레임마다 pressed/down/released)
소비 순서: ImGui → Editor 툴 → Play 컨트롤 → 카메라
```

**Phase 8C 구현 (I1):** ImGui 는 InputState 가 아니라 이번 프레임의 PlatformEvent 를 그대로 받는다
(`apps/client/ui/ImGuiLayer` — 공식 imgui_impl_win32 없음, OS 무관). 순서: 이벤트 → `ImGui::NewFrame` →
`setCapture(WantCaptureMouse, WantCaptureKeyboard)` → ActionState → 게임 입력. 키보드 내비게이션은 끈다 (패널을 누른 뒤에도
WASD 가 게임으로). 글자 칸이 활성이면 창의 글자 입력(IME)을 켜고, 끝나면 우리가 켠 것만 끈다. 커서 · 클립보드는 창 API
([ADR-0023](adr/0023-imgui-docking-1-92-dynamic-textures-own-platform-layer.md)).

`settings/input.json` 형식 (`platform/common/ActionMap`):

```json
{ "format": "sandbox.input", "version": 1,
  "actions": { "camera.pan.up": ["W", "Up"], "editor.redo": ["Ctrl+Y", "Ctrl+Shift+Z"], "camera.drag": ["MouseMiddle"] } }
```

```text
- 액션 이름: 소문자·숫자·_ 를 . 으로 잇는다. 바인딩: 수정자(Ctrl·Shift·Alt·Super, 순서 무관) + Key 이름 또는 MouseLeft·Right·
  Middle·X1·X2. 모르는 키·중복 바인딩·모르는 최상위 키는 오류 (위치: "파일:/actions/<이름>/<번호>"). version ≠ 1 은 VersionMismatch.
- 수정자는 정확히 일치 ("Z" 는 Ctrl+Z 에서 꺼짐). 수정자 키 자신의 바인딩("LeftShift")은 자기 비트를 뺀다.
  누른 프레임은 **눌린 순간의 수정자**, 계속 누르는 동안은 지금 수정자로 본다 — 한 프레임 안의 Ctrl↓ Q↓ Ctrl↑ 도 "Ctrl+Q".
- ActionState: pressed = 켜짐 && (지난 프레임 꺼짐 || 바인딩 키가 이번 프레임에 새로 눌림), released = 꺼짐 && 지난 프레임 켜짐.
- 같은 바인딩이 두 액션에 있으면 conflicts() 가 보고한다 (로드는 성공 — 앱이 경고).
- 기본 바인딩은 SandboxClient 실행 파일 안 (apps/client/DefaultInput). --input <file> 이 액션 단위로 덮어쓴다 (빈 배열 = 해제).
  [계획] 사용자 경로 platformPaths()/settings/input.json 자동 로드 · 저장 — Phase 8.
```

| 규칙 | 내용                                                                                                                                                                                         |
|------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| I1   | ImGui가 WantCaptureMouse/Keyboard면 그 프레임의 하위 소비자에게 해당 입력을 주지 않는다                                                                                                      |
| I2   | Simulation은 InputState를 보지 않는다. 입력은 SimCommand로만 시뮬레이션에 들어간다                                                                                                           |
| I3   | 포커스를 잃으면 눌린 키를 모두 released로 만든다 (끈적이는 키 방지)                                                                                                                          |
| I4   | 텍스트 입력은 KeyDown과 별개 이벤트 (IME 조합 지원)                                                                                                                                          |
| I5   | Gamepad: 인터페이스만 Phase 6에 정의 (`platform/common/Gamepad.hpp` — IGamepadSource, GamepadState). 구현은 콘텐츠가 요구할 때 (Windows XInput→GameInput, Linux evdev, macOS GameController) |

## 6. 플랫폼별 구현

### 6.1 Windows (Phase 6 — 구현: `platform/windows/Win32Window.cpp`, `Win32Platform.cpp`)

```text
- RegisterClassExW / CreateWindowExW, 유니코드 API 만
- Per-Monitor DPI Awareness v2 (매니페스트) → WM_DPICHANGED 처리
- 메시지 루프: PeekMessageW 로 큐 비우기 → PlatformEvent 변환
- 마우스: WM_MOUSEMOVE(절대) + Raw Input(상대, 캡처 모드)
- 텍스트: WM_CHAR (서로게이트 쌍 결합), IME: WM_IME_*
- 크기 변경 중 렌더: WM_SIZE 이벤트만 쌓고, 실제 swapchain resize 는 렌더 루프에서
- 고해상도 대기: CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) — timeBeginPeriod 사용 안 함
- WinMain 대신 main (CMake WIN32 서브시스템 + 콘솔 옵션 --console)
- 명령줄 인자는 UTF-16 명령줄(GetCommandLineW · CommandLineToArgvW)에서 UTF-8 로 다시 만든다 — main 의 argv 는 시스템 코드
  페이지(cp949)라 한글 이름 · 경로가 깨진다 (foundation/io/Console::utf8Arguments, Phase 10B. 모든 실행 파일)
```

구현 세부 (Phase 6):

```text
- DPI: 매니페스트(apps/client/SandboxClient.manifest, MSVC) + 런타임 SetProcessDpiAwarenessContext(v2 → 안 되면 v1).
  창은 96 DPI 크기로 만든 뒤 GetDpiForWindow · AdjustWindowRectExForDpi 로 다시 맞춘다. highDpi=false 면 배율 1 고정.
- 키: 스캔 코드(+확장 비트) → Win32KeyMap. 가상 키가 이기는 키: VK_HANGUL(Lang1) · VK_HANJA(Lang2) · Pause · NumLock.
  AltGr 의 가짜 왼쪽 Ctrl 은 버린다. 두 Shift 중 하나를 떼면 실제로 떼어진 쪽만 KeyUp. PrintScreen 은 KEYUP 만 와서 둘 다 만든다.
  Alt 단독의 메뉴 진입(SC_KEYMENU)·Alt+글자 경고음(WM_SYSCHAR)은 막고, Alt+F4 는 DefWindowProc 에 맡긴다.
- 마우스: 버튼을 누르면 SetCapture (창 밖에서 떼도 받는다). CS_DBLCLKS → clicks = 2. 휠 한 칸 = 1.0 (WHEEL_DELTA 120).
  캡처: ClipCursor + 커서 숨김 + Raw Input(usage 1/2) 상대 이동, 원격 데스크톱의 절대 좌표 Raw Input 도 차이로 바꾼다.
- 텍스트: WM_CHAR UTF-16 서로게이트 결합. 텍스트 입력을 끄면 ImmAssociateContextEx(hwnd, nullptr, 0) 로 IME 를 뗀다.
- 포커스를 잃으면 ClipCursor 해제·SetCapture 해제 + FocusLost (I3).
- 렌더러가 없는 동안 창 클래스 배경 브러시로 어두운 회색을 칠한다 (Phase 7 에서 nullptr).
- 콘솔: platform::attachConsole() — 출력이 이미 파이프·파일·콘솔이면 그대로, 아니면 부모 콘솔에 붙거나 새 콘솔.
```

### 6.2 Linux (Phase 13)

```text
13B X11: Xlib 창 + XInput2(마우스) + XKB(키) + XIM(텍스트), Vulkan 표면은 VK_KHR_xlib_surface
13C Wayland: wl_compositor + libdecor(xdg-shell · 창 장식) + wl_seat + xkbcommon + text-input-v3, 표면 VK_KHR_wayland_surface
(13A 는 창 없이 Vulkan 백엔드 · 헤드리스 시험 — 06 5.2, ADR-0032)
런타임 선택: WindowDesc.system = Auto · X11 · Wayland (SandboxClient --platform auto|x11|wayland).
             Auto = WAYLAND_DISPLAY 가 있으면 Wayland, 만들지 못하면 경고 후 X11 (Xwayland). 강제한 쪽은 대신하지 않는다.
             IWindow::backendName() = 실제로 고른 것 ("창 Wayland" 로그), --version = 빌드에 든 것 ("window Wayland · X11")
```

구현 세부 (Phase 13B — `platform/linux/X11Window.cpp`, `LinuxPlatform.cpp`, `X11KeyMap`, ADR-0033):

```text
- 빌드: SBX_ENABLE_X11 (Linux 기본 ON) — libx11-dev 필요(없으면 구성 오류 + 패키지 안내), libxi-dev 가 있으면 XInput2.
  OFF 면 platform/stub (창 없음, --headless 만). XInitThreads (Vulkan WSI 가 같은 연결을 다른 스레드에서 쓴다).
- 로캘: LC_CTYPE = 사용자 로캘, UTF-8 이 아니면 C.UTF-8. XSetLocaleModifiers("") — XMODIFIERS(@im=ibus · fcitx) 를 따른다.
- 창: XCreateWindow (배경 없음 — 렌더러가 칠한다), WM_DELETE_WINDOW → CloseRequested, WM_CLASS sandbox/Sandbox, 최소
  320×200 (resizable=false 면 고정). 크기 = ConfigureNotify → Resized. 최소화 = UnmapNotify · _NET_WM_STATE_HIDDEN.
  DPI = Xft.dpi ÷ 96 (없으면 1, highDpi=false 면 1). 실행 중 바뀜(XSETTINGS)은 [계획].
- 키: keycode = evdev 코드 + 8 → X11KeyMap (물리 위치, Xlib 없이 순수 표 — KEY_HANGEUL = Lang1, KEY_HANJA = Lang2).
  자동 반복 = XkbSetDetectableAutoRepeat (지원하지 않으면 같은 시각의 Release + Press 쌍을 반복으로). 수정자 = state
  (+ 수정자 키 자신의 비트를 누름에서 더하고 뗌에서 뺀다).
- 텍스트: XIM + XIC (조합 창은 입력기의 것 — PreeditNothing), Xutf8LookupString. 텍스트 입력을 끄면 XUnsetICFocus + 조합 버림.
  입력기가 보낸 확정 글자(keycode 0)는 TextInput 만. XIM 이 없으면 XLookupString (Latin-1).
- 마우스: 버튼 1 · 2 · 3 = 왼쪽 · 가운데 · 오른쪽, 4 · 5 = 휠 위 · 아래, 6 · 7 = 왼쪽 · 오른쪽, 8 · 9 = X1 · X2. 더블클릭은 직접
  판정 (같은 버튼 · 500 ms · 4 px 안, 세 번째는 다시 1). 캡처 = XGrabPointer(창 안에 가둠 · 빈 커서) + XInput2 RawMotion
  (가속 없음). XInput2 가 없으면 커서를 가운데로 되돌리며 잰다. 포커스를 잃으면 잡기를 풀고 다시 얻으면 다시 잡는다.
- 커서: cursorfont (XCreateFontCursor — 대각선 크기 화살표는 모서리 모양), 숨김 = 빈 비트맵. Xcursor 테마는 [계획].
- 클립보드: CLIPBOARD + UTF8_STRING. 우리가 가진 동안 SelectionRequest(TARGETS · UTF8_STRING · STRING)에 답한다. 읽기는
  XConvertSelection → SelectionNotify 를 250 ms 까지. INCR(큰 글) · 창을 닫은 뒤 남기기(SAVE_TARGETS)는 [계획].
- 제목: _NET_WM_NAME (UTF8_STRING) + WM_NAME (Xutf8SetWMProperties).
- 포커스를 잃으면(키보드 잡기 때문의 NotifyGrab · NotifyUngrab 은 빼고) FocusLost (I3), IC 포커스 해제.
- 기다림: waitEvents = poll(ConnectionNumber). PreciseSleeper = sleep_until (Linux hrtimer).
```

구현 세부 (Phase 13C — `platform/linux/WaylandWindow.cpp`, `WaylandInput`, `LinuxWindows.hpp`, ADR-0034):

```text
- 빌드: SBX_ENABLE_WAYLAND (Linux 기본 ON) — pkg-config 로 wayland-client · wayland-cursor · xkbcommon · libdecor-0, 확장
  프로토콜 XML(text-input-v3 · relative-pointer · pointer-constraints)은 wayland-scanner 로 빌드 때 생성 (cmake/SbxWayland.cmake).
  없으면 구성 오류 + apt 안내. X11 · Wayland 둘 다 끄면 platform/stub (--headless 만).
- 창: libdecor (서버 장식이 있으면 서버 틀, 없으면 플러그인 틀 — GNOME). app_id sandbox, 최소 320×200 (resizable=false 면
  고정). configure → Resized (크기가 없으면 떠 있던 크기 — 최대화를 풀 때). close → CloseRequested. 최소화 = libdecor
  SUSPENDED 만 (Wayland 는 최소화를 알려 주지 않는다). 렌더러 전에는 wl_shm 어두운 버퍼 (없으면 창이 지도에 오르지 않는다).
- 배율: 창이 걸친 출력의 wl_output.scale 중 가장 큰 정수 → set_buffer_scale, framebuffer = 논리 × 배율 (highDpi=false 면 1).
  바뀌면 ContentScaleChanged + Resized. 분수 배율은 [계획].
- 키: wl_keyboard evdev 코드 → X11KeyMap (+ 8, X11 과 같은 Key). 수정자 · 글자 = xkbcommon (서버가 준 키맵), 데드 키 ·
  Compose = 로캘의 compose 표. 자동 반복은 클라이언트가 repeat_info 로 (수정자 키 제외, 밀리면 다섯 개까지, 포커스를 잃으면 멈춤).
- 텍스트: 켜져 있으면 xkb 글자 + text-input-v3 (enable · content type · commit). 입력기 확정 글자 = commit_string → done 에서
  TextInput. 조합 중 글자(preedit)는 입력기 창이 그린다.
- 마우스: 장식 표면(제목 줄 · 테두리)의 enter/motion/button 은 빼고 우리 표면 것만. 휠 = axis_discrete 또는 axis ÷ 10 (frame
  마다 한 MouseWheel, 아래 = -). 더블클릭 = X11 과 같은 판정. 캡처 = pointer-constraints lock(PERSISTENT) + relative-pointer
  (가속 없는 값, 캡처 중 위치는 묶어 둔다). 서버에 없으면 커서 숨김만.
- 커서: wl_cursor 테마 (XCURSOR_THEME · XCURSOR_SIZE × 배율, CSS 이름 → X 이름 대체), 숨김 · 캡처 = 커서 표면 없음.
- 클립보드: wl_data_device. 쓰기 = data_source(text/plain;charset=utf-8 · UTF8_STRING · text/plain)를 마지막 입력 serial 로
  set_selection (입력이 없으면 서버가 거절할 수 있다). 우리가 가진 동안은 바로 돌려준다. 읽기 = receive + 파이프 250 ms.
- 기다림: waitEvents = libdecor_dispatch(시간 제한 — 다음 반복 시각까지). 연결이 끊기면 CloseRequested.
```

### 6.3 macOS (Phase 14)

```text
- CocoaWindow.mm: NSApplication(메인 스레드), NSWindow, NSView(wantsLayer, makeBackingLayer=CAMetalLayer)
- NSEvent → PlatformEvent, NSTextInputClient 로 IME
- Retina: backingScaleFactor → contentScale, CAMetalLayer.contentsScale 동기화
- 창·이벤트는 메인 스레드 전용. Render 스레드 분리 시에도 이 규칙 유지
```

구현 세부 (Phase 14B — `platform/macos/CocoaWindow.mm`, `MacKeyMap`, `MacPlatform.cpp`, ADR-0036). **미검증**: macOS 에서 빌드 ·
실행해 보지 않았다 (개발 환경에 macOS 가 없다 — GNUstep 헤더로 문법 · 형 · ARC 만 검사). 첫 확인은 CI 의 macOS 잡 · MANUAL-QA 14B.

```text
- 빌드: APPLE 이면 OBJCXX 를 켜고 CocoaWindow.mm 만 -fobjc-arc, AppKit · QuartzCore · CoreGraphics. 배포 목표 macOS 13.0
  (CMAKE_OSX_DEPLOYMENT_TARGET). 정의 SBX_HAS_COCOA (PUBLIC). 공개 헤더(MacWindows.hpp)에 ObjC 타입 없음.
- 앱: NSApplication · ActivationPolicyRegular (번들 없는 실행 파일도 Dock · 키보드 포커스), 앱 메뉴 "종료"(⌘Q) = 키 창의
  performClose: → CloseRequested (바로 끝내지 않는다).
- 창: NSWindow (Titled · Closable · Miniaturizable, resizable 이면 Resizable, 탭 끔, 최소 320×200), 가운데. windowShouldClose =
  NO + CloseRequested (창은 소유자가 파괴할 때 닫힌다). windowDidResize → Resized, windowDidChangeBackingProperties →
  ContentScaleChanged (+ Resized), (de)miniaturize → minimized, become/resignKey → FocusGained/Lost.
- 좌표: 포인트 = 논리 단위, 뷰는 flipped (왼쪽 위 원점). framebuffer = convertRectToBacking (highDpi=false 면 포인트 그대로).
  CAMetalLayer 의 contentsScale · drawableSize 를 창이 맞춘다. NativeWindowHandle{Cocoa, window = CAMetalLayer*}.
- 키: keyCode(kVK_*) → MacKeyMap (물리 위치). 반복 = isARepeat. 수정자 키 = flagsChanged + 장치별 비트(왼 · 오른 구별),
  Caps Lock = 바뀔 때마다 누름 + 뗌. ⌘ 를 누른 채 뗀 keyUp 은 pollEvents 가 키 창에 직접 넘긴다. Command = Super.
- 글자: 켜져 있을 때만 interpretKeyEvents → insertText → TextInput (제어 문자 · 기능 키 문자 0xF700~0xF8FF 제외).
  setMarkedText(조합 중)는 받아 두기만, 끄면 버린다 ([계획] UI 에 그리기). doCommandBySelector 는 무시 (경고음 없음).
- 마우스: 왼쪽 · 오른쪽 · other(buttonNumber 2 · 3 · 4 = 가운데 · X1 · X2). 더블클릭 = clickCount == 2 (세 번째 1).
  휠 = scrollingDelta (정밀 장치 ÷ 10, X 는 부호를 뒤집어 오른쪽 +, 위 = +). 이동 = 추적 영역(NSTrackingMouseMoved).
- 캡처: 커서 숨김 + 창 가운데로 옮기고 CGAssociateMouseAndMouseCursorPosition(false) + 이벤트 deltaX/Y (가속 섞임 —
  GCMouse 는 [계획]). 포커스를 잃으면 풀고 다시 얻으면 잡는다. 커서 숨김(NSCursor hide)은 횟수를 세므로 한 번씩만.
- 커서: NSCursor (arrow · IBeam · pointingHand · openHand · resizeLeftRight · resizeUpDown · operationNotAllowed). 대각선
  크기 커서는 공개 API 가 없어 가로 화살표로.
- 클립보드: NSPasteboard 의 문자열 (generalPasteboard). 제목: setTitle (UTF-8 → NSString).
- 기다림: pollEvents = nextEventMatchingMask(distantPast) → sendEvent, waitEvents = 제한 시간까지 기다려 꺼낸 이벤트를 큐 앞에
  되돌린다. PreciseSleeper = sleep_until.
```

## 7. Audio

```cpp
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    virtual bool init(const AudioDesc&) = 0;
    virtual SoundHandle load(const SoundAsset&) = 0;
    virtual VoiceId play(SoundHandle, const PlayParams&) = 0;    // volume, pitch, position(2D 패닝), loop
    virtual void stop(VoiceId) = 0;
    virtual void setListener(Vec2 position) = 0;
    virtual void setBusVolume(AudioBus, float) = 0;               // Master / Sfx / Ambient / Ui
    virtual void update(float dtSeconds) = 0;                   // 끝난(반복 아님) 목소리를 거둔다 (ADR-0017)
};
```

코드: `platform/common/Audio.hpp`. `SoundHandle`·`VoiceId`는 세대 핸들 — 끝나거나 멈춘 목소리의 옛 id 는 무효입니다.
목소리 상한(`AudioDesc.maxVoices`)을 넘거나 pitch ≤ 0 이면 무효 VoiceId. 버스 볼륨은 0..1 로 자르고 NaN 은 0.
`NullAudioBackend`(`platform/audio/`)는 소리를 내지 않지만 이 규칙의 기준 구현입니다 (SandboxClient 가 지금 씁니다).

```text
구현 순서: NullAudioBackend (Phase 6) → MiniaudioBackend (Phase 8 이후)
SFML Audio 는 거치지 않는다 (요구사항상 임시 허용이지만 miniaudio 가 같은 비용으로 최종 형태).
경로: Server EventStream → 복제 Event → Client AudioExtraction(이벤트 → 사운드 매핑, 콘텐츠 데이터)
      → AudioEvent 큐 → Audio 스레드 → IAudioBackend
렌더러를 꺼도 오디오는 동작한다 (RTS 의 RenderQueue 경유 구조를 반복하지 않음).
```

## 8. 기타 플랫폼 서비스

| 서비스             | 인터페이스                               | 비고                                                                                                       |
|--------------------|------------------------------------------|------------------------------------------------------------------------------------------------------------|
| 고해상도 시간      | `foundation/time` (`steady_clock` 래핑)  | 시뮬레이션은 사용 금지. `[계획]` — 지금은 std::chrono 직접                                                 |
| 프레임 페이싱      | `platform::FramePacer`, `PreciseSleeper` | Phase 6 구현. 한 주기 넘게 밀리면 따라잡지 않는다                                                          |
| 콘솔               | `platform::attachConsole()`              | Phase 6 구현 (Windows GUI 실행 파일)                                                                       |
| 파일 대화상자      | `IFileDialog` `[후속]`                   | 초기에는 ImGui 내부 파일 브라우저                                                                          |
| 사용자 데이터 경로 | `platformPaths()` `[계획]` Phase 8       | Windows `%APPDATA%/Sandbox`, Linux `$XDG_DATA_HOME/sandbox`, macOS `~/Library/Application Support/Sandbox` |
| 크래시 덤프        | `[후속]`                                 | Windows MiniDumpWriteDump                                                                                  |

## 9. 필수 테스트

```text
- InputSystem 단위 테스트: 이벤트 시퀀스 → pressed/released/down 정확성, 포커스 상실 시 해제, ImGui 캡처 게이팅
- ActionMap 파싱·충돌 검출
- 수동 체크리스트 (Phase 6): 창 생성·리사이즈·최소화·DPI 변경(다른 모니터로 이동)·한글 IME 입력·Alt+Tab
```

구현된 것 (Phase 6): `tests/unit/platform/` — Key 이름 왕복, Win32 키 표(전 키 왕복·한/영·확장 키), InputSystem(탭·반복·I1·I3·I4),
HeadlessWindow, ActionMap(형식 오류 위치·정확 일치·눌린 순간 수정자·병합·충돌·가장자리), NullAudioBackend.
`tests/unit/client/` — 앱 상태기계, HeadlessWindow 로 돈 프레임 루프. 수동 체크리스트는 [qa/MANUAL-QA](qa/MANUAL-QA.md) Phase 6.

구현된 것 (Phase 13B): X11 키 표(전 키 왕복 · evdev + 8 · 한/영 · 한자 — 모든 OS 의 `platform` 스위트), SandboxTests 스위트
`x11`(`tests/unit/platform/test_x11_window.cpp` — 실제 X 서버에 창을 만들고 XSendEvent 합성 이벤트: 닫기 요청 · 키 · 자동 반복 ·
수정자 · 텍스트 입력 켬/끔 · 버튼 · 더블클릭 · 휠 · X1 · 이동 · 크기 바꾸기 · 포커스 · 클립보드 · UTF-8 제목 · 커서 · 캡처).
CTest `platform_x11` · `client_x11_world_smoke` (xvfb-run 이 있으면 가상 화면, 없으면 DISPLAY — 없으면 케이스가 건너뛴다).
수동: [qa/MANUAL-QA](qa/MANUAL-QA.md) Phase 13B (한글 입력기 · 창 관리자 · 실제 GPU).

구현된 것 (Phase 13C): `platform` 스위트에 Wayland 입력 해석(`test_wayland_input.cpp` — 서버 없이 xkb 키맵 이름으로: evdev →
Key · scancode = evdev + 8 · 텍스트 켬/끔 · 제어 문자 · 수정자 비트 · 배열(fr) · 데드 키(us intl) · 자동 반복 시각 · 수정자 키는
반복 없음 · 포커스 상실 · 키맵 없음 · 버튼 코드 · 더블클릭)과 강제 Wayland 는 X11 로 대신하지 않음, 창 시스템 이름 왕복.
스위트 `wayland`(`test_wayland_window.cpp` — 실제 컴포지터: 핸들 · 논리 크기 · 제목 · 클립보드 · 커서 · 캡처 · 텍스트 입력 ·
두 연결). Wayland 는 입력을 합성할 수 없어 키 · 마우스는 사람 손 시험으로. CTest `platform_wayland` ·
`client_wayland_world_smoke` (`tools/wayland/run_headless.sh` = weston headless — 없으면 WAYLAND_DISPLAY, 없으면 건너뛴다).
수동: [qa/MANUAL-QA](qa/MANUAL-QA.md) Phase 13C (GNOME · KDE · 한글 입력기 · 배율 2 · 실제 GPU).

구현된 것 (Phase 14B, 미검증): Mac 키 표(`platform` 스위트, 모든 OS — kVK 코드 · 왕복 · 장치별 수정자 비트 · Caps Lock · 수정자
집합), 스위트 `cocoa`(`tests/unit/platform/test_cocoa_window.mm`, macOS 만 — 실제 AppKit 창의 뷰에 NSEvent 를 넘겨 키 ·
반복 · flagsChanged · 글자 켬/끔 · 조합 버림 · 버튼 · clickCount · other 버튼 · 이동 · 휠 · 닫기 요청 · 크기 · 클립보드 · 커서 ·
캡처). CTest `platform_cocoa`. 수동: [qa/MANUAL-QA](qa/MANUAL-QA.md) Phase 14B.
