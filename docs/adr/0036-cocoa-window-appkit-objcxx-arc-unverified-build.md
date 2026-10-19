# ADR-0036. macOS 창은 AppKit(Objective-C++ · ARC)로 만들고, macOS 없이 GNUstep 헤더로 문법만 검사한다

- 상태: **Accepted** · 날짜: 2026-10-19
- 관련: [07-PLATFORM](../07-PLATFORM.md) 3 · 6.3 · 9장, [ADR-0017](0017-physical-keys-event-queue-headless-window.md),
  [ADR-0035](0035-msl-spirv-cross-tool-metal-indices-runtime-compile.md), [16-ROADMAP](../16-ROADMAP.md) Phase 14

## 맥락

14B = macOS 창 · 입력. IWindow · PlatformEvent · InputSystem 계약(ADR-0017)은 Win32 · X11 · Wayland 로 굳었다. 개발 · 검증
환경(클라우드 Linux · 사용자 Windows PC)에는 macOS 가 없어 코드를 빌드하거나 실행할 수 없다 (ADR-0035 맥락 — 사용자가 "여기서
검증되는 것부터, 나머지는 미검증으로 표시하고 Mac · CI 에 맡긴다"를 골랐다). 정할 것:

```text
1. 언어 · 구조 (Objective-C++ · ARC · pimpl, 공개 헤더에 ObjC 타입 0 개)
2. 키 · 수정자 · 글자(입력기) · 마우스 · 캡처 · 커서 · 클립보드
3. 이벤트 루프 (NSApplication 을 누가 돌리나)
4. Metal 이 받을 표면 (NativeWindowHandle)
5. macOS 없이 무엇을 확인하나 · 시험
```

## 결정

```text
1. platform/macos/CocoaWindow.mm (Objective-C++, ARC — 그 파일만 -fobjc-arc), MacPlatform.cpp, MacWindows.hpp (선언만).
   C++ 클래스가 ObjC 객체(NSWindow · 내용 뷰 · 위임)를 멤버로 쥔다 (ARC 가 수명 관리). AppKit · QuartzCore · CoreGraphics
   프레임워크. CMAKE_OSX_DEPLOYMENT_TARGET 13.0 (Metal 3 · Apple Silicon — 06 2장). 창 · 이벤트는 메인 스레드 전용.
2. 키 = NSEvent.keyCode (가상 키 코드 kVK_* — 물리 위치) → platform/macos/MacKeyMap (AppKit 없는 순수 표, 모든 OS 의 단위
   시험). 한/영 · 한자 = HID LANG1/2 가 오는 kVK_JIS_Kana · Eisu → Lang1 · Lang2. 수정자 키는 flagsChanged — 장치별 비트
   (NX_DEVICE*KEYMASK)로 왼쪽 · 오른쪽 누름 · 뗌을 가린다. Caps Lock 은 바뀔 때마다 누름 + 뗌. Command = Super, Option = Alt.
   ⌘ 를 누른 채 뗀 키의 keyUp 은 AppKit 이 창에 보내지 않는다 → pollEvents 가 키 창에 직접 넘긴다.
   글자 = 글자 입력이 켜져 있을 때만 interpretKeyEvents → NSTextInputClient.insertText → TextInput (기능 키 문자 0xF700~
   0xF8FF · 제어 문자는 버린다). 조합 중 글자(setMarkedText)는 받아 두기만 하고 끄면 버린다 — UI 에 그리기는 [계획].
   마우스 = 왼쪽 · 오른쪽 · other(buttonNumber 2 · 3 · 4 = 가운데 · X1 · X2), 더블클릭 = clickCount == 2 (세 번째는 1),
   휠 = scrollingDelta (정밀 장치는 ÷ 10, X 는 부호를 뒤집어 오른쪽 +). 캡처 = 커서 숨김 + 창 가운데로 옮긴 뒤
   CGAssociateMouseAndMouseCursorPosition(false) + 이벤트 deltaX/Y (시스템 가속이 섞인다 — GCMouse 의 가속 없는 값은 [계획]),
   포커스를 잃으면 풀고 다시 얻으면 잡는다. 커서 = NSCursor (대각선 크기 커서는 공개 API 가 없어 가로 화살표로).
   클립보드 = NSPasteboard 문자열. 좌표 = 포인트(논리), 뷰는 flipped (왼쪽 위 원점), framebuffer = 포인트 × backingScaleFactor.
3. 엔진의 프레임 루프가 이벤트를 꺼낸다: pollEvents = nextEventMatchingMask(distantPast) → sendEvent 반복, waitEvents =
   제한 시간까지 기다려 꺼낸 이벤트를 큐 앞에 되돌린다. [NSApp run] 을 쓰지 않는다 (GLFW 와 같은 방식). 번들 없는 실행
   파일도 Dock · 키보드 포커스를 받게 ActivationPolicyRegular, 앱 메뉴의 "종료"(⌘Q) = 창 닫기 요청 (프로세스를 바로 끝내지
   않는다 — CloseRequested).
4. 내용 뷰의 layer = CAMetalLayer (layer-backed, makeBackingLayer). NativeWindowHandle{Cocoa, window = CAMetalLayer*}
   (07 3장) — Metal 백엔드(14C)는 AppKit 을 모른다. 창이 contentsScale · drawableSize 를 맞춘다.
5. 시험 · 확인:
   - MacKeyMap: 모든 OS 의 unit_platform (표 · 왕복 · 장치별 수정자 비트 · Caps Lock · 수정자 집합).
   - SandboxTests 스위트 cocoa (tests/unit/platform/test_cocoa_window.mm, macOS 만, CTest platform_cocoa): 실제 창을 만들고
     뷰에 NSEvent 를 넘긴다 (키 · 반복 · flagsChanged · 글자 켬/끔 · 조합 버림 · 버튼 · clickCount · other 버튼 · 이동 ·
     휠 · 닫기 요청 · 크기 · 클립보드 · 커서 · 캡처). 화면 없는 세션이면 건너뛴다.
   - macOS 가 없는 개발 환경: GNUstep 의 AppKit 헤더 + 없는 Apple API(CAMetalLayer · CoreGraphics 일부 · 10.12 이후 이름)를
     적은 검사용 머리 파일로 clang -fsyntax-only -fobjc-arc 를 돌린다 — C++ · Objective-C 문법 · 형 · ARC 규칙을 본다.
     Apple API 이름 · 시그니처 자체는 확인하지 못한다 → 실제 빌드 · 실행은 CI 의 macOS 잡과 MANUAL-QA 14B (미검증 표시).
```

## 근거

```text
- AppKit 은 Objective-C API 다. C++ 에서 objc_msgSend 를 직접 부르면 코드가 길고 형 검사가 없다.
  ARC 는 수명 버그(해제 · 누수)를 줄이고, .mm 하나에 가두면 나머지 엔진은 C++ 만 본다 (06 · 07 의 규칙).
- 가상 키 코드는 물리 위치라 ADR-0017 의 Key 와 뜻이 같다 — 세 OS 의 표가 같은 Key 를 낸다 (바인딩 파일 공유).
- 엔진이 루프를 쥐면 Win32 · X11 · Wayland 와 같은 pollEvents/waitEvents 계약을 그대로 지킨다. [NSApp run] 은 제어를 돌려주지
  않아 엔진 루프를 콜백으로 뒤집어야 한다.
- CAMetalLayer 를 넘기면 Metal 백엔드가 NSView · NSWindow 를 몰라도 된다 (07 3장의 원래 결정).
- GNUstep 검사는 완전하지 않지만 0 보다 낫다: 쓴 코드의 문법 · C++ 형 · ARC 소유 오류를 macOS 없이 잡는다. 시스템 헤더를
  Apple SDK 에서 가져오는 것(osxcross)은 Xcode 라이선스가 Apple 기기 밖 사용을 허락하지 않아 택하지 않는다.
```

## 결과

- 얻는 것: macOS 에서 SandboxClient 창 (렌더러 없이 — Metal 은 14C), 키 · 글자 · 마우스 · 캡처 · 클립보드 · 배율, Mac 키 표
  단위 시험(모든 OS), macOS 창 시험 스위트 (CI 의 macOS 잡에서 돈다).
- 포기하는 것 / [계획]: 실제 macOS 에서의 빌드 · 실행 확인 (CI 첫 실행 · MANUAL-QA 14B), 조합 중 글자 UI, 가속 없는 캡처 이동
  (GCMouse), 대각선 크기 커서, 전체 화면(Spaces), 게임패드 (GameController — I5).
- 위험: 컴파일 · 실행해 보지 않은 코드다 — CI 의 macOS 잡이 첫 확인이고, 실패하면 로그를 받아 고친다. GitHub 의 macOS
  실행기에서 창 서버가 없으면 cocoa 스위트는 건너뛴다 (그때는 Mac 에서 MANUAL-QA).

## 대안

| 대안                                      | 기각 사유                                                                      |
|-------------------------------------------|--------------------------------------------------------------------------------|
| C++ 에서 objc_msgSend 직접 (ObjC 없이)    | 셀렉터 · 형을 손으로 맞춰야 하고 컴파일러가 아무것도 검사하지 않는다           |
| GLFW · SDL                                | ADR-0033 과 같은 이유 — 이벤트 큐 · 텍스트 분리 · 캡처 계약을 다시 감싸야 한다 |
| [NSApp run] + 타이머 콜백으로 엔진 루프   | 다른 OS 와 루프 모양이 달라지고 waitEvents 계약을 지킬 수 없다                 |
| keyDown 의 characters 로 글자             | 입력기(한글 조합)를 거치지 않는다 — NSTextInputClient 가 표준 경로             |
| osxcross 로 Apple SDK 를 받아 교차 컴파일 | Xcode · SDK 라이선스 (Apple 기기 밖 사용 불가)                                 |

## 재검토 조건

조합 중 글자를 게임 UI 에 그려야 할 때 (marked text 를 InputSystem 에 싣는다), 캡처 이동의 가속이 문제가 될 때 (GCMouse),
CI 의 macOS 잡 · Mac 이 생겨 GNUstep 검사가 필요 없어질 때 (검사 스크립트를 버린다).
