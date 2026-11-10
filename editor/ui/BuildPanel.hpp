#pragma once
// "빌드" 창 (v0.8 M6-T3 — docs/25-STUDIO-ARCHITECTURE.md 5장 ⑩, ADR-0112). 지금 프로젝트 · Export Profile 고르기 ·
// [검사] [빌드] · 마지막 보고 (오류 · 경고 · 에셋 그룹 · 패키지 경로 · digest). 화면은 고른 일만 — 일은 Application 이
// 명령 표 (studio.check · studio.build) 와 BuildService 로 (25 S2).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "editor/services/BuildService.hpp"

namespace sbx::editor {

struct BuildPanelState {
    int profileIndex = 0; // 0 = 프로필 없이, 1 ~ = profiles[i - 1]
    std::optional<services::BuildReport> last;
};

enum class BuildPanelAction : u8 { Check, Build };

// 고른 프로필 이름 (없으면 빈 글자)
[[nodiscard]] std::string chosenProfile(const BuildPanelState& state, const std::vector<std::string>& profiles);

std::optional<BuildPanelAction> drawBuildPanel(BuildPanelState& state, const std::filesystem::path& project,
                                               const std::vector<std::string>& profiles, bool running);

} // namespace sbx::editor
