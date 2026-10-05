# ADR-0005. 타깃 의존 경계, Render는 Core를 모른다, 네이티브 플랫폼 계층

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [01-ARCHITECTURE](../01-ARCHITECTURE.md) 2장, [07-PLATFORM](../07-PLATFORM.md)

## 맥락

RTS는 문서상 "core는 SFML을 모른다"였지만 `UICommand.hpp`·`LogicThread.cpp`가 SFML을 include했고,
"headless" 테스트가 SFML::System/Window와 UI 소스를 링크했습니다. 규칙이 문서에만 있으면 지켜지지 않습니다.
요구사항 예시 다이어그램은 `SandboxCore ← SandboxRender`였습니다.

## 결정

```text
1. 타깃: Foundation → Core → Network → Server ; Foundation → Platform → Render ; Core+Render → Editor ; 전부 → Client
2. SandboxRender 는 SandboxCore 에 의존하지 않는다. ECS → RenderWorld 변환은 SandboxClient/presentation.
3. SandboxFoundation 을 따로 둬서 Platform·Render 가 시뮬레이션 코드 없이 기본 타입을 쓰게 한다.
4. 경계를 CMake 링크 그래프 검사와 include 린트(CTest arch)로 기계적으로 강제한다.
5. 창·입력은 SDL3 같은 래퍼 없이 네이티브(Win32 / X11·Wayland / Cocoa)로 구현한다.
```

## 근거

```text
- Render 가 Core 를 모르면 "Renderer 변경 없이 콘텐츠 변경"이 링크 수준에서 보장되고 Render 테스트가 ECS 없이 돈다.
- 네이티브 계층은 요구사항이며, NativeWindowHandle 을 RHI 에 직접 넘기는 구조가 단순해진다.
```

## 결과

- 얻는 것: 경계 위반이 빌드·테스트 실패로 드러남. 헤드리스 서버의 의존성 최소.
- 포기하는 것: Extraction이 Client에 있어 Render 단독으로는 월드를 그릴 수 없음(의도).
- 위험: Wayland 구현 비용 (16-ROADMAP R12).

## 대안

| 대안 | 기각 사유 |
|---|---|
| Render → Core 의존 (요구사항 예시) | 위 근거 |
| SDL3 (창·입력·게임패드) | 요구사항이 네이티브. 단 **Wayland 일정 위험 시 `SdlWindow`를 `IWindow` 구현으로 추가** 가능 (구조 변경 없음) |
| GLFW | 입력·IME·게임패드 범위가 SDL3보다 좁음 |

## 재검토 조건

Phase 13에서 Wayland 구현이 2주 이상 막힐 때 SDL3 백엔드 추가를 ADR로.
