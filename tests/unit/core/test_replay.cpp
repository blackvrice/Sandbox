// 명령 JSON · 리플레이 기록·재생 (Phase 5C, D3). docs/09-SERIALIZATION.md 4장.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/command/CommandJson.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/debug/RandomWalk.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/replay/Replay.hpp"

using namespace sbx;
using ecs::Json;
using ecs::stableIdOf;
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

const content::ContentDatabase& content() {
    return content::ContentDatabase::builtin();
}

sim::WorldDesc desc() {
    sim::WorldDesc d;
    d.seed = 11;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    return d;
}

fs::path tempDir(std::string_view name) {
    const fs::path p = fs::temp_directory_path() / "sbx-tests" / "replay" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

// 명령을 다양하게 넣는 작은 입력: 생성 → 이동 → 컴포넌트 변경 → 일시정지 편집 → 한 틱 진행 → 재개 → 삭제 → 칠하기
struct Driver {
    u32 seq = 0;
    std::vector<NetEntityId> made;
    void before(sim::SimulationWorld& w, u64 call) {
        const auto at = [&](cmd::CommandPayload p) {
            w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, ++seq}, std::move(p)});
        };
        if (call == 0) {
            for (int i = 0; i < 6; ++i) {
                at(cmd::CreateEntity{Vec2{static_cast<f32>(i) * 2.f - 5.f, 1.5f},
                                     {cmd::ComponentValue{stableIdOf<comp::Velocity>, Json::object()},
                                      cmd::ComponentValue{stableIdOf<comp::RandomWalk>, Json{{"speed", 1.5}}}},
                                     ""});
            }
        }
        if (call == 3 && made.size() >= 3) {
            at(cmd::MoveEntity{{made[0], made[2]}, Vec2{0.25f, -0.5f}, false});
            at(cmd::ChangeComponent{made[1], stableIdOf<comp::RandomWalk>, Json{{"speed", 0.5}}});
        }
        if (call == 10) {
            at(cmd::PauseSimulation{});
        }
        if (call == 12 && made.size() >= 4) {
            at(cmd::MoveEntity{{made[3]}, Vec2{3.f, 3.f}, true}); // 편집 단계 (틱 번호 그대로)
        }
        if (call == 13) {
            at(cmd::StepSimulation{2});
        }
        if (call == 17) {
            at(cmd::ResumeSimulation{});
        }
        if (call == 20 && made.size() >= 5) {
            at(cmd::DeleteEntity{{made[4]}});
            at(cmd::PaintTerrain{"core.rock", {}, Vec2i{4, 4}, cmd::BrushShape::Square, 1});
        }
    }
    void after(const sim::SimulationWorld& w) {
        for (const auto& r : w.lastResults()) {
            for (const NetEntityId id : r.created) {
                made.push_back(id);
            }
        }
    }
};

// 시작 세이브 + 리플레이를 dir 에 만든다. 원본 월드의 최종 해시를 돌려준다.
u64 record(const fs::path& dir, u64 calls) {
    sim::SimulationWorld w(catalog(), content(), desc());
    REQUIRE(persist::saveWorld(w, dir / "start").has_value());
    replay::ReplayHeader h;
    h.startWorld = "start";
    h.startWorldHash = *w.worldHash();
    h.hashInterval = 5;
    replay::ReplayRecorder rec(w, h);
    Driver d;
    for (u64 c = 0; c < calls; ++c) {
        d.before(w, c);
        w.tick();
        d.after(w);
        REQUIRE(rec.afterTick().has_value());
    }
    REQUIRE(rec.finish().has_value());
    REQUIRE(replay::writeReplay(rec.replay(), dir / "replay.sbxr").has_value());
    return *w.worldHash();
}

Expected<replay::PlaybackResult> play(const fs::path& dir, replay::Replay r) {
    auto loaded = persist::loadWorld(catalog(), content(), dir / r.header.startWorld);
    REQUIRE(loaded.has_value());
    return replay::playReplay(*loaded->world, r);
}

} // namespace

