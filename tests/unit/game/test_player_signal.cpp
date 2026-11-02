// 명령 PlayerSignal (v0.5 M2-T5 (3), 11 6.1장): 콘텐츠 signals.json — 형식 · V13 · 중복 (V1), 적용 — 선언된 이름 · 값
// 범위 · 한 틱의 보내는 쪽마다 perTick · 게임 세션은 Running 에서만 · 월드는 바뀌지 않는다, 리플레이 JSON 왕복.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/command/CommandJson.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/game/GameRules.hpp"
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

struct Root {
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "player-signal" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        file("t/pack.json", R"({"id": "t", "version": "1"})");
    }
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] content::ContentLoadResult load() const {
        return content::ContentLoader::load(root, std::vector<std::string>{"t"}, catalog());
    }
    fs::path root;
};

std::string dump(const content::ContentLoadResult& r) {
    std::string s;
    for (const content::ContentIssue& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

bool hasIssue(const content::ContentLoadResult& r, std::string_view rule, std::string_view fragment) {
    for (const content::ContentIssue& i : r.issues) {
        if (i.rule == rule && i.describe().find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

constexpr std::string_view kSignals = R"([
  {"id": "t.place", "min": 0, "max": 224},
  {"id": "t.ready"},
  {"id": "t.burst", "min": -5, "max": 5, "perTick": 3}
])";

struct SignalWorld {
    explicit SignalWorld(const content::ContentDatabase& db) : world(catalog(), db, desc()) {}
    static sim::WorldDesc desc() {
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "core.grass";
        return d;
    }
    void push(cmd::ClientId issuer, cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, issuer, ++seq}, std::move(p)});
    }
    // 한 틱에 넣은 명령들의 결과 (받아들임 · 거절 글)
    std::vector<std::string> tick() {
        world.tick();
        std::vector<std::string> out;
        for (const cmd::CommandResult& r : world.lastResults()) {
            out.push_back(r.accepted ? std::string("ok") : std::string(errorCodeName(r.error.code)));
        }
        return out;
    }
    sim::SimulationWorld world;
    u32 seq = 0;
};

} // namespace

