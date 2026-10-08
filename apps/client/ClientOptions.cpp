#include "apps/client/ClientOptions.hpp"

#include <array>
#include <charconv>
#include <format>

namespace sbx::client {
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

Expected<ClientOptions> parseClientOptions(std::span<const std::string_view> args) {
    ClientOptions opts;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const auto value = [&]() -> Expected<std::string_view> {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 에 값이 필요합니다", a));
            }
            return args[++i];
        };
        if (a == "--help" || a == "-h") {
            opts.showHelp = true;
        } else if (a == "--version") {
            opts.showVersion = true;
        } else if (a == "--console") {
            opts.console = true;
        } else if (a == "--headless") {
            opts.headless = true;
        } else if (a == "--log-input") {
            opts.logInput = true;
        } else if (a == "--no-render") {
            opts.noRender = true;
        } else if (a == "--rhi-debug") {
            opts.rhiDebug = true;
        } else if (a == "--rhi-gbv") {
            opts.rhiGbv = true;
        } else if (a == "--rhi-warp") {
            opts.rhiWarp = true;
        } else if (a == "--rhi-fl11") {
            opts.rhiFl11 = true;
        } else if (a == "--vsync") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            if (*v != "on" && *v != "off") {
                return makeError(ErrorCode::InvalidArgument, std::format("--vsync 는 on|off: '{}'", *v));
            }
            opts.vsync = *v == "on";
        } else if (a == "--frames-in-flight") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            if (*v != "2" && *v != "3") {
                return makeError(ErrorCode::InvalidArgument, std::format("--frames-in-flight 는 2|3: '{}'", *v));
            }
            opts.framesInFlight = *v == "2" ? 2u : 3u;
        } else if (a == "--log-level") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            const auto level = parseLevel(*v);
            if (!level) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("알 수 없는 로그 레벨 '{}' (trace|debug|info|warn|error|off)", *v));
            }
            opts.logLevel = level;
        } else if (a == "--frames") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            auto n = parseU64(a, *v);
            if (!n) {
                return std::unexpected(n.error());
            }
            if (*n == 0) {
                return makeError(ErrorCode::InvalidArgument, "--frames 는 1 이상");
            }
            opts.frames = *n;
        } else if (a == "--fps") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            auto n = parseU64(a, *v);
            if (!n || *n > 1000) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("--fps 는 0(페이싱 없음)~1000 사이의 정수: '{}'", *v));
            }
            opts.fps = static_cast<double>(*n);
        } else if (a == "--width" || a == "--height") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            auto n = parseU64(a, *v);
            if (!n || *n < 64 || *n > 16384) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 는 64~16384: '{}'", a, *v));
            }
            (a == "--width" ? opts.width : opts.height) = static_cast<u32>(*n);
        } else if (a == "--world" || a == "--connect" || a == "--name") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            if (v->empty()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 에 빈 값", a));
            }
            if (a == "--world") {
                opts.world = std::string(*v);
            } else if (a == "--connect") {
                opts.connect = std::string(*v);
            } else {
                opts.name = std::string(*v);
            }
        } else if (a == "--seed") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            auto n = parseU64(a, *v);
            if (!n) {
                return std::unexpected(n.error());
            }
            opts.seed = *n;
        } else if (a == "--threads") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            auto n = parseU64(a, *v);
            if (!n || *n > 64) {
                return makeError(ErrorCode::InvalidArgument, std::format("--threads 는 0~64: '{}'", *v));
            }
            opts.simThreads = static_cast<u32>(*n);
        } else if (a == "--content" || a == "--assets") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            (a == "--content" ? opts.contentRoot : opts.assetRoot) = std::string(*v);
        } else if (a == "--font") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            opts.font = std::string(*v);
        } else if (a == "--no-ui") {
            opts.noUi = true;
        } else if (a == "--input") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            opts.inputFile = std::string(*v);
        } else {
            return makeError(ErrorCode::InvalidArgument, std::format("알 수 없는 옵션 '{}' (--help)", a));
        }
    }
    if (opts.world && opts.connect) {
        return makeError(ErrorCode::InvalidArgument, "--world(로컬 서버) 와 --connect(원격 서버) 중 하나만");
    }
    return opts;
}

