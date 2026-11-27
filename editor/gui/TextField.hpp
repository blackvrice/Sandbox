#pragma once
// 글자 칸 하나 (UI-R2-05) — GraphicsUI TextInput + 호스트의 글자 대상 (확정 글자 · IME 자리) + 편집 콜백 (클립보드) + 눌러 캐럿 놓기 +
// 그리기 (선택 · 캐럿 · 자리표시 글). 패널마다 같은 칸을 다시 쓰지 않게 모았다 (홈 화면의 칸은 같은 동작 — 옮기기는 후속).
//
// 수명: 화면이 소유한다. mount 가 그 화면의 호스트에 핸들러 · 글자 대상을 건다 (호스트가 핸들러를 소유 — 화면 바꾸기가 지운다).

#include <functional>
#include <memory>
#include <string>

#include <graphics-ui/core/controls.hpp>

#include "editor/services/StudioTokens.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::editor::gui {

namespace gui = graphics_ui::core;

class TextField {
public:
    // 처리하지 않은 키 (Enter · ↑↓ …) 를 화면이 받는다 — 처리했으면 true
    using KeyHook = std::function<bool(const gui::KeyEvent&)>;
    using Changed = std::function<void(const std::string&)>;

    explicit TextField(std::string placeholder, double emSize = 14.0, usize maxBytes = 512)
        : m_placeholder(std::move(placeholder)), m_em(emSize), m_maxBytes(maxBytes) {}

    Expected<void> mount(uihost::UiHost& host, gui::ComponentHandle handle, Changed changed = {}, KeyHook keys = {});
    // 바깥에서 값 바꾸기 (게시 콜백 없음 — 실행 취소 기록을 비운다)
    void setValue(std::string value);
    [[nodiscard]] const std::string& value() const noexcept { return m_value; }
    [[nodiscard]] gui::ComponentHandle handle() const noexcept { return m_handle; }
    [[nodiscard]] double emSize() const noexcept { return m_em; }

    // box = 배치 상자 (화면 좌표 — 눌러 캐럿 놓기가 쓴다), focused = 호스트 포커스가 이 칸
    void paint(const uihost::UiHost& host, const gui::LayoutBox& box, bool focused, const services::StudioColors& colors,
               uihost::PaintList& out);

private:
    gui::EventReply onEvent(const gui::RoutedEvent& ev);
    gui::TextEditCallbacks callbacks();

    std::string m_placeholder;
    double m_em;
    usize m_maxBytes;
    std::string m_value;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_handle;
    gui::LayoutRect m_bounds;
    std::unique_ptr<gui::TextInput> m_input;
    Changed m_changed;
    KeyHook m_keys;
};

} // namespace sbx::editor::gui
