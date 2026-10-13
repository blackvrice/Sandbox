# ADR-0033. Linux 첫 창은 Xlib 으로 만들고, 키는 evdev keycode 표, 글자는 XIM, 캡처는 XInput2 Raw 로 받는다

- 상태: **Accepted** · 날짜: 2026-10-13
- 관련: [07-PLATFORM](../07-PLATFORM.md) 2 · 4 · 6.2 · 9장, [06-RENDERING](../06-RENDERING.md) 5.2,
  [ADR-0017](0017-physical-keys-event-queue-headless-window.md), [ADR-0032](0032-vulkan-volk-timeline-binary-present-d3d-parity.md),
  [16-ROADMAP](../16-ROADMAP.md) Phase 13

## 맥락

13B = Linux 의 첫 창. IWindow · PlatformEvent · InputSystem 계약(ADR-0017)은 Win32 로 굳었다 — 물리 키, 텍스트 입력 분리(I4),
포커스를 잃으면 모두 떼기(I3), 논리 좌표, 캡처 중 가속 없는 상대 이동, UTF-8 클립보드. Vulkan 백엔드(13A)는 표면만 기다린다.
Wayland(13C)가 뒤따르므로 X11 은 Xwayland 위에서도 돌아야 한다. 정할 것:

```text
1. Xlib 인가 XCB 인가, 무엇을 링크하나 (빌드 머신 요구), 창이 없는 Linux 빌드
2. 물리 키를 무엇으로 정하나 (keycode · keysym · xkbcommon)
3. 글자 · 한글 입력 (입력기)
4. 마우스 캡처의 상대 이동, 더블클릭
5. 클립보드 · 제목 · DPI
6. Vulkan 표면이 Xlib 타입을 어떻게 보나
7. 시험 (화면 없는 CI · 클라우드)
```

## 결정

