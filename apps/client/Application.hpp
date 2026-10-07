#pragma once
// 클라이언트 앱 상태기계와 프레임 루프. docs/01-ARCHITECTURE.md 3장 (Application), docs/07-PLATFORM.md.
//
//   Boot → MainMenu → Connecting → InWorld(Play|Edit) → Shutdown
//
// Phase 6: Boot → MainMenu → Shutdown 만 실제로 지난다. Phase 7A 부터 렌더러가 있으면 매 프레임 화면을 지운다(Clear).
// MainMenu 는 아직 그릴 UI 가 없어 창 제목 줄에 입력 상태를
// 보여 준다 (수동 QA — 키·마우스·휠·더블클릭·IME 글자·DPI·포커스). Connecting·InWorld 는 Phase 8~10. [계획]
//
// 상태 전이는 요청만 받고 프레임 끝에서 적용한다 (프레임 중간에 상태가 바뀌어 반쯤 다른 상태로 도는 일이 없게).

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "apps/client/FrameRenderer.hpp"
#include "foundation/types/Error.hpp"
#include "platform/common/ActionMap.hpp"
#include "platform/common/Audio.hpp"
#include "platform/common/InputSystem.hpp"
#include "platform/common/Platform.hpp"
#include "platform/common/Window.hpp"

namespace sbx::client {

enum class AppState : u8 { Boot = 0, MainMenu, Connecting, InWorld, Shutdown };
enum class WorldMode : u8 { Play = 0, Edit };

[[nodiscard]] std::string_view appStateName(AppState s) noexcept;
// 허용된 전이: Boot→MainMenu · MainMenu→Connecting · Connecting→InWorld|MainMenu · InWorld→MainMenu,
// 그리고 Shutdown 이 아닌 모든 상태 → Shutdown. 같은 상태로의 전이는 없다.
[[nodiscard]] bool transitionAllowed(AppState from, AppState to) noexcept;

struct AppConfig {
    u64 maxFrames = 0; // 0 = 창을 닫을 때까지
    bool logInput = false;
    f64 fps = 60;                       // 오디오 update 의 명목 dt 에만 쓴다 (페이싱은 run 에 넘기는 FramePacer)
    u32 titleEveryFrames = 6;           // 제목 줄 갱신 간격 (60 Hz 에서 100 ms). 상태가 바뀌면 바로
    IFrameRenderer* renderer = nullptr; // 없으면 그리지 않는다 (--headless, 렌더 백엔드가 없는 OS, --no-render)
};

class Application {
public:
    // window · audio 는 Application 보다 오래 산다
    Application(platform::IWindow& window, platform::ActionMap actions, platform::IAudioBackend& audio,
                AppConfig config);

    // 다음 프레임 경계에서 바꿀 상태. 허용되지 않은 전이는 오류. Shutdown 요청은 다른 요청보다 우선한다.
    Expected<void> request(AppState next);
    // InWorld 안에서만 (Play ↔ Edit)
    Expected<void> setWorldMode(WorldMode mode);

    // 한 프레임: 이벤트 → 입력 → 액션 → 디버그 동작 → 오디오 → 제목 → 상태 전이. 끝났으면(Shutdown) false.
    bool frame();
    // frame() 을 끝날 때까지. pacer 가 있으면 프레임 사이에 잔다. 최소화 중에는 이벤트를 기다린다. 종료 코드를
    // 돌려준다.
    int run(platform::FramePacer* pacer);

    [[nodiscard]] AppState state() const noexcept { return m_state; }
    [[nodiscard]] WorldMode worldMode() const noexcept { return m_mode; }
    [[nodiscard]] u64 frameCount() const noexcept { return m_frame; }
    [[nodiscard]] const platform::InputSystem& input() const noexcept { return m_input; }
    [[nodiscard]] const platform::ActionMap& actions() const noexcept { return m_actionMap; }
    [[nodiscard]] const std::string& typedText() const noexcept { return m_text; }
    // 창 제목 줄 문자열 (MainMenu 의 입력 모니터)
    [[nodiscard]] std::string statusLine() const;

private:
    void handleEvent(const platform::PlatformEvent& e);
    void handleDebugActions();
    [[nodiscard]] bool pressed(const std::optional<platform::ActionId>& id) const noexcept;
    void applyPendingTransition();

    platform::IWindow& m_window;
    platform::ActionMap m_actionMap;
    platform::ActionState m_actionState;
    platform::IAudioBackend& m_audio;
    AppConfig m_config;

    platform::PlatformEventQueue m_events;
    platform::InputSystem m_input;

    AppState m_state = AppState::Boot;
    std::optional<AppState> m_pending;
    WorldMode m_mode = WorldMode::Play;
    u64 m_frame = 0;
    u64 m_lastTitleFrame = 0;
    bool m_titleDirty = true;
    std::string m_lastTitle;

    // 입력 모니터 (MainMenu)
    std::string m_text; // F2 로 켠 글자 입력이 모인 곳 (최근 것만)
    Vec2 m_wheelTotal;
    platform::MouseButton m_lastClickButton = platform::MouseButton::Left;
    u8 m_lastClicks = 0;
    platform::CursorShape m_cursor = platform::CursorShape::Arrow;
    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

    struct Ids {
        std::optional<platform::ActionId> quit, textInput, captureMouse, cycleCursor, copyText, pasteText, escape,
            eraseChar;
    } m_ids;
};

// 이벤트 한 줄 설명 (--log-input)
[[nodiscard]] std::string describeEvent(const platform::PlatformEvent& e);

} // namespace sbx::client
