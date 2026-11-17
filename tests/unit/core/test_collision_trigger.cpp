// 충돌 layer · mask · 트리거 (v0.11 RT-06-4, ADR-0150): 보통 콜라이더 둘은 밀어낸다, core.trigger 가 한쪽이라도 있으면
// 겹쳐도 밀지 않는다, layer · mask 가 서로 맞지 않으면 겹치지 않는다 (한쪽만 맞아도 안 된다), 트리거가 없는 월드의
// 해시는 그대로 (콜라이더에 필드를 더하지 않았다 — 골든은 CTest 가 본다).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Movement.hpp"
#include "core/components/core/Transform.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/log/Log.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

struct QuietLog {
    QuietLog() {
        log::setSink([](log::Level, std::string_view, std::string_view) {});
    }
    ~QuietLog() { log::setSink(nullptr); }
    QuietLog(const QuietLog&) = delete;
    QuietLog& operator=(const QuietLog&) = delete;
};

// 줄마다 (y) 한 쌍 — x 0 과 0.2 에 놓는다 (반지름 0.5 — 깊이 0.8 겹침)
constexpr std::string_view kSpawn = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.place",
  onStart(ctx) {
    ctx.spawn("t.solid", 0, 0)
    ctx.spawn("t.solid", 0.2, 0)
    ctx.spawn("t.solid", 0, 4)
    ctx.spawn("t.trigger", 0.2, 4)
    ctx.spawn("t.red", 0, -4)
    ctx.spawn("t.blue", 0.2, -4)
    ctx.spawn("t.red", 4, -4)
    ctx.spawn("t.picky", 4.2, -4)
  },
})
)";

fs::path writePack(std::string_view name) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "collision-trigger" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("content/t/pack.json", R"({"id": "t", "version": "1"})");
    put("content/t/prefabs/solid.json",
        R"({"id": "t.solid", "components": {"core.transform": {}, "core.collider": {"radius": 0.5}}})");
    put("content/t/prefabs/trigger.json",
        R"({"id": "t.trigger", "components": {"core.transform": {}, "core.collider": {"radius": 0.5}, "core.trigger": {}}})");
    // red: layer 1 · mask 1, blue: layer 2 · mask 2 — 서로 맞지 않는다. picky: layer 1 (red 의 mask 에 맞음) · mask 2
    // (red 의 layer 1 은 안 맞음) — 한쪽만 맞으면 겹치지 않는다
    put("content/t/prefabs/red.json",
        R"({"id": "t.red", "components": {"core.transform": {}, "core.collider": {"radius": 0.5, "layer": 1, "mask": 1}}})");
    put("content/t/prefabs/blue.json",
        R"({"id": "t.blue", "components": {"core.transform": {}, "core.collider": {"radius": 0.5, "layer": 2, "mask": 2}}})");
    put("content/t/prefabs/picky.json",
        R"({"id": "t.picky", "components": {"core.transform": {}, "core.collider": {"radius": 0.5, "layer": 1, "mask": 2}}})");
    put("content/t/scripts/place.ts", kSpawn);
    return root;
}

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

// saveId 차례의 x 위치 (onStart 의 spawn 차례 = saveId 차례)
std::vector<f32> xs(sim::SimulationWorld& w) {
    std::map<SaveId, f32> bySave;
    for (auto [e, tr, id] : w.registry().view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
        bySave[id.saveId] = tr.position.x;
    }
    std::vector<f32> out;
    for (const auto& [s, x] : bySave) {
        out.push_back(x);
    }
    return out;
}

} // namespace

TEST_SUITE("core") {
    TEST_CASE("collision (v0.11 RT-06-4): 보통 둘은 밀어낸다 · 트리거는 밀지 않는다 · layer · mask 가 맞지 않으면 "
              "겹치지 않는다") {
        QuietLog quiet;
        const fs::path root = writePack("basic");
        content::ContentLoadResult r =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(r.ok());
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        w.tick(); // 틱 1 끝에 생긴다
        std::vector<f32> before = xs(w);
        REQUIRE(before.size() == 8);
        w.tick(); // 틱 2 의 Collision
        const std::vector<f32> after = xs(w);
        REQUIRE(after.size() == 8);
        // 보통 쌍: 깊이 0.8 의 반씩 — 0 → -0.4, 0.2 → 0.6
        CHECK(after[0] == doctest::Approx(-0.4f));
        CHECK(after[1] == doctest::Approx(0.6f));
        // 보통 + 트리거: 그대로
        CHECK(after[2] == before[2]);
        CHECK(after[3] == before[3]);
        // red · blue (서로 안 맞음), red · picky (한쪽만 맞음): 그대로
        CHECK(after[4] == before[4]);
        CHECK(after[5] == before[5]);
        CHECK(after[6] == before[6]);
        CHECK(after[7] == before[7]);

        // 트리거 표시는 세이브에 남는다 (불러온 월드도 밀지 않는다) · 해시가 같다
        REQUIRE(persist::saveWorld(w, root / "at2").has_value());
        auto loaded = persist::loadWorld(engine(), r.db, root / "at2");
        REQUIRE(loaded.has_value());
        CHECK(*loaded->world->worldHash() == *w.worldHash());
        usize triggers = 0;
        for (auto [e, t] : loaded->world->registry().view<ecs::Read<comp::Trigger>>()) {
            (void)e;
            (void)t;
            ++triggers;
        }
        CHECK(triggers == 1);
        w.tick();
        loaded->world->tick();
        CHECK(*loaded->world->worldHash() == *w.worldHash());
    }
}
