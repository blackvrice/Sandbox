// v0.12 통합 시험 NG-20 (ADR-0165): 제작 흐름을 헤드리스로 끝까지 — 새 프로젝트 → 에셋 가져오기 (.meta) → 팩 (Prefab
// 변형 · 애니메이션 · 스크립트 · 타일 머티리얼) → 맵 (지형 칠하기 — 편집기의 브러시와 같은 명령) → 시작 월드 저장
// (Ctrl+S) → 프로젝트 검사 → 패키지 빌드 → 패키지에서 자동 플레이 (승리) + 리플레이 기록 → 끝 월드 저장 → 불러오기 =
// 같은 해시 · 나란히 같은 해시 → 시작 세이브에서 리플레이 = 같은 해시. 틀 (팩 · 에셋 · 자동 플레이) 은 samples/dungeon
// (템플릿 dungeon). SBX_NG20_WRITE_SAMPLE=<폴더> 면 칠한 시작 월드를 그 폴더 (samples/dungeon/worlds/main) 에도 쓴다 —
// 샘플을 다시 만들 때만.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <vector>

#include "core/asset/AssetDatabase.hpp"
#include "core/command/SimCommand.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/game/GameRules.hpp"
#include "core/package/GamePackage.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/project/GameProject.hpp"
#include "core/replay/Replay.hpp"
#include "core/scenarios/PlayScript.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "core/world/WorldGrid.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const SemVer kEngine{0, 7, 0, {}, {}};

// 보석 동굴의 맵 — 16 × 12 방 (바닥), 테두리 벽, 가운데 세로 벽 (x 8, y 1 ~ 6)
std::vector<Vec2i> wallCells() {
    std::vector<Vec2i> cells;
    for (int x = 0; x <= 15; ++x) {
        cells.push_back({x, 0});
        cells.push_back({x, 11});
    }
    for (int y = 1; y <= 10; ++y) {
        cells.push_back({0, y});
        cells.push_back({15, y});
    }
    for (int y = 1; y <= 6; ++y) {
        cells.push_back({8, y});
    }
    return cells;
}

