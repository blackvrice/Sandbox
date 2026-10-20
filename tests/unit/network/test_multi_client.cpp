// Phase 12 완료 기준 (docs/16-ROADMAP.md 12장, 10-EDITOR 7장): 역할 × 명령 권한 표를 서버 끝까지 (명령 15 종 + 월드 요청),
// 두 클라이언트가 같은 개체 · 같은 칸을 동시에 편집해도 서버 순서 하나로 정해져 모두가 같은 월드로 모인다.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <random>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/scenarios/Scenario.hpp"
#include "network/client/ClientSession.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/LoopbackTransport.hpp"

using namespace sbx;
using namespace sbx::net;
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

const content::ContentDatabase& builtin() {
    static const content::ContentDatabase db = content::ContentDatabase::builtin();
    return db;
}

constexpr f64 kFrame = 1.0 / 30.0;

class QuietScenario final : public scenario::IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "quiet"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "test"; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 100; }
    void setup(sim::SimulationWorld& world) override {
        for (u32 i = 0; i < 40; ++i) {
            cmd::CreateEntity c;
            c.position = {static_cast<f32>(i % 8) * 2.f - 8.f, static_cast<f32>(i / 8) * 2.f - 5.f};
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{1, cmd::kServerIssuer, i + 1}, c});
        }
    }
    void beforeTick(sim::SimulationWorld&) override {}
};

struct Rig {
    LoopbackNetwork hub;
    LoopbackTransport serverTransport{hub};
    std::unique_ptr<ServerHost> host;
    f64 now = 0;

    explicit Rig(ServerHostDesc desc) {
        desc.randomSeed = 13;
        desc.snapshotBytesPerSecond = 0;
        host = std::make_unique<ServerHost>(
            serverTransport,
            std::make_unique<scenario::ScenarioRunner>(catalog(), builtin(), std::make_unique<QuietScenario>(), 1),
            desc);
        REQUIRE(host->start({"srv", 7777}).has_value());
    }
    void step() {
        now += kFrame;
        host->update(kFrame);
    }
};

struct Peer {
    LoopbackTransport transport;
    ClientSession session;
    Peer(Rig& rig, std::string name) : transport(rig.hub), session(transport, desc(std::move(name))) {
        REQUIRE(session.connect({"srv", 7777}, rig.now).has_value());
    }
    static ClientSessionDesc desc(std::string name) {
        ClientSessionDesc d;
        d.displayName = std::move(name);
        d.contentHash = builtin().contentHash();
        d.catalog = &catalog();
        d.content = &builtin();
        return d;
    }
};

void pump(Rig& rig, std::initializer_list<Peer*> peers, int frames = 1) {
    for (int i = 0; i < frames; ++i) {
        for (auto* p : peers) {
            p->session.update(rig.now);
        }
        rig.step();
        for (auto* p : peers) {
            p->session.update(rig.now);
        }
    }
}

std::vector<NetEntityId> serverIds(sim::SimulationWorld& world) {
    std::vector<NetEntityId> ids;
    world.registry().forEachEntityByIndex([&](ecs::EntityId e) {
        if (const auto* ni = world.registry().tryRead<comp::NetIdentity>(e)) {
            ids.push_back(ni->netId);
        }
    });
    std::ranges::sort(ids);
    return ids;
}

struct Expect {
    const char* name;
    cmd::CommandPayload payload;
    Role minimum; // 이 역할부터 (10-EDITOR 7장 표 — 서버 코드와 따로 적는다)
};

} // namespace

