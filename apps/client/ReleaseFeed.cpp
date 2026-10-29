#include "apps/client/ReleaseFeed.hpp"

#include <filesystem>
#include <format>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::client {

Expected<std::vector<ReleaseInfo>> parseReleaseFeed(std::string_view jsonText) {
    const nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        return makeError(ErrorCode::ParseError, "릴리스 피드가 JSON 배열이 아니다");
    }
    std::vector<ReleaseInfo> out;
    for (const nlohmann::json& r : j) {
        if (!r.is_object()) {
            continue;
        }
        const auto flag = [&](const char* key) {
            const auto it = r.find(key);
            return it != r.end() && it->is_boolean() && it->get<bool>();
        };
        if (flag("draft")) {
            continue;
        }
        const auto tag = r.find("tag_name");
        if (tag == r.end() || !tag->is_string()) {
            continue;
        }
        const std::string tagText = tag->get<std::string>();
        std::string_view v = tagText;
        if (v.starts_with('v') || v.starts_with('V')) {
            v.remove_prefix(1);
        }
        auto version = parseSemVer(v);
        if (!version) {
            continue;
        }
        ReleaseInfo info;
        info.version = std::move(*version);
        info.tag = tagText;
        if (const auto url = r.find("html_url"); url != r.end() && url->is_string()) {
            info.url = url->get<std::string>();
        }
        info.prerelease = flag("prerelease");
        out.push_back(std::move(info));
    }
    return out;
}

std::optional<ReleaseInfo> newestRelease(const std::vector<ReleaseInfo>& releases, const SemVer& current) {
    std::optional<ReleaseInfo> best;
    for (const ReleaseInfo& r : releases) {
        if (compareSemVer(r.version, current) > 0 && (!best || compareSemVer(r.version, best->version) > 0)) {
            best = r;
        }
    }
    return best;
}

std::optional<std::string> checkReleaseFeed(std::string_view feed, const SemVer& current) {
    if (feed.starts_with("http://") || feed.starts_with("https://")) {
        log::info("update", "URL 피드는 아직 읽지 않는다 ([계획] — v0.5 M1-T4 공개 채널과 함께): {}", feed);
        return std::nullopt;
    }
    const auto text = io::readFile(std::filesystem::path(std::u8string(feed.begin(), feed.end())));
    if (!text) {
        log::debug("update", "릴리스 피드를 읽지 못했다 (조용히 건너뛴다): {}", text.error().describe());
        return std::nullopt;
    }
    const auto releases = parseReleaseFeed(*text);
    if (!releases) {
        log::warn("update", "릴리스 피드 {}: {}", feed, releases.error().message);
        return std::nullopt;
    }
    const auto newest = newestRelease(*releases, current);
    if (!newest) {
        log::info("update", "새 버전 없음 (지금 {}, 피드 {} 개)", current.toString(), releases->size());
        return std::nullopt;
    }
    return updateNotice(*newest, current);
}

std::string updateNotice(const ReleaseInfo& release, const SemVer& current) {
    return std::format("새 버전 {}{} 이 있다 (지금 {}){} — 받기 · 설치는 직접", release.version.toString(),
                       release.prerelease ? " (프리릴리스)" : "", current.toString(),
                       release.url.empty() ? std::string() : " — " + release.url);
}

} // namespace sbx::client
