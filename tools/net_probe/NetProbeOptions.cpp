#include "tools/net_probe/NetProbeOptions.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <format>

namespace sbx::probe {
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

Expected<f64> parseNumber(std::string_view option, std::string_view text) {
    f64 v = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (text.empty() || ec != std::errc{} || ptr != text.data() + text.size() || !std::isfinite(v)) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} 에는 수가 필요합니다: '{}'", option, text));
    }
    return v;
}

} // namespace

Expected<NetProbeOptions> parseNetProbeOptions(std::span<const std::string_view> args) {
    NetProbeOptions o;
    for (usize i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        auto value = [&]() -> Expected<std::string_view> {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 에 값이 필요합니다", a));
            }
            return args[++i];
        };
        auto number = [&]() -> Expected<f64> {
            const auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            return parseNumber(a, *v);
        };
        if (a == "--help" || a == "-h") {
            o.showHelp = true;
        } else if (a == "--pause") {
            o.commands.emplace_back(cmd::PauseSimulation{});
        } else if (a == "--resume") {
            o.commands.emplace_back(cmd::ResumeSimulation{});
        } else if (a == "--connect" || a == "--name" || a == "--content-root" || a == "--log-level") {
            const auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            if (a == "--connect") {
                o.connect = std::string(*v);
            } else if (a == "--name") {
                o.name = std::string(*v);
            } else if (a == "--content-root") {
                o.contentRoot = std::string(*v);
            } else {
                o.logLevel = parseLevel(*v);
                if (!o.logLevel) {
                    return makeError(ErrorCode::InvalidArgument, std::format("알 수 없는 로그 레벨 '{}'", *v));
                }
            }
        } else if (a == "--seconds" || a == "--timeout" || a == "--step" || a == "--speed") {
            const auto v = number();
            if (!v) {
                return std::unexpected(v.error());
            }
            if (a == "--seconds" || a == "--timeout") {
                if (*v < 0 || *v > 3600) {
                    return makeError(ErrorCode::InvalidArgument, std::format("{} 는 0 ~ 3600 초", a));
                }
                (a == "--seconds" ? o.seconds : o.connectTimeoutSeconds) = *v;
            } else if (a == "--step") {
                if (*v < 1 || *v > 3600 || std::floor(*v) != *v) {
                    return makeError(ErrorCode::InvalidArgument, "--step 은 1 ~ 3600 틱");
                }
                o.commands.emplace_back(cmd::StepSimulation{static_cast<u32>(*v)});
            } else {
                o.commands.emplace_back(cmd::SetSimulationSpeed{static_cast<f32>(*v)}); // 범위는 서버가 본다
            }
        } else if (a == "--create") {
            const auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            const auto comma = v->find(',');
            if (comma == std::string_view::npos) {
                return makeError(ErrorCode::InvalidArgument, "--create 는 x,y 입니다", std::string(*v));
            }
            const auto x = parseNumber(a, v->substr(0, comma));
            const auto y = parseNumber(a, v->substr(comma + 1));
            if (!x || !y) {
                return makeError(ErrorCode::InvalidArgument, "--create 는 x,y 입니다", std::string(*v));
            }
            cmd::CreateEntity c;
            c.position = {static_cast<f32>(*x), static_cast<f32>(*y)};
            o.commands.emplace_back(std::move(c));
        } else {
            return makeError(ErrorCode::InvalidArgument, std::format("unknown option '{}'", a));
        }
    }
    return o;
}

std::string netProbeUsage() {
    return "사용법: sbx_net_probe [옵션]\n"
           "  SandboxServer 에 접속해 핸드셰이크 · 명령 · 서버 통계를 확인한다 (화면 없음 — Phase 9).\n"
           "  -h, --help              이 도움말\n"
           "      --connect <h:p>     서버 주소 (기본 127.0.0.1:7777)\n"
           "      --name <이름>        표시 이름 (기본 probe, 32 글자까지)\n"
           "      --content-root <d>  콘텐츠 팩 루트 (서버가 쓰는 팩을 여기서 읽어 해시를 맞춘다)\n"
           "      --seconds <s>       결과를 받은 뒤 통계를 보며 머무는 시간 (기본 2)\n"
           "      --timeout <s>       접속을 기다리는 시간 (기본 10)\n"
           "      --log-level <lvl>   trace|debug|info|warn|error|off\n"
           "  보낼 명령 (쓴 순서대로; 일시정지 · 한 틱 · 속도는 admin 역할부터 — 서버 --default-role)\n"
           "      --pause | --resume | --step <n> | --speed <x> | --create <x,y>\n"
           "종료 코드: 0 접속 · 명령 모두 수락, 1 접속 실패 · 거절, 2 잘못된 인자, 3 거절된 명령이 있음\n";
}

} // namespace sbx::probe
