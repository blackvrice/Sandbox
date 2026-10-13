# ADR-0034. Wayland 창은 libwayland + libdecor 로 만들고, 글자는 xkbcommon · text-input-v3, 창 시스템은 실행 중에 고른다

- 상태: **Accepted** · 날짜: 2026-10-13
- 관련: [07-PLATFORM](../07-PLATFORM.md) 2 · 6.2 · 9장, [06-RENDERING](../06-RENDERING.md) 5.2,
  [ADR-0017](0017-physical-keys-event-queue-headless-window.md), [ADR-0033](0033-x11-xlib-evdev-keys-xim-xinput2-raw.md),
  [16-ROADMAP](../16-ROADMAP.md) Phase 13

## 맥락

13C = Linux 의 두 번째 창. 지금 데스크톱(GNOME · KDE)의 기본 세션은 Wayland 이고 X11 창(13B)은 Xwayland 위에서 돈다 — 배율이
흐려지고 입력기가 XIM 을 거친다. IWindow 계약(ADR-0017)과 X11 의 키 표(ADR-0033)는 그대로 둔다. 정할 것:

```text
1. 무엇을 링크하나 (libwayland · 프로토콜 코드 생성 · 창 틀), 빌드 머신 요구
2. 창 틀 (GNOME 은 서버 장식이 없다)
3. 키 · 글자 · 자동 반복 · 한글 입력
4. 마우스 캡처 · 휠 · 더블클릭 · 커서
5. 배율 · 최소화 · 클립보드
6. X11 과 Wayland 중 무엇을 쓰나 (한 빌드에 둘 다)
7. Vulkan 표면
8. 시험 (입력을 합성할 수 없는 서버)
```

## 결정

