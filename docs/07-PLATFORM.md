# 07. 플랫폼 — 창 · 입력 · 오디오

> **규범 문서.** OS 계층 추상화를 정합니다. 상태: 전부 `[계획]` — Windows Phase 6, Linux Phase 13, macOS Phase 14.

---

## 1. 경계

```text
SandboxPlatform 은 Foundation 에만 의존한다. ECS·Renderer·Network 를 모른다.
플랫폼 헤더(windows.h, X11/*, wayland-*, Cocoa)는 platform/<os>/ 의 .cpp/.mm 안에만.
공개 헤더(platform/common/)는 플랫폼 독립 타입만 노출한다.
```

## 2. IWindow

```cpp
struct WindowDesc {
    std::uint32_t width = 1600, height = 900;
    std::string   title = "Sandbox";
    bool resizable = true, highDpi = true;
};

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual void pollEvents(PlatformEventQueue& out) = 0;   // 큐에 쌓기만 한다. 콜백 없음
    virtual void waitEvents(std::chrono::milliseconds timeout) = 0;  // 최소화 시 CPU 절약
    virtual Extent2D framebufferSize() const = 0;          // 픽셀
    virtual Extent2D windowSize() const = 0;               // 논리 단위
    virtual float    contentScale() const = 0;             // DPI 배율
    virtual bool     shouldClose() const = 0;
    virtual bool     minimized() const = 0;
    virtual void     setTitle(std::string_view) = 0;
    virtual void     setCursor(CursorShape) = 0;
    virtual void     setCursorCaptured(bool) = 0;
    virtual void     setTextInputActive(bool) = 0;          // IME
    virtual std::string clipboardText() const = 0;
    virtual void     setClipboardText(std::string_view) = 0;
    virtual NativeWindowHandle nativeHandle() const = 0;    // RHI 만 사용
};
std::unique_ptr<IWindow> createWindow(const WindowDesc&);   // platform/<os>/ 에서 구현
```

창은 **렌더링을 하지 않습니다** (`clear/display` 없음). 버스·콜백을 생성자로 받지 않고 이벤트 큐를 채웁니다.

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

`Key`는 플랫폼 독립 enum입니다 (RTS `core/model/Key.hpp` 계승: 알파벳·숫자·기호·F1~F24·방향·수정자·키패드).
바인딩은 레이아웃 독립을 위해 **scancode 기준**으로 저장하고, UI 표시만 Key 이름을 씁니다.

## 5. Input System

```text
PlatformEventQueue → InputSystem::beginFrame/consume → InputState
InputState: keys[down/pressed/released], mouse{pos, delta, buttons, wheel}, text(UTF-8 버퍼), focus
ActionMap (settings/input.json): "camera.pan" = [MouseMiddle drag, W/A/S/D], "editor.delete" = [Delete], ...
소비 순서: ImGui → Editor 툴 → Play 컨트롤 → 카메라
```

| 규칙 | 내용 |
|---|---|
| I1 | ImGui가 WantCaptureMouse/Keyboard면 그 프레임의 하위 소비자에게 해당 입력을 주지 않는다 |
| I2 | Simulation은 InputState를 보지 않는다. 입력은 SimCommand로만 시뮬레이션에 들어간다 |
| I3 | 포커스를 잃으면 눌린 키를 모두 released로 만든다 (끈적이는 키 방지) |
| I4 | 텍스트 입력은 KeyDown과 별개 이벤트 (IME 조합 지원) |
| I5 | Gamepad: 인터페이스만 Phase 6에 정의. 구현은 콘텐츠가 요구할 때 (Windows XInput→GameInput, Linux evdev, macOS GameController) |

## 6. 플랫폼별 구현

### 6.1 Windows (Phase 6)

```text
- RegisterClassExW / CreateWindowExW, 유니코드 API 만
- Per-Monitor DPI Awareness v2 (매니페스트) → WM_DPICHANGED 처리
- 메시지 루프: PeekMessageW 로 큐 비우기 → PlatformEvent 변환
- 마우스: WM_MOUSEMOVE(절대) + Raw Input(상대, 캡처 모드)
- 텍스트: WM_CHAR (서로게이트 쌍 결합), IME: WM_IME_*
- 크기 변경 중 렌더: WM_SIZE 이벤트만 쌓고, 실제 swapchain resize 는 렌더 루프에서
- 고해상도 대기: CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) — timeBeginPeriod 사용 안 함
- WinMain 대신 main (CMake WIN32 서브시스템 + 콘솔 옵션 --console)
```

### 6.2 Linux (Phase 13)

```text
13a X11: Xlib 창 + XInput2(마우스) + XKB(키) + XIM(텍스트), Vulkan 표면은 VK_KHR_xlib_surface
13b Wayland: wl_compositor + xdg-shell + libdecor(창 장식) + wl_seat + xkbcommon + text-input-v3
런타임 선택: WAYLAND_DISPLAY 있으면 Wayland, 실패 시 X11 (--platform=x11|wayland 로 강제)
```

### 6.3 macOS (Phase 14)

```text
- CocoaWindow.mm: NSApplication(메인 스레드), NSWindow, NSView(wantsLayer, makeBackingLayer=CAMetalLayer)
- NSEvent → PlatformEvent, NSTextInputClient 로 IME
- Retina: backingScaleFactor → contentScale, CAMetalLayer.contentsScale 동기화
- 창·이벤트는 메인 스레드 전용. Render 스레드 분리 시에도 이 규칙 유지
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
    virtual void update() = 0;
};
```

```text
구현 순서: NullAudioBackend (Phase 6) → MiniaudioBackend (Phase 8 이후)
SFML Audio 는 거치지 않는다 (요구사항상 임시 허용이지만 miniaudio 가 같은 비용으로 최종 형태).
경로: Server EventStream → 복제 Event → Client AudioExtraction(이벤트 → 사운드 매핑, 콘텐츠 데이터)
      → AudioEvent 큐 → Audio 스레드 → IAudioBackend
렌더러를 꺼도 오디오는 동작한다 (RTS 의 RenderQueue 경유 구조를 반복하지 않음).
```

## 8. 기타 플랫폼 서비스

| 서비스 | 인터페이스 | 비고 |
|---|---|---|
| 고해상도 시간 | `foundation/time` (`steady_clock` 래핑) | 시뮬레이션은 사용 금지 |
| 파일 대화상자 | `IFileDialog` `[후속]` | 초기에는 ImGui 내부 파일 브라우저 |
| 사용자 데이터 경로 | `platformPaths()` | Windows `%APPDATA%/Sandbox`, Linux `$XDG_DATA_HOME/sandbox`, macOS `~/Library/Application Support/Sandbox` |
| 크래시 덤프 | `[후속]` | Windows MiniDumpWriteDump |

## 9. 필수 테스트

```text
- InputSystem 단위 테스트: 이벤트 시퀀스 → pressed/released/down 정확성, 포커스 상실 시 해제, ImGui 캡처 게이팅
- ActionMap 파싱·충돌 검출
- 수동 체크리스트 (Phase 6): 창 생성·리사이즈·최소화·DPI 변경(다른 모니터로 이동)·한글 IME 입력·Alt+Tab
```
