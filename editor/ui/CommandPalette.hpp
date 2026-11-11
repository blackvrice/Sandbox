#pragma once
// 명령 팔레트 · 단축키 표 (v0.8 ST-06-2 — docs/25-STUDIO-ARCHITECTURE.md 4장, docs/29-STUDIO-UI.md, ADR-0122).
// CommandService 의 표 하나를 메뉴 · 툴바 · 팔레트 · 단축키 표가 같이 본다.
//
// 팔레트: Ctrl+Shift+P — 이름 · id 로 찾기 (CommandService::search), ↑ ↓ 고르기 · Enter 실행 · Esc 닫기 · 포커스를
// 잃으면 닫기. 지금 할 수 없는 명령은 흐리게 (고를 수 없다). 줄마다 단축키 (또는 keyHint — 행동 표가 듣는 키).
// 단축키 표: 명령마다 키 · 이름 · id, 아래에 충돌 (같은 곳의 같은 단축키 — CommandService::conflicts).
// shortcutChord: "Ctrl+Shift+P" 꼴을 ImGui 키 조합으로 (글자 · 숫자 · F1 ~ F12 · 몇 이름 키). 모르는 꼴은 nullopt.

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

#include "editor/services/CommandService.hpp"

namespace sbx::editor {

struct PaletteState {
    bool open = false;
    bool focusInput = false;
    std::array<char, 128> query{};
    int selected = 0;
};

// 맞춘 꼴 단축키 → ImGui 키 조합 (모르는 키 · 빈 글자면 nullopt)
[[nodiscard]] std::optional<ImGuiKeyChord> shortcutChord(std::string_view shortcut) noexcept;

// 팔레트를 연다 (다음 프레임에 입력 칸에 포커스)
void openPalette(PaletteState& state);
// 그린다 — 실행할 명령 id (없으면 nullopt). 열려 있지 않으면 아무것도 그리지 않는다
[[nodiscard]] std::optional<std::string> drawCommandPalette(PaletteState& state,
                                                            const services::CommandService& commands);
// 단축키 표 창 (open 이 false 면 그리지 않는다 — 닫기 단추가 바꾼다)
void drawShortcutTable(bool& open, const services::CommandService& commands);

} // namespace sbx::editor
