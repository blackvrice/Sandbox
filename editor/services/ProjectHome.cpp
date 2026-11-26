#include "editor/services/ProjectHome.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <system_error>

namespace sbx::editor::services {

namespace {

std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

bool containsNoCase(std::string_view hay, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != hay.end();
}

} // namespace

std::vector<usize> filterRecent(const std::vector<RecentProject>& recent, std::string_view query) {
    std::vector<usize> out;
    for (usize i = 0; i < recent.size(); ++i) {
        const auto& r = recent[i];
        if (containsNoCase(r.name, query) || containsNoCase(r.gameId, query) || containsNoCase(utf8(r.dir), query)) {
            out.push_back(i);
        }
    }
    return out;
}

std::filesystem::path suggestProjectDir(const std::vector<RecentProject>& recent,
                                        std::string_view name) {
    std::string folder;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            folder.push_back(c);
        }
    }
    if (folder.empty()) {
        folder = "project";
    }
    std::error_code ec;
    std::filesystem::path parent =
        !recent.empty() ? recent.front().dir.parent_path() : std::filesystem::current_path(ec);
    std::filesystem::path dir = parent / folder;
    // 있는 폴더면 -2, -3 … (빈 폴더는 그대로 써도 된다)
    for (int n = 2; std::filesystem::exists(dir, ec) && !std::filesystem::is_empty(dir, ec) && n < 100; ++n) {
        dir = parent / std::format("{}-{}", folder, n);
    }
    return dir;
}

std::string suggestGameId(std::string_view name) {
    std::string tail;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 && u < 0x80) {
            tail.push_back(static_cast<char>(std::tolower(u)));
        } else if ((c == ' ' || c == '-' || c == '_') && !tail.empty() && tail.back() != '_') {
            tail.push_back('_');
        }
    }
    while (!tail.empty() && tail.back() == '_') {
        tail.pop_back();
    }
    return "me." + (tail.empty() ? std::string("game") : tail);
}

std::string displayTime(std::string_view utc) {
    // "2026-11-10T09:12:03Z" → "2026-11-10 09:12 (UTC)"
    if (utc.size() >= 16 && utc[10] == 'T') {
        return std::format("{} {} (UTC)", utc.substr(0, 10), utc.substr(11, 5));
    }
    return std::string(utc);
}

} // namespace sbx::editor::services
