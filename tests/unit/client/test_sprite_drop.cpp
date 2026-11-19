// Sprite → Object (v0.13 ST-08-5 — editor/SpriteDrop · Application::dropSpriteOnWorld, ADR-0167): 계획 (팩 · 머티리얼 이름 ·
// Prefab id · 같은 그림 재사용 · 다른 그림이면 _2) 과, 헤드리스 앱 (로컬 서버 + 프로젝트 월드) 에서 끌어 놓기와 같은 길 —
// materials.json (asset:<id>) · 오버레이 Prefab · 놓은 자리의 개체 · Ctrl+Z 로 사라짐 · 월드 저장 → 다시 열면 Prefab ·
// 개체 · 머티리얼이 그대로 (에셋 이름을 바꿔도 id 참조).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/asset/AssetDatabase.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/project/GameProject.hpp"
#include "editor/SpriteDrop.hpp"
#include "foundation/io/FileIo.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"
#include "render/asset/MaterialLibrary.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
namespace fs = std::filesystem;

namespace {

constexpr f64 kDt = 1.0 / 30.0;
const SemVer kEngine{0, 1, 0, {}, {}};

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_sprite_drop_{}_{:08x}", tag, std::random_device{}());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog c = [] {
        ecs::ComponentCatalog made;
        (void)comp::registerCoreComponents(made);
        return made;
    }();
    return c;
}

void put(const fs::path& f, std::string_view bytes) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary) << bytes;
}

// ecosystem_small 로 만든 프로젝트 (팩 content/ecosystem) + 에셋 assets/art/Hero.png (id = 'b' × 32)
fs::path makeGame(const fs::path& root) {
    const fs::path dir = root / "Game";
    project::NewProjectOptions o;
    o.gameId = "studio.drop";
    o.fromScenario = "ecosystem_small";
    o.contentRoot = SBX_CONTENT_DIR;
    REQUIRE(project::createProject(dir, o, kEngine, catalog()).has_value());
    put(dir / "assets/art/Hero.png", "PNG-hero");
    REQUIRE(asset::importAssets(dir, [] { return std::string(32, 'b'); }).has_value());
    return dir;
}

const std::string kHeroId(32, 'b');

} // namespace