std::string clientUsage() {
    return "사용법: SandboxClient [옵션]\n"
           "  -h, --help             이 도움말\n"
           "      --version          버전과 빌드 정보\n"
           "      --log-level <lvl>  trace|debug|info|warn|error|off (기본 info)\n"
           "      --console          (Windows) 콘솔 창에 로그를 낸다\n"
           "      --headless         OS 창 없이 실행 (CI, 창이 아직 없는 OS)\n"
           "      --frames <n>       n 프레임 뒤 끝낸다\n"
           "      --fps <hz>         프레임 페이싱 (기본: 렌더러 없는 창 60, 렌더러·--headless 0 = 페이싱 없음)\n"
           "      --width <w>        창 너비 (논리 단위, 기본 1600)\n"
           "      --height <h>       창 높이 (논리 단위, 기본 900)\n"
           "      --input <file>     기본 키 바인딩 위에 덮어쓸 settings/input.json\n"
           "      --log-input        입력 이벤트를 로그로 (수동 QA)\n"
           "      --no-render        렌더러 없이 창만 (Phase 6 동작)\n"
           "      --rhi-debug        D3D12 Debug Layer (Windows \"그래픽 도구\" 선택적 기능 필요)\n"
           "      --rhi-gbv          GPU-Based Validation (느림, Debug Layer 포함)\n"
           "      --rhi-warp         소프트웨어 디바이스 WARP\n"
           "      --rhi-fl11         D3D12 FL 11_0 어댑터도 허용 (기본은 12_0 이상 — 오래된 GPU · Wine 시험용)\n"
           "      --vsync on|off     수직 동기 (기본 on)\n"
           "      --frames-in-flight 2|3  CPU 가 앞서 기록할 프레임 수 (기본 2)\n"
           "      --world <이름|폴더> 싱글플레이 — 같은 프로세스의 서버로 시나리오(ecosystem_survival, "
           "ecosystem_small,\n"
           "                         eco_lifecycle, random_walk_1k …) 또는 세이브 폴더(world.json)를 돌려 접속 (Phase "
           "10B)\n"
           "      --connect <h:p>    SandboxServer 에 접속 (포트 기본 7777). 콘텐츠가 다르면 서버 팩을 --content 에서 "
           "읽는다\n"
           "      --name <이름>      접속 이름 (기본 player, 32 글자까지)\n"
           "      --seed <n>         --world 시나리오의 월드 시드 (기본 1)\n"
           "      --threads <n>      --world 시뮬레이션의 Worker 수 (기본: 코어 수 - 2, 1~4. 결과는 같다 — D5)\n"
           "      --content <dir>    콘텐츠 팩 루트 (기본: 빌드 때 정한 저장소의 content/)\n"
           "      --assets <dir>     에셋 루트 — 스프라이트 PNG · materials.json (기본: 저장소의 assets/)\n"
           "      --font <file>      UI 폰트 (.ttf/.ttc — 기본: Windows 맑은 고딕, 없으면 내장 영문 폰트)\n"
           "      --no-ui            ImGui 패널 없이 (제목 줄만)"
           "\n"
           "창 안 단축키 (수동 QA, docs/qa/MANUAL-QA.md): F2 글자 입력 켜기/끄기, F3 마우스 캡처, F4 커서 모양,\n"
           "Ctrl+C / Ctrl+V 글자 복사·붙여넣기, Esc 캡처 해제·글자 지우기, Ctrl+Q 종료, F1 패널(시뮬레이션 · 네트워크 "
           "· 통계).\n"
           "월드: WASD·화살표 이동, 휠 확대(커서 기준), 가운데 끌기, Home 맞춤, 왼쪽 클릭 · 끌기 선택, Space "
           "일시정지,\n"
           ". 한 틱, = / - 속도 (서버 명령 — 원격 서버는 admin 역할부터).\n";
}

} // namespace sbx::client
