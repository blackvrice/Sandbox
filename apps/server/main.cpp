// SandboxServer 진입점. 창·GPU·오디오 없이 실행된다 (docs/01-ARCHITECTURE.md 4장).
#include <cstdio>
#include <string_view>
#include <vector>

#include "apps/server/ServerOptions.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/log/Log.hpp"

namespace {

// 종료 코드: 0 정상, 1 아직 구현되지 않은 동작, 2 잘못된 인자
constexpr int kExitOk = 0;
constexpr int kExitNotImplemented = 1;
constexpr int kExitBadArgs = 2;

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const auto opts = sbx::server::parseServerOptions(args);
    if (!opts) {
        std::fprintf(stderr, "SandboxServer: %s\n\n%s", opts.error().describe().c_str(),
                     sbx::server::serverUsage().c_str());
        return kExitBadArgs;
    }
    if (opts->logLevel) {
        sbx::log::setLevel(*opts->logLevel);
    }
    if (opts->showHelp) {
        std::fputs(sbx::server::serverUsage().c_str(), stdout);
        return kExitOk;
    }
    if (opts->showVersion) {
        std::printf("SandboxServer %.*s (commit %.*s, %.*s, %.*s)\n%s\n",
                    static_cast<int>(sbx::build::kVersionString.size()), sbx::build::kVersionString.data(),
                    static_cast<int>(sbx::build::kGitCommit.size()), sbx::build::kGitCommit.data(),
                    static_cast<int>(sbx::build::kSystemName.size()), sbx::build::kSystemName.data(),
                    static_cast<int>(sbx::build::kCompilerId.size()), sbx::build::kCompilerId.data(),
                    sbx::sim::describeSimulationConstants().c_str());
        return kExitOk;
    }

    sbx::log::warn("server", "월드 실행은 아직 구현되지 않았습니다 (Phase 9). --help 를 참고하십시오.");
    return kExitNotImplemented;
}
