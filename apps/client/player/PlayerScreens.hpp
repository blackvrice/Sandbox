#pragma once
// Player 모드 화면 (v0.8 NG-03 — docs/25-STUDIO-ARCHITECTURE.md 12장, ADR-0121): 받는 사람의 홈 (최근 게임 · 게임 파일
// 열기) 과 열지 못했을 때의 오류 화면 (무엇이 · 무엇을 하면 · 자세히 · 보고 묶음 만들기). Studio 의 디자인 위젯
// (StudioWidgets) 을 그대로 쓴다. 화면은 고른 일만 돌려준다 — 여는 일 · 보고 묶음은 Application.

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "apps/client/player/PlayerLibrary.hpp"

namespace sbx::client {

struct PlayerHomeState {
    std::array<char, 512> openPath{};
    std::string message;
    bool failed = false;
    std::string reportPath; // 오류 화면에서 만든 보고 묶음
};

struct PlayerAction {
    enum class Kind : u8 { OpenGame, Forget, MakeReport, Back, Quit };
    Kind kind = Kind::OpenGame;
    std::filesystem::path path;
};

std::optional<PlayerAction> drawPlayerHome(PlayerHomeState& state, const PlayerLibrary& library,
                                           std::string_view version);
std::optional<PlayerAction> drawPlayError(PlayerHomeState& state, const PlayError& error);

} // namespace sbx::client