```text
1. Xlib (libX11 링크, find_package(X11)) + 있으면 XInput2 (libXi, SBX_HAS_XINPUT2). 옵션 SBX_ENABLE_X11 (Linux 기본 ON — 개발
   파일이 없으면 구성 오류와 함께 apt 패키지 안내, OFF 면 platform/stub 창 = --headless 만). SBX_HAS_X11 은 PUBLIC 정의.
   XInitThreads (Vulkan WSI 가 같은 연결을 다른 스레드에서 쓴다). 구현 platform/linux/X11Window.cpp · LinuxPlatform.cpp.
2. keycode = evdev 코드 + 8 (Xorg · Xwayland · Xvfb 공통) → 표 하나 (platform/linux/X11KeyMap, Xlib 없이 순수 — 모든 OS 의 단위
   시험이 확인). keysym · xkb 맵은 쓰지 않는다 — 배열과 무관한 물리 위치가 Key 의 뜻이다. KEY_HANGEUL(130) = Lang1,
   KEY_HANJA(131) = Lang2, 오른쪽 Alt 는 RightAlt (한/영으로 쓰는지는 xkb 맵이 정한다). 자동 반복 = XkbSetDetectableAutoRepeat
   (Release 없이 Press 가 다시 오면 repeat) — 지원하지 않는 서버는 같은 시각의 Release + Press 쌍을 반복으로 본다.
   수정자 = 이벤트의 state (+ 수정자 키 자신의 비트를 누름에 더하고 뗌에서 뺀다 — Win32 의 GetKeyState 와 같은 결과).
3. XIM + XIC (XIMPreeditNothing · XIMStatusNothing — 조합 창은 입력기의 것), Xutf8LookupString. LC_CTYPE 은 사용자 로캘,
   UTF-8 이 아니면 C.UTF-8 (LC_NUMERIC 은 그대로). XSetLocaleModifiers("") — XMODIFIERS 로 ibus · fcitx 가 붙는다.
   텍스트 입력 끔 = XUnsetICFocus + 조합 버림 (게임 키가 입력기로 가지 않는다). 입력기가 보낸 확정 글자(keycode 0)는
   TextInput 만. XIM 이 없으면 XLookupString (Latin-1).
4. 캡처 = XGrabPointer(창 안에 가둠, 빈 커서) + XInput2 RawMotion (루트 창에 선택, 가속 없는 장치 값). XInput2 가 없으면 커서를
   가운데로 되돌리며 차이를 잰다 (가속이 섞인다). 포커스를 잃으면 잡기를 풀고, 다시 얻으면 다시 잡는다. 더블클릭은 X 가
   알려 주지 않는다 → 같은 버튼 · 500 ms · 4 px 안이면 clicks = 2 (세 번째는 다시 1). 휠 = 버튼 4 · 5 (세로), 6 · 7 (가로),
   8 · 9 = X1 · X2. 버튼을 누르면 X 가 알아서 잡는다 (창 밖에서 떼도 온다).
5. 클립보드 = CLIPBOARD 선택 + UTF8_STRING. 우리가 가진 동안 SelectionRequest(TARGETS · UTF8_STRING · STRING)에 답한다. 읽기는
   XConvertSelection 후 SelectionNotify 를 250 ms 까지 기다린다 (그동안 다른 이벤트는 큐에 남는다). 큰 글(INCR) · 창을 닫은
   뒤 남기기(클립보드 관리자 SAVE_TARGETS)는 [계획]. 제목 = _NET_WM_NAME(UTF8_STRING) + WM_NAME(Xutf8SetWMProperties).
   DPI = Xft.dpi ÷ 96 (없으면 1), 실행 중 바뀜(XSETTINGS)은 [계획]. 최소화 = UnmapNotify 또는 _NET_WM_STATE_HIDDEN.
6. VulkanSwapChain.cpp 만 <X11/Xlib.h> + <vulkan/vulkan_xlib.h> 를 include 하고 vkCreateXlibSurfaceKHR 은
   vkGetInstanceProcAddr 로 얻는다 — VK_USE_PLATFORM_XLIB_KHR 를 전역에 켜지 않는다 (volk · 다른 파일이 Xlib 매크로를 보지
   않는다). 인스턴스 확장 = VK_KHR_surface + VK_KHR_xlib_surface. NativeWindowHandle{Xlib, window = Window, display = Display*}.
7. 시험: SandboxTests 스위트 x11 — 실제 X 서버에 창을 만들고 XSendEvent 합성 이벤트(키 · 반복 · 수정자 · 글자 · 버튼 · 더블클릭 ·
   휠 · 이동 · 닫기 · 포커스) + 크기 바꾸기 · 클립보드 · 제목 · 커서 · 캡처. CTest platform_x11 · client_x11_world_smoke (xvfb-run
   이 있으면 가상 화면 — 클라이언트는 lavapipe 로 월드를 그린다). DISPLAY 가 없으면 케이스가 건너뛴다. 사람 손 시험은
   Xvfb + xdotool(XTEST — 실제 입력 경로) 로.
```

## 근거

