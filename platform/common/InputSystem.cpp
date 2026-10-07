#include "platform/common/InputSystem.hpp"

#include <type_traits>

#include "foundation/text/Utf8.hpp"

namespace sbx::platform {

Modifiers InputState::modifiers() const noexcept {
    Modifiers m;
    for (const Key k : {Key::LeftShift, Key::RightShift, Key::LeftCtrl, Key::RightCtrl, Key::LeftAlt, Key::RightAlt,
                        Key::LeftSuper, Key::RightSuper}) {
        if (down(k)) {
            m.bits = static_cast<u8>(m.bits | modifierBitOf(k));
        }
    }
    return m;
}

void InputSystem::beginFrame() {
    m_state.keyPressed.reset();
    m_state.keyReleased.reset();
    m_state.keyRepeated.reset();
    m_state.mouseDelta = {};
    m_state.wheel = {};
    m_state.buttonPressed.fill(false);
    m_state.buttonReleased.fill(false);
    m_state.buttonClicks.fill(0);
    m_state.text.clear();
    m_captureMouse = false;
    m_captureKeyboard = false;
    m_filteredDirty = true;
}

void InputSystem::releaseAll() {
    m_state.keyReleased |= m_state.keyDown;
    m_state.keyDown.reset();
    for (usize i = 0; i < kMouseButtonCount; ++i) {
        if (m_state.buttonDown[i]) {
            m_state.buttonDown[i] = false;
            m_state.buttonReleased[i] = true;
        }
    }
}

void InputSystem::consume(const PlatformEvent& event) {
    m_filteredDirty = true;
    InputState& s = m_state;
    std::visit(
        [&](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, KeyDown>) {
                if (e.key == Key::Unknown) {
                    return;
                }
                const auto i = static_cast<usize>(e.key);
                if (e.repeat) {
                    s.keyRepeated.set(i);
                    s.keyDown.set(i); // 포커스를 얻기 전부터 눌려 있던 키: down 만 (pressed 아님)
                } else if (!s.keyDown.test(i)) {
                    s.keyPressMods[i] = s.modifiers().bits; // 이 키를 누르기 직전까지 눌려 있던 수정자
                    s.keyDown.set(i);
                    s.keyPressed.set(i);
                }
            } else if constexpr (std::is_same_v<E, KeyUp>) {
                if (e.key == Key::Unknown) {
                    return;
                }
                const auto i = static_cast<usize>(e.key);
                if (s.keyDown.test(i)) {
                    s.keyDown.reset(i);
                    s.keyReleased.set(i);
                }
            } else if constexpr (std::is_same_v<E, TextInput>) {
                utf8::append(s.text, e.codepoint);
            } else if constexpr (std::is_same_v<E, MouseMove>) {
                s.mousePosition = e.position;
                s.mouseDelta += e.delta;
            } else if constexpr (std::is_same_v<E, MouseButtonDown>) {
                const auto i = static_cast<usize>(e.button);
                s.mousePosition = e.position;
                if (!s.buttonDown[i]) {
                    s.buttonPressMods[i] = s.modifiers().bits;
                    s.buttonDown[i] = true;
                    s.buttonPressed[i] = true;
                }
                s.buttonClicks[i] = e.clicks;
            } else if constexpr (std::is_same_v<E, MouseButtonUp>) {
                const auto i = static_cast<usize>(e.button);
                s.mousePosition = e.position;
                if (s.buttonDown[i]) {
                    s.buttonDown[i] = false;
                    s.buttonReleased[i] = true;
                }
            } else if constexpr (std::is_same_v<E, MouseWheel>) {
                s.wheel += e.delta;
            } else if constexpr (std::is_same_v<E, FocusLost>) {
                releaseAll(); // I3: 끈적이는 키 방지
                s.focused = false;
            } else if constexpr (std::is_same_v<E, FocusGained>) {
                s.focused = true;
            }
            // Resized · ContentScaleChanged · CloseRequested 는 입력이 아니다 (앱이 직접 본다)
        },
        event);
}

void InputSystem::consume(const PlatformEventQueue& q) {
    for (const PlatformEvent& e : q) {
        consume(e);
    }
}

void InputSystem::setCapture(bool mouse, bool keyboard) noexcept {
    m_captureMouse = mouse;
    m_captureKeyboard = keyboard;
    m_filteredDirty = true;
}

const InputState& InputSystem::downstream() const noexcept {
    if (!m_captureMouse && !m_captureKeyboard) {
        return m_state;
    }
    if (m_filteredDirty) {
        m_filtered = m_state;
        if (m_captureKeyboard) {
            m_filtered.keyDown.reset();
            m_filtered.keyPressed.reset();
            m_filtered.keyReleased.reset();
            m_filtered.keyRepeated.reset();
            m_filtered.text.clear();
            m_filtered.keyboardCaptured = true;
        }
        if (m_captureMouse) {
            m_filtered.buttonDown.fill(false);
            m_filtered.buttonPressed.fill(false);
            m_filtered.buttonReleased.fill(false);
            m_filtered.buttonClicks.fill(0);
            m_filtered.wheel = {};
            m_filtered.mouseDelta = {};
            m_filtered.mouseCaptured = true;
        }
        m_filteredDirty = false;
    }
    return m_filtered;
}

} // namespace sbx::platform