```text
1. libwayland-client · wayland-cursor · xkbcommon · libdecor-0 을 pkg-config 로 (PkgConfig::SBX_WAYLAND). 확장 프로토콜
   (text-input-unstable-v3 · relative-pointer-unstable-v1 · pointer-constraints-unstable-v1)은 wayland-protocols 의 XML 을
   wayland-scanner 로 빌드 때 C 로 만든다 (cmake/SbxWayland.cmake, <빌드>/generated/wayland, 경고 없이 컴파일하는 C 파일).
   옵션 SBX_ENABLE_WAYLAND (Linux 기본 ON — 없으면 구성 오류 + apt 안내, OFF 면 X11 만). SBX_HAS_WAYLAND 은 PUBLIC 정의.
   구현 platform/linux/WaylandWindow.cpp (wayland 헤더는 이 파일만) · WaylandInput (xkbcommon 만, 서버 없이 시험된다).
2. libdecor: 서버가 xdg-decoration 을 주면 서버 틀, 아니면 플러그인(cairo · gtk)이 그린 틀. 크기 · 최대화 · 최소화 · 닫기 ·
   크기 바꾸기 끌기는 libdecor 가 처리하고 configure 로 알려 준다. configure 에 크기가 없으면(처음 · 최대화 풀기) 떠 있던
   크기로 돌아간다. 장식 표면도 같은 클라이언트의 표면이라 wl_pointer 가 받는다 — 우리 표면에 들어온 동안만 입력으로 낸다.
   렌더러가 버퍼를 붙이기 전에는 wl_shm 어두운 버퍼 하나를 붙인다 (버퍼가 없으면 창이 지도에 오르지 않고 입력도 없다).
3. 키 = wl_keyboard 의 evdev 코드 → X11KeyMap 의 같은 표 (+ 8, 물리 위치 — ADR-0033). 키맵(서버가 준 memfd 문자열) · 수정자 ·
   글자 = xkbcommon (xkb_state_key_get_utf32, Ctrl 변환 결과의 제어 문자는 버린다 — I4). 데드 키 · Compose = xkb compose
   (사용자 로캘의 표). 자동 반복은 서버가 보내지 않는다 → repeat_info(초당 횟수 · 지연)로 클라이언트가 만든다 (프레임이
   멈췄으면 다섯 개까지만 따라잡는다, 수정자 키는 반복하지 않는다, 포커스를 잃으면 멈춘다). 한글 입력기 = text-input-v3:
   텍스트 입력을 켜면 enable + content type + commit, 확정 글자(commit_string)는 done 에서 TextInput 으로. 입력기가 가져간
   키는 wl_keyboard 로 오지 않는다. 조합 중 글자(preedit)는 입력기의 창이 그린다 — 우리 UI 에 그리기는 [계획].
4. 캡처 = pointer-constraints lock(PERSISTENT) + relative-pointer (가속 없는 값). 캡처 중에는 위치를 묶어 둔다 (잠금이 늦게
   켜지는 서버도 절대 이동이 새지 않는다). 서버에 둘 중 하나가 없으면 커서 숨김만 (경고). 휠 = axis_discrete(한 칸) 있으면
   그것, 없으면 axis 값 ÷ 10 (터치패드), frame 마다 한 MouseWheel. 버튼 = BTN_LEFT · RIGHT · MIDDLE · SIDE/BACK(X1) ·
   EXTRA/FORWARD(X2). 더블클릭 = X11 과 같은 판정 (같은 버튼 · 500 ms · 4 논리 px, 세 번째는 다시 1). 커서 = wl_cursor 테마
   (XCURSOR_THEME · XCURSOR_SIZE, CSS 이름 → X 이름 대체), 숨김 = 커서 표면 없음.
5. 배율 = 창이 걸친 출력(wl_surface.enter)의 wl_output.scale 중 가장 큰 정수 → set_buffer_scale, framebuffer = 논리 × 배율
   (highDpi=false 면 1). 분수 배율(wp-fractional-scale · viewporter)은 [계획]. 최소화는 Wayland 가 알려 주지 않는다 — libdecor 의
   SUSPENDED 상태만 minimized. 클립보드 = wl_data_device: 쓰기는 data_source(text/plain;charset=utf-8 · UTF8_STRING ·
   text/plain)를 마지막 입력 serial 로 set_selection (입력이 한 번도 없으면 서버가 거절할 수 있다 — 규약), 우리가 가진 동안은
   서버를 거치지 않고 돌려준다. 읽기 = data_offer.receive + 파이프를 250 ms 까지 (X11 과 같은 상한).
6. 한 빌드에 X11 · Wayland 둘 다 (Linux 기본). WindowDesc.system = Auto · X11 · Wayland. Auto: WAYLAND_DISPLAY 가 있으면
   Wayland, 만들지 못하면 경고 후 X11 (Xwayland). 강제한 쪽은 대신하지 않는다 (실패는 오류). 한쪽만 든 빌드는 그쪽만. SandboxClient --platform
   auto|x11|wayland. IWindow::backendName() 이 실제로 고른 것을 알린다 ("창 Wayland"), --version 은 빌드에 든 것("Wayland · X11").
7. VulkanSwapChain.cpp 만 <vulkan/vulkan_wayland.h> 를 include, vkCreateWaylandSurfaceKHR 은 vkGetInstanceProcAddr 로.
   Linux 인스턴스 확장 = VK_KHR_surface + 있는 것(xlib_surface · wayland_surface). NativeWindowHandle{Wayland, window =
   wl_surface*, display = wl_display*}. 크기는 서버가 정하지 않으므로 스왑체인 extent = framebufferSize.
8. 시험: WaylandInput 은 서버 없이 xkb 키맵 이름("us" · "fr" · "us intl")으로 platform 스위트 (키 · scancode · 수정자 ·
   배열 · 데드 키 · 반복 · 키맵 없음 · 버튼 · 더블클릭). 창은 SandboxTests 스위트 wayland — 실제 컴포지터에 만든다 (핸들 ·
   크기 · 제목 · 클립보드 · 커서 · 캡처 · 두 연결). Wayland 는 다른 클라이언트에 입력을 보낼 수 없어 입력 합성은 없다.
   tools/wayland/run_headless.sh = 자기 XDG_RUNTIME_DIR 에 weston --backend=headless 를 띄워 명령 하나를 돌린다 → CTest
   platform_wayland · client_wayland_world_smoke. 사람 손 시험은 Xvfb 위의 weston(x11 백엔드) + xdotool (weston 이 X 입력을
   Wayland 입력으로 옮긴다) · wl-copy · wl-paste. CI(ubuntu-24.04)가 X11 · Wayland 개발 파일 · Xvfb · weston 을 설치한다.
```

## 근거

