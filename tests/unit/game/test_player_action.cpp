// 명령 PlayerAction (v0.2 M3-T1, ADR-0047): 콘텐츠 actions.json 의 행동을 월드의 한 점에 쓴다 — 대상 고르기 (반경 · 태그 ·
// 조건 · 가까운 순 maxTargets), 효과 (field · destroy · spawn · tag · event), 거절은 원자적, 게임 세션은 Running 에서만.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include "core/command/CommandJson.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/game/GameSession.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/game/GameRules.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/hash/Fnv1a.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kActions = R"([
  {"id": "eco.feed", "name": "먹이 주기", "targets": {"tags": {"all": ["animal"]}}, "range": 3, "maxTargets": 2,
   "conditions": [{"field": "target.life.energy.value", "op": "<", "value": 95}],
   "effects": [{"op": "field.add", "who": "target", "field": "life.energy.value", "value": 20},
               {"op": "event", "name": "eco.fed"}]},
  {"id": "eco.smite", "targets": {"tags": {"all": ["prey"]}}, "range": 2,
   "effects": [{"op": "destroy", "who": "target"}, {"op": "field.add", "who": "target", "field": "life.energy.value", "value": 1}]},
  {"id": "eco.sow", "targets": {"tags": {"all": ["plant"]}}, "range": 2,
   "effects": [{"op": "spawn", "prefab": "eco.grass", "count": 2}, {"op": "tag.add", "who": "target", "tag": "dead"}]}
])";
constexpr std::string_view kMode = R"({"id": "eco.play", "name": "놀이"})";

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_pa_{}_{:x}", tag, std::random_device{}());
        fs::remove_all(path);
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
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const content::ContentDatabase& actionContent() {
    static const TempDir dir("content");
    static const content::ContentDatabase db = [] {
        const fs::path pack = dir.path / "ecosystem";
        fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", pack, fs::copy_options::recursive);
        std::ofstream(pack / "actions.json", std::ios::binary) << kActions;
        fs::create_directories(pack / "modes");
        std::ofstream(pack / "modes" / "play.json", std::ios::binary) << kMode;
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(dir.path, ids, catalog());
        REQUIRE(r.has_value());
        REQUIRE(r->playerActions().size() == 3);
        return std::move(*r);
    }();
    return db;
}

struct ActionWorld {
    sim::SimulationWorld world{catalog(), actionContent(), desc()};
    u32 seq = 0;

    static sim::WorldDesc desc() {
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "eco.grassland";
        d.startPaused = true; // 개체가 움직이지 않게 — 명령은 편집 단계에서 적용된다
        return d;
    }
    cmd::CommandResult run(cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    NetEntityId spawn(std::string prefab, Vec2 pos) {
        const auto r = run(cmd::CreateEntity{pos, {}, std::move(prefab)});
        INFO(r.error.describe());
        REQUIRE(r.accepted);
        return r.created.front();
    }
    [[nodiscard]] f32 energy(NetEntityId id) const {
        return world.registry().read<comp::Energy>(world.resolve(id)).value;
    }
};

} // namespace