// 환경 변수 (UserSettings 와 같은 꼴 — MSVC 의 getenv 경고 C4996 은 읽기만이라 이 자리에서만 끈다)
std::string env(const char* name) {
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

struct TempDir {
    fs::path path;
    TempDir() : path(fs::temp_directory_path() / std::format("sbx_ng20_{:x}", std::random_device{}())) {
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

TEST_SUITE("project") {
    TEST_CASE("v0.12 integration (NG-20): 새 프로젝트 → 가져오기 → Prefab · 애니메이션 · 스크립트 → 맵 → 저장 → 검사 → "
              "빌드 → 패키지 실행 (승리) → 저장 · 불러오기 · 리플레이 해시") {
        const fs::path sample = fs::path(SBX_SAMPLES_DIR) / "dungeon";
        TempDir tmp;
        const fs::path game = tmp.path / "game";

        // 1. 새 프로젝트 (빈 월드) — Studio 홈의 "새 프로젝트" 와 같은 함수
        project::NewProjectOptions opt;
        opt.gameId = "sandbox.dungeon_ng20";
        opt.name = "보석 동굴 (NG-20)";
        auto made = project::createProject(game, opt, kEngine, catalog());
        REQUIRE_MESSAGE(made.has_value(), (made ? std::string() : made.error().describe()));

        // 2. 팩 · 에셋을 넣고 매니페스트에 (팩 · 게임 모드) — 제작자가 파일을 두는 것
        fs::copy(sample / "content", game / "content",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        fs::create_directories(game / "assets");
        fs::copy(sample / "assets", game / "assets",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        for (const auto& e : fs::recursive_directory_iterator(game / "assets")) {
            if (e.path().extension() == ".meta") {
                fs::remove(e.path()); // 가져오기를 처음부터
            }
        }
        project::GameManifest manifest = made->manifest;
        manifest.contentPacks = {"dun"};
        manifest.gameMode = "dun.collect";
        manifest.schemaVersion = 3;
        REQUIRE(project::saveManifest(game, manifest).has_value());

        // 3. 에셋 가져오기 (.meta — 스프라이트 시트 셋)
        const auto imported = asset::importAssets(game);
        REQUIRE_MESSAGE(imported.has_value(), (imported ? std::string() : imported.error().describe()));
        CHECK(*imported == 3);

        // 4. 콘텐츠 — Prefab 변형 (붉은 슬라임) 이 풀렸다
        content::ContentLoadResult loaded =
            content::ContentLoader::load(game / "content", std::vector<std::string>{"dun"}, catalog());
        REQUIRE_MESSAGE(loaded.ok(), (loaded.issues.empty() ? std::string() : loaded.issues.front().describe()));
        const content::Prefab* red = loaded.db.findPrefab("dun.slime_red");
        REQUIRE(red != nullptr);
        CHECK(red->tags == loaded.db.findPrefab("dun.slime")->tags);
        CHECK(red->components.size() == loaded.db.findPrefab("dun.slime")->components.size());
        CHECK(!loaded.db.packs().front().animations.empty());

        // 5. 맵 — 바닥으로 채운 월드에 벽 타일을 칠하고 (편집기의 지형 브러시 = PaintTerrain), 시작 월드로 저장
        // (Ctrl+S)
        {
            sim::WorldDesc d;
            d.bounds = world::GridBounds{world::ChunkCoord{0, 0}, world::ChunkCoord{0, 0}};
            d.fillMaterial = "dun.floor";
            d.startPaused = false; // 바로 도는 게임의 시작 월드 (sbx resave --resume 과 같다)
            sim::SimulationWorld w(catalog(), loaded.db, d);
            REQUIRE(game::resetSession(w, "dun.collect").has_value());
            w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, 1},
                                      cmd::PaintTerrain{"dun.wall", wallCells(), {}, {}, 0}});
            w.tick();
            REQUIRE(w.lastResults().size() == 1);
            CHECK(w.lastResults().front().accepted);
            CHECK(w.grid().moveCostAt({8, 3}) == 0);
            REQUIRE(persist::saveWorld(w, game / "worlds" / "main").has_value());
            if (const std::string out = env("SBX_NG20_WRITE_SAMPLE"); !out.empty()) {
                REQUIRE(persist::saveWorld(w, fs::path(out)).has_value()); // 샘플을 다시 만들 때만
            }
        }

        // 6. 프로젝트 검사 · 패키지 빌드
        const project::ProjectReport report = project::validateProject(game, kEngine, catalog());
        for (const content::ContentIssue& i : report.issues) {
            INFO(i.describe());
        }
        REQUIRE(report.ok());
        const fs::path pkg = tmp.path / "out" / "dungeon.sbpkg";
        auto built = package::buildPackage(game, pkg, kEngine, catalog());
        REQUIRE_MESSAGE(built.has_value(), (built ? std::string() : built.error().describe()));

        // 7. 패키지에서 자동 플레이 + 리플레이 기록 (시작 세이브 = 패키지의 시작 월드)
        auto src = scenario::openWorldSource(pkg.string(), {}, 1, catalog(), tmp.path / "cache");
        REQUIRE_MESSAGE(src.has_value(), (src ? std::string() : src.error().describe()));
        sim::SimulationWorld& w = src->runner->world();
        auto script = scenario::loadPlayScript(sample / "playthrough.json");
        REQUIRE(script.has_value());
        const fs::path rec = tmp.path / "rec";
        const auto startSave = persist::saveWorld(w, rec / "start");
        REQUIRE(startSave.has_value());
        replay::ReplayHeader header;
        header.startWorldHash = startSave->worldHash;
        header.packs = src->packs;
        header.startWorld = "start";
        header.hashInterval = 10;
        replay::ReplayRecorder recorder(w, header);
        const sim::Tick startTick = w.currentTick();
        scenario::attachPlayScript(*src->runner, *script);
        for (u64 step = 0; step < scenario::playStepLimit(*script) && !scenario::playFinished(w, startTick, *script);
             ++step) {
            src->runner->step();
            REQUIRE(recorder.afterTick().has_value());
        }
        REQUIRE(recorder.finish().has_value());
        INFO(scenario::describePlayResult(w));
        CHECK(scenario::checkPlayExpectation(w, script->expect).empty()); // Finished · win
        const u64 finalHash = *w.worldHash();

        // 8. 끝 월드 저장 → 불러오기 = 같은 해시, 나란히 30 틱 = 같은 해시
        REQUIRE(persist::saveWorld(w, tmp.path / "end").has_value());
        auto back = persist::loadWorld(catalog(), *src->content, tmp.path / "end");
        REQUIRE_MESSAGE(back.has_value(), (back ? std::string() : back.error().describe()));
        CHECK(*back->world->worldHash() == finalHash);
        for (int i = 0; i < 30; ++i) {
            w.tick();
            back->world->tick();
        }
        CHECK(*back->world->worldHash() == *w.worldHash());

        // 9. 시작 세이브에서 리플레이 = 기록의 해시 · 끝 해시
        auto start = persist::loadWorld(catalog(), *src->content, rec / "start");
        REQUIRE(start.has_value());
        auto played = replay::playReplay(*start->world, recorder.replay());
        REQUIRE_MESSAGE(played.has_value(), (played ? std::string() : played.error().describe()));
        CHECK_FALSE(played->mismatch.has_value());
        CHECK(played->hashesCompared > 5);
        CHECK(*start->world->worldHash() == finalHash);
    }
}
