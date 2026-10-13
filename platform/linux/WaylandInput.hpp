#pragma once
// Wayland 입력 해석 (xkbcommon): 키맵 · 수정자 · 글자(합성 키 포함) · 클라이언트 자동 반복 · 버튼 · 더블클릭.
// wayland-client 를 모른다 — 단위 시험이 xkb 키맵 이름("us")으로 같은 길을 돈다. docs/07-PLATFORM.md 6.2, ADR-0034.
//
// Wayland 의 키 코드는 evdev 코드다 (X 처럼 +8 하지 않는다) → 물리 Key 는 X11KeyMap 의 표를 그대로 쓴다.
// 자동 반복은 서버가 보내지 않는다 — repeat_info(초당 횟수, 지연)로 클라이언트가 만든다.

#include <chrono>
#include <optional>
#include <string_view>

#include "foundation/math/Vec2.hpp"
#include "foundation/types/Types.hpp"
#include "platform/common/PlatformEvent.hpp"

struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct xkb_compose_table;
struct xkb_compose_state;

namespace sbx::platform::wl {

class KeyboardState {
public:
    using Clock = std::chrono::steady_clock;

    KeyboardState();
    ~KeyboardState();
    KeyboardState(const KeyboardState&) = delete;
    KeyboardState& operator=(const KeyboardState&) = delete;

    // 서버가 준 키맵 (XKB_KEYMAP_FORMAT_TEXT_V1). 실패하면 false — 키는 나오고 글자는 안 나온다
    bool setKeymapText(std::string_view text);
    // 규칙 이름으로 (시험 · 키맵을 못 받았을 때): layout = "us", variant = "intl" …
    bool setKeymapNames(const char* layout, const char* variant = nullptr);
    [[nodiscard]] bool hasKeymap() const noexcept { return m_state != nullptr; }
    // 합성 표(로캘의 Compose)를 읽었나 — 없으면 데드 키가 글자를 만들지 않는다
    [[nodiscard]] bool hasCompose() const noexcept { return m_compose != nullptr; }

    void setModifiers(u32 depressed, u32 latched, u32 locked, u32 group);
    // 키맵의 수정자 비트 (XKB_MOD_NAME_SHIFT = "Shift" …). 시험이 서버 대신 modifiers 를 만들 때
    [[nodiscard]] u32 modifierMask(std::string_view name) const noexcept;
    void setRepeatInfo(i32 ratePerSecond, i32 delayMs);

    // evdev 코드 하나. emitText = 글자 입력이 켜져 있고 입력기(text-input-v3)가 글자를 맡지 않을 때
    void key(u32 evdevCode, bool down, Clock::time_point now, bool emitText, PlatformEventQueue& out);
    // 자동 반복 — 시각이 되었으면 KeyDown{repeat} (+ 글자) 를 낸다
    void tick(Clock::time_point now, bool emitText, PlatformEventQueue& out);
    [[nodiscard]] std::optional<Clock::time_point> nextRepeat() const noexcept;
    // 포커스를 잃으면 반복을 멈추고 합성을 버린다 (눌린 키는 InputSystem 이 FocusLost 로 뗀다 — I3)
    void focusLost() noexcept;

    [[nodiscard]] Modifiers modifiers() const noexcept;

private:
    void emitTextFor(u32 evdevCode, PlatformEventQueue& out);
    [[nodiscard]] bool keyRepeats(u32 evdevCode) const noexcept;

    xkb_context* m_ctx = nullptr;
    xkb_keymap* m_keymap = nullptr;
    xkb_state* m_state = nullptr;
    xkb_compose_table* m_composeTable = nullptr;
    xkb_compose_state* m_compose = nullptr;
    i32 m_rate = 25;     // 초당 (0 = 반복 없음)
    i32 m_delay = 600;   // ms
    u32 m_repeatKey = 0; // evdev + 1 (0 = 없음)
    Clock::time_point m_nextRepeat{};
};

// 리눅스 버튼 코드 (BTN_LEFT 0x110 …) → MouseButton
[[nodiscard]] std::optional<MouseButton> buttonFromLinux(u32 code) noexcept;

// 더블클릭 판정 — Wayland · X11 모두 알려 주지 않는다. 같은 버튼 · 500 ms · 4 논리 px 안이면 2, 세 번째는 다시 1
class ClickCounter {
public:
    u8 press(u32 button, u32 timeMs, Vec2 position) noexcept;

private:
    u32 m_button = ~0u;
    u32 m_time = 0;
    Vec2 m_pos;
    bool m_lastDouble = false;
};

} // namespace sbx::platform::wl
