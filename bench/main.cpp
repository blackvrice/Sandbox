// sbx_bench — 벤치마크 실행기. docs/14-PERFORMANCE.md
//   sbx_bench [--quick] [--out result.json] [--machine <name>] [--threads n] [--only <이름 접두사>]
// 측정은 RelWithDebInfo 로 한다. Debug 수치는 기록하지 않는다.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include "BenchUtil.hpp"
#include "foundation/BuildInfo.hpp"

int main(int argc, char** argv) {
    sbx::bench::Context ctx;
    std::string outPath;
    std::string machine;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--quick") {
            ctx.quick = true;
            ctx.repeats = 1;
        } else if (a == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (a == "--machine" && i + 1 < argc) {
            machine = argv[++i];
        } else if (a == "--threads" && i + 1 < argc) {
            ctx.threads = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "--only" && i + 1 < argc) {
            ctx.only = argv[++i];
        } else {
            std::fprintf(
                stderr,
                "usage: sbx_bench [--quick] [--out file.json] [--machine name] [--threads n] [--only prefix]\n");
            return 2;
        }
    }
    if (machine.empty()) {
        const char* env = std::getenv("COMPUTERNAME");
        if (env == nullptr) {
            env = std::getenv("HOSTNAME");
        }
        machine = env != nullptr ? env : "unknown";
    }

#ifdef NDEBUG
    constexpr const char* buildKind = "optimized";
#else
    constexpr const char* buildKind = "debug";
    std::fprintf(stderr, "경고: Debug 빌드 수치는 기록하지 않습니다 (RelWithDebInfo 로 측정하십시오)\n");
#endif

    std::printf("sbx_bench %s (%s, %s)\n", std::string(sbx::build::kVersionString).c_str(), buildKind,
                std::string(sbx::build::kCompilerId).c_str());
    if (sbx::bench::wants(ctx, "ecs.")) {
        sbx::bench::runEcsBenchmarks(ctx);
    }
    sbx::bench::runSimBenchmarks(ctx);
    sbx::bench::runRenderBenchmarks(ctx);

    if (!outPath.empty()) {
        nlohmann::json doc = {{"machine", machine},
                              {"threads", ctx.threads},
                              {"build", buildKind},
                              {"compiler", std::string(sbx::build::kCompilerId)},
                              {"commit", std::string(sbx::build::kGitCommit)},
                              {"version", std::string(sbx::build::kVersionString)},
                              {"results", ctx.results}};
        std::ofstream(outPath) << doc.dump(2) << '\n';
        std::printf("결과: %s\n", outPath.c_str());
    }
    return 0;
}
