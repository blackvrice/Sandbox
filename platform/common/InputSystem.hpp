#pragma once
// 입력 상태. docs/07-PLATFORM.md 5장 (규칙 I1~I5), ADR-0017.
//
//   PlatformEventQueue → InputSystem::beginFrame() + consume(e)… → InputState
//
// 소비 순서: ImGui → Editor 툴 → Play 컨트롤 → 카메라. ImGui 는 raw() 를 보고 WantCapture* 를 정한다.
// 그 결과를 setCapture() 로 알리면 downstream() 은 가로챈 장치를 "아무 입력 없음"으로 보여 준다 (I1).
// Simulation 은 InputState 를 보지 않는다 — 입력은 SimCommand 로만 들어간다 (I2).

#include <array>
#include <bitset>
#include <string>

#include "foundation/math/Vec2.hpp"
#include "platform/common/Gamepad.hpp"
#include "platform/common/PlatformEvent.hpp"

namespace sbx::platform {

struct InputState {
    // 키: down = 지금 눌림, pressed = 이 프레임에 눌림(자동 반복 제외), released = 이 프레임에 뗌,
    // repeated = 이 프레임에 자동 반복이 왔음. 한 프레임 안에 눌렀다 떼면 pressed 와 released 가 함께 켜지고 down 은
    // 꺼진다.
    std::bitset<kKeyCount> keyDown;
    std::bitset<kKeyCount> keyPressed;
    std::bitset<kKeyCount> keyReleased;
    std::bitset<kKeyCount> keyRepeated;

    Vec2 mousePosition; // 논리 좌표
    Vec2 mouseDelta;    // 이 프레임 누적
    Vec2 wheel;         // 이 프레임 누적 (칸)
    std::array<bool, kMouseButtonCount> buttonDown{};
    std::array<bool, kMouseButtonCount> buttonPressed{};
    std::array<bool, kMouseButtonCount> buttonReleased{};
    std::array<u8, kMouseButtonCount> buttonClicks{}; // 이 프레임 마지막 누름의 클릭 수 (2 = 더블클릭)
    // 눌린 순간의 수정자 (Modifiers::bits). 한 프레임 안에 Ctrl↓ Q↓ Ctrl↑ Q↑ 가 다 와도 "Ctrl+Q" 를 알아보게.
    std::array<u8, kKeyCount> keyPressMods{};
    std::array<u8, kMouseButtonCount> buttonPressMods{};

    std::string text; // 이 프레임에 확정된 글자 (UTF-8)
    bool focused = true;
    bool mouseCaptured = false;    // downstream 에서: 상위 소비자(ImGui)가 마우스를 가져갔다
    bool keyboardCaptured = false; // downstream 에서: 상위 소비자가 키보드를 가져갔다

    std::array<GamepadState, kMaxGamepads> gamepads{}; // [계획] 구현은 콘텐츠가 요구할 때 (I5)

    [[nodiscard]] bool down(Key k) const noexcept { return k != Key::Unknown && keyDown.test(index(k)); }
    [[nodiscard]] bool pressed(Key k) const noexcept { return k != Key::Unknown && keyPressed.test(index(k)); }
    [[nodiscard]] bool released(Key k) const noexcept { return k != Key::Unknown && keyReleased.test(index(k)); }
    [[nodiscard]] bool repeated(Key k) const noexcept { return k != Key::Unknown && keyRepeated.test(index(k)); }
    [[nodiscard]] bool down(MouseButton b) const noexcept { return buttonDown[index(b)]; }
    [[nodiscard]] bool pressed(MouseButton b) const noexcept { return buttonPressed[index(b)]; }
    [[nodiscard]] bool released(MouseButton b) const noexcept { return buttonReleased[index(b)]; }
    [[nodiscard]] u8 clicks(MouseButton b) const noexcept { return buttonClicks[index(b)]; }

    // 지금 눌린 수정자 키로부터 (좌우 구분 없음)
    [[nodiscard]] Modifiers modifiers() const noexcept;

private:
    static constexpr usize index(Key k) noexcept { return static_cast<usize>(k); }
    static constexpr usize index(MouseButton b) noexcept { return static_cast<usize>(b); }
};

class InputSystem {
public:
    // 프레임 단위 값(pressed·released·repeated·delta·wheel·text·clicks)을 지우고 캡처를 푼다. down 은 유지.
    void beginFrame();
    void consume(const PlatformEvent& e);
    void consume(const PlatformEventQueue& q);

    // 모든 입력 (ImGui 가 본다)
    [[nodiscard]] const InputState& raw() const noexcept { return m_state; }

    // I1: 상위 소비자가 이 프레임에 가져간 장치. beginFrame 이 푼다.
    void setCapture(bool mouse, bool keyboard) noexcept;
    // 하위 소비자(에디터 툴·플레이 컨트롤·카메라)가 보는 입력. 가져간 장치는 비어 있다:
    //   키보드 → 키·글자 없음,  마우스 → 버튼·휠·이동량 없음 (위치는 남는다 — 호버 표시용)
    [[nodiscard]] const InputState& downstream() const noexcept;

private:
    void releaseAll(); // I3

    InputState m_state;
    bool m_captureMouse = false;
    bool m_captureKeyboard = false;
    mutable InputState m_filtered;
    mutable bool m_filteredDirty = true;
};

} // namespace sbx::platform
