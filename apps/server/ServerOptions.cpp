#include "apps/server/ServerOptions.hpp"

#include <array>
#include <charconv>
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

Expected<u64> parseU64(std::string_view option, std::string_view text) {
    u64 v = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (text.empty() || ec != std::errc{} || ptr != text.data() + text.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} 에는 0 이상의 정수가 필요합니다: '{}'", option, text));
    }
    return v;
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
        } else if (a == "--scenario") {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, "--scenario 에 값이 필요합니다");
            }
            opts.scenario = std::string(args[++i]);
        } else if (a == "--ticks" || a == "--seed") {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 에 값이 필요합니다", a));
            }
            const auto v = parseU64(a, args[++i]);
            if (!v) {
                return std::unexpected(v.error());
            }
            if (a == "--ticks") {
                opts.ticks = *v;
            } else {
                opts.seed = *v;
            }
        } else if (a == "--realtime") {
            opts.realtime = true;
        } else if (a == "--content-root") {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, "--content-root 에 값이 필요합니다");
            }
            opts.contentRoot = std::string(args[++i]);
        } else {
            return makeError(ErrorCode::InvalidArgument, std::format("unknown option '{}'", a));
        }
    }
    if ((opts.ticks || opts.realtime) && !opts.scenario) {
        return makeError(ErrorCode::InvalidArgument, "--ticks/--realtime 은 --scenario 와 함께 씁니다");
    }
    return opts;
}

std::string serverUsage() {
    return "사용법: SandboxServer [옵션]\n"
           "  -h, --help             이 도움말\n"
           "      --version          버전과 빌드 정보\n"
           "      --log-level <lvl>  trace|debug|info|warn|error|off (기본 info)\n"
           "      --scenario <name>  헤드리스 시나리오 실행 (sbx_sim_check --list-scenarios)\n"
           "      --ticks <n>        진행할 틱 수 (기본: 시나리오 기본값)\n"
           "      --seed <n>         월드 시드 (기본 1)\n"
           "      --realtime         30 Hz 로 페이싱 (기본: 최대 속도)\n"
           "      --content-root <d> 콘텐츠 팩 루트 (기본: 저장소의 content/)\n"
           "\n"
           "월드 파일·네트워크(--world, --port …)는 Phase 9 에서 추가됩니다 (docs/16-ROADMAP.md).\n";
}

} // namespace sbx::server
