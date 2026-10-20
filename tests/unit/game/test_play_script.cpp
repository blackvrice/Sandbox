// 자동 플레이 스크립트 (v0.2 M6-T1, ADR-0055): 읽기 · 형식 오류, 단계마다 명령 (Lobby 편집 단계에서도), 끝 판정
// (Finished · 자동 재시작 모드 · maxTicks), 저장소 samples/rabbit_guard 를 같은 프로세스에서 끝까지 — playthrough.json
// 의 expect 그대로.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/game/GameRules.hpp"
#include "core/scenarios/PlayScript.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/BuildInfo.hpp"

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

const fs::path kSample = fs::path(SBX_SAMPLES_DIR) / "rabbit_guard";

} // namespace

TEST_SUITE("core") {

    TEST_CASE("play script: 읽기 — 단계 · maxTicks · expect, 형식 오류") {
        const auto ok = scenario::parsePlayScript(R"({"format": 1, "name": "t", "maxTicks": 90,
            "steps": [{"at": 1, "command": {"op": "game", "action": "start"}},
                      {"at": 1, "command": {"op": "speed", "speed": 2}},
                      {"at": 40, "command": {"op": "action", "action": "eco.feed", "position": [1, 2]}}],
            "expect": {"state": "Finished", "result": "win", "team": 1, "rule": 2, "tick": 77, "hash": "0x00000000000000ff"}})",
                                                  "t.json");
        REQUIRE_MESSAGE(ok.has_value(), ok.error().describe());
        CHECK(ok->name == "t");
        CHECK(ok->maxTicks == 90);
        REQUIRE(ok->steps.size() == 3);
        CHECK(std::holds_alternative<cmd::GameControl>(ok->steps[0].command));
        CHECK(std::holds_alternative<cmd::PlayerAction>(ok->steps[2].command));
        CHECK(ok->expect.result == "win");
        CHECK(ok->expect.hash == 0xffu);

        // 해시는 툴체인 키별 (ADR-0012) — 이 툴체인 항목이 없으면 보지 않는다
        const std::string keyed =
            std::format(R"({{"format": 1, "steps": [], "expect": {{"hash": {{"{}": "0x02", "Other-x": "0x03"}}}}}})",
                        build::kToolchainKey);
        CHECK(scenario::parsePlayScript(keyed, "k.json")->expect.hash == 2u);
        CHECK_FALSE(scenario::parsePlayScript(R"({"format": 1, "steps": [], "expect": {"hash": {"Other-x": "0x03"}}})",
                                              "k.json")
                        ->expect.hash.has_value());

        const auto bad = [](std::string_view text, std::string_view fragment) {
            CAPTURE(text);
            const auto r = scenario::parsePlayScript(text, "t.json");
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().message.find(fragment) != std::string::npos);
        };
        bad(R"({"format": 2, "steps": []})", "format");
        bad(R"({"format": 1, "steps": [], "speed": 1})", "모르는 키 'speed'");
        bad(R"({"format": 1, "steps": [{"at": 0, "command": {"op": "pause"}}]})", "at");
        bad(R"({"format": 1, "steps": [{"at": 5, "command": {"op": "pause"}}, {"at": 2, "command": {"op": "pause"}}]})",
            "오름차순");
        bad(R"({"format": 1, "steps": [{"at": 1, "command": {"op": "fly"}}]})", "/steps/0/command");
        bad(R"({"format": 1, "steps": [], "expect": {"hash": "zz"}})", "expect.hash");
        bad("[1, 2]", "JSON 객체");
    }

    TEST_CASE("play script: 저장소 samples/rabbit_guard — 같은 프로세스에서 끝까지, expect 그대로 · 두 번 같다") {
        REQUIRE(project::isProjectFolder(kSample));
        const auto script = scenario::loadPlayScript(kSample / "playthrough.json");
        REQUIRE_MESSAGE(script.has_value(), script.error().describe());
        const auto play = [&] {
            auto src = scenario::openWorldSource(kSample.string(), {}, 1, catalog());
            REQUIRE_MESSAGE(src.has_value(), src.error().describe());
            CHECK(src->warnings.empty()); // 샘플은 경고 없이 열린다 (콘텐츠 · 시작 월드가 맞다)
            const auto r = scenario::runPlayScript(*src->runner, *script, 20'000);
            REQUIRE_MESSAGE(r.has_value(), r.error().describe());
            const sim::SimulationWorld& w = src->runner->world();
            const auto diff = scenario::checkPlayExpectation(w, script->expect);
            for (const auto& d : diff) {
                MESSAGE(d);
            }
            CHECK(diff.empty());
            return scenario::describePlayResult(w);
        };
        const std::string a = play();
        CHECK(a.starts_with("게임 Finished · win"));
        CHECK(play() == a);
    }

    TEST_CASE("play script: 세션 없는 월드는 maxTicks 에서 끝, expect 가 다르면 목록") {
        auto src = scenario::openWorldSource("random_walk_1k", SBX_CONTENT_DIR, 1, catalog());
        REQUIRE(src.has_value());
        scenario::PlayScript s;
        s.maxTicks = 30;
        CHECK(scenario::runPlayScript(*src->runner, s, 100).has_value());
        CHECK(src->runner->world().currentTick() == 30);
        CHECK(scenario::describePlayResult(src->runner->world()).starts_with("게임 없음 · tick 30"));
        scenario::PlayExpectation x;
        x.state = "Finished";
        x.tick = 31;
        const auto diff = scenario::checkPlayExpectation(src->runner->world(), x);
        CHECK(diff.size() == 2);
        // 끝나지 않으면 오류 (maxTicks 0 · 단계 상한)
        scenario::PlayScript forever;
        CHECK(scenario::runPlayScript(*src->runner, forever, 5).error().code == ErrorCode::OutOfRange);
    }
}
