#pragma once
// "콘솔" 창 (v0.8 UI-3 — docs/29-STUDIO-UI.md 4장, ADR-0118). 엔진 로그의 최근 줄 (log::recentLines — 256 줄 고리) 을
// Studio 안에서: 수준 거르기 (전부 · 경고 이상 · 오류만) · 글자 거르기 · 지우기 (그 뒤 줄만) · 복사 · 끝으로 따라가기.
// 빌드 · 테스트 세션 · 리플레이 · 자동 저장 결과가 로그로 오므로 터미널을 보지 않아도 된다 (Roblox Output · Unity
// Console · Godot Output 과 같은 자리 — 29 2장). 로그가 바뀌었을 때만 줄을 다시 가져온다 (log::recentTotal).

#include <array>
#include <string>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::editor {

struct ConsoleState {
    int level = 0;                 // 0 전부 · 1 경고 이상 · 2 오류만
    std::array<char, 96> filter{}; // 글자 거르기 (대소문자 그대로)
    bool follow = true;            // 새 줄이 오면 끝으로
    u64 seen = 0;                  // 마지막으로 가져온 log::recentTotal
    u64 clearedAt = 0;             // 지우기를 누른 때의 total — 그 뒤 줄만 보인다
    std::vector<std::string> lines;
};

// 줄의 수준 ("[warn] [studio] …" → 1, "[error] …" → 2, 그 밖 0)
[[nodiscard]] int consoleLineLevel(const std::string& line) noexcept;
// 보일 줄의 차례 (lines 의 첫 줄 = total - lines.size() 번째 줄 — clearedAt 앞은 뺀다)
[[nodiscard]] std::vector<usize> consoleVisible(const ConsoleState& state, u64 total);

void drawConsolePanel(ConsoleState& state);

} // namespace sbx::editor
