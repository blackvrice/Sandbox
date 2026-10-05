#include "apps/server/ServerOptions.hpp"

#include <array>
#include <format>

namespace sbx::server {
namespace {

std::optional<log::Level> parseLevel(std::string_view s) {
    constexpr std::array levels{log::Level::Trace, log::Level::Debug, log::Level::Info,
                                log::Level::Warn,  log::Level::Error, log::Level::Off};
    for (const auto l : levels) {
        if (log::levelName(l) == s) {
            return l;
        }
    }
    return std::nullopt;
}

} // namespace

Expected<ServerOptions> parseServerOptions(std::span<const std::string_view> args) {
    ServerOptions opts;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        if (a == "--help" || a == "-h") {
            opts.showHelp = true;
        } else if (a == "--version") {
            opts.showVersion = true;
        } else if (a == "--log-level") {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, "--log-level 에 값이 필요합니다");
            }
            const auto level = parseLevel(args[++i]);
            if (!level) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("알 수 없는 로그 레벨 '{}' (trace|debug|info|warn|error|off)", args[i]));
            }
            opts.logLevel = level;
        } else {
            return makeError(ErrorCode::InvalidArgument, std::format("unknown option '{}'", a));
        }
    }
    return opts;
}

std::string serverUsage() {
    return "사용법: SandboxServer [옵션]\n"
           "  -h, --help             이 도움말\n"
           "      --version          버전과 빌드 정보\n"
           "      --log-level <lvl>  trace|debug|info|warn|error|off (기본 info)\n"
           "\n"
           "월드 실행(--world, --port, --ticks …)은 Phase 9 에서 추가됩니다 (docs/16-ROADMAP.md).\n";
}

} // namespace sbx::server