```text
- Xlib 은 XIM · Xutf8LookupString · 클립보드 편의 함수가 있고 Vulkan 의 VK_KHR_xlib_surface 가 그대로 받는다. XCB 는 IME 가
  없어 결국 Xlib 을 같이 쓴다. dlopen 으로 libX11 을 여는 방식(GLFW · SDL)은 빌드 머신에 헤더가 필요 없는 장점이 있지만 함수
  표 수백 줄이 생긴다 — 개발 패키지 하나(libx11-dev)가 더 싸다.
- evdev keycode 는 물리 위치 그대로다 (ADR-0017 의 Key 와 같은 뜻). keysym 은 배열을 따라가 AZERTY 에서 W 자리가 Z 가 된다.
  xkbcommon 은 Wayland(13C)에서 필요해질 때 더한다 — 그때도 keycode 표는 같다 (Wayland 키 = evdev 코드).
- XIM 은 ibus · fcitx 모두 지원하는 가장 넓은 길이다. 조합 창을 입력기에 맡기면(PreeditNothing) 우리 UI 가 조합 문자열을 그릴
  필요가 없다 — Win32 의 시스템 조합 창과 같은 수준.
- XInput2 Raw 는 Win32 Raw Input 과 같은 뜻(가속 없음). 가운데로 되돌리기는 대체 경로로만.
- 합성 이벤트(XSendEvent)는 같은 처리 함수를 지나므로 표 · 상태 기계를 실제 서버에서 시험할 수 있다. xdotool(XTEST)은 진짜 입력
  장치 경로라 QA 에서 IME 를 뺀 거의 모든 것을 손으로 해 볼 수 있다 (Wine 과 달리 ImGui 단추도 눌린다).
```

## 결과

- 얻는 것: Linux 의 SandboxClient 창 (월드 · 패널 · 에디터 툴 · 클립보드 · 캡처 · 크기 바꾸기 — Vulkan 으로 그린다), --version
  "window X11", 한글 UI 글꼴(Noto CJK · 나눔고딕을 찾는다), 키 표 단위 시험(모든 OS), x11 스위트 · CTest 두 개.
- 포기하는 것 / [계획]: Wayland (13C — xkbcommon · text-input-v3 · libdecor), XSETTINGS 로 실행 중 DPI 바뀜, 클립보드 INCR ·
  창을 닫은 뒤 남기기, 조합 중 글자를 우리 UI 에 그리기(on-the-spot preedit), Xcursor 테마 커서(지금은 cursorfont — 대각선 크기
  화살표가 모서리 모양), 전체 화면, 게임패드(evdev, I5).
- 위험: 입력기마다 XIM 동작이 조금씩 다르다 (fcitx5 · ibus — MANUAL-QA 에서 실제 데스크톱으로). 창 관리자가 없는 환경(Xvfb)에서는
  포커스가 오지 않을 수 있다 (시험은 합성 FocusIn · xdotool windowfocus). keycode = evdev + 8 이 아닌 오래된 서버(XFree86 kbd
  드라이버)는 키가 어긋난다 — 지금 배포판에는 없다.

## 대안

| 대안                                        | 기각 사유                                                                                                      |
|---------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| XCB 만                                      | IME(XIM) · 글자 변환이 없어 결국 Xlib 을 함께 쓴다                                                             |
| libX11 · libXi 를 dlopen (헤더도 자체 선언) | 함수 포인터 표가 길고 타입을 다시 적어야 한다 — 개발 패키지 하나로 충분하다                                    |
| keysym 으로 Key 를 정한다                   | 배열을 따라간다 (AZERTY 의 W 자리 = Z) — Key 는 물리 위치다 (ADR-0017)                                         |
| xkbcommon 으로 keycode → 키 이름            | X11 에는 필요 없다 (표 하나로 충분). Wayland(13C)에서 글자 변환에 쓴다                                         |
| 커서 캡처 = 가운데로 되돌리기만             | 가속이 섞이고 빠른 움직임에서 튄다 — XInput2 가 있으면 Raw 로                                                  |
| GLFW · SDL 로 창                            | 이벤트 큐 · 텍스트 입력 분리 · 클립보드 · 캡처 계약을 다시 감싸야 하고, Win32 를 직접 짠 것과 짝이 맞지 않는다 |

## 재검토 조건

Wayland(13C)가 기본이 되어 X11 경로를 쓰는 사용자가 거의 없을 때 (유지 범위를 줄인다), 입력기 문제가 XIM 으로 풀리지 않을 때
(ibus · fcitx 의 D-Bus 직접 연동), 고해상도 화면에서 실행 중 배율 바뀜이 필요할 때 (XSETTINGS), 클립보드에 큰 글을 다룰 때 (INCR).
