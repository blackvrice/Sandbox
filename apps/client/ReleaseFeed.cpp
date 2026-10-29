#include "apps/client/ReleaseFeed.hpp"

#include <filesystem>
#include <format>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"
#include "platform/process/Process.hpp"

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

bool isUrlFeed(std::string_view feed) {
    return feed.find("://") != std::string_view::npos;
}

Expected<std::vector<std::string>> curlArguments(std::string_view url, std::string_view userAgent) {
    if (!url.starts_with("https://")) {
        return makeError(ErrorCode::InvalidArgument, "피드 URL 은 https:// 만 읽는다");
    }
    if (url.size() > kMaxFeedUrlBytes) {
        return makeError(ErrorCode::InvalidArgument, "피드 URL 이 너무 길다");
    }
    for (const char c : url) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) {
            return makeError(ErrorCode::InvalidArgument, "피드 URL 에 공백 · 제어 문자가 있다");
        }
    }
    // 셸 없이 띄운다 (proc::runProcess). "--" 뒤라 URL 이 옵션으로 읽히지 않는다. https 만 (리디렉트도)
    return std::vector<std::string>{"curl",
                                    "--fail",
                                    "--silent",
                                    "--show-error",
                                    "--location",
                                    "--proto",
                                    "=https",
                                    "--proto-redir",
                                    "=https",
                                    "--max-time",
                                    std::to_string(kFeedTimeoutSeconds),
                                    "--max-filesize",
                                    std::to_string(kMaxFeedBytes),
                                    "-H",
                                    "Accept: application/vnd.github+json",
                                    "-A",
                                    std::string(userAgent),
                                    "--",
                                    std::string(url)};
}

FeedCheck checkReleaseFeed(std::string_view feed, const SemVer& current) {
    FeedCheck out;
    std::string text;
    if (isUrlFeed(feed)) {
        auto args = curlArguments(feed, "SandboxClient/" + current.toString());
        if (!args) {
            out.detail = std::format("릴리스 피드 {}: {}", feed, args.error().message);
            return out;
        }
        auto r = proc::runProcess(*args, {}, kMaxFeedBytes + 4096);
        if (!r) {
            out.detail = std::format("릴리스 피드를 읽지 못했다 (curl — 조용히 건너뛴다): {}", r.error().message);
            return out;
        }
        if (r->exitCode != 0) {
            // 오프라인 · 시간 초과 · HTTP 오류 — 조용히 (curl 의 한 줄만 로그로)
            const std::string first = r->output.substr(0, r->output.find('\n'));
            out.detail =
                std::format("릴리스 피드를 읽지 못했다 (curl 종료 {} — 조용히 건너뛴다): {}", r->exitCode, first);
            return out;
        }
        text = std::move(r->output);
    } else {
        auto file = io::readFile(std::filesystem::path(std::u8string(feed.begin(), feed.end())));
        if (!file) {
            out.detail = std::format("릴리스 피드를 읽지 못했다 (조용히 건너뛴다): {}", file.error().describe());
            return out;
        }
        text = std::move(*file);
    }
    const auto releases = parseReleaseFeed(text);
    if (!releases) {
        out.detail = std::format("릴리스 피드 {}: {}", feed, releases.error().message);
        return out;
    }
    const auto newest = newestRelease(*releases, current);
    if (!newest) {
        out.detail = std::format("새 버전 없음 (지금 {}, 피드 {} 개)", current.toString(), releases->size());
        return out;
    }
    out.notice = updateNotice(*newest, current);
    out.detail = *out.notice;
    return out;
}

std::string updateNotice(const ReleaseInfo& release, const SemVer& current) {
    return std::format("새 버전 {}{} 이 있다 (지금 {}){} — 받기 · 설치는 직접", release.version.toString(),
                       release.prerelease ? " (프리릴리스)" : "", current.toString(),
                       release.url.empty() ? std::string() : " — " + release.url);
}

} // namespace sbx::client
