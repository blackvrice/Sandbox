#pragma once
// 액션 바인딩. docs/07-PLATFORM.md 5장, ADR-0017.
//
// settings/input.json:
//   { "format": "sandbox.input", "version": 1,
//     "actions": { "camera.pan.up": ["W", "Up"], "editor.undo": ["Ctrl+Z"], "camera.drag": ["MouseMiddle"] } }
//
// 바인딩 = 수정자 0개 이상 + 키 또는 마우스 버튼 하나. 키 이름은 물리 위치(Key.hpp)라 배열과 무관하다.
// 수정자는 **정확히** 맞아야 한다: "Z" 는 Ctrl+Z 를 누를 때 켜지지 않는다. 바인딩된 키가 수정자 키 자신이면
// (예: "LeftShift") 그 키가 만드는 수정자는 비교에서 뺀다.
// 같은 바인딩이 두 액션에 있으면 conflicts() 가 보고한다 (로드는 실패하지 않는다 — 문맥이 다른 액션은 겹쳐도 된다).

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"
#include "platform/common/InputSystem.hpp"

namespace sbx::platform {

struct Binding {
    enum class Kind : u8 { Key = 0, Mouse };
    Kind kind = Kind::Key;
    Key key = Key::Unknown;                 // kind == Key
    MouseButton button = MouseButton::Left; // kind == Mouse
    Modifiers mods;

    friend bool operator==(const Binding&, const Binding&) noexcept = default;
};

// "Ctrl+Shift+Z", "Alt+MouseLeft", "F2". 수정자 이름: Ctrl · Shift · Alt · Super (순서 무관, 중복 금지).
[[nodiscard]] Expected<Binding> parseBinding(std::string_view text);
// 정규형: 수정자는 Ctrl, Shift, Alt, Super 순서. parseBinding(bindingName(b)) == b.
[[nodiscard]] std::string bindingName(const Binding& b);

// 이 프레임에 바인딩의 키(버튼)가 새로 눌렸고, 그 순간의 수정자가 정확히 맞는다 (자동 반복 제외).
[[nodiscard]] bool bindingPressed(const Binding& b, const InputState& s) noexcept;
// 이 프레임에 바인딩이 "켜져" 있는가: bindingPressed 이거나, 키(버튼)가 눌려 있고 지금 수정자가 정확히 맞는다.
[[nodiscard]] bool bindingActive(const Binding& b, const InputState& s) noexcept;

using ActionId = u16;

struct ActionConflict {
    Binding binding;
    std::string first; // 이름 순으로 앞
    std::string second;
};

class ActionMap {
public:
    static constexpr u32 kFormatVersion = 1;

    // source 는 오류 메시지의 위치 (파일 이름 등)
    [[nodiscard]] static Expected<ActionMap> parse(std::string_view jsonText, std::string_view source);
    [[nodiscard]] static Expected<ActionMap> loadFile(const std::filesystem::path& path);

    // overrides 에 있는 액션은 바인딩을 통째로 바꾸고(빈 배열 = 해제), 없는 액션은 추가한다.
    void merge(const ActionMap& overrides);

    [[nodiscard]] std::optional<ActionId> find(std::string_view name) const noexcept;
    [[nodiscard]] usize size() const noexcept { return m_actions.size(); }
    [[nodiscard]] std::string_view name(ActionId id) const { return m_actions.at(id).name; }
    [[nodiscard]] std::span<const Binding> bindings(ActionId id) const { return m_actions.at(id).bindings; }

    [[nodiscard]] std::vector<ActionConflict> conflicts() const;
    // 같은 형식으로 다시 쓴다 (액션 이름 순, 2칸 들여쓰기)
    [[nodiscard]] std::string toJson() const;

    [[nodiscard]] bool active(ActionId id, const InputState& s) const;
    [[nodiscard]] bool triggered(ActionId id, const InputState& s) const;

private:
    struct Action {
        std::string name;
        std::vector<Binding> bindings;
    };
    std::vector<Action> m_actions; // 이름 순
};

// 프레임마다 액션의 켜짐/꺼짐 가장자리를 계산한다.
//   pressed  = 이번 프레임 켜짐 && (지난 프레임 꺼짐 || 바인딩 키가 이번 프레임에 새로 눌림)
//   released = 이번 프레임 꺼짐 && 지난 프레임 켜짐
// 한 프레임 안에 눌렀다 뗀 키는 그 프레임에 켜짐(pressed)으로, 다음 프레임에 released 로 보인다.
// 연속한 두 프레임에서 각각 탭하면 두 번 모두 pressed 다 (가운데의 뗌이 프레임 안에 숨어도).
// 누른 프레임의 수정자는 키가 눌린 순간의 것, 계속 누르고 있는 동안은 지금의 것으로 본다.
class ActionState {
public:
    void update(const ActionMap& map, const InputState& s);
    void reset() noexcept;

    [[nodiscard]] bool down(ActionId id) const noexcept { return id < m_cur.size() && m_cur[id] != 0; }
    [[nodiscard]] bool pressed(ActionId id) const noexcept {
        return down(id) && (!(id < m_prev.size() && m_prev[id] != 0) || m_edge[id] != 0);
    }
    [[nodiscard]] bool released(ActionId id) const noexcept {
        return !down(id) && id < m_prev.size() && m_prev[id] != 0;
    }

private:
    std::vector<u8> m_cur;
    std::vector<u8> m_prev;
    std::vector<u8> m_edge; // 바인딩 키가 이번 프레임에 새로 눌림
};

} // namespace sbx::platform
