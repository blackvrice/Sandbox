// 플레이어 표 (v0.5 M2-T5 (4a), ADR-0093): 서버 명령 PlayerJoin · PlayerLeave → SimulationWorld::players() — 넣기 ·
// 바꾸기 (같은 id) · 빼기, 서버만 (issuer 0), 잘못된 값, 리플레이 JSON 왕복, WorldHash · 개체에 닿지 않는다, 스크립트
// ctx.playerCount · playerId · playerRole.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/command/CommandJson.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/game/GameOwner.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"

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

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

struct World {
    explicit World(const content::ContentDatabase& db = content::ContentDatabase::builtin())
        : world(catalog(), db, smallWorld()) {}
    cmd::CommandResult run(cmd::ClientId issuer, cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, issuer, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    // "id 역할 이름" — id 순
    std::vector<std::string> players() const {
        std::vector<std::string> out;
        for (const auto& [id, row] : world.players()) {
            out.push_back(std::format("{} {} {}", id, row.role, row.name));
        }
        return out;
    }
    sim::SimulationWorld world;
    u32 seq = 0;
};

// 팩 t 를 임시 폴더에 쓴다 (파일 이름 → 내용)
fs::path writePack(std::string_view name, std::initializer_list<std::pair<std::string, std::string>> files) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "player-table" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    for (const auto& [rel, text] : files) {
        const fs::path f = root / "t" / rel;
        fs::create_directories(f.parent_path());
        std::ofstream(f, std::ios::binary) << text;
    }
    return root;
}

std::string issues(const content::ContentLoadResult& r) {
    std::string out;
    for (const content::ContentIssue& i : r.issues) {
        out += i.describe() + "\n";
    }
    return out;
}

constexpr std::string_view kOwnerPrefabs =
    R"([{"id": "t.board", "components": {"core.transform": {}}}, {"id": "t.stone", "components": {"core.transform": {}}}])";
} // namespace

