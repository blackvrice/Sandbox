#pragma once
// 창이 쌓는 이벤트. docs/07-PLATFORM.md 4장.
// 창은 콜백을 부르지 않고 PlatformEventQueue 를 채운다. 앱이 프레임마다 비운다.
// 좌표는 논리 단위(픽셀 ÷ contentScale)다. Resized 만 픽셀(framebuffer)과 논리(window)를 함께 싣는다.

#include <variant>
#include <vector>

#include "foundation/math/Vec2.hpp"
#include "foundation/types/Types.hpp"
#include "platform/common/Key.hpp"

namespace sbx::platform {

struct Extent2D {
    u32 width = 0;
    u32 height = 0;

    friend constexpr bool operator==(Extent2D, Extent2D) noexcept = default;
};

struct KeyDown {
    Key key = Key::Unknown;
    u32 scancode = 0; // OS 원값 (진단용). 바인딩은 key 를 쓴다
    Modifiers mods;
    bool repeat = false; // 자동 반복
};
struct KeyUp {
    Key key = Key::Unknown;
    u32 scancode = 0;
    Modifiers mods;
};
// 확정된 글자 하나 (IME 조합이 끝난 글자 포함). 제어 문자는 오지 않는다.
// 창의 setTextInputActive(true) 동안에만 온다 (ADR-0017).
struct TextInput {
    char32_t codepoint = 0;
};
struct MouseMove {
    Vec2 position; // 클라이언트 영역 기준 논리 좌표
    Vec2 delta;    // 커서 캡처 중에는 Raw Input 상대 이동 (가속 없음)
};
struct MouseButtonDown {
    MouseButton button = MouseButton::Left;
    Vec2 position;
    u8 clicks = 1; // 2 = 더블클릭
};
struct MouseButtonUp {
    MouseButton button = MouseButton::Left;
    Vec2 position;
};
struct MouseWheel {
    Vec2 delta; // 한 칸 = 1.0. y 양수 = 위(앞)로, x 양수 = 오른쪽
};
struct FocusGained {};
struct FocusLost {};
struct Resized {
    Extent2D framebuffer; // 픽셀
    Extent2D window;      // 논리 단위
};
struct ContentScaleChanged {
    f32 scale = 1.f;
};
struct CloseRequested {};

using PlatformEvent = std::variant<KeyDown, KeyUp, TextInput, MouseMove, MouseButtonDown, MouseButtonUp, MouseWheel,
                                   FocusGained, FocusLost, Resized, ContentScaleChanged, CloseRequested>;

// 프레임 하나 분량의 이벤트. 도착 순서를 보존한다.
class PlatformEventQueue {
public:
    void push(PlatformEvent e) { m_events.push_back(std::move(e)); }
    void clear() noexcept { m_events.clear(); }
    // other 의 이벤트를 순서대로 뒤에 옮기고 other 를 비운다
    void takeFrom(PlatformEventQueue& other) {
        for (PlatformEvent& e : other.m_events) {
            m_events.push_back(std::move(e));
        }
        other.m_events.clear();
    }
    [[nodiscard]] bool empty() const noexcept { return m_events.empty(); }
    [[nodiscard]] usize size() const noexcept { return m_events.size(); }
    [[nodiscard]] const std::vector<PlatformEvent>& events() const noexcept { return m_events; }
    [[nodiscard]] auto begin() const noexcept { return m_events.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_events.end(); }

private:
    std::vector<PlatformEvent> m_events;
};

} // namespace sbx::platform