TEST_SUITE("core") {

    TEST_CASE("command json: every payload round-trips, entity refs go through the mapper") {
        const auto out = [](NetEntityId n) { return u64{n} * 100; };
        const auto in = [](u64 s) { return static_cast<NetEntityId>(s / 100); };
        const std::vector<cmd::CommandPayload> all{
            cmd::CreateEntity{Vec2{1.25f, -3.1f}, {cmd::ComponentValue{42, Json{{"a", 1}}}}, "eco.rabbit"},
            cmd::DeleteEntity{{3, 4}},
            cmd::MoveEntity{{5}, Vec2{0.1f, 0.2f}, true},
            cmd::AddComponent{6, cmd::ComponentValue{0xFFFF'FFFF'FFFF'FFFFull, Json::object()}},
            cmd::RemoveComponent{7, 99},
            cmd::ChangeComponent{8, 100, Json{{"speed", 2.5}}},
            cmd::PaintTerrain{"core.water", {Vec2i{-1, 2}, Vec2i{3, -4}}, Vec2i{5, 6}, cmd::BrushShape::Square, 3},
            cmd::PauseSimulation{},
            cmd::ResumeSimulation{},
            cmd::StepSimulation{7},
            cmd::SetSimulationSpeed{0.25f},
        };
        for (const auto& p : all) {
            const Json j = cmd::payloadToJson(p, out);
            INFO(j.dump());
            const auto back = cmd::payloadFromJson(Json::parse(j.dump()), in); // 텍스트까지 왕복
            REQUIRE(back.has_value());
            CHECK(back->index() == p.index());
            CHECK(cmd::payloadToJson(*back, out) == j);
        }
        CHECK(cmd::payloadToJson(all[1], out)["targets"] == Json::array({300, 400})); // saveId 쪽으로 바뀌었다
        CHECK_FALSE(cmd::payloadFromJson(Json{{"op", "fly"}}, in).has_value());
        CHECK_FALSE(cmd::payloadFromJson(Json{{"op", "move"}, {"targets", {1}}}, in).has_value());
        CHECK_FALSE(cmd::payloadFromJson(Json{{"op", "remove"}, {"target", 1}, {"id", "42"}}, in).has_value());
    }

    TEST_CASE("replay: record from a start save and play back with the same hashes (D3), incl. pause/step edits") {
        const fs::path dir = tempDir("roundtrip");
        const u64 finalHash = record(dir, 40);
        auto r = replay::readReplay(dir / "replay.sbxr");
        REQUIRE(r.has_value());
        CHECK(r->calls == 40);
        CHECK(r->finalHash == finalHash);
        CHECK(r->commands.size() >= 12);
        CHECK(r->hashes.size() >= 4);
        // 일시정지 편집 단계의 명령은 같은 틱 번호지만 다른 call 에 있다
        bool editFound = false;
        for (const auto& c : r->commands) {
            editFound = editFound || (c.payload["op"] == "move" && c.payload["absolute"] == true);
        }
        CHECK(editFound);
        const auto res = play(dir, *r);
        REQUIRE(res.has_value());
        CHECK_FALSE(res->mismatch.has_value());
        CHECK(res->warnings.empty());
        CHECK(res->calls == 40);
        CHECK(res->hashesCompared == r->hashes.size() + 1);
    }

    TEST_CASE("replay: a changed command is caught at the next hash record; a different start world is refused") {
        const fs::path dir = tempDir("tamper");
        (void)record(dir, 30);
        auto r = replay::readReplay(dir / "replay.sbxr");
        REQUIRE(r.has_value());
        // call 3 의 상대 이동량을 바꾼다
        bool changed = false;
        for (auto& c : r->commands) {
            if (!changed && c.payload["op"] == "move") {
                c.payload["value"] = Json::array({0.5, -0.5});
                changed = true;
            }
        }
        REQUIRE(changed);
        const auto res = play(dir, *r);
        REQUIRE(res.has_value());
        REQUIRE(res->mismatch.has_value());
        CHECK(res->mismatch->call <= 5); // 다음 해시 레코드 (5 틱 간격)
        CHECK_FALSE(res->simVersionDiffers);

        auto other = *replay::readReplay(dir / "replay.sbxr");
        other.header.startWorldHash ^= 1;
        CHECK_FALSE(play(dir, other).has_value());
    }

    TEST_CASE("replay: file reader rejects truncated files, wrong magic and unknown records") {
        const fs::path dir = tempDir("reader");
        (void)record(dir, 10);
        std::stringstream ss;
        ss << std::ifstream(dir / "replay.sbxr").rdbuf();
        const std::string text = ss.str();
        const auto write = [&](const std::string& name, const std::string& t) {
            std::ofstream(dir / name, std::ios::binary) << t;
            return replay::readReplay(dir / name);
        };
        CHECK(write("ok.sbxr", text).has_value());
        // 마지막 줄(end 레코드)을 떼어 낸다
        const std::string noEnd = text.substr(0, text.rfind('\n', text.size() - 2) + 1);
        REQUIRE(noEnd.size() < text.size());
        CHECK_FALSE(write("truncated.sbxr", noEnd).has_value());
        CHECK_FALSE(write("magic.sbxr", "{\"magic\":\"NOPE\"}\n").has_value());
        CHECK_FALSE(write("unknown.sbxr", noEnd + "{\"k\":\"zzz\"}\n").has_value());
        std::string crlf;
        for (const char ch : text) {
            if (ch == '\n') {
                crlf += '\r';
            }
            crlf += ch;
        }
        CHECK(write("crlf.sbxr", crlf).has_value()); // Windows 줄 끝
    }

} // TEST_SUITE
