#include "tools/sim_check/SimCheckOptions.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace sbx::simcheck {
namespace {

template <class T>
Expected<T> parseNumber(std::string_view option, std::string_view text) {
    T value{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc{} || ptr != text.data() + text.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} 에는 0 이상의 정수가 필요합니다: '{}'", option, text));
    }
    return value;
}

} // namespace

Expected<SimCheckOptions> parseSimCheckOptions(std::span<const std::string_view> args) {
    SimCheckOptions o;
    for (usize i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const auto value = [&]() -> Expected<std::string_view> {
            if (i + 1 >= args.size()) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 에 값이 필요합니다", a));
            }
            return args[++i];
        };
        const auto number = [&]<class T>(T& out) -> Expected<void> {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            auto n = parseNumber<T>(a, *v);
            if (!n) {
                return std::unexpected(n.error());
            }
            out = *n;
            return {};
        };

        Expected<void> r{};
        if (a == "-h" || a == "--help") {
            o.showHelp = true;
        } else if (a == "--list-scenarios") {
            o.listScenarios = true;
        } else if (a == "--scenario") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            o.scenario = std::string(*v);
        } else if (a == "--seed") {
            u64 s = 0;
            r = number(s);
            o.seed = s;
        } else if (a == "--ticks") {
            sim::Tick t = 0;
            r = number(t);
            o.ticks = t;
        } else if (a == "--hash-every") {
            sim::Tick t = 0;
            r = number(t);
            if (r && t == 0) {
                return makeError(ErrorCode::InvalidArgument, "--hash-every 는 1 이상이어야 합니다");
            }
            o.hashEvery = t;
        } else if (a == "--hash-at") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            std::string_view list = *v;
            while (!list.empty()) {
                const usize comma = list.find(',');
                auto t = parseNumber<sim::Tick>("--hash-at", list.substr(0, comma));
                if (!t) {
                    return std::unexpected(t.error());
                }
                o.hashAt.push_back(*t);
                list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
            }
            std::sort(o.hashAt.begin(), o.hashAt.end());
            o.hashAt.erase(std::unique(o.hashAt.begin(), o.hashAt.end()), o.hashAt.end());
        } else if (a == "--print-hash") {
            o.printHash = true;
        } else if (a == "--repeat") {
            r = number(o.repeat);
            if (r && (o.repeat == 0 || o.repeat > 16)) {
                return makeError(ErrorCode::InvalidArgument, "--repeat 는 1~16 이어야 합니다");
            }
        } else if (a == "--golden") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            o.goldenPath = std::string(*v);
        } else if (a == "--record-golden") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            o.recordPath = std::string(*v);
        } else if (a == "--save-at") {
            sim::Tick t = 0;
            r = number(t);
            if (r && t == 0) {
                return makeError(ErrorCode::InvalidArgument, "--save-at 은 1 이상이어야 합니다");
            }
            o.saveAt = t;
        } else if (a == "--save-dir") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            o.saveDir = std::string(*v);
        } else if (a == "--content-root") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            o.contentRoot = std::string(*v);
        } else if (a == "--validate-content") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            std::string_view list = *v;
            while (!list.empty()) {
                const usize comma = list.find(',');
                if (comma != 0) {
                    o.validatePacks.emplace_back(list.substr(0, comma));
                }
                list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
            }
            if (o.validatePacks.empty()) {
                return makeError(ErrorCode::InvalidArgument, "--validate-content 에 팩 id 가 필요합니다");
            }
        } else if (a == "--inject-divergence") {
            sim::Tick t = 0;
            r = number(t);
            o.injectDivergence = t;
        } else if (a == "--profile") {
            o.profile = true;
        } else if (a == "--require-prefabs") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            std::string_view list = *v;
            while (!list.empty()) {
                const usize comma = list.find(',');
                if (comma != 0) {
                    o.requirePrefabs.emplace_back(list.substr(0, comma));
                }
                list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
            }
            if (o.requirePrefabs.empty()) {
                return makeError(ErrorCode::InvalidArgument, "--require-prefabs 에 Prefab id 가 필요합니다");
            }
        } else if (a == "--replay-roundtrip") {
            o.replayRoundtrip = true;
        } else if (a == "--replay-dir" || a == "--replay") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            (a == "--replay" ? o.replayFile : o.replayDir) = std::string(*v);
        } else if (a == "--threads") {
            auto v = value();
            if (!v) {
                return std::unexpected(v.error());
            }
            std::string_view list = *v;
            o.threads.clear();
            while (true) {
                const usize comma = list.find(',');
                auto n = parseNumber<u32>(a, list.substr(0, comma));
                if (!n) {
                    return std::unexpected(n.error());
                }
                if (*n > kMaxThreads) {
                    return makeError(ErrorCode::OutOfRange, std::format("--threads 는 {} 이하", kMaxThreads));
                }
                o.threads.push_back(*n);
                if (comma == std::string_view::npos) {
                    break;
                }
                list = list.substr(comma + 1);
            }
        } else {
            return makeError(ErrorCode::InvalidArgument, std::format("unknown option '{}'", a));
        }
        if (!r) {
            return std::unexpected(r.error());
        }
    }
    if (!o.goldenPath.empty() && !o.recordPath.empty()) {
        return makeError(ErrorCode::InvalidArgument, "--golden 과 --record-golden 은 함께 쓸 수 없습니다");
    }
    if (o.injectDivergence && o.repeat < 2) {
        return makeError(ErrorCode::InvalidArgument, "--inject-divergence 는 --repeat 2 이상과 함께 씁니다");
    }
    if (!o.replayFile.empty() && (o.replayRoundtrip || o.scenario || !o.goldenPath.empty() || !o.recordPath.empty() ||
                                  o.saveAt || o.repeat != 1)) {
        return makeError(
            ErrorCode::InvalidArgument,
            "--replay 는 기록된 리플레이만 재생한다 (--scenario · --golden · --save-at · --repeat 와 함께 쓸 수 없다)");
    }
    if (!o.replayDir.empty() && !o.replayRoundtrip) {
        return makeError(ErrorCode::InvalidArgument, "--replay-dir 은 --replay-roundtrip 과 함께 쓴다");
    }
    if (!o.hashAt.empty() && o.hashEvery) {
        return makeError(ErrorCode::InvalidArgument, "--hash-at 과 --hash-every 는 함께 쓸 수 없습니다");
    }
    return o;
}

