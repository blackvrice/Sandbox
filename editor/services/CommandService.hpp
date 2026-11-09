#pragma once
// Studio 명령 표 (v0.8 ST-06-1 — docs/25-STUDIO-ARCHITECTURE.md 3장, ADR-0089 3 · ADR-0108). 메뉴 · 명령 팔레트 ·
// 단축키 · 도움말이 같은 표를 본다. UI 를 모른다 (editor/services 는 ImGui · render · platform 을 include 하지 않는다 —
// tools/check_includes).
//
// id: "<영역>.<이름>" (소문자 · 숫자 · '_' · '-', 점 하나 이상, 64 바이트까지) — 겹치면 등록 거절.
// shortcut: "Ctrl+Shift+P" 꼴 — 수정자 Ctrl · Shift · Alt · Super 를 이 차례로, 키는 글자 하나 · 숫자 · F1 ~ F24 · 이름
// 키. 대소문자 · 수정자 차례 · 별칭 (Control · Cmd · Win · Meta · Option · Esc · Del · Return) 을 맞춰 적는다. 비면 단축키
// 없음, 꼴이 틀리면 등록 거절.
// context: 단축키가 듣는 곳 ("global" — 어디서나, 그 밖은 화면 이름 "scene" · "script" …). 같은 단축키가 겹치는 곳
// (같은 곳이거나 한쪽이 global) 에 둘이면 충돌 — 등록은 받고 conflicts() 가 알린다 (사용자 키 덮기가 풀 수 있다).
// when: 실행할 수 있는가 (없으면 언제나) — 팔레트는 흐리게, 단축키 · run 은 하지 않는다.
// 단축키 실행 (runShortcut): 그 곳 (context) 의 명령이 global 보다 먼저, 같은 곳 안에서는 등록 차례 — 실행할 수 있는 첫
// 명령 하나.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"

namespace sbx::editor::services {

inline constexpr std::string_view kGlobalContext = "global";

struct CommandSpec {
    std::string id;
    std::string title;    // 메뉴 · 팔레트에 보이는 이름
    std::string shortcut; // 등록 뒤에는 맞춘 꼴 (normalizeShortcut)
    std::string context{kGlobalContext};
    std::function<bool()> when{}; // 없으면 언제나
    std::function<void()> run{};
};

struct ShortcutConflict {
    std::string shortcut;
    std::string first;  // 먼저 등록한 명령 id
    std::string second; // 나중
};

enum class RunResult : u8 { Ran, Unknown, Disabled };

// 단축키를 맞춘 꼴로 ("ctrl+shift+p" → "Ctrl+Shift+P", "shift+control+z" → "Ctrl+Shift+Z"). 빈 글자는 빈 글자
[[nodiscard]] Expected<std::string> normalizeShortcut(std::string_view text);
[[nodiscard]] bool isCommandId(std::string_view id) noexcept;

class CommandService {
public:
    // 거절: id 꼴 · 겹친 id · 단축키 꼴 · run 없음 · 제목 없음
    Expected<void> add(CommandSpec spec);
    [[nodiscard]] const CommandSpec* find(std::string_view id) const;
    [[nodiscard]] bool enabled(std::string_view id) const; // 있고 when 이 참
    RunResult run(std::string_view id) const;
    // 그 곳에서 이 단축키 (맞춘 꼴이 아니어도 된다) 의 실행할 수 있는 명령을 실행 — 실행한 명령 id (없으면 nullopt)
    std::optional<std::string> runShortcut(std::string_view shortcut, std::string_view context) const;
    // 팔레트 찾기: 제목 · id 에 query 가 든 것 (대소문자 무시 — ASCII), 제목 앞에서 맞은 것 → 제목 안 → id 차례, 같으면
    // 등록 차례. 빈 query 는 모두
    [[nodiscard]] std::vector<const CommandSpec*> search(std::string_view query) const;
    [[nodiscard]] std::vector<ShortcutConflict> conflicts() const;
    [[nodiscard]] const std::vector<CommandSpec>& commands() const noexcept { return m_commands; }

private:
    std::vector<CommandSpec> m_commands; // 등록 차례
};

} // namespace sbx::editor::services