TEST_SUITE("core") {

    TEST_CASE("player action: 반경 · 태그 · 조건에 맞는 개체를 가까운 순으로 maxTargets 개") {
        ActionWorld w;
        const NetEntityId near = w.spawn("eco.rabbit", {1.f, 0.f});
        const NetEntityId wolf = w.spawn("eco.wolf", {1.5f, 0.f}); // 에너지 100 — 조건 (< 95) 에 맞지 않는다
        const NetEntityId second = w.spawn("eco.rabbit", {2.f, 0.f});
        const NetEntityId third = w.spawn("eco.rabbit", {2.5f, 0.f}); // 반경 안이지만 maxTargets 2 에서 밀린다
        const NetEntityId out = w.spawn("eco.rabbit", {5.f, 0.f});    // 반경 밖
        const NetEntityId grass = w.spawn("eco.grass", {0.5f, 0.f});  // 태그가 다르다
        const f32 e0 = w.energy(near);
        const auto r = w.run(cmd::PlayerAction{"eco.feed", {0.f, 0.f}});
        REQUIRE_MESSAGE(r.accepted, r.error.describe());
        CHECK(r.created.empty());
        CHECK(w.energy(near) == doctest::Approx(e0 + 20));
        CHECK(w.energy(second) == doctest::Approx(e0 + 20));
        CHECK(w.energy(wolf) == doctest::Approx(100));
        CHECK(w.energy(third) == doctest::Approx(e0));
        CHECK(w.energy(out) == doctest::Approx(e0));
        CHECK(w.world.resolve(grass).valid());
        usize fed = 0;
        for (const auto& ev : w.world.events().events()) {
            fed += ev.kind == sim::EventKind::Custom && ev.code == fnv1a64("eco.fed") ? 1 : 0;
        }
        CHECK(fed == 2);
        // 조건: 에너지가 95 이상이면 대상이 아니다 → 다시 먹이면 near 는 상한(100)에 가깝고 …
        for (int i = 0; i < 3; ++i) {
            (void)w.run(cmd::PlayerAction{"eco.feed", {0.f, 0.f}});
        }
        CHECK(w.energy(near) <= 100.f); // FieldMeta 범위 (max) 로 자른다 · 조건으로 멈춘다
    }

    TEST_CASE("player action: destroy 뒤 효과는 건너뛰고, spawn 은 대상 둘레에 · tag.add") {
        ActionWorld w;
        const NetEntityId rabbit = w.spawn("eco.rabbit", {0.5f, 0.5f});
        auto r = w.run(cmd::PlayerAction{"eco.smite", {0.f, 0.f}});
        REQUIRE_MESSAGE(r.accepted, r.error.describe());
        CHECK_FALSE(w.world.resolve(rabbit).valid());

        const NetEntityId grass = w.spawn("eco.grass", {-3.f, -3.f});
        const usize before = w.world.registry().aliveCount();
        r = w.run(cmd::PlayerAction{"eco.sow", {-3.f, -3.f}});
        REQUIRE_MESSAGE(r.accepted, r.error.describe());
        REQUIRE(r.created.size() == 2);
        CHECK(w.world.registry().aliveCount() == before + 2);
        for (const NetEntityId id : r.created) {
            const Vec2 p = w.world.registry().read<comp::Transform>(w.world.resolve(id)).position;
            CHECK(std::abs(p.x + 3.f) <= 0.5f);
            CHECK(std::abs(p.y + 3.f) <= 0.5f);
        }
        const auto dead = actionContent().findTag("dead");
        REQUIRE(dead);
        CHECK(w.world.registry().read<comp::Tags>(w.world.resolve(grass)).set.test(*dead));
    }

    TEST_CASE("player action: 거절은 아무것도 바꾸지 않는다 — 대상 없음 · 없는 행동 · 경계 밖") {
        ActionWorld w;
        (void)w.spawn("eco.rabbit", {6.f, 6.f});
        const u64 h = *w.world.worldHash();
        auto r = w.run(cmd::PlayerAction{"eco.feed", {-6.f, -6.f}});
        CHECK_FALSE(r.accepted);
        CHECK(r.error.code == ErrorCode::NotFound);
        CHECK(r.error.message.find("대상이 없다") != std::string::npos);
        r = w.run(cmd::PlayerAction{"eco.nope", {6.f, 6.f}});
        CHECK(r.error.code == ErrorCode::NotFound);
        r = w.run(cmd::PlayerAction{"eco.feed", {9999.f, 0.f}});
        CHECK(r.error.code == ErrorCode::OutOfRange);
        CHECK(*w.world.worldHash() == h);
    }

    TEST_CASE("player action: 게임 세션이 있으면 Running 에서만") {
        ActionWorld w;
        const NetEntityId rabbit = w.spawn("eco.rabbit", {0.f, 0.f});
        REQUIRE(game::resetSession(w.world, "eco.play").has_value());
        auto r = w.run(cmd::PlayerAction{"eco.feed", {0.f, 0.f}});
        CHECK_FALSE(r.accepted);
        CHECK(r.error.message.find("Lobby") != std::string::npos);
        REQUIRE(w.run(cmd::ResumeSimulation{}).accepted);
        REQUIRE(w.run(cmd::GameControl{cmd::GameControlOp::Start}).accepted);
        for (int i = 0; i < 3 && game::findSession(w.world.registry())->gameState() != comp::GameState::Running; ++i) {
            w.world.tick();
        }
        REQUIRE(game::findSession(w.world.registry())->gameState() == comp::GameState::Running);
        r = w.run(cmd::PlayerAction{"eco.feed",
                                    {w.world.registry().read<comp::Transform>(w.world.resolve(rabbit)).position}});
        CHECK_MESSAGE(r.accepted, r.error.describe());
    }

    TEST_CASE("player action: 결정적 (두 월드 같은 해시) · 리플레이 JSON 왕복") {
        const auto play = [] {
            ActionWorld w;
            (void)w.spawn("eco.grass", {1.f, 1.f});
            (void)w.spawn("eco.rabbit", {1.5f, 1.f});
            (void)w.run(cmd::PlayerAction{"eco.sow", {1.f, 1.f}});
            (void)w.run(cmd::PlayerAction{"eco.feed", {1.f, 1.f}});
            return *w.world.worldHash();
        };
        CHECK(play() == play());
        const cmd::CommandPayload p = cmd::PlayerAction{"eco.feed", {1.5f, -2.f}};
        const auto j = cmd::payloadToJson(p, [](NetEntityId n) { return u64{n}; });
        CHECK(j["op"] == "action");
        auto back = cmd::payloadFromJson(j, [](u64 n) { return static_cast<NetEntityId>(n); });
        REQUIRE(back.has_value());
        REQUIRE(std::holds_alternative<cmd::PlayerAction>(*back));
        CHECK(std::get<cmd::PlayerAction>(*back).action == "eco.feed");
        CHECK(std::get<cmd::PlayerAction>(*back).position.x == doctest::Approx(1.5f));
        CHECK(cmd::commandName(p) == "PlayerAction");
    }

} // TEST_SUITE