TEST_SUITE("network") {

    TEST_CASE("roles × commands: the 10-EDITOR 7 permission table holds end to end (15 payloads + world requests)") {
        const ecs::StableId transform = catalog().find("core.transform")->stableId;
        const std::vector<Expect> table{
            {"create", cmd::CreateEntity{{1.f, 1.f}, {}, {}}, Role::Editor},
            {"move", cmd::MoveEntity{{1}, {0.5f, 0.f}, false}, Role::Editor},
            {"change", cmd::ChangeComponent{1, transform, ecs::Json{{"rotation", 0.5}}}, Role::Editor},
            {"remove", cmd::RemoveComponent{2, catalog().find("core.velocity")->stableId}, Role::Editor},
            {"add", cmd::AddComponent{2, {catalog().find("core.velocity")->stableId, ecs::Json::object()}},
             Role::Editor},
            {"delete", cmd::DeleteEntity{{3}}, Role::Editor},
            {"paint", cmd::PaintTerrain{"core.water", {{0, 0}, {1, 0}}, {}, cmd::BrushShape::Circle, 0}, Role::Editor},
            {"content", cmd::EditContent{content::ContentKind::Rule, "core.none", nullptr}, Role::Editor},
            {"pause", cmd::PauseSimulation{}, Role::Admin},
            {"step", cmd::StepSimulation{1}, Role::Admin},
            {"resume", cmd::ResumeSimulation{}, Role::Admin},
            {"speed", cmd::SetSimulationSpeed{2.f}, Role::Admin},
            // v0.2 M2-T3: 게임 시작 · 중단 = 실행 제어 열 (이 월드엔 세션이 없어 admin 은 NotFound 로 거절된다)
            {"game start", cmd::GameControl{cmd::GameControlOp::Start}, Role::Admin},
            {"game abort", cmd::GameControl{cmd::GameControlOp::Abort}, Role::Admin},
            // v0.2 M3-T1: 플레이어 행동 = player 부터 (없는 행동이라 거절되지만 권한 거절은 아니다)
            {"action", cmd::PlayerAction{"core.none", {0.f, 0.f}}, Role::Player},
        };
        for (u8 ri = 0; ri <= static_cast<u8>(Role::Owner); ++ri) {
            const Role role = static_cast<Role>(ri);
            CAPTURE(roleName(role));
            const fs::path saves = fs::temp_directory_path() / std::format("sbx_matrix_{:x}", std::random_device{}());
            ServerHostDesc d;
            d.defaultRole = role;
            d.savesRoot = saves;
            Rig rig(d);
            Peer p(rig, "who");
            pump(rig, {&p}, 6);
            REQUIRE(p.session.state() == ClientState::Connected);
            CHECK(p.session.role() == role);

            for (const Expect& e : table) {
                CAPTURE(e.name);
                const u32 seq = p.session.sendCommand(e.payload);
                REQUIRE(seq != 0);
                std::optional<CommandResultMsg> got;
                for (int i = 0; i < 20 && !got; ++i) {
                    pump(rig, {&p});
                    for (auto& r : p.session.takeResults()) {
                        if (r.sequence == seq) {
                            got = r;
                        }
                    }
                }
                REQUIRE(got);
                if (role >= e.minimum) {
                    CHECK(got->reason != ErrorCode::PermissionDenied); // 받아들이거나, 대상 · 값 때문에 거절
                } else {
                    CHECK_FALSE(got->accepted);
                    CHECK(got->reason == ErrorCode::PermissionDenied);
                }
            }

            // 월드 요청: 목록 = 모두, 나머지 = admin 부터
            const auto request = [&](WorldOpKind op, std::string name) {
                REQUIRE(p.session.requestWorldOp(op, std::move(name)));
                for (int i = 0; i < 20; ++i) {
                    pump(rig, {&p});
                    for (auto& r : p.session.takeWorldOpResults()) {
                        if (r.op == op) {
                            return r;
                        }
                    }
                }
                FAIL("WorldOpResult 가 오지 않았다");
                return WorldOpResultMsg{};
            };
            const bool admin = role >= Role::Admin;
            for (const auto& [op, name] :
                 std::vector<std::pair<WorldOpKind, std::string>>{{WorldOpKind::Save, "m"},
                                                                  {WorldOpKind::RecordStart, "rec"},
                                                                  {WorldOpKind::RecordStop, ""},
                                                                  {WorldOpKind::Load, "nope"},
                                                                  {WorldOpKind::Play, "nope"},
                                                                  {WorldOpKind::Restart, ""}}) {
                CAPTURE(worldOpName(op));
                const WorldOpResultMsg r = request(op, name);
                const bool denied = r.detail.find("(admin 부터)") != std::string::npos;
                CHECK(denied == !admin);
                if (!admin) {
                    CHECK_FALSE(r.accepted);
                }
            }
            if (admin) {
                CHECK(fs::is_directory(saves / "m"));
                CHECK(fs::is_regular_file(saves / "rec" / "replay.sbxr"));
            }
            const u64 rev = p.session.worldListRevision();
            REQUIRE(p.session.requestWorldOp(WorldOpKind::List));
            pump(rig, {&p}, 3);
            CHECK(p.session.worldListRevision() == rev + 1);
            CHECK(p.session.worldList()->worlds.size() == (admin ? 2u : 0u));
            rig.host->stop();
            std::error_code ec;
            fs::remove_all(saves, ec);
        }
    }

    TEST_CASE("two editors editing the same entities and cells at once converge on the server's order") {
        ServerHostDesc d;
        d.defaultRole = Role::Editor;
        Rig rig(d);
        Peer a(rig, "a");
        Peer b(rig, "b");
        pump(rig, {&a, &b}, 8);
        REQUIRE(a.session.world() != nullptr);
        REQUIRE(b.session.world() != nullptr);
        const ecs::StableId transform = catalog().find("core.transform")->stableId;

        std::mt19937 rng(42);
        const auto uni = [&](f32 lo, f32 hi) { return std::uniform_real_distribution<f32>(lo, hi)(rng); };
        const std::array<const char*, 3> mats{"core.water", "core.sand", "core.rock"};
        u64 sent = 0;
        for (int frame = 0; frame < 90; ++frame) {
            const auto ids = serverIds(rig.host->world());
            REQUIRE_FALSE(ids.empty());
            const NetEntityId target = ids[rng() % ids.size()];
            // 같은 개체를 한쪽은 상대 이동, 다른 쪽은 절대 위치로 — 같은 프레임에
            a.session.sendCommand(cmd::MoveEntity{{target}, {uni(-1, 1), uni(-1, 1)}, false});
            b.session.sendCommand(cmd::MoveEntity{{target}, {uni(-10, 10), uni(-10, 10)}, true});
            sent += 2;
            if (frame % 5 == 0) {
                a.session.sendCommand(cmd::ChangeComponent{target, transform, ecs::Json{{"rotation", uni(0, 3)}}});
                b.session.sendCommand(cmd::ChangeComponent{target, transform, ecs::Json{{"rotation", uni(-3, 0)}}});
                sent += 2;
            }
            if (frame % 7 == 0) { // 겹치는 칸을 다른 머티리얼로
                const Vec2i c{static_cast<i32>(rng() % 12) - 6, static_cast<i32>(rng() % 12) - 6};
                a.session.sendCommand(cmd::PaintTerrain{mats[rng() % 3], {c, {c.x + 1, c.y}}, {}, {}, 0});
                b.session.sendCommand(cmd::PaintTerrain{mats[rng() % 3], {c, {c.x, c.y + 1}}, {}, {}, 0});
                sent += 2;
            }
            if (frame % 11 == 3) { // 한쪽이 지우는 개체를 다른 쪽이 옮긴다
                a.session.sendCommand(cmd::DeleteEntity{{target}});
                b.session.sendCommand(cmd::MoveEntity{{target}, {1.f, 0.f}, false});
                sent += 2;
            }
            if (frame % 13 == 0) {
                b.session.sendCommand(cmd::CreateEntity{{uni(-8, 8), uni(-8, 8)}, {}, {}});
                ++sent;
            }
            pump(rig, {&a, &b});
        }
        pump(rig, {&a, &b}, 30); // 결과 · 마지막 스냅숏까지

        u64 accepted = 0;
        u64 results = 0;
        for (Peer* p : {&a, &b}) {
            for (const auto& r : p->session.takeResults()) {
                ++results;
                accepted += r.accepted ? 1 : 0;
            }
        }
        CHECK(results == sent); // 모든 명령에 결과가 하나씩
        CHECK(accepted > sent / 2);
        CHECK(accepted < sent); // 지운 개체를 옮긴 것은 거절된다

        sim::SimulationWorld& world = rig.host->world();
        const auto ids = serverIds(world);
        for (Peer* p : {&a, &b}) {
            const ClientWorld& cw = *p->session.world();
            CAPTURE(p->session.welcome()->clientId);
            CHECK(cw.serverTick() == world.currentTick());
            REQUIRE(cw.entityCount() == ids.size());
            for (const NetEntityId id : ids) {
                CAPTURE(id);
                const ecs::EntityId se = world.resolve(id);
                const ecs::EntityId ce = cw.find(id);
                REQUIRE(ce != ecs::kNullEntity);
                const auto* st = world.registry().tryRead<comp::Transform>(se);
                const auto* ct = cw.registry().tryRead<comp::Transform>(ce);
                REQUIRE(st != nullptr);
                REQUIRE(ct != nullptr);
                CHECK(ct->position.x == doctest::Approx(st->position.x).epsilon(1e-4));
                CHECK(ct->position.y == doctest::Approx(st->position.y).epsilon(1e-4));
                CHECK(ct->rotation == doctest::Approx(st->rotation).epsilon(1e-4));
            }
            for (i32 y = -8; y <= 8; ++y) {
                for (i32 x = -8; x <= 8; ++x) {
                    CHECK(cw.grid().materialAt({x, y}) == world.grid().materialAt({x, y}));
                }
            }
        }
    }
}
