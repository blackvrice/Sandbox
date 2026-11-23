# ADR-0170. Linux 렌더 표면 — X11 창은 같은 연결의 XCB 로, 자동 창 선택은 X11 (Xwayland) 먼저 · Wayland 는 렌더러 없이

- 상태: **Accepted** · 날짜: 2026-11-23
- 관련: [ADR-0168](0168-graphics-sdk-rhi-backend.md) (Graphics SDK 단일 백엔드), [ADR-0033](0033-x11-xlib-evdev-keys-xim-xinput2-raw.md) (X11 창),
  [ADR-0034](0034-wayland-libdecor-xkbcommon-text-input-v3-runtime-select.md) (Wayland 창), [07-PLATFORM](../07-PLATFORM.md) 6.2장

## 맥락

Graphics SDK 는 Linux 에서 **XCB 표면만** 만든다 (`gfx::SurfaceKind::Xcb` — Xlib · Wayland 표면 없음, SDK-REQ-008). 그런데
Sandbox 의 Linux 창은 X11 = `NativeWindowHandle::Kind::Xlib` (Display*), Wayland = `Kind::Wayland` 를 주므로 ADR-0168 뒤로 Linux
클라이언트 · Studio 는 **창에 그릴 수 없었다** (스왑체인 Unsupported → "렌더러 없이 계속합니다"). 리팩터링 전 자체 Vulkan 백엔드는
둘 다 그렸다 — 회귀다. 창 스모크 시험 (client_x11 · client_wayland_world_smoke) 은 프레임 수만 봐 이를 잡지 못했다.

## 결정

1. **X11 = 같은 연결의 XCB.** `NativeWindowHandle` 에 `xcbConnection` (Xlib 창의 `XGetXCBConnection(Display*)`) 을 더한다. X 창
   id 는 `xcb_window_t` 와 같다. 어댑터 (`GraphicsSdkTypes::mapSurface`) 는 Xlib + xcbConnection 을 SDK 의 Xcb 표면으로 바꾼다.
   이벤트는 그대로 Xlib 이 받는다 (Vulkan WSI 의 xcb 표면은 같은 연결을 쓴다). libX11-xcb 가 필요하다 (`libx11-xcb-dev`).
2. **자동 창 선택은 X 서버 먼저.** `--platform auto` 는 `DISPLAY` 가 있으면 (Wayland 세션의 Xwayland 포함) X11, X 서버가 없는
   Wayland 세션이면 Wayland. 예전 규칙 (WAYLAND_DISPLAY 면 Wayland 먼저) 은 렌더러가 그릴 수 없는 창을 골랐다.
3. **Wayland 창은 렌더러 없이.** `--platform wayland` 로 강제하면 창 · 입력은 되고 렌더러는 이유 ("Graphics SDK 에 Wayland 표면이
   없습니다 — X11 (Xwayland) 창으로 그린다") 와 함께 빠진다. Wayland 표면은 SDK 의 일 (SDK-REQ-008) — SDK 가 지원하면 이 ADR 을
   Superseded 로 하고 자동 선택을 되돌린다.
4. **스모크 시험이 그리기를 본다.** client_x11_world_smoke (lavapipe 가 있을 때) 는 "GraphicsSDK 스왑체인" 줄을 요구하고 "렌더러
   없이 계속합니다" 면 실패. test_x11_window 는 xcbConnection 이 있는지 본다.

## 결과

- Linux X11 (와 Wayland 세션의 Xwayland) 에서 다시 창에 그린다. 순수 Wayland (X 서버 없음) 에서는 렌더러가 없다 — 알려진 제약.
- X11 창은 libX11-xcb 에 링크한다 (CI · 15-BUILD 의 apt 목록).