std::string simCheckUsage() {
    return "사용법: sbx_sim_check [옵션]\n"
           "  -h, --help               이 도움말\n"
           "      --list-scenarios     시나리오 목록\n"
           "      --scenario <name>    실행할 시나리오 (기본 random_walk_1k)\n"
           "      --seed <n>           월드 시드 (기본 1)\n"
           "      --ticks <n>          진행할 틱 수 (기본: 시나리오 기본값)\n"
           "      --hash-every <n>     n 틱마다 해시 (기본 30)\n"
           "      --hash-at <t1,t2..>  지정한 틱에서만 해시\n"
           "      --print-hash         체크포인트마다 해시 출력\n"
           "      --repeat <n>         같은 입력의 월드 n 개를 나란히 돌려 비교 (D1)\n"
           "      --save-at <t>        D2: t 틱 뒤 저장→로드한 월드를 나란히 돌려 비교\n"
           "      --save-dir <dir>     --save-at 의 저장 폴더 (기본: 임시 폴더)\n"
           "      --golden <file>      골든 파일과 비교 (이 툴체인 항목이 없으면 종료 코드 3)\n"
           "      --record-golden <f>  이 툴체인의 해시를 골든 파일에 기록\n"
           "      --profile            System 별 평균 시간\n"
           "      --content-root <dir> 콘텐츠 팩 루트 (기본: 저장소의 content/)\n"
           "      --validate-content <팩[,팩]>  콘텐츠 검증만 (V1~V7), 오류가 있으면 종료 코드 1\n"
           "      --require-prefabs <id[,id..]>  끝에 이 Prefab 이 0 이면 실패 (밸런스: 세 종 공존)\n"
           "      --replay-roundtrip   D3: 리플레이를 기록하고 시작 세이브에서 재생해 해시 비교\n"
           "      --replay-dir <dir>   --replay-roundtrip 의 출력 폴더 (start/ + replay.sbxr, 기본: 임시 폴더)\n"
           "      --replay <file>      기록된 리플레이(replay.sbxr)를 재생해 검사만\n"
           "      --threads <n[,n..]>  경로 Job 의 Worker 수 (기본 0). 여럿이면 나란히 돌려 비교 (D5)\n"
           "      --inject-divergence <t>  하네스 자체 시험: t 틱에 두 번째 실행만 살짝 바꾼다\n"
           "종료 코드: 0 일치, 1 불일치, 2 잘못된 인자/오류, 3 골든 항목 없음\n";
}

} // namespace sbx::simcheck
