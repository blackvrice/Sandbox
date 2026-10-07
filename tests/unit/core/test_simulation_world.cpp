#include <doctest/doctest.h>

#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Lifetime.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/debug/RandomWalk.hpp"
#include "core/components/life/Age.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "core/systems/RandomWalkSystem.hpp"

using namespace sbx;
using namespace sbx::sim;
using namespace sbx::cmd;
using ecs::Json;
using ecs::stableIdOf;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        const auto r = comp::registerCoreComponents(c);
        REQUIRE(r.has_value());
        return c;
    }();
    return cat;
}

struct Fixture {
    explicit Fixture(bool defaults = true)
        : world(catalog(), content::ContentDatabase::builtin(),
                WorldDesc{.seed = 7, .registerDefaultSystems = defaults}) {}

    // 다음 틱 몫으로 명령 하나를 넣고 tick() 한 번. 결과 하나를 돌려준다.
    CommandResult run(CommandPayload p) {
        world.enqueue(SimCommand{CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    NetEntityId spawn(Vec2 pos, std::vector<ComponentValue> comps = {}) {
        const auto r = run(CreateEntity{pos, std::move(comps)});
        REQUIRE(r.accepted);
        REQUIRE(r.created.size() == 1);
        return r.created.front();
    }
    ecs::EntityId entity(NetEntityId id) const { return world.resolve(id); }

    SimulationWorld world;
    u32 seq = 0;
};

// 테스트용 System: 매 틱 ECB 로 엔티티 하나를 만든다
class SpawnerSystem final : public ISystem {
public:
    std::string_view name() const noexcept override { return "TestSpawner"; }
    void run(SystemContext& ctx) override {
        const auto p = ctx.ecb.createEmpty();
        ctx.ecb.emplace(p, comp::Transform{Vec2{static_cast<f32>(ctx.tick), 0.f}, 0.f});
    }
};

// 테스트용 System: 실행 순서를 기록한다
class OrderProbe final : public ISystem {
public:
    OrderProbe(std::string_view n, std::vector<std::string_view>& log) : m_name(n), m_log(log) {}
    std::string_view name() const noexcept override { return m_name; }
    void run(SystemContext& ctx) override {
        CHECK(ctx.reg.structureLocked());
        CHECK(ctx.dt == kFixedDt);
        m_log.push_back(m_name);
    }

private:
    std::string_view m_name;
    std::vector<std::string_view>& m_log;
};

} // namespace

TEST_SUITE("core") {

    TEST_CASE("world: create command assigns transform, saveId and netId in order") {
        Fixture f;
        const NetEntityId a = f.spawn(Vec2{1, 2});
        const NetEntityId b = f.spawn(Vec2{3, 4}, {{stableIdOf<comp::Velocity>, Json{{"value", {1.0, 0.0}}}}});
        CHECK(a == 1);
        CHECK(b == 2);
        const auto eb = f.entity(b);
        REQUIRE(eb.valid());
        CHECK(f.world.registry().read<comp::Persistence>(eb).saveId == 2);
        CHECK(f.world.registry().read<comp::Velocity>(eb).value == Vec2{1, 0});
        CHECK(f.world.currentTick() == 2);
        CHECK(f.world.events().count(EventKind::EntitySpawned) == 1);
    }

    TEST_CASE("world: invalid create is rejected atomically") {
        Fixture f;
        // 두 번째 컴포넌트 값이 범위 밖 → 엔티티를 만들지 않는다
        const auto r = f.run(CreateEntity{
            Vec2{},
            {{stableIdOf<comp::Velocity>, Json::object()}, {stableIdOf<comp::RandomWalk>, Json{{"speed", 1000.0}}}}});
        CHECK_FALSE(r.accepted);
        CHECK(r.error.code == ErrorCode::ValidationFailed);
        CHECK(f.world.registry().aliveCount() == 0);
        CHECK(f.world.nextSaveId() == 1);

        CHECK(f.run(CreateEntity{Vec2{}, {{0x1234, Json::object()}}}).error.code == ErrorCode::NotFound);
        CHECK(f.run(CreateEntity{Vec2{}, {{stableIdOf<comp::Persistence>, Json{{"saveId", 5}}}}}).error.code ==
              ErrorCode::PermissionDenied);
        CHECK(f.run(CreateEntity{
                        Vec2{},
                        {{stableIdOf<comp::Velocity>, Json::object()}, {stableIdOf<comp::Velocity>, Json::object()}}})
                  .error.code == ErrorCode::InvalidArgument);
        CHECK(f.run(CreateEntity{Vec2{std::numeric_limits<f32>::infinity(), 0}, {}}).error.code ==
              ErrorCode::InvalidArgument);
        CHECK(f.world.registry().aliveCount() == 0);
    }

    TEST_CASE("world: delete, move, add, remove, change") {
        Fixture f(false);
        const NetEntityId a = f.spawn(Vec2{0, 0});
        const NetEntityId b = f.spawn(Vec2{5, 5});

        CHECK(f.run(MoveEntity{{a, b}, Vec2{1, 1}, false}).accepted);
        CHECK(f.world.registry().read<comp::Transform>(f.entity(b)).position == Vec2{6, 6});
        CHECK(f.run(MoveEntity{{a}, Vec2{-3, 2}, true}).accepted);
        CHECK(f.world.registry().read<comp::Transform>(f.entity(a)).position == Vec2{-3, 2});

        // 대상 하나가 없으면 아무도 움직이지 않는다
        CHECK_FALSE(f.run(MoveEntity{{a, 999}, Vec2{1, 0}, false}).accepted);
        CHECK(f.world.registry().read<comp::Transform>(f.entity(a)).position == Vec2{-3, 2});

        const auto added = f.run(AddComponent{a, {stableIdOf<comp::Age>, Json{{"maxAgeTicks", 100}}}});
        INFO(added.error.describe());
        REQUIRE(added.accepted);
        CHECK(f.world.registry().read<comp::Age>(f.entity(a)).maxAgeTicks == 100);
        CHECK(f.run(AddComponent{a, {stableIdOf<comp::Age>, Json::object()}}).error.code == ErrorCode::AlreadyExists);
        CHECK(f.run(AddComponent{a, {stableIdOf<comp::NetIdentity>, Json::object()}}).error.code ==
              ErrorCode::PermissionDenied);

        // ChangeComponent 는 원자적: 두 키 중 하나가 틀리면 둘 다 그대로
        CHECK_FALSE(f.run(ChangeComponent{a, stableIdOf<comp::Age>, Json{{"ageTicks", 5}, {"nope", 1}}}).accepted);
        CHECK(f.world.registry().read<comp::Age>(f.entity(a)).ageTicks == 0);
        CHECK(f.run(ChangeComponent{a, stableIdOf<comp::Age>, Json{{"ageTicks", 5}}}).accepted);
        CHECK(f.world.registry().read<comp::Age>(f.entity(a)).ageTicks == 5);
        CHECK(f.world.registry().read<comp::Age>(f.entity(a)).maxAgeTicks == 100);
        CHECK(f.run(ChangeComponent{b, stableIdOf<comp::Age>, Json::object()}).error.code == ErrorCode::NotFound);
        CHECK(f.run(ChangeComponent{a, stableIdOf<comp::Persistence>, Json{{"saveId", 1}}}).error.code ==
              ErrorCode::PermissionDenied);

        CHECK(f.run(RemoveComponent{a, stableIdOf<comp::Age>}).accepted);
        CHECK_FALSE(f.world.registry().has<comp::Age>(f.entity(a)));
        CHECK(f.run(RemoveComponent{a, stableIdOf<comp::Age>}).error.code == ErrorCode::NotFound);

        const ecs::EntityId ea = f.entity(a);
        CHECK(f.run(DeleteEntity{{a}}).accepted);
        CHECK_FALSE(f.world.registry().alive(ea));
        CHECK_FALSE(f.world.resolve(a).valid());
        CHECK(f.world.events().count(EventKind::EntityDestroyed) == 1);
        CHECK(f.world.events().events().front().saveId == 1);
        CHECK(f.run(DeleteEntity{{a}}).error.code == ErrorCode::NotFound);
        CHECK(f.run(DeleteEntity{{}}).error.code == ErrorCode::InvalidArgument);

        // netId·saveId 는 재사용되지 않는다 (파괴된 슬롯을 다시 쓰더라도)
        const NetEntityId c = f.spawn(Vec2{});
        CHECK(c == 3);
        CHECK(f.world.registry().read<comp::Persistence>(f.entity(c)).saveId == 3);
    }

    TEST_CASE("world: commands apply in (executeTick, issuer, sequence) order regardless of enqueue order") {
        Fixture f(false);
        f.world.enqueue(SimCommand{CommandHeader{1, 2, 1}, CreateEntity{Vec2{2, 0}, {}}});
        f.world.enqueue(SimCommand{CommandHeader{1, 1, 2}, CreateEntity{Vec2{1, 2}, {}}});
        f.world.enqueue(SimCommand{CommandHeader{1, 1, 1}, CreateEntity{Vec2{1, 1}, {}}});
        f.world.enqueue(SimCommand{CommandHeader{2, 0, 1}, CreateEntity{Vec2{9, 9}, {}}});
        f.world.tick();
        REQUIRE(f.world.lastResults().size() == 3);
        CHECK(f.world.lastResults()[0].sequence == 1);
        CHECK(f.world.lastResults()[0].issuer == 1);
        CHECK(f.world.lastResults()[2].issuer == 2);
        CHECK(f.world.registry().read<comp::Transform>(f.world.resolve(1)).position == Vec2{1, 1});
        CHECK(f.world.registry().read<comp::Transform>(f.world.resolve(3)).position == Vec2{2, 0});
        CHECK(f.world.commandQueue().size() == 1);
    }

    TEST_CASE("world: pause turns tick() into an edit step; step and resume") {
        Fixture f;
        const NetEntityId a = f.spawn(Vec2{}, {{stableIdOf<comp::Velocity>, Json{{"value", {30.0, 0.0}}}}});
        CHECK(f.run(PauseSimulation{}).accepted);
        const Tick pausedAt = f.world.currentTick();
        // 편집 단계: 틱이 오르지 않고, 명령은 바로 보이며, System 은 돌지 않는다
        const auto r = f.run(MoveEntity{{a}, Vec2{100, 0}, true});
        CHECK(r.accepted);
        CHECK(f.world.currentTick() == pausedAt);
        CHECK_FALSE(f.world.lastTickRanSystems());
        CHECK(f.world.registry().read<comp::Transform>(f.entity(a)).position == Vec2{100, 0});
        CHECK(f.world.clock().editSequence() == 1);

        CHECK(f.run(StepSimulation{2}).accepted);
        CHECK(f.world.currentTick() == pausedAt); // Step 은 다음 호출부터
        f.world.tick();
        f.world.tick();
        CHECK(f.world.currentTick() == pausedAt + 2);
        CHECK(f.world.registry().read<comp::Transform>(f.entity(a)).position.x == doctest::Approx(102.0));
        f.world.tick();
        CHECK(f.world.currentTick() == pausedAt + 2); // Step 소진 → 다시 편집 단계

        CHECK(f.run(StepSimulation{0}).error.code == ErrorCode::OutOfRange);
        CHECK(f.run(ResumeSimulation{}).accepted);
        f.world.tick();
        CHECK(f.world.currentTick() == pausedAt + 3);
        CHECK(f.run(StepSimulation{1}).error.code == ErrorCode::InvalidArgument);
    }

    TEST_CASE("world: speed only changes pacing") {
        Fixture f(false);
        CHECK(f.run(SetSimulationSpeed{2.0f}).accepted);
        CHECK(f.world.clock().speed() == 2.0f);
        CHECK(f.world.clock().tickIntervalNanos() == 16'666'666);
        CHECK(f.run(SetSimulationSpeed{9.0f}).error.code == ErrorCode::OutOfRange);
        CHECK(f.run(SetSimulationSpeed{std::numeric_limits<f32>::quiet_NaN()}).error.code == ErrorCode::OutOfRange);
        CHECK(f.world.clock().speed() == 2.0f);
    }

    TEST_CASE("world: movement integrates with fixed dt and leaves resting entities untouched") {
        Fixture f;
        const NetEntityId moving = f.spawn(Vec2{}, {{stableIdOf<comp::Velocity>, Json{{"value", {3.0, -6.0}}}}});
        const NetEntityId resting = f.spawn(Vec2{50, 50}, {{stableIdOf<comp::Velocity>, Json::object()}});
        const auto* pool = f.world.registry().findPool<comp::Transform>();
        const Tick createdAt = f.world.currentTick();
        for (int i = 0; i < 30; ++i) {
            f.world.tick();
        }
        const Vec2 p = f.world.registry().read<comp::Transform>(f.entity(moving)).position;
        // 생성 틱(1)부터 System 이 돈다: 틱 1, 2(두 번째 생성), 3..32 → 32 틱 적분
        CHECK(p.x == doctest::Approx(3.0 * 32 / 30.0).epsilon(1e-4));
        CHECK(p.y == doctest::Approx(-6.0 * 32 / 30.0).epsilon(1e-4));
        const u32 idx = pool->indexOf(f.entity(resting));
        CHECK(pool->changedAt(idx) == createdAt);
    }

    TEST_CASE("world: lifecycle destroys by age and lifetime and reports saveIds") {
        Fixture f;
        const NetEntityId aged = f.spawn(Vec2{}, {{stableIdOf<comp::Age>, Json{{"maxAgeTicks", 5}}}});
        const NetEntityId timed = f.spawn(Vec2{}, {{stableIdOf<comp::Lifetime>, Json{{"expireTick", 6}}}});
        const NetEntityId immortal = f.spawn(Vec2{}, {{stableIdOf<comp::Age>, Json::object()}});
        std::vector<SaveId> died;
        for (int i = 0; i < 10; ++i) {
            f.world.tick();
            for (const SimEvent& e : f.world.events().events()) {
                if (e.kind == EventKind::EntityDestroyed) {
                    died.push_back(e.saveId);
                }
            }
        }
        CHECK_FALSE(f.world.resolve(aged).valid());
        CHECK_FALSE(f.world.resolve(timed).valid());
        REQUIRE(f.world.resolve(immortal).valid());
        CHECK(f.world.registry().read<comp::Age>(f.world.resolve(immortal)).ageTicks == 11);
        CHECK(died == std::vector<SaveId>{1, 2});
    }

    TEST_CASE("world: entities created by system ECBs get identities at sync point 17") {
        Fixture f(false);
        f.world.scheduler().emplace<SpawnerSystem>(Stage::Production);
        f.world.tick();
        f.world.tick();
        CHECK(f.world.registry().aliveCount() == 2);
        CHECK(f.world.resolve(2).valid());
        CHECK(f.world.registry().read<comp::Transform>(f.world.resolve(2)).position == Vec2{2, 0});
        CHECK(f.world.worldHash().has_value());
    }

    TEST_CASE("scheduler: runs by stage then registration order, under structural lock") {
        Fixture f(false);
        std::vector<std::string_view> log;
        f.world.scheduler().emplace<OrderProbe>(Stage::Lifecycle, "L", log);
        f.world.scheduler().emplace<OrderProbe>(Stage::Behavior, "B1", log);
        f.world.scheduler().emplace<OrderProbe>(Stage::Movement, "M", log);
        f.world.scheduler().emplace<OrderProbe>(Stage::Behavior, "B2", log);
        f.world.tick();
        CHECK(log == std::vector<std::string_view>{"B1", "B2", "M", "L"});
        CHECK(stageName(Stage::Movement) == "Movement");
    }

    TEST_CASE("hash: equal for equal histories, sensitive to state, independent of creation slot layout") {
        Fixture a;
        Fixture b;
        for (Fixture* f : {&a, &b}) {
            f->spawn(Vec2{1, 1}, {{stableIdOf<comp::Velocity>, Json{{"value", {1.0, 0.0}}}}});
            f->spawn(Vec2{2, 2});
        }
        REQUIRE(a.world.worldHash().has_value());
        CHECK(*a.world.worldHash() == *b.world.worldHash());

        CHECK(b.run(MoveEntity{{2}, Vec2{0.01f, 0}, false}).accepted);
        a.world.tick();
        CHECK(*a.world.worldHash() != *b.world.worldHash());

        // 해시는 EntityId(슬롯)가 아니라 saveId 순서다: 슬롯을 비틀어도 같은 상태면 같은 해시
        Fixture c(false);
        Fixture d(false);
        c.spawn(Vec2{1, 0});
        c.spawn(Vec2{2, 0});
        const auto filler = d.world.registry().create(); // 슬롯 0 을 미리 차지
        d.world.registry().destroy(filler);              // → 자유 목록에서 재사용 (generation 이 다르다)
        d.spawn(Vec2{1, 0});
        d.spawn(Vec2{2, 0});
        CHECK(c.world.resolve(1) != d.world.resolve(1));
        CHECK(*c.world.worldHash() == *d.world.worldHash());
    }

    TEST_CASE("hash: entities without identity are an error, not silently skipped") {
        Fixture f(false);
        const auto e = f.world.registry().create();
        f.world.registry().emplace<comp::Transform>(e);
        const auto h = f.world.worldHash();
        REQUIRE_FALSE(h.has_value());
        CHECK(h.error().code == ErrorCode::ValidationFailed);
        f.world.assignPendingIdentities();
        CHECK(f.world.worldHash().has_value());
    }

    TEST_CASE("random walk: direction is unit length and deterministic") {
        rnd::CounterRng a(5);
        rnd::CounterRng b(5);
        for (int i = 0; i < 200; ++i) {
            const Vec2 d = sys::RandomWalkSystem::randomDirection(a);
            CHECK(d == sys::RandomWalkSystem::randomDirection(b));
            CHECK(d.length() == doctest::Approx(1.0).epsilon(1e-5));
        }
    }

    TEST_CASE("random walk: walkers move and keep personal space from a stacked neighbour") {
        Fixture f;
        const Json walk{{"speed", 1.0}, {"intervalTicks", 30}, {"personalSpace", 1.0}};
        const NetEntityId a =
            f.spawn(Vec2{0, 0}, {{stableIdOf<comp::Velocity>, Json::object()}, {stableIdOf<comp::RandomWalk>, walk}});
        const NetEntityId b = f.spawn(
            Vec2{0.5f, 0}, {{stableIdOf<comp::Velocity>, Json::object()}, {stableIdOf<comp::RandomWalk>, walk}});
        f.world.tick();
        const Vec2 va = f.world.registry().read<comp::Velocity>(f.entity(a)).value;
        const Vec2 vb = f.world.registry().read<comp::Velocity>(f.entity(b)).value;
        CHECK(va.x < 0.f); // 서로 반대쪽으로 밀어낸다
        CHECK(vb.x > 0.f);
        CHECK(va.length() == doctest::Approx(1.0).epsilon(1e-5));
    }

} // TEST_SUITE