TEST_SUITE("client") {
    TEST_CASE("sprite drop (v0.13 ST-08-5): id 마디 · 프로젝트 팩 · 계획 (새 Prefab · 같은 그림 재사용 · 다른 그림이면 _2)") {
        CHECK(editor::spriteIdPart("Hero") == "hero");
        CHECK(editor::spriteIdPart("big-tree.v2") == "big_tree_v2");
        CHECK(editor::spriteIdPart("3coins") == "s_3coins");
        CHECK(editor::spriteIdPart("돌") == "sprite");
        CHECK(editor::spriteIdPart("__") == "sprite");

        TempDir root("plan");
        const fs::path dir = makeGame(root.path);
        auto opened = project::openProject(dir, kEngine, catalog());
        REQUIRE(opened.has_value());
        const content::ContentDatabase& db = *opened->content;
        const content::ContentSourcePack* pack = editor::projectPackOf(db, dir);
        REQUIRE(pack != nullptr);
        CHECK_FALSE(editor::projectPackOf(db, root.path / "elsewhere"));

        const asset::AssetScan scan = asset::scanProjectAssets(dir);
        REQUIRE(scan.find(kHeroId) != nullptr);
        const auto plan = editor::planSpriteDrop(db, dir, *scan.find(kHeroId));
        REQUIRE_MESSAGE(plan.has_value(), plan.error().describe());
        CHECK(plan->packFolder == "ecosystem");
        CHECK(plan->material == "ecosystem/Hero");
        CHECK(plan->prefabId == std::format("{}.hero", pack->info.id));
        REQUIRE(plan->prefab.has_value());
        const auto& comps = (*plan->prefab)["components"];
        CHECK(comps.contains("core.transform"));
        CHECK(comps["render.sprite"]["material"] == "ecosystem/Hero");
        CHECK((*plan->prefab)["category"] == "Sprites");

        // 같은 id 의 Prefab 이 다른 그림 → _2, 같은 그림 → 재사용 (팩 파일에 넣고 다시 연다)
        put(dir / "content/ecosystem/prefabs/hero.json",
            std::format(R"({{"id": "{}.hero", "components": {{"core.transform": {{}},
              "render.sprite": {{"material": "eco/rabbit"}}}}}})",
                        pack->info.id));
        auto again = project::openProject(dir, kEngine, catalog());
        REQUIRE(again.has_value());
        const auto second = editor::planSpriteDrop(*again->content, dir, *scan.find(kHeroId));
        REQUIRE(second.has_value());
        CHECK(second->prefabId == std::format("{}.hero_2", pack->info.id));
        CHECK(second->prefab.has_value());
        put(dir / "content/ecosystem/prefabs/hero.json",
            std::format(R"({{"id": "{}.hero", "components": {{"core.transform": {{}},
              "render.sprite": {{"material": "ecosystem/Hero"}}}}}})",
                        pack->info.id));
        auto same = project::openProject(dir, kEngine, catalog());
        REQUIRE(same.has_value());
        const auto third = editor::planSpriteDrop(*same->content, dir, *scan.find(kHeroId));
        REQUIRE(third.has_value());
        CHECK(third->prefabId == std::format("{}.hero", pack->info.id));
        CHECK_FALSE(third->prefab.has_value());
    }

    TEST_CASE("sprite drop (v0.13 ST-08-5, 헤드리스 앱): 머티리얼 · Prefab · 개체 · 되돌리기 · 저장 → 다시 열기") {
        TempDir root("app");
        const fs::path dir = makeGame(root.path);
        const std::string start = project::openProject(dir, kEngine, catalog())->manifest.entryWorld;
        REQUIRE(start.starts_with("worlds/"));
        std::string prefabId;
        const Vec2 at{3.5f, 2.5f};
        {
            render::MaterialLibrary mats;
            NetworkSessionDesc nd;
            nd.world = dir.string();
            nd.contentRoot = SBX_CONTENT_DIR;
            nd.inlineServer = true;
            nd.savesRoot = project::projectSavesRoot(dir); // 앱 (main) 과 같게 — 프로젝트의 worlds/
            auto session = NetworkSession::create(nd, mats);
            REQUIRE_MESSAGE(session.has_value(), session.error().describe());
            auto ns = std::move(*session);
            HeadlessWindow window;
            ImGuiLayer imgui(window);
            NullAudioBackend audio;
            audio.init({});
            AppConfig cfg;
            cfg.world = ns.get();
            cfg.fixedDt = kDt;
            cfg.ui = &imgui;
            cfg.projectDir = dir;
            auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
            REQUIRE(map.has_value());
            Application app(window, std::move(*map), audio, cfg);
            const auto frames = [&](int n) {
                for (int i = 0; i < n; ++i) {
                    REQUIRE(app.frame());
                }
            };
            for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
                REQUIRE(app.frame());
            }
            REQUIRE(app.state() == AppState::InWorld);
            ns->togglePause(); // 움직이지 않게 — 편집 명령은 그대로
            frames(4);
            CHECK_FALSE(mats.contains("ecosystem/Hero"));

            // 없는 에셋 — 이유
            CHECK_FALSE(app.dropSpriteForTest(std::string(32, 'c'), at).empty());
            // 놓기
            const std::string why = app.dropSpriteForTest(kHeroId, at);
            REQUIRE_MESSAGE(why.empty(), why);
            CHECK(mats.contains("ecosystem/Hero")); // 렌더러 머티리얼을 다시 읽었다
            const auto matsJson = nlohmann::json::parse(
                *io::readFile(dir / "content/ecosystem/presentation/materials.json"));
            CHECK(matsJson["materials"]["ecosystem/Hero"]["sprite"] == "asset:" + kHeroId);
            for (int i = 0; i < 200 && app.editor().lastCreated().empty(); ++i) {
                REQUIRE(app.frame());
            }
            REQUIRE(app.editor().lastCreated().size() == 1);
            const NetEntityId id = app.editor().lastCreated().front();
            const auto drawn = [&]() -> const SnapshotSprite* {
                for (const SnapshotSprite& s : ns->lastSnapshot().sprites) {
                    if (s.id == id) {
                        return &s;
                    }
                }
                return nullptr;
            };
            for (int i = 0; i < 200 && drawn() == nullptr; ++i) {
                REQUIRE(app.frame());
            }
            REQUIRE(drawn() != nullptr);
            CHECK(drawn()->position.x == doctest::Approx(at.x));
            CHECK(drawn()->position.y == doctest::Approx(at.y));
            const content::ContentSourcePack* pack = editor::projectPackOf(*ns->content(), dir);
            REQUIRE(pack != nullptr);
            prefabId = std::format("{}.hero", pack->info.id);
            REQUIRE(ns->content()->findPrefab(prefabId) != nullptr); // 오버레이 Prefab
            CHECK(app.editor().stats().rejected == 0);

            // Ctrl+Z (edit.undo) — 개체가 사라진다 (Prefab 은 남는다 — 따로 되돌린다)
            REQUIRE(app.commands().run("edit.undo") == editor::services::RunResult::Ran);
            for (int i = 0; i < 200 && drawn() != nullptr; ++i) {
                REQUIRE(app.frame());
            }
            CHECK(drawn() == nullptr);
            // 다시 놓기 — 같은 그림의 Prefab 을 쓴다 (새 Prefab 없음)
            REQUIRE(app.dropSpriteForTest(kHeroId, at).empty());
            for (int i = 0; i < 200 && (app.editor().lastCreated().empty() ||
                                        app.editor().lastCreated().front() == id);
                 ++i) {
                REQUIRE(app.frame());
            }
            frames(10);
            CHECK(ns->content()->findPrefab(std::format("{}.hero_2", pack->info.id)) == nullptr);

            // 시작 월드로 저장 (Ctrl+S 의 길)
            ns->worldOp(WorldOp::Save, start.substr(7));
            // 결과는 앱 (월드 창) 이 가져간다 — 디스크의 세이브에 그 Prefab 의 개체가 생길 때까지
            const fs::path entities = dir / fs::path(start) / "entities.jsonl";
            const auto savedHasPrefab = [&] {
                const auto text = io::readFile(entities);
                return text.has_value() && text->find(prefabId) != std::string::npos;
            };
            for (int i = 0; i < 300 && !savedHasPrefab(); ++i) {
                REQUIRE(app.frame());
            }
            REQUIRE(savedHasPrefab());
        }
        // 에셋 이름을 바꿔도 (id 참조) — 다시 열면 Prefab · 개체 · 머티리얼 그대로
        REQUIRE(asset::moveAsset(dir, "assets/art/Hero.png", "assets/chars/Knight.png").has_value());
        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        CHECK(mats.contains("ecosystem/Hero"));
        for (int i = 0; i < 300 && !ns->ready(); ++i) {
            ns->update(kDt);
        }
        REQUIRE(ns->ready());
        CHECK(ns->content()->findPrefab(prefabId) != nullptr);
    }
}
