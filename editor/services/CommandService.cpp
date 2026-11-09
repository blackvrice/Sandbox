#include "editor/services/CommandService.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace sbx::editor::services {
namespace {

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

// 이름 키 (맞춘 꼴) — 별칭은 앞의 것으로
constexpr std::array<std::pair<std::string_view, std::string_view>, 18> kNamedKeys{{
    {"enter", "Enter"},
    {"return", "Enter"},
    {"escape", "Escape"},
    {"esc", "Escape"},
    {"tab", "Tab"},
    {"space", "Space"},
    {"backspace", "Backspace"},
    {"delete", "Delete"},
    {"del", "Delete"},
    {"insert", "Insert"},
    {"home", "Home"},
    {"end", "End"},
    {"pageup", "PageUp"},
    {"pagedown", "PageDown"},
    {"up", "Up"},
    {"down", "Down"},
    {"left", "Left"},
    {"right", "Right"},
}};

std::optional<std::string> keyName(std::string_view key) {
    if (key.size() == 1) {
        const char c = key[0];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return std::string(1, c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }
        static constexpr std::string_view kPunct = "`-=[]\\;',./";
        if (kPunct.find(c) != std::string_view::npos) {
            return std::string(1, c);
        }
        return std::nullopt;
    }
    if ((key[0] == 'f' || key[0] == 'F') && key.size() <= 3) {
        i32 n = 0;
        for (usize i = 1; i < key.size(); ++i) {
            if (key[i] < '0' || key[i] > '9') {
                return std::nullopt;
            }
            n = n * 10 + (key[i] - '0');
        }
        if (n >= 1 && n <= 24 && key[1] != '0') {
            return std::format("F{}", n);
        }
        return std::nullopt;
    }
    for (const auto& [alias, name] : kNamedKeys) {
        if (equalsIgnoreCase(key, alias)) {
            return std::string(name);
        }
    }
    return std::nullopt;
}

bool overlaps(std::string_view a, std::string_view b) {
    return a == b || a == kGlobalContext || b == kGlobalContext;
}

} // namespace

Expected<std::string> normalizeShortcut(std::string_view text) {
    if (text.empty()) {
        return std::string();
    }
    // 수정자 비트: Ctrl 1 · Shift 2 · Alt 4 · Super 8 (적는 차례)
    u32 mods = 0;
    std::optional<std::string> key;
    usize start = 0;
    while (start <= text.size()) {
        usize plus = text.find('+', start);
        if (plus == start && plus + 1 == text.size()) { // 끝의 "+" 키 자체 ("Ctrl++" 는 받지 않는다 — '=' 를 쓴다)
            return makeError(ErrorCode::InvalidArgument, std::format("단축키 '{}' — '+' 키는 쓸 수 없다", text));
        }
        if (plus == std::string_view::npos) {
            plus = text.size();
        }
        const std::string_view part = text.substr(start, plus - start);
        if (part.empty()) {
            return makeError(ErrorCode::InvalidArgument, std::format("단축키 '{}' — 빈 칸", text));
        }
        if (key) {
            return makeError(ErrorCode::InvalidArgument, std::format("단축키 '{}' — 키는 끝에 하나", text));
        }
        const std::string p = lowerAscii(part);
        u32 bit = 0;
        if (p == "ctrl" || p == "control") {
            bit = 1;
        } else if (p == "shift") {
            bit = 2;
        } else if (p == "alt" || p == "option") {
            bit = 4;
        } else if (p == "super" || p == "cmd" || p == "win" || p == "meta") {
            bit = 8;
        }
        if (bit != 0) {
            if ((mods & bit) != 0) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("단축키 '{}' — 수정자 '{}' 가 두 번", text, part));
            }
            mods |= bit;
        } else {
            key = keyName(part);
            if (!key) {
                return makeError(ErrorCode::InvalidArgument, std::format("단축키 '{}' — 모르는 키 '{}'", text, part));
            }
        }
        start = plus + 1;
    }
    if (!key) {
        return makeError(ErrorCode::InvalidArgument, std::format("단축키 '{}' — 키가 없다 (수정자만)", text));
    }
    std::string out;
    for (const auto& [bit, name] :
         {std::pair<u32, std::string_view>{1, "Ctrl"}, {2, "Shift"}, {4, "Alt"}, {8, "Super"}}) {
        if ((mods & bit) != 0) {
            out += name;
            out += '+';
        }
    }
    return out + *key;
}

