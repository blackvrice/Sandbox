// sbx — 게임 프로젝트 도구 (v0.2 M1-T4, ADR-0043). Core 만 링크한다 (서버처럼 그래픽 · 창 · 네트워크 없음).
//
//   sbx new MyGame --id studio.mygame --from ecosystem_small    템플릿에서 새 프로젝트
//   sbx check MyGame                                            프로젝트 검사 (오류면 종료 코드 1)
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/project/GameProject.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/Scenario.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/io/Console.hpp"
#include "tools/sbx/SbxOptions.hpp"

namespace {

using namespace sbx;
using namespace sbx::tool;

void printErr(std::string_view s) {
    std::fprintf(stderr, "sbx: %.*s\n", static_cast<int>(s.size()), s.data());
}

SemVer engineVersion() {
    return SemVer{build::kVersionMajor, build::kVersionMinor, build::kVersionPatch, {}, {}};
}

int runCheck(const std::filesystem::path& dir, const ecs::ComponentCatalog& catalog) {
    const SemVer engine = engineVersion();
    const auto r = project::validateProject(dir, engine, catalog);
    for (const content::ContentIssue& i : r.issues) {
        std::printf("%s\n", i.describe().c_str());
    }
    if (r.manifestRead) {
        std::printf("프로젝트 %s %s (\"%s\", 엔진 %s 범위 \"%s\")\n", r.manifest.gameId.c_str(),
                    r.manifest.version.toString().c_str(), r.manifest.name.c_str(), engine.toString().c_str(),
                    r.manifest.engine.text.c_str());
    }
    const std::string world = r.worldOpened ? std::format("열림 — tick {}, 엔티티 {}", r.worldTick, r.worldEntities)
                                            : std::string("열지 못함");
    std::printf("프로젝트 검사: 오류 %zu, 경고 %zu (Prefab %zu, contentHash %s, 시작 월드 %s)\n", r.errorCount(),
                r.warningCount(), r.prefabs, replay::formatHash(r.contentHash).c_str(), world.c_str());
    return r.ok() ? kExitOk : kExitCheckFailed;
}

int runNew(const SbxOptions& opt, const ecs::ComponentCatalog& catalog) {
    project::NewProjectOptions n;
    n.gameId = opt.gameId;
    n.name = opt.name;
    n.fromScenario = opt.from;
    n.contentRoot = opt.contentRoot.empty() ? std::filesystem::path(SBX_DEFAULT_CONTENT_DIR)
                                            : std::filesystem::path(opt.contentRoot);
    n.seed = opt.seed;
    const auto made = project::createProject(std::filesystem::path(opt.dir), n, engineVersion(), catalog);
    if (!made) {
        printErr(made.error().describe());
        return kExitError;
    }
    std::string packs;
    for (const auto& id : made->copiedPacks) {
        packs += (packs.empty() ? "" : ", ") + id;
    }
    std::printf("프로젝트를 만들었습니다: %s — %s %s \"%s\" (엔진 \"%s\")\n", opt.dir.c_str(),
                made->manifest.gameId.c_str(), made->manifest.version.toString().c_str(), made->manifest.name.c_str(),
                made->manifest.engine.text.c_str());
    std::printf("  템플릿 %s · 팩 %s · 시작 월드 %s (tick %llu, 엔티티 %zu)\n",
                opt.from.empty() ? "빈 월드" : opt.from.c_str(), packs.empty() ? "내장" : packs.c_str(),
                made->manifest.entryWorld.c_str(), static_cast<unsigned long long>(made->worldTick),
                made->worldEntities);
    std::printf("  실행: SandboxClient --world %s  ·  서버: SandboxServer --world %s\n", opt.dir.c_str(),
                opt.dir.c_str());
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    console::useUtf8Output();
    const std::vector<std::string> argStore = console::utf8Arguments(argc, argv);
    std::vector<std::string_view> args(argStore.begin() + (argStore.empty() ? 0 : 1), argStore.end());
    const auto parsed = parseSbxOptions(args);
    if (!parsed) {
        printErr(parsed.error().describe());
        std::fputs(sbxUsage().c_str(), stderr);
        return kExitError;
    }
    const SbxOptions& opt = *parsed;
    switch (opt.command) {
    case SbxCommand::Help:
        std::fputs(sbxUsage().c_str(), stdout);
        return kExitOk;
    case SbxCommand::Version:
        std::printf("sbx %.*s (%.*s)\n", static_cast<int>(build::kVersionString.size()), build::kVersionString.data(),
                    static_cast<int>(build::kGitCommit.size()), build::kGitCommit.data());
        return kExitOk;
    case SbxCommand::Templates:
        std::printf("%-18s %s\n", "-", "(--from 없음) 빈 월드 — 내장 콘텐츠, 16 × 16 청크, 일시정지");
        for (const auto& s : scenario::scenarioList()) {
            std::printf("%-18.*s %.*s\n", static_cast<int>(s.name.size()), s.name.data(),
                        static_cast<int>(s.description.size()), s.description.data());
        }
        return kExitOk;
    case SbxCommand::New:
    case SbxCommand::Check:
        break;
    }
    ecs::ComponentCatalog catalog;
    if (auto r = comp::registerCoreComponents(catalog); !r) {
        printErr(r.error().describe());
        return kExitError;
    }
    return opt.command == SbxCommand::New ? runNew(opt, catalog) : runCheck(std::filesystem::path(opt.dir), catalog);
}