TEST_SUITE("core") {

    TEST_CASE("player signal: signals.json 을 읽는다 — 기본값 (min = max = 0 · perTick 1), id 정렬") {
        Root root("load_ok");
        root.file("t/signals.json", kSignals);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        CHECK(r.issues.empty());
        REQUIRE(r.db.signals().size() == 3);
        CHECK(r.db.signals()[0].id == "t.burst"); // 정렬
        const content::SignalDef* ready = r.db.findSignal("t.ready");
        REQUIRE(ready != nullptr);
        CHECK(ready->min == 0);
        CHECK(ready->max == 0);
        CHECK(ready->perTick == 1);
        const content::SignalDef* burst = r.db.findSignal("t.burst");
        REQUIRE(burst != nullptr);
        CHECK(burst->min == -5);
        CHECK(burst->perTick == 3);
        CHECK(r.db.findSignal("t.none") == nullptr);
    }

    TEST_CASE("player signal: signals.json 의 잘못 — V13 (범위 · perTick · 정수) · V1 (중복 · 이름공간) · 모르는 키") {
        const auto bad = [](std::string_view name, std::string_view text, std::string_view rule,
                            std::string_view part) {
            Root root(name);
            root.file("t/signals.json", text);
            const content::ContentLoadResult r = root.load();
            CAPTURE(text);
            CHECK_FALSE(r.ok());
            CHECK_MESSAGE(hasIssue(r, rule, part), dump(r));
        };
        bad("minmax", R"([{"id": "t.a", "min": 3, "max": 1}])", "V13", "min 3 이 max 1 보다 크다");
        bad("tick0", R"([{"id": "t.a", "perTick": 0}])", "V13", "perTick 은 1~8");
        bad("tick9", R"([{"id": "t.a", "perTick": 9}])", "V13", "perTick 은 1~8");
        bad("frac", R"([{"id": "t.a", "max": 1.5}])", "V13", "max 는 32 비트 정수");
        bad("big", R"([{"id": "t.a", "max": 4294967296}])", "V13", "max 는 32 비트 정수");
        bad("dup", R"([{"id": "t.a"}, {"id": "t.a"}])", "V1", "PlayerSignal id 't.a' 중복");
        bad("ns", R"([{"id": "x.a"}])", "V1", "x.a");
        bad("key", R"([{"id": "t.a", "rate": 3}])", "V3", "rate");
        bad("shape", R"({"id": "t.a"})", "JSON", "PlayerSignal 배열");
    }

    TEST_CASE("player signal: 받아들임 · 없는 이름 · 값 범위 · 한 틱의 보내는 쪽마다 perTick — 다음 틱에 다시 센다") {
        Root root("apply");
        root.file("t/signals.json", kSignals);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        SignalWorld w(r.db);
        const u64 before = w.world.worldHash().value();
        w.push(1, cmd::PlayerSignal{"t.place", 224});
        w.push(1, cmd::PlayerSignal{"t.place", 225}); // 범위 밖 (센 수에 들지 않는다)
        w.push(1, cmd::PlayerSignal{"t.place", -1});
        w.push(1, cmd::PlayerSignal{"t.nope", 0});
        w.push(1, cmd::PlayerSignal{"t.place", 3}); // 이 틱에 1 의 t.place 는 하나 받았다
        w.push(2, cmd::PlayerSignal{"t.place", 3}); // 다른 쪽은 따로 센다
        w.push(1, cmd::PlayerSignal{"t.burst", -5});
        w.push(1, cmd::PlayerSignal{"t.burst", 5});
        w.push(1, cmd::PlayerSignal{"t.burst", 0});
        w.push(1, cmd::PlayerSignal{"t.burst", 0}); // perTick 3 을 넘는다
        const std::vector<std::string> first = w.tick();
        // 적용 차례 = (executeTick, issuer, sequence) — 보낸 쪽 2 의 신호는 1 의 것이 모두 끝난 뒤
        const std::vector<std::string> want{"ok", "OutOfRange", "OutOfRange", "NotFound",    "RateLimited",
                                            "ok", "ok",         "ok",         "RateLimited", "ok"};
        CHECK(first == want);
        w.push(1, cmd::PlayerSignal{"t.place", 7}); // 다음 틱 — 다시 받는다
        CHECK(w.tick() == std::vector<std::string>{"ok"});
        // 신호는 월드를 바꾸지 않는다 — 신호 없이 같은 틱을 돈 월드와 같은 해시
        SignalWorld plain(r.db);
        plain.world.tick();
        plain.world.tick();
        CHECK(w.world.worldHash().value() == plain.world.worldHash().value());
        CHECK(before != 0);
    }

    TEST_CASE("player signal: 게임 세션이 있으면 Running 에서만") {
        Root root("session");
        root.file("t/signals.json", kSignals).file("t/modes/m.json", R"({"id": "t.play", "name": "놀이"})");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        SignalWorld w(r.db);
        REQUIRE(game::resetSession(w.world, "t.play").has_value());
        w.push(1, cmd::PlayerSignal{"t.ready", 0}); // Lobby — 편집 단계에서 적용되고 거절
        CHECK(w.tick() == std::vector<std::string>{"InvalidArgument"});
        w.push(1, cmd::GameControl{cmd::GameControlOp::Start});
        (void)w.tick(); // Starting
        (void)w.tick(); // Running
        w.push(1, cmd::PlayerSignal{"t.ready", 0});
        CHECK(w.tick() == std::vector<std::string>{"ok"});
    }

    TEST_CASE("player signal: 리플레이 JSON 왕복 · 이름") {
        const cmd::CommandPayload p = cmd::PlayerSignal{"t.place", -42};
        const auto j = cmd::payloadToJson(p, [](NetEntityId n) { return u64{n}; });
        CHECK(j["op"] == "signal");
        auto back = cmd::payloadFromJson(j, [](u64 n) { return static_cast<NetEntityId>(n); });
        REQUIRE(back.has_value());
        REQUIRE(std::holds_alternative<cmd::PlayerSignal>(*back));
        CHECK(std::get<cmd::PlayerSignal>(*back).name == "t.place");
        CHECK(std::get<cmd::PlayerSignal>(*back).value == -42);
        CHECK(cmd::commandName(p) == "PlayerSignal");
        // value 는 없어도 된다 (0), 정수가 아니거나 32 비트 밖이면 거절
        const auto noValue = cmd::payloadFromJson(ecs::Json{{"op", "signal"}, {"name", "t.ready"}},
                                                  [](u64 n) { return static_cast<NetEntityId>(n); });
        REQUIRE(noValue.has_value());
        CHECK(std::get<cmd::PlayerSignal>(*noValue).value == 0);
        CHECK_FALSE(cmd::payloadFromJson(ecs::Json{{"op", "signal"}, {"name", "t.a"}, {"value", 1.5}}, [](u64 n) {
                        return static_cast<NetEntityId>(n);
                    }).has_value());
        CHECK_FALSE(
            cmd::payloadFromJson(ecs::Json{{"op", "signal"}, {"name", "t.a"}, {"value", 4294967296ll}}, [](u64 n) {
                return static_cast<NetEntityId>(n);
            }).has_value());
        CHECK_FALSE(cmd::payloadFromJson(ecs::Json{{"op", "signal"}}, [](u64 n) {
                        return static_cast<NetEntityId>(n);
                    }).has_value());
    }

} // TEST_SUITE
