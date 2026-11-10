#pragma once
// "레이아웃" 창 (v0.8 M6-T6 — docs/25-STUDIO-ARCHITECTURE.md 4장, ADR-0114). 지금 레이아웃 · 고르기 · 다른 이름으로
// 저장 · 기본 배치로. 화면은 고른 일만 — 적용 (ImGui 프레임 밖) · 저장은 Application 이 LayoutService 와 (25 S2).

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::editor {

struct LayoutPanelState {
    std::array<char, 33> name{}; // 다른 이름으로 저장 (LayoutService::kMaxName + NUL)
    std::string message;         // 마지막 결과 (실패면 이유)
};

struct LayoutPanelAction {
    enum class Kind : u8 { Select, SaveAs, Reset } kind = Kind::Select;
    std::string name; // Select · SaveAs
};

std::optional<LayoutPanelAction> drawLayoutPanel(LayoutPanelState& state, const std::string& active,
                                                 const std::vector<std::string>& names, bool enabled);

} // namespace sbx::editor