```text
- libwayland 는 모든 Wayland 클라이언트가 쓰는 유일한 구현이고, 프로토콜 코드는 XML 에서 생성하는 것이 표준 방식이다
  (GLFW · SDL 도 같다). 생성 코드를 저장소에 넣으면 wayland-protocols 판과 어긋난다.
- GNOME(mutter)은 서버 장식을 주지 않는다 — 틀이 없으면 창을 옮기거나 닫을 수 없다. libdecor 는 GNOME 에선 그려 주고
  KDE · sway 에선 서버 장식을 쓴다 (SDL · GLFW 의 선택과 같다). 직접 xdg-shell + 틀 그리기는 수백 줄과 테마 일치 문제다.
- Wayland 키 코드는 evdev 그대로라 13B 의 표를 다시 쓴다 — 두 창 시스템이 같은 Key 를 낸다 (바인딩 파일이 같다).
- 서버가 반복을 보내지 않는 것은 Wayland 규약이다. 반복을 클라이언트가 만들면 프레임 루프와 같은 시계로 잰다.
- text-input-v3 는 GNOME · KDE · sway 의 입력기(ibus · fcitx5)가 지원하는 현재 규약이다. 실패하면 xkb 글자는 그대로 나온다.
- 실행 중 선택은 같은 바이너리가 X11 데스크톱 · Wayland 데스크톱 · 원격(X 전달)에서 모두 돌게 한다. Auto 의 X11 되돌림은
  Wayland 서버가 필요한 프로토콜(xdg-shell)을 주지 않는 드문 경우를 덮는다.
- weston headless 는 GPU · 화면 없이 실제 Wayland 서버다 — 창 생성 · configure · 클립보드 · 스왑체인(wl_shm)을 CI 에서 돈다.
```

## 결과

- 얻는 것: Wayland 데스크톱에서 SandboxClient 가 Xwayland 없이 돈다 (틀 · 최대화 · 크기 바꾸기 · 닫기 · 키 · 글자 · 반복 · 데드
  키 · 캡처 · 휠 · 클립보드 · 정수 배율 · Vulkan), --platform, CTest 두 개 + 단위 시험, Linux CI 가 창까지 돈다.
- 포기하는 것 / [계획]: 분수 배율, 조합 중 글자를 우리 UI 에 그리기, 커서 모양 프로토콜(wp-cursor-shape), 전체 화면, 창을 닫은
  뒤 클립보드 남기기, 끌어 놓기(DnD), 최소화를 확실히 아는 방법(Wayland 에 없다 — SUSPENDED 를 주지 않는 서버는 계속 그린다),
  게임패드 (I5).
- 위험: 서버마다 프로토콜 지원이 다르다 (text-input-v3 · pointer-constraints 가 없으면 입력기 · 캡처가 약해진다 — 로그로
  알린다). libdecor 플러그인이 없으면 틀이 없다 (libdecor-0-plugin-1-cairo 또는 -gtk 를 권한다). 실제 데스크톱 · 한글 입력기 ·
  배율 2 는 MANUAL-QA 13C 로 사람이 확인한다.

## 대안

| 대안                                        | 기각 사유                                                                                          |
|---------------------------------------------|----------------------------------------------------------------------------------------------------|
| xdg-shell 만 (틀 없음)                      | GNOME 에서 창을 옮기거나 닫을 수 없다                                                              |
| 틀을 직접 그린다 (subsurface + 렌더러)      | 크기 바꾸기 · 그림자 · 테마 · 단추 배치를 모두 다시 만든다 — libdecor 가 이미 한다                 |
| libwayland 를 dlopen (헤더 자체 선언)       | 생성 코드 · 함수 표가 길다. 개발 패키지 넷으로 충분하고 CI 가 설치한다                             |
| keysym 으로 Key                             | 배열을 따라간다 — Key 는 물리 위치다 (ADR-0017 · 0033)                                             |
| 서버 반복을 기다린다                        | Wayland 서버는 반복 키를 보내지 않는다                                                             |
| Wayland 만 (X11 을 뺀다)                    | X11 데스크톱 · X 전달 · 오래된 배포판이 남아 있다 — 13B 를 버릴 이유가 없다                        |
| 빌드 때 하나만 고른다                       | 배포판 패키지 하나가 두 세션에서 돌아야 한다                                                       |
| GLFW · SDL                                  | ADR-0033 과 같은 이유 — 이벤트 큐 · 텍스트 분리 · 캡처 계약을 다시 감싸야 하고 X11 과 짝이 안 맞다 |

## 재검토 조건

분수 배율 화면이 흔해질 때 (wp-fractional-scale + viewporter), 입력기 조합 글자를 게임 UI 안에 그려야 할 때, 커서 모양 프로토콜이
널리 퍼질 때 (wp-cursor-shape — 테마 읽기를 뺀다), X11 세션 사용자가 거의 없어질 때 (Auto 의 X11 되돌림 · 13B 유지 범위를 줄인다).
