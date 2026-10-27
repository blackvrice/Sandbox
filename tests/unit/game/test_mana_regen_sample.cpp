// 데모 samples/mana_regen (v0.3 M6-T1, ADR-0069): 스크립트 컴포넌트 eco.mana + server 시스템 (regen · newborn) + client
// 시스템 (hud) + 마나를 쓰는 행동 eco.energize. 같은 프로세스에서 끝까지 (expect · 두 번 같다 · 기운 주기가
// 받아들여진다), 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린 해시, 로컬 세션: 복제 월드의 마나 = 서버 값 (멈춘 뒤),
// Inspector 값 · 편집 (ChangeComponent), client 시스템의 글자 수 = 마나 있는 개체 수.
#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <format>

#include "apps/client/NetworkSession.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/scenarios/PlayScript.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "core/script/ScriptSystemAdapter.hpp"

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

const fs::path kSample = fs::path(SBX_SAMPLES_DIR) / "mana_regen";
constexpr f64 kDt = 1.0 / 30.0;

scenario::PlayScript playScript() {
    auto s = scenario::loadPlayScript(kSample / "playthrough.json");
    REQUIRE_MESSAGE(s.has_value(), s.error().describe());
    return std::move(*s);
}

// 자동 플레이를 끝까지 — 받아들여진 기운 주기 (스크립트 명령 중 speed 빼고) 를 센다
struct Played {
    std::string result;
    u32 energized = 0;
};
Played play(sim::Tick saveAt = 0, const fs::path& saveDir = {}) {
    auto src = scenario::openWorldSource(kSample.string(), {}, 1, catalog());
    REQUIRE_MESSAGE(src.has_value(), src.error().describe());
    CHECK(src->warnings.empty());
    const scenario::PlayScript s = playScript();
    scenario::ScenarioRunner& runner = *src->runner;
    const sim::Tick start = runner.world().currentTick();
    scenario::attachPlayScript(runner, s);
    Played out;
    while (!scenario::playFinished(runner.world(), start, s)) {
        runner.step();
        for (const cmd::CommandResult& r : runner.world().lastResults()) {
            out.energized += r.accepted && r.issuer == cmd::kServerIssuer ? 1 : 0;
        }
        if (saveAt != 0 && runner.world().currentTick() == saveAt) {
            REQUIRE(persist::saveWorld(runner.world(), saveDir).has_value());
        }
        REQUIRE(runner.world().currentTick() < start + s.maxTicks + 10);
    }
    out.energized -= 1; // 첫 단계의 speed 명령
    const auto diff = scenario::checkPlayExpectation(runner.world(), s.expect);
    for (const auto& d : diff) {
        MESSAGE(d);
    }
    CHECK(diff.empty());
    out.result = scenario::describePlayResult(runner.world());
    return out;
}

const ecs::ComponentInfo& mana(const ecs::ComponentCatalog& cat) {
    const ecs::ComponentInfo* info = cat.find("eco.mana");
    REQUIRE(info != nullptr);
    REQUIRE(info->dynamicLayout != nullptr);
    return *info;
}

} // namespace

