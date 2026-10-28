#include "foundation/diag/Report.hpp"

#include <chrono>
#include <cstdlib>
#include <format>

#include "foundation/BuildInfo.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::diag {
namespace {

namespace fs = std::filesystem;

std::string envOr(const char* name) {
    // getenv 는 이 함수 안에서만 읽고 바로 복사한다 (보고 경로 — 시뮬레이션과 무관)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* v = std::getenv(name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return v != nullptr ? std::string(v) : std::string();
}

// JSON 문자열 (따옴표 포함). Foundation 은 JSON 라이브러리를 모른다 — 제어 문자 · 따옴표 · 역슬래시만 바꾼다 (UTF-8
// 그대로)
std::string quote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

std::string utcNow(std::string_view pattern) {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::vformat(std::string("{:") + std::string(pattern) + "}", std::make_format_args(now));
}

} // namespace

fs::path defaultReportsRoot() {
    if (std::string v = envOr("SBX_REPORTS_DIR"); !v.empty()) {
        return fs::path(v);
    }
#ifdef _WIN32
    if (std::string v = envOr("LOCALAPPDATA"); !v.empty()) {
        return fs::path(v) / "sandbox" / "reports";
    }
#else
    if (std::string v = envOr("XDG_STATE_HOME"); !v.empty()) {
        return fs::path(v) / "sandbox" / "reports";
    }
    if (std::string v = envOr("HOME"); !v.empty()) {
        return fs::path(v) / ".local" / "state" / "sandbox" / "reports";
    }
#endif
    std::error_code ec;
    return fs::temp_directory_path(ec) / "sandbox-reports";
}

Expected<fs::path> createReportDir(const fs::path& root, std::string_view app) {
    if (auto made = io::createDirectories(root); !made) {
        return std::unexpected(made.error());
    }
    const std::string base = std::format("{}-{}", utcNow("%Y%m%d-%H%M%S"), app);
    for (int n = 1; n < 1000; ++n) {
        const fs::path dir = root / (n == 1 ? base : std::format("{}-{}", base, n));
        std::error_code ec;
        if (fs::create_directory(dir, ec)) {
            return dir;
        }
        if (ec) {
            return makeError(ErrorCode::IoError, std::format("보고 폴더를 만들 수 없다 — {}", ec.message()),
                             io::displayPath(dir));
        }
    }
    return makeError(ErrorCode::IoError, "같은 초의 보고 폴더가 너무 많다", io::displayPath(root));
}

Expected<void> writeReport(const fs::path& dir, const ReportInfo& info, std::span<const std::string> logLines,
                           const ScrubRules& rules, std::span<const std::string> extraFiles) {
    const auto s = [&](std::string_view text) { return quote(scrub(text, rules)); };
    std::string json = "{\n";
    json += std::format("  \"format\": {},\n", kReportFormat);
    json += std::format("  \"app\": {},\n", s(info.app));
    json += std::format("  \"reason\": {},\n", s(info.reason));
    json += std::format("  \"time\": {},\n", quote(utcNow("%Y-%m-%dT%H:%M:%SZ")));
    json += std::format("  \"version\": {},\n", quote(build::kVersionString));
    json += std::format("  \"commit\": {},\n", quote(build::kGitCommit));
    json += std::format("  \"toolchain\": {},\n", quote(build::kToolchainKey));
    json += std::format("  \"system\": {},\n", quote(build::kSystemName));
    json += "  \"args\": [";
    for (std::size_t i = 0; i < info.args.size(); ++i) {
        json += (i == 0 ? "" : ", ") + s(info.args[i]);
    }
    json += "],\n  \"context\": {";
    for (std::size_t i = 0; i < info.context.size(); ++i) {
        json += std::format("{}\n    {}: {}", i == 0 ? "" : ",", s(info.context[i].first), s(info.context[i].second));
    }
    json += info.context.empty() ? "},\n" : "\n  },\n";
    json += "  \"files\": [\"log.txt\"";
    for (const std::string& f : extraFiles) {
        json += ", " + quote(f);
    }
    json += "]\n}\n";
    if (auto w = io::writeFileAtomic(dir / "report.json", json); !w) {
        return w;
    }
    std::string log;
    for (const std::string& line : logLines) {
        log += scrub(line, rules);
        log += '\n';
    }
    return io::writeFileAtomic(dir / "log.txt", log);
}

} // namespace sbx::diag
