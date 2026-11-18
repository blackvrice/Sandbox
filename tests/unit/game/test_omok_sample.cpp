// samples/omok (v0.13 M7-T1 — v0.12 오목 개발): 자동 플레이 셋 (흑 가로 · 백 대각선 · 둔 칸 무시) 이 sbx test 와 같은
// 결과, 중간 판 세이브 → 로드 — 흑 다섯 수 · 백 넷 수 뒤 저장한 월드와 이어 둔 월드에 같은 남은 수를 넣으면 수마다 같은
// 해시 · 같은 승자 (흑 다섯 줄). 판 상태 (월드 상태 omok.game) · 돌 (복제 컴포넌트 omok.stone) · 승리 표시
// (omok.result) 가 세이브를 지난다.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/scenarios/PlayScript.hpp"
#include "core/scenarios/WorldSource.hpp"

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

const fs::path kOmok = fs::path(SBX_SAMPLES_DIR) / "omok";

void place(sim::SimulationWorld& w, i32 cell, u32 seq) {
    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, seq}, cmd::PlayerSignal{"omok.place", cell}});
}

} // namespace

TEST_SUITE("game") {
    TEST_CASE("samples/omok (M7-T1): 자동 플레이 셋 — 흑 가로 · 백 대각선 승리, 둔 칸은 무시") {
        for (const fs::path& file : scenario::projectPlayScripts(kOmok)) {
            CAPTURE(file.string());
            auto src = scenario::openWorldSource(kOmok.string(), {}, 1, catalog());
            REQUIRE_MESSAGE(src.has_value(), src.error().describe());
            CHECK(src->warnings.empty());
            const auto script = scenario::loadPlayScript(file);
            REQUIRE(script.has_value());
            REQUIRE(scenario::runPlayScript(*src->runner, *script, scenario::playStepLimit(*script)).has_value());
            CHECK(scenario::checkPlayExpectation(src->runner->world(), script->expect).empty());
        }
    }

    TEST_CASE("samples/omok (M7-T1): 중간 판 세이브 → 로드 — 이어 둔 수마다 같은 해시, 같은 승자") {
        auto src = scenario::openWorldSource(kOmok.string(), {}, 1, catalog());
        REQUIRE_MESSAGE(src.has_value(), src.error().describe());
        sim::SimulationWorld& a = src->runner->world();
        // 게임 시작 → 흑 112 · 백 97 · 흑 113 · 백 98 · 흑 114 · 백 99 · 흑 115 (흑 넷 줄 — 아직 승부 없음)
        a.enqueue(cmd::SimCommand{cmd::CommandHeader{a.currentTick() + 1, cmd::kServerIssuer, 1},
                                  cmd::GameControl{cmd::GameControlOp::Start}});
        a.tick();
        a.tick();
        u32 seq = 1;
        for (const i32 cell : {112, 97, 113, 98, 114, 99, 115}) {
            place(a, cell, ++seq);
            a.tick();
            REQUIRE(a.lastResults().size() == 1);
            CHECK(a.lastResults().front().accepted);
        }
        CHECK(scenario::describePlayResult(a).starts_with("게임 Running"));
        const fs::path dir = fs::temp_directory_path() / "sbx-tests" / "omok-mid";
        std::error_code ec;
        fs::remove_all(dir, ec);
        REQUIRE(persist::saveWorld(a, dir).has_value());
        auto loaded = persist::loadWorld(catalog(), *src->content, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        sim::SimulationWorld& b = *loaded->world;
        CHECK(*b.worldHash() == *a.worldHash());
        // 남은 수: 백 100 · 흑 116 (흑 다섯 줄) — 두 월드에 같은 명령
        for (const i32 cell : {100, 116}) {
            ++seq;
            place(a, cell, seq);
            place(b, cell, seq);
            a.tick();
            b.tick();
            CHECK(*b.worldHash() == *a.worldHash());
        }
        for (int i = 0; i < 5; ++i) { // 끝 조건 (승리 표시) 을 세는 틱
            a.tick();
            b.tick();
        }
        CHECK(*b.worldHash() == *a.worldHash());
        CHECK(scenario::describePlayResult(a).starts_with("게임 Finished · win"));
        CHECK(scenario::describePlayResult(b) == scenario::describePlayResult(a));
        fs::remove_all(dir, ec);
    }
}
