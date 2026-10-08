#include "apps/server/ServerOptions.hpp"

#include <algorithm>
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

Expected<u64> parseBounded(std::string_view option, std::string_view text, u64 min, u64 max) {
    auto v = parseU64(option, text);
    if (v && (*v < min || *v > max)) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} 는 {} ~ {} 입니다: '{}'", option, min, max, text));
    }
    return v;
}

constexpr std::array<std::string_view, 5> kRoles{"observer", "player", "editor", "admin", "owner"};

} // namespace

Expected<ServerOptions> parseServerOptions(std::span<const std::string_view> args) {
    ServerOptions opts;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        // 값이 필요한 옵션
        auto value = [&]() -> Expected<std::string_view> {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 에 값이 필요합니다", a));
            }
            return args[++i];
        };
        if (a == "--help" || a == "-h") {
            opts.showHelp = true;
        } else if (a == "--version") {
            opts.showVersion = true;
        } else if (a == "--log-level") {
            const auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            const auto level = parseLevel(*v);
            if (!level) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("알 수 없는 로그 레벨 '{}' (trace|debug|info|warn|error|off)", *v));
            }
            opts.logLevel = level;
        } else if (a == "--scenario" || a == "--world" || a == "--content-root" || a == "--bind" ||
                   a == "--default-role") {
            const auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            if (a == "--scenario") {
                opts.scenario = std::string(*v);
            } else if (a == "--world") {
                opts.world = std::string(*v);
            } else if (a == "--content-root") {
                opts.contentRoot = std::string(*v);
            } else if (a == "--bind") {
                opts.bind = std::string(*v);
            } else {
                if (std::find(kRoles.begin(), kRoles.end(), *v) == kRoles.end()) {
                    return makeError(ErrorCode::InvalidArgument,
                                     std::format("--default-role 은 observer|player|editor|admin|owner: '{}'", *v));
                }
                opts.defaultRole = std::string(*v);
            }
        } else if (a == "--ticks" || a == "--seed" || a == "--port" || a == "--max-clients" || a == "--threads" ||
                   a == "--snapshot-kbps") {
            const auto text = value();
            if (!text) {
                return std::unexpected(text.error());
            }
            Expected<u64> v = a == "--port"            ? parseBounded(a, *text, 0, 65535)
                              : a == "--max-clients"   ? parseBounded(a, *text, 1, 255)
                              : a == "--threads"       ? parseBounded(a, *text, 0, 64)
                              : a == "--snapshot-kbps" ? parseBounded(a, *text, 0, 1024 * 1024)
                                                       : parseU64(a, *text);
            if (!v) {
                return std::unexpected(v.error());
            }
            if (a == "--ticks") {
                opts.ticks = *v;
            } else if (a == "--seed") {
                opts.seed = *v;
            } else if (a == "--port") {
                opts.port = static_cast<u16>(*v);
            } else if (a == "--max-clients") {
                opts.maxClients = static_cast<u32>(*v);
            } else if (a == "--threads") {
                opts.threads = static_cast<u32>(*v);
            } else {
                opts.snapshotKBps = static_cast<u32>(*v);
            }
        } else if (a == "--realtime") {
            opts.realtime = true;
        } else if (a == "--exit") {
            opts.exitAtTicks = true;
        } else {
            return makeError(ErrorCode::InvalidArgument, std::format("unknown option '{}'", a));
        }
    }
    if (opts.scenario && opts.world) {
        return makeError(ErrorCode::InvalidArgument, "--scenario(헤드리스) 와 --world(네트워크 서버) 중 하나만");
    }
    if (opts.world) {
        if (opts.realtime) {
            return makeError(ErrorCode::InvalidArgument,
                             "--world 서버는 언제나 실시간입니다 (--realtime 은 --scenario 용)");
        }
        if (opts.ticks.has_value() != opts.exitAtTicks) {
            return makeError(ErrorCode::InvalidArgument, "--world 와 함께라면 --ticks N 과 --exit 를 같이 씁니다");
        }
    } else {
        if ((opts.ticks || opts.realtime) && !opts.scenario) {
            return makeError(ErrorCode::InvalidArgument, "--ticks/--realtime 은 --scenario 와 함께 씁니다");
        }
        if (opts.exitAtTicks) {
            return makeError(ErrorCode::InvalidArgument, "--exit 는 --world 와 함께 씁니다");
        }
    }
    return opts;
}

std::string serverUsage() {
    return "사용법: SandboxServer [옵션]\n"
           "  -h, --help               이 도움말\n"
           "      --version            버전과 빌드 정보\n"
           "      --log-level <lvl>    trace|debug|info|warn|error|off (기본 info)\n"
           "      --content-root <d>   콘텐츠 팩 루트 (기본: 저장소의 content/)\n"
           "      --seed <n>           월드 시드 (기본 1)\n"
           "\n"
           "네트워크 서버 (Phase 9 — 클라이언트가 접속해 명령을 보낸다. 암호화 없음: LAN · 신뢰하는 환경 전용)\n"
           "      --world <이름|폴더>   시나리오 이름(ecosystem_small …) 또는 세이브 폴더(world.json)\n"
           "      --port <n>           UDP 포트 (기본 7777, 0 = 빈 포트)\n"
           "      --bind <주소>        받을 주소 (기본: 모든 인터페이스)\n"
           "      --max-clients <n>    최대 접속 (기본 16)\n"
           "      --default-role <r>   새 접속자 역할 observer|player|editor|admin|owner (기본 editor —\n"
           "                           일시정지 · 속도는 admin 부터, docs/10-EDITOR.md 7장)\n"
           "      --threads <n>        시뮬레이션 Worker 수 (기본: 코어 수 - 2, 1 ~ 4)\n"
           "      --snapshot-kbps <n>  클라이언트당 복제 예산 KB/s (기본 256, 0 = 제한 없음)\n"
           "      --ticks <n> --exit   그 틱에 도달하면 접속자를 끊고 끝낸다 (CI)\n"
           "      Ctrl+C               접속자에게 서버 종료를 알리고 끝낸다\n"
           "\n"
           "헤드리스 시나리오 (Phase 3 — 네트워크 없이 최대 속도로)\n"
           "      --scenario <name>    시나리오 실행 (sbx_sim_check --list-scenarios)\n"
           "      --ticks <n>          진행할 틱 수 (기본: 시나리오 기본값)\n"
           "      --realtime           30 Hz 로 페이싱\n";
}

} // namespace sbx::server