TEST_SUITE("core") {

    TEST_CASE("player table: Join 은 넣고 같은 id 는 바꾼다, Leave 는 뺀다 — 서버만, 잘못된 값은 거절") {
        World w;
        CHECK(w.run(cmd::kServerIssuer, cmd::PlayerJoin{7, 2, "영희"}).accepted);
        CHECK(w.run(cmd::kServerIssuer, cmd::PlayerJoin{3, 1, "철수"}).accepted);
        CHECK(w.players() == std::vector<std::string>{"3 1 철수", "7 2 영희"}); // id 순
        const auto again = w.run(cmd::kServerIssuer, cmd::PlayerJoin{3, 4, "철수2"});
        CHECK(again.accepted);
        CHECK(again.created.empty()); // 개체를 만들지 않는다
        CHECK(w.players() == std::vector<std::string>{"3 4 철수2", "7 2 영희"});
        CHECK(w.run(cmd::kServerIssuer, cmd::PlayerLeave{3}).accepted);
        CHECK(w.players() == std::vector<std::string>{"7 2 영희"});
        CHECK(w.run(cmd::kServerIssuer, cmd::PlayerLeave{3}).error.code == ErrorCode::NotFound);
        CHECK(w.run(1, cmd::PlayerJoin{1, 1, "가짜"}).error.code == ErrorCode::PermissionDenied);
        CHECK(w.run(1, cmd::PlayerLeave{7}).error.code == ErrorCode::PermissionDenied);
        CHECK(w.run(cmd::kServerIssuer, cmd::PlayerJoin{0, 1, "서버"}).error.code == ErrorCode::InvalidArgument);
        CHECK(w.run(cmd::kServerIssuer, cmd::PlayerJoin{9, 5, "역할"}).error.code == ErrorCode::InvalidArgument);
        CHECK(w.run(cmd::kServerIssuer, cmd::PlayerJoin{9, 1, std::string(65, 'x')}).error.code ==
              ErrorCode::InvalidArgument);
        CHECK(w.players() == std::vector<std::string>{"7 2 영희"});
    }

    TEST_CASE("player table: 개체 · WorldHash 에 닿지 않는다 — 같은 게임은 접속과 무관하게 같다 (ADR-0093)") {
        const auto play = [](bool players) {
            World w;
            (void)w.run(cmd::kServerIssuer, cmd::CreateEntity{{1, 1}, {}, {}});
            if (players) {
                (void)w.run(cmd::kServerIssuer, cmd::PlayerJoin{1, 1, "a"});
                (void)w.run(cmd::kServerIssuer, cmd::PlayerJoin{2, 2, "b"});
            } else {
                w.world.tick();
                w.world.tick();
            }
            (void)w.run(cmd::kServerIssuer, cmd::CreateEntity{{2, 2}, {}, {}}); // saveId 가 밀리지 않는다
            return std::pair{w.world.worldHash().value(), w.world.registry().aliveCount()};
        };
        CHECK(play(true) == play(false));
    }

    TEST_CASE("player table: 리플레이 JSON 왕복 · 이름 · 잘못된 JSON") {
        const auto toJson = [](const cmd::CommandPayload& p) {
            return cmd::payloadToJson(p, [](NetEntityId n) { return u64{n}; });
        };
        const auto fromJson = [](const ecs::Json& j) {
            return cmd::payloadFromJson(j, [](u64 n) { return static_cast<NetEntityId>(n); });
        };
        const auto back = fromJson(toJson(cmd::PlayerJoin{65535, 4, "주인"}));
        REQUIRE(back.has_value());
        const auto& pj = std::get<cmd::PlayerJoin>(*back);
        CHECK(pj.player == 65535);
        CHECK(pj.role == 4);
        CHECK(pj.name == "주인");
        const auto leave = fromJson(toJson(cmd::PlayerLeave{12}));
        REQUIRE(leave.has_value());
        CHECK(std::get<cmd::PlayerLeave>(*leave).player == 12);
        CHECK(cmd::commandName(cmd::PlayerJoin{}) == "PlayerJoin");
        CHECK(cmd::commandName(cmd::PlayerLeave{}) == "PlayerLeave");
        CHECK_FALSE(fromJson(ecs::Json{{"op", "player.leave"}, {"player", 0}}).has_value());
        CHECK_FALSE(fromJson(ecs::Json{{"op", "player.leave"}, {"player", 70000}}).has_value());
        CHECK_FALSE(fromJson(ecs::Json{{"op", "player.join"}, {"player", 1}, {"role", 5}, {"name", "x"}}).has_value());
        CHECK_FALSE(fromJson(ecs::Json{{"op", "player.join"}, {"player", 1}, {"role", 1}}).has_value());
    }

    TEST_CASE("player table: 스크립트 ctx.playerCount · playerId (id 순 · 범위 밖 -1) · playerRole (없으면 -1)") {
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "player-table" / "script";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "t" / "scripts");
        std::ofstream(root / "t" / "pack.json", std::ios::binary) << R"({"id": "t", "version": "1"})";
        std::ofstream(root / "t" / "scripts" / "p.ts", std::ios::binary) << R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.p",
  onTick(ctx) {
    const first = ctx.playerId(0)
    ctx.log(`${ctx.playerCount()} ${first} ${ctx.playerRole(first)} ${ctx.playerId(1)} ${ctx.playerId(2)} ${ctx.playerId(-1)} ${ctx.playerRole(99)} ${ctx.playerRole(0)}`)
  },
})
)";
        const content::ContentLoadResult r =
            content::ContentLoader::load(root, std::vector<std::string>{"t"}, catalog());
        REQUIRE(r.ok());
        World w(r.db);
        (void)w.run(cmd::kServerIssuer, cmd::PlayerJoin{9, 1, "b"});
        (void)w.run(cmd::kServerIssuer, cmd::PlayerJoin{4, 3, "a"});
        const auto& logs = w.world.scriptSystems()->host().recentLogs();
        REQUIRE_FALSE(logs.empty());
        CHECK(logs.back().text == "2 4 3 9 -1 -1 -1 -1");
    }

    TEST_CASE("owner: PlayerAction spawn 의 \"owner\": true — 만든 개체의 주인 = 명령의 issuer, 서버 · owner 없음은 "
              "주인 없음") {
        const fs::path root = writePack(
            "owner_action",
            {{"pack.json", R"({"id": "t", "version": "1"})"},
             {"prefabs/p.json", std::string(kOwnerPrefabs)},
             {"actions.json",
              R"([{"id": "t.place", "range": 2, "effects": [{"op": "spawn", "prefab": "t.stone", "owner": true}]},
                  {"id": "t.drop", "range": 2, "effects": [{"op": "spawn", "prefab": "t.stone"}]}])"}});
        const content::ContentLoadResult r =
            content::ContentLoader::load(root, std::vector<std::string>{"t"}, catalog());
        REQUIRE_MESSAGE(r.ok(), issues(r));
        CHECK(r.db.findPlayerAction("t.place")->effects.at(0).owner);
        CHECK_FALSE(r.db.findPlayerAction("t.drop")->effects.at(0).owner);
        World w(r.db);
        REQUIRE(w.run(cmd::kServerIssuer, cmd::CreateEntity{{1, 1}, {}, "t.board"}).accepted);
        const auto ownerOf = [&](const cmd::CommandResult& res) -> i64 {
            REQUIRE_MESSAGE(res.accepted, res.error.describe());
            REQUIRE(res.created.size() == 1);
            const comp::GameOwner* o = w.world.registry().tryRead<comp::GameOwner>(w.world.resolve(res.created[0]));
            return o != nullptr ? i64{o->player} : -1;
        };
        CHECK(ownerOf(w.run(5, cmd::PlayerAction{"t.place", {1, 1}})) == 5);
        CHECK(ownerOf(w.run(cmd::kServerIssuer, cmd::PlayerAction{"t.place", {1, 1}})) == -1); // 서버 = 주인 없음
        CHECK(ownerOf(w.run(5, cmd::PlayerAction{"t.drop", {1, 1}})) == -1);                   // owner 를 켜지 않았다
    }

    TEST_CASE("owner: \"owner\" 는 bool · Rule 의 spawn 에서는 오류 (명령을 낸 쪽이 없다)") {
        const auto bad = [](std::string_view name, std::string file, std::string text, std::string_view part) {
            const fs::path root = writePack(name, {{"pack.json", R"({"id": "t", "version": "1"})"},
                                                   {"prefabs/p.json", std::string(kOwnerPrefabs)},
                                                   {std::move(file), std::move(text)}});
            const content::ContentLoadResult r =
                content::ContentLoader::load(root, std::vector<std::string>{"t"}, catalog());
            CHECK_FALSE(r.ok());
            CHECK_MESSAGE(issues(r).find(part) != std::string::npos, issues(r));
        };
        bad("owner_type", "actions.json",
            R"([{"id": "t.place", "effects": [{"op": "spawn", "prefab": "t.stone", "owner": "yes"}]}])",
            "owner 는 true · false");
        bad("owner_rule", "rules/r.json",
            R"([{"id": "t.r", "action": "t.act", "effects": [{"op": "spawn", "prefab": "t.stone", "owner": true}]}])",
            "owner 는 PlayerAction (actions.json) 의 spawn 에서만");
    }

    TEST_CASE("owner: 스크립트 ctx.setOwner — 새 개체 핸들 · 있는 주인 바꾸기 · 범위 밖은 트랩 (쓰기 버림)") {
        const fs::path root =
            writePack("owner_script", {{"pack.json", R"({"id": "t", "version": "1"})"},
                                       {"prefabs/p.json", std::string(kOwnerPrefabs)},
                                       {"scripts/o.ts", R"(import { defineSystem, Owner } from "sandbox"
export default defineSystem({
  id: "t.o",
  onTick(ctx) {
    if (ctx.tick === 2) {
      const s = ctx.spawn("t.stone", 3, 3)
      if (s !== null) { ctx.setOwner(s, 7) }
    } else if (ctx.tick === 3) {
      for (const e of ctx.query(Owner)) { ctx.setOwner(e, 9) }
    } else if (ctx.tick === 4) {
      for (const e of ctx.query(Owner)) { ctx.setOwner(e, 70000) }
    }
  },
})
)"}});
        const content::ContentLoadResult r =
            content::ContentLoader::load(root, std::vector<std::string>{"t"}, catalog());
        REQUIRE_MESSAGE(r.ok(), issues(r));
        World w(r.db);
        const auto owners = [&] {
            std::vector<u32> out;
            for (auto [e, o] : w.world.registry().view<ecs::Read<comp::GameOwner>>()) {
                out.push_back(o.player);
            }
            return out;
        };
        w.world.tick();
        w.world.tick(); // 틱 2: 만들고 주인 7 (같은 호출의 새 개체 핸들)
        CHECK(owners() == std::vector<u32>{7});
        w.world.tick(); // 틱 3: 있는 주인을 9 로
        CHECK(owners() == std::vector<u32>{9});
        w.world.tick(); // 틱 4: 70000 — HostError 트랩, 그 호출의 쓰기를 버린다
        CHECK(owners() == std::vector<u32>{9});
        CHECK(w.world.scriptSystems()->trapCount(0) == 1);
    }

} // TEST_SUITE
