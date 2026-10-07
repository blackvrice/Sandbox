// 속성 테스트: Registry 를 단순 참조 모델(std::map)과 무작위 연산으로 대조한다. docs/13-TESTING.md L1′
//
// 시드를 고정한다. 실패하면 출력된 시드·연산 번호로 그대로 재현된다.
#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "../unit/ecs/TestComponents.hpp"
#include "core/ecs/EntityCommandBuffer.hpp"

using namespace sbx;
using namespace sbx::ecs;
using test::Frozen;
using test::Health;
using test::Position;

namespace {

// 테스트 전용 결정적 난수 (엔진 RandomService 는 Phase 3)
struct SplitMix64 {
    u64 state;
    u64 next() {
        u64 z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    u32 below(u32 n) { return static_cast<u32>(next() % n); }
};

struct ModelEntity {
    std::optional<i32> health;
    std::optional<Vec2> position;
    bool frozen = false;
};

struct Harness {
    Registry reg;
    std::map<u64, ModelEntity> model; // EntityId.raw → 상태
    std::vector<EntityId> everCreated;
    SplitMix64 rng;

    explicit Harness(u64 seed) : rng{seed} {}

    EntityId pickAlive() {
        auto it = model.begin();
        std::advance(it, static_cast<long>(rng.below(static_cast<u32>(model.size()))));
        return EntityId{it->first};
    }

    void step() {
        const u32 op = model.empty() ? 0 : rng.below(100);
        if (op < 20) { // create
            const EntityId e = reg.create();
            REQUIRE_FALSE(model.contains(e.raw));
            model[e.raw] = {};
            everCreated.push_back(e);
        } else if (op < 30) { // destroy
            const EntityId e = pickAlive();
            CHECK(reg.destroy(e));
            model.erase(e.raw);
        } else if (op < 45) { // emplaceOrReplace health
            const EntityId e = pickAlive();
            const auto v = static_cast<i32>(rng.below(1000));
            reg.emplaceOrReplace<Health>(e, Health{v, 1000});
            model[e.raw].health = v;
        } else if (op < 58) { // emplaceOrReplace position
            const EntityId e = pickAlive();
            const Vec2 p{static_cast<f32>(rng.below(100)), static_cast<f32>(rng.below(100))};
            reg.emplaceOrReplace<Position>(e, Position{p});
            model[e.raw].position = p;
        } else if (op < 63) { // frozen
            const EntityId e = pickAlive();
            reg.emplaceOrReplace<Frozen>(e);
            model[e.raw].frozen = true;
        } else if (op < 73) { // remove random component
            const EntityId e = pickAlive();
            switch (rng.below(3)) {
            case 0:
                CHECK(reg.remove<Health>(e) == model[e.raw].health.has_value());
                model[e.raw].health.reset();
                break;
            case 1:
                CHECK(reg.remove<Position>(e) == model[e.raw].position.has_value());
                model[e.raw].position.reset();
                break;
            default:
                CHECK(reg.remove<Frozen>(e) == model[e.raw].frozen);
                model[e.raw].frozen = false;
                break;
            }
        } else if (op < 88) { // write via view
            const i32 delta = static_cast<i32>(rng.below(5));
            for (auto [e, h] : reg.view<Write<Health>>()) {
                h.value += delta;
                model[e.raw].health = *model[e.raw].health + delta;
            }
        } else { // ECB 묶음: 일부 파괴 + 생성(가끔 즉시 상쇄)
            EntityCommandBuffer ecb;
            const EntityId victim = pickAlive();
            ecb.destroy(victim);
            const PendingEntity p = ecb.createEmpty();
            const auto hv = static_cast<i32>(rng.below(1000));
            ecb.emplace(p, Health{hv, 1000});
            const bool cancel = rng.below(4) == 0;
            if (cancel) {
                ecb.destroy(p);
            }
            std::vector<EntityId> resolved;
            ecb.apply(reg, &resolved);
            model.erase(victim.raw);
            if (!cancel) {
                REQUIRE(resolved[0].valid());
                model[resolved[0].raw] = ModelEntity{hv, std::nullopt, false};
                everCreated.push_back(resolved[0]);
            }
        }
    }

    void verify(int opIndex) {
        INFO(std::format("op #{}", opIndex));
        REQUIRE(reg.aliveCount() == model.size());
        for (const auto& [raw, m] : model) {
            const EntityId e{raw};
            REQUIRE(reg.alive(e));
            REQUIRE(reg.has<Health>(e) == m.health.has_value());
            if (m.health) {
                REQUIRE(reg.read<Health>(e).value == *m.health);
            }
            REQUIRE(reg.has<Position>(e) == m.position.has_value());
            if (m.position) {
                REQUIRE(reg.read<Position>(e).p == *m.position);
            }
            REQUIRE(reg.has<Frozen>(e) == m.frozen);
        }
        // 한때 존재했던 핸들 중 모델에 없는 것은 죽어 있어야 한다 (옛 generation 포함)
        for (const EntityId e : everCreated) {
            if (!model.contains(e.raw)) {
                REQUIRE_FALSE(reg.alive(e));
                REQUIRE_FALSE(reg.has<Health>(e));
            }
        }
        // view 결과 = 모델 필터
        std::set<u64> expected;
        for (const auto& [raw, m] : model) {
            if (m.health && m.position && !m.frozen) {
                expected.insert(raw);
            }
        }
        std::set<u64> actual;
        for (auto [e, h, p] : reg.view<Read<Health>, Read<Position>, Exclude<Frozen>>()) {
            (void)h;
            (void)p;
            REQUIRE(actual.insert(e.raw).second); // 중복 방문 없음
        }
        REQUIRE(actual == expected);
        // 풀 불변식 P1~P3
        for (const ComponentPoolBase* pool : reg.poolsByStableId()) {
            const auto valid = pool->validate();
            const std::string why = valid.has_value() ? std::string() : valid.error().describe();
            INFO(why);
            REQUIRE(valid.has_value());
        }
    }
};

} // namespace

TEST_SUITE("property") {

    TEST_CASE("property: registry matches reference model over 100k random operations") {
        for (const u64 seed : {1ull, 42ull, 0xC0FFEEull}) {
            INFO(std::format("seed {:#x}", seed));
            Harness h(seed);
            constexpr int kOps = 100'000 / 3 + 1;
            for (int i = 0; i < kOps; ++i) {
                h.step();
                if (i % 997 == 0) {
                    h.verify(i);
                }
            }
            h.verify(kOps);
        }
    }

    TEST_CASE("property: same operation sequence gives identical entity ids and dense order") {
        Harness a(7);
        Harness b(7);
        for (int i = 0; i < 20'000; ++i) {
            a.step();
            b.step();
        }
        REQUIRE(a.everCreated == b.everCreated);
        const auto pa = a.reg.findPool<Health>()->entities();
        const auto pb = b.reg.findPool<Health>()->entities();
        REQUIRE(std::equal(pa.begin(), pa.end(), pb.begin(), pb.end()));
    }

} // TEST_SUITE
