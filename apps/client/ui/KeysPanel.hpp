#pragma once
// 키 다시 묶기 (v0.7 M4-T3 — ADR-0083 5, docs/20-CREATOR-ALPHA.md 5장).
//
//   KeyBindingEditor   엔진 기본값 (DefaultInput) 과 지금 맵 (기본 + 사용자 input.json + --input + 패널에서 바꾼 것).
//                      rebind = 액션의 바인딩을 그 하나로, reset = 엔진 액션은 기본값으로 · play.* 는 덮어쓰기를 빼서
//                      팩 기본으로, resetAll = 기본값 그대로. overrides = 기본과 다른 것만 (저장 — <설정
//                      폴더>/input.json)
//   "키" 패널          엔진 키 · 게임 키 (팩 play.* — 지금 쓰는 값) 목록, 바꾸기 (다음에 누른 키 — Esc 취소) · 기본 ·
//                      모두 기본 · 저장, 겹침 (엔진 · 게임 키를 함께 — 노랑). ImGui:: 호출만 (값은 KeysView 로)

#include <optional>
#include <string>
#include <vector>

#include "platform/common/ActionMap.hpp"

namespace sbx::client {

class KeyBindingEditor {
public:
    KeyBindingEditor() = default;
    KeyBindingEditor(platform::ActionMap defaults, platform::ActionMap current)
        : m_defaults(std::move(defaults)), m_current(std::move(current)) {}

    [[nodiscard]] const platform::ActionMap& defaults() const noexcept { return m_defaults; }
    [[nodiscard]] const platform::ActionMap& current() const noexcept { return m_current; }
    void rebind(std::string_view action, const platform::Binding& binding);
    void reset(std::string_view action);
    void resetAll();
    [[nodiscard]] platform::ActionMap overrides() const { return m_current.overridesFrom(m_defaults); }
    [[nodiscard]] bool dirty() const noexcept { return m_dirty; } // 저장한 뒤 바뀌었다
    void markSaved() noexcept { m_dirty = false; }

private:
    platform::ActionMap m_defaults;
    platform::ActionMap m_current;
    bool m_dirty = false;
};

struct KeyRow {
    std::string action;
    std::string keys;     // "W / Up" (없으면 빈 글자)
    bool play = false;    // 팩 게임 키 (play.*)
    bool changed = false; // 기본과 다르다 (엔진: 기본 바인딩과 다름, 게임: 사용자 덮어쓰기가 있다)
};

struct KeysView {
    std::vector<KeyRow> rows;           // 엔진 (이름 순) 다음 게임 키 (이름 순)
    std::vector<std::string> conflicts; // "Q — a · b"
    bool dirty = false;
    bool canSave = true; // 안전 모드 · 설정 폴더 없음이면 false
};

// play = 지금의 게임 키 맵 (PlayKeys::map — 팩 기본 + 사용자 play.* 덮어쓰기)
[[nodiscard]] KeysView buildKeysView(const KeyBindingEditor& editor, const platform::ActionMap& play);

struct KeysPanelState {
    std::string capturing; // 바꾸는 중인 액션 (비면 없음 — 다음에 누른 키가 그 바인딩)
    std::string status;    // 마지막 결과 (저장 · 오류 · 취소)
};

struct KeysPanelRequest {
    enum class Kind : u8 { Capture, Cancel, Reset, ResetAll, Save };
    Kind kind = Kind::Capture;
    std::string action;
};

[[nodiscard]] std::optional<KeysPanelRequest> drawKeysPanel(const KeysView& view, const KeysPanelState& state);

} // namespace sbx::client
