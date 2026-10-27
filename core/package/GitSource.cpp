#include "core/package/GitSource.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <vector>

#include "foundation/io/FileIo.hpp"

namespace sbx::package {
namespace {

namespace fs = std::filesystem;

constexpr usize kMaxUrlBytes = 2048;
constexpr usize kMaxRevBytes = 256;

// 실패한 git 의 말 — 마지막 몇 줄만 (진단이 너무 길지 않게)
// 경로 → UTF-8 (Windows 의 path::string() 은 ANSI 코드 페이지라 한글 경로가 깨진다)
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string tail(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return std::string(text.size() > 400 ? text.substr(text.size() - 400) : text);
}

} // namespace

std::optional<std::string> checkGitUrl(std::string_view url) {
    if (url.empty()) {
        return "git URL 이 비었다";
    }
    if (url.size() > kMaxUrlBytes) {
        return std::format("git URL 이 {} 바이트를 넘는다", kMaxUrlBytes);
    }
    if (url.front() == '-') {
        return "git URL 은 '-' 로 시작할 수 없다 (옵션으로 읽힌다)";
    }
    for (const char c : url) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F || c == ' ') {
            return "git URL 에 공백 · 제어 문자를 쓸 수 없다";
        }
    }
    if (url.starts_with("ext::") || url.find("::") != std::string_view::npos) {
        return "git 의 원격 도우미 문법 (<transport>::<address>) 은 쓸 수 없다 — https · ssh · file · 경로만";
    }
    return std::nullopt;
}

std::optional<std::string> checkGitRev(std::string_view rev) {
    if (rev.empty()) {
        return "rev (태그 · 커밋) 가 비었다";
    }
    if (rev.size() > kMaxRevBytes) {
        return std::format("rev 가 {} 바이트를 넘는다", kMaxRevBytes);
    }
    if (rev.front() == '-') {
        return "rev 는 '-' 로 시작할 수 없다 (옵션으로 읽힌다)";
    }
    const bool ok = std::all_of(rev.begin(), rev.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-' || c == '/';
    });
    if (!ok || rev.find("..") != std::string_view::npos) {
        return "rev 는 태그 · 커밋 이름 ([A-Za-z0-9._/-], '..' 없이)";
    }
    return std::nullopt;
}

Expected<GitFetched> fetchGitPack(std::string_view url, std::string_view rev, std::string_view expectId,
                                  const fs::path& cacheRoot, const ProcessRunner& run) {
    if (auto bad = checkGitUrl(url)) {
        return makeError(ErrorCode::InvalidArgument, *bad, std::string(url));
    }
    if (auto bad = checkGitRev(rev)) {
        return makeError(ErrorCode::InvalidArgument, *bad, std::string(rev));
    }
    const fs::path work =
        cacheRoot / "git-tmp" /
        std::format("{:x}", static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{work};
    if (auto r = io::createDirectories(work); !r) {
        return std::unexpected(r.error());
    }
    const fs::path repo = work / "repo";
    const auto git = [&](std::vector<std::string> args, const fs::path& cwd,
                         std::string_view what) -> Expected<ProcessResult> {
        // 대화형 질문은 끈다. 줄 끝 변환을 끈다 — 사용자의 core.autocrlf (Windows 기본 true) 가 LF 를 CRLF 로 바꾸면
        // 머신마다 digest 가 달라진다. 저장소의 .gitattributes (eol) 는 그대로 — 저장소가 정한 것이라 어디서나 같다
        std::vector<std::string> argv{
            "git", "-c",         "core.askPass=", "-c", "credential.interactive=never", "-c", "core.autocrlf=false",
            "-c",  "core.eol=lf"};
        argv.insert(argv.end(), args.begin(), args.end());
        auto r = run(argv, cwd);
        if (!r) {
            if (r.error().code == ErrorCode::NotFound) {
                return makeError(
                    ErrorCode::NotFound,
                    "git 을 찾을 수 없다 — Git 소스는 시스템 git 이 필요하다 (로컬 폴더 · 캐시 소스는 된다)");
            }
            return std::unexpected(r.error());
        }
        if (r->exitCode != 0) {
            return makeError(ErrorCode::IoError,
                             std::format("git {} 실패 (종료 코드 {}): {}", what, r->exitCode, tail(r->output)),
                             std::string(url));
        }
        return r;
    };
    // 1. 받기 (작업 트리 없이) → 2. rev 꺼내기 (태그 · 커밋 — 분리된 HEAD) → 3. 푼 커밋
    if (auto r = git({"clone", "--quiet", "--no-checkout", "--", std::string(url), utf8(repo)}, work, "clone"); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = git({"-c", "advice.detachedHead=false", "checkout", "--quiet", "--detach", std::string(rev), "--"},
                     repo, "checkout");
        !r) {
        return std::unexpected(r.error());
    }
    auto head = git({"rev-parse", "HEAD"}, repo, "rev-parse");
    if (!head) {
        return std::unexpected(head.error());
    }
    std::string commit = head->output.substr(0, head->output.find_first_of("\r\n"));
    if (commit.size() != 40 || !std::all_of(commit.begin(), commit.end(),
                                            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
        return makeError(ErrorCode::IoError, std::format("git rev-parse 의 답이 커밋이 아니다: '{}'", commit),
                         std::string(url));
    }
    auto cached = cacheLocalPack(repo, expectId, cacheRoot); // .git 은 digest 에 들지 않는다 (PackSource)
    if (!cached) {
        return std::unexpected(cached.error());
    }
    return GitFetched{std::move(*cached), std::move(commit)};
}

} // namespace sbx::package