TEST_SUITE("core") {
    TEST_CASE("sample mana_regen: 열기 — 스크립트 컴포넌트 · 시스템 셋 (server 2 · client 1), 토끼마다 마나") {
        auto src = scenario::openWorldSource(kSample.string(), {}, 1, catalog());
        REQUIRE_MESSAGE(src.has_value(), src.error().describe());
        CHECK(src->warnings.empty());
        const sim::SimulationWorld& w = src->runner->world();
        const auto& programs = w.content().scriptPrograms();
        REQUIRE(programs);
        CHECK(programs->systemCount(script::api::HostDomain::Server) == 2);
        CHECK(programs->systemCount(script::api::HostDomain::Client) == 1);
        REQUIRE(w.scriptSystems() != nullptr);
        CHECK(w.scriptSystems()->systemId(0) == "eco.mana.newborn");
        CHECK(w.scriptSystems()->systemId(1) == "eco.mana.regen");
        const ecs::ComponentInfo& info = mana(w.catalog());
        const ecs::ComponentPoolBase* pool = w.registry().poolByStableId(info.stableId);
        REQUIRE(pool != nullptr);
        CHECK(pool->size() == 75); // 시작 월드의 토끼 — 태어날 때 마나 50 (newborn 이 틱 1 에)
        CHECK(info.getNumber(pool->getRaw(pool->entities()[0]), "current").value_or(-1) == 50.0);
    }

    TEST_CASE("sample mana_regen: 자동 플레이 끝까지 — expect 그대로 · 두 번 같다 · 기운 주기가 받아들여진다") {
        const Played a = play();
        CHECK(a.result.starts_with("게임 없음 · tick 1801 · hash "));
        CHECK(a.energized > 0); // 마나 30 이상인 토끼에 쓰였다 (빗나간 것은 거절 — 결정적)
        const Played b = play();
        CHECK(b.result == a.result);
        CHECK(b.energized == a.energized);
        MESSAGE(std::format("{} · 받아들여진 기운 주기 {}", a.result, a.energized));
    }

    TEST_CASE("sample mana_regen: 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린 해시 (스크립트 컴포넌트 · 시스템 상태)") {
        const fs::path dir = fs::temp_directory_path() / "sbx-tests" / "mana_regen_save";
        std::error_code ec;
        fs::remove_all(dir, ec);
        const Played straight = play(907, dir); // 주기 (15 틱) 가운데 · 기운 주기 사이에서 저장
        const scenario::PlayScript s = playScript();

        auto src = scenario::openWorldSource(kSample.string(), {}, 1, catalog());
        REQUIRE(src.has_value());
        auto loaded = persist::loadWorld(catalog(), src->runner->world().content(), dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->hashVerified);
        REQUIRE(loaded->world->scriptSystems() != nullptr);
        // 나머지 스크립트 명령 (at 은 러너 단계 = 틱 — 시작 틱 1 에서 단계 n 은 틱 n + 1 을 만든다)
        sim::SimulationWorld& w = *loaded->world;
        u32 seq = 1000;
        while (w.currentTick() < 1801) {
            const u64 step = w.currentTick(); // 다음 tick() 이 이 단계 (시작 틱 1 → 단계 1 이 틱 2)
            for (const auto& st : s.steps) {
                if (st.at == step) {
                    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, cmd::kServerIssuer, ++seq},
                                              st.command});
                }
            }
            w.tick();
        }
        CHECK(scenario::describePlayResult(w) == straight.result);
        fs::remove_all(dir, ec);
    }

    TEST_CASE("sample mana_regen: 로컬 세션 — 복제 월드의 마나 = 서버 값, Inspector 값 · 편집, client 시스템 글자") {
        render::MaterialLibrary mats;
        client::NetworkSessionDesc d;
        d.world = kSample.string();
        d.inlineServer = true;
        auto made = client::NetworkSession::create(d, mats);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        client::NetworkSession& ns = **made;
        const auto frame = [&] {
            ns.update(kDt);
            render::RenderWorld w;
            ns.extract(w);
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        for (int i = 0; i < 90; ++i) { // 3 초 — 마나가 몇 번 찬다
            frame();
        }
        ns.togglePause();
        for (int i = 0; i < 60 && !ns.paused(); ++i) {
            frame();
        }
        REQUIRE(ns.paused());
        for (int i = 0; i < 30; ++i) { // 멈춘 월드의 스냅숏이 온다
            frame();
        }
        const sim::SimulationWorld& server = ns.localServer()->host().world();
        const net::ClientWorld* cw = ns.clientWorld();
        REQUIRE(cw != nullptr);
        REQUIRE(cw->serverTick() == server.currentTick());

        // 복제 월드의 마나 바이트 = 서버 값 (netId 마다)
        const ecs::ComponentInfo& info = mana(cw->catalog());
        const ecs::ComponentPoolBase* clientPool = cw->registry().poolByStableId(info.stableId);
        const ecs::ComponentPoolBase* serverPool = server.registry().poolByStableId(info.stableId);
        REQUIRE(clientPool != nullptr);
        REQUIRE(serverPool != nullptr);
        CHECK(clientPool->size() == serverPool->size());
        usize compared = 0;
        NetEntityId someone = kInvalidNetEntityId;
        for (const ecs::EntityId e : clientPool->entities()) {
            const NetEntityId id = cw->registry().read<comp::NetIdentity>(e).netId;
            const ecs::EntityId se = server.resolve(id);
            REQUIRE(se.valid());
            CHECK(std::memcmp(clientPool->getRaw(e), serverPool->getRaw(se), info.dynamicLayout->stride()) == 0);
            ++compared;
            someone = id;
        }
        CHECK(compared > 0);

        // client 시스템 — 마나 있는 개체마다 글자
        const auto overlay = ns.scriptOverlay();
        REQUIRE(overlay.has_value());
        CHECK(overlay->labels.size() == compared);
        REQUIRE(overlay->lines.size() == 1);
        CHECK(overlay->lines[0].starts_with(std::format("토끼 {} · 마나 가득 ", compared)));

        // Inspector: 마나 값이 보이고, 고치면 서버에 (ChangeComponent)
        const auto comps = ns.components(someone);
        const auto it = std::find_if(comps.begin(), comps.end(), [](const auto& c) { return c.name == "eco.mana"; });
        REQUIRE(it != comps.end());
        CHECK(it->value.contains("current"));
        cmd::ChangeComponent change;
        change.target = someone;
        change.stableId = info.stableId;
        change.patch = {{"regen", 7.5}};
        REQUIRE(ns.submit(change) != 0);
        bool done = false;
        for (int i = 0; i < 30 && !done; ++i) {
            frame();
            for (const auto& o : ns.takeOutcomes()) {
                CHECK(o.accepted);
                done = true;
            }
        }
        REQUIRE(done);
        CHECK(info.getNumber(serverPool->getRaw(server.resolve(someone)), "regen").value_or(-1) == 7.5);
    }
}