bool isCommandId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64 || id.front() == '.' || id.back() == '.' ||
        id.find('.') == std::string_view::npos || id.find("..") != std::string_view::npos) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

Expected<void> CommandService::add(CommandSpec spec) {
    if (!isCommandId(spec.id)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("명령 id '{}' — \"<영역>.<이름>\" (소문자 · 숫자 · _ · -)", spec.id));
    }
    if (find(spec.id) != nullptr) {
        return makeError(ErrorCode::AlreadyExists, std::format("명령 '{}' 가 이미 있다", spec.id));
    }
    if (spec.title.empty() || !spec.run) {
        return makeError(ErrorCode::InvalidArgument, std::format("명령 '{}' — 제목과 실행이 필요하다", spec.id));
    }
    if (spec.context.empty()) {
        return makeError(ErrorCode::InvalidArgument, std::format("명령 '{}' — 빈 context", spec.id));
    }
    Expected<std::string> shortcut = normalizeShortcut(spec.shortcut);
    if (!shortcut) {
        return std::unexpected(shortcut.error());
    }
    spec.shortcut = std::move(*shortcut);
    m_commands.push_back(std::move(spec));
    return {};
}

const CommandSpec* CommandService::find(std::string_view id) const {
    const auto it =
        std::find_if(m_commands.begin(), m_commands.end(), [&](const CommandSpec& c) { return c.id == id; });
    return it == m_commands.end() ? nullptr : &*it;
}

bool CommandService::enabled(std::string_view id) const {
    const CommandSpec* c = find(id);
    return c != nullptr && (!c->when || c->when());
}

RunResult CommandService::run(std::string_view id) const {
    const CommandSpec* c = find(id);
    if (c == nullptr) {
        return RunResult::Unknown;
    }
    if (c->when && !c->when()) {
        return RunResult::Disabled;
    }
    c->run();
    return RunResult::Ran;
}

std::optional<std::string> CommandService::runShortcut(std::string_view shortcut, std::string_view context) const {
    const Expected<std::string> s = normalizeShortcut(shortcut);
    if (!s || s->empty()) {
        return std::nullopt;
    }
    // 그 곳의 명령 먼저, 그다음 global
    for (const bool exact : {true, false}) {
        for (const CommandSpec& c : m_commands) {
            const bool here = exact ? c.context == context : (c.context == kGlobalContext && context != kGlobalContext);
            if (here && c.shortcut == *s && (!c.when || c.when())) {
                c.run();
                return c.id;
            }
        }
    }
    return std::nullopt;
}

std::vector<const CommandSpec*> CommandService::search(std::string_view query) const {
    const std::string q = lowerAscii(query);
    std::vector<std::pair<i32, const CommandSpec*>> hits;
    for (const CommandSpec& c : m_commands) {
        const std::string title = lowerAscii(c.title);
        i32 rank = -1;
        if (q.empty() || title.starts_with(q)) {
            rank = 0;
        } else if (title.find(q) != std::string::npos) {
            rank = 1;
        } else if (c.id.find(q) != std::string::npos) {
            rank = 2;
        }
        if (rank >= 0) {
            hits.emplace_back(rank, &c);
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<const CommandSpec*> out;
    out.reserve(hits.size());
    for (const auto& [rank, c] : hits) {
        out.push_back(c);
    }
    return out;
}

std::vector<ShortcutConflict> CommandService::conflicts() const {
    std::vector<ShortcutConflict> out;
    for (usize i = 0; i < m_commands.size(); ++i) {
        for (usize j = i + 1; j < m_commands.size(); ++j) {
            const CommandSpec& a = m_commands[i];
            const CommandSpec& b = m_commands[j];
            if (!a.shortcut.empty() && a.shortcut == b.shortcut && overlaps(a.context, b.context)) {
                out.push_back({a.shortcut, a.id, b.id});
            }
        }
    }
    return out;
}

} // namespace sbx::editor::services
