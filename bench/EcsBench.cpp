// ECS 벤치: ecs.iterate, ecs.churn (docs/14-PERFORMANCE.md 2장)
#include <cstdio>
#include <cstdlib>
#include <format>
#include <vector>

#include "BenchUtil.hpp"
#include "core/ecs/EntityCommandBuffer.hpp"
#include "core/ecs/Registry.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::bench {
namespace {

struct A {
    Vec2 v;
};
struct B {
    Vec2 v{1.f, 2.f};
};
struct C {
    f32 x = 1.f;
};
struct D {
    f32 y = 2.f;
};
template <class V>
void reflect(V& vis, A& c) {
    vis.field("v", c.v);
}
template <class V>
void reflect(V& vis, B& c) {
    vis.field("v", c.v);
}
template <class V>
void reflect(V& vis, C& c) {
    vis.field("x", c.x);
}
template <class V>
void reflect(V& vis, D& c) {
    vis.field("y", c.y);
}

} // namespace
} // namespace sbx::bench

SBX_COMPONENT(sbx::bench::A, "bench.a", 1, sbx::ecs::ComponentFlags::None);
SBX_COMPONENT(sbx::bench::B, "bench.b", 1, sbx::ecs::ComponentFlags::None);
SBX_COMPONENT(sbx::bench::C, "bench.c", 1, sbx::ecs::ComponentFlags::None);
SBX_COMPONENT(sbx::bench::D, "bench.d", 1, sbx::ecs::ComponentFlags::None);

namespace sbx::bench {
namespace {

using namespace ecs;

struct Rng {
    u64 s;
    u32 below(u32 n) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>((s >> 33) % n);
    }
};

void populate(Registry& r, int n, int comps) {
    for (int i = 0; i < n; ++i) {
        const EntityId e = r.create();
        r.emplace<A>(e);
        if (comps >= 2) {
            r.emplace<B>(e);
        }
        if (comps >= 3) {
            r.emplace<C>(e);
        }
        if (comps >= 4) {
            r.emplace<D>(e);
        }
    }
}

void iterate(Context& ctx, int n, int comps) {
    Registry r;
    populate(r, n, comps);
    constexpr f32 dt = 1.f / 30.f;
    const int passes = ctx.quick ? 2 : (n <= 1000 ? 2000 : (n <= 10000 ? 200 : 40));
    const auto samples = measure(ctx.repeats, [&] {
        for (int p = 0; p < passes; ++p) {
            if (comps == 1) {
                for (auto [e, a] : r.view<Write<A>>()) {
                    a.v.x += dt;
                }
            } else if (comps == 2) {
                for (auto [e, a, b] : r.view<Write<A>, Read<B>>()) {
                    a.v += b.v * dt;
                }
            } else {
                for (auto [e, a, b, c, d] : r.view<Write<A>, Read<B>, Read<C>, Read<D>>()) {
                    a.v += b.v * (c.x * d.y * dt);
                }
            }
        }
        doNotOptimize(r);
    });
    // 결과 수가 기대와 다르면 측정이 무의미하다 (예: 빠진 풀 때문에 빈 view)
    const usize visited = comps == 1   ? r.view<Read<A>>().count()
                          : comps == 2 ? r.view<Read<A>, Read<B>>().count()
                                       : r.view<Read<A>, Read<B>, Read<C>, Read<D>>().count();
    if (visited != static_cast<usize>(n)) {
        std::fprintf(stderr, "ecs.iterate: 방문 수 %zu != %d\n", visited, n);
        std::abort();
    }
    const double nsPerEntity = median(samples) / (static_cast<double>(passes) * n);
    ctx.results.push_back({{"scenario", "ecs.iterate"},
                           {"params", {{"entities", n}, {"components", comps}}},
                           {"metrics", {{"ns_per_entity", nsPerEntity}}}});
    std::printf("  ecs.iterate  entities=%-6d components=%d  %.2f ns/entity\n", n, comps, nsPerEntity);
}

// 틱마다 생존 엔티티의 1% 파괴 + 1% 생성(2 컴포넌트) + 1% 컴포넌트 추가/제거. ECB 경유.
void churn(Context& ctx, int n) {
    const int ticks = ctx.quick ? 5 : 200;
    const auto samples = measure(ctx.repeats, [&] {
        Registry r;
        populate(r, n, 2);
        std::vector<EntityId> alive;
        r.forEachEntityByIndex([&](EntityId e) { alive.push_back(e); });
        Rng rng{12345};
        const int perTick = std::max(1, n / 100);
        for (int t = 0; t < ticks; ++t) {
            EntityCommandBuffer ecb;
            for (int k = 0; k < perTick; ++k) {
                const u32 i = rng.below(static_cast<u32>(alive.size()));
                ecb.destroy(alive[i]);
                alive[i] = alive.back();
                alive.pop_back();
                const PendingEntity p = ecb.createEmpty();
                ecb.emplace(p, A{});
                ecb.emplace(p, B{});
                const EntityId other = alive[rng.below(static_cast<u32>(alive.size()))];
                if (rng.below(2) == 0) {
                    ecb.emplace(other, C{});
                } else {
                    ecb.remove<C>(other);
                }
            }
            std::vector<EntityId> created;
            ecb.apply(r, &created);
            alive.insert(alive.end(), created.begin(), created.end());
            r.clearTickLogs();
        }
        doNotOptimize(r);
    });
    const double opsPerRun =
        static_cast<double>(ticks) * std::max(1, n / 100) * 5.0; // destroy+create+2 emplace+add/remove
    const double nsPerOp = median(samples) / opsPerRun;
    ctx.results.push_back({{"scenario", "ecs.churn"},
                           {"params", {{"entities", n}, {"ticks", ticks}}},
                           {"metrics", {{"ns_per_op", nsPerOp}}}});
    std::printf("  ecs.churn    entities=%-6d ticks=%d       %.2f ns/op (populate 포함)\n", n, ticks, nsPerOp);
}

} // namespace

void runEcsBenchmarks(Context& ctx) {
    const std::vector<int> sizes = ctx.quick ? std::vector<int>{1000} : std::vector<int>{1000, 10000, 50000};
    for (const int n : sizes) {
        for (const int comps : {1, 2, 4}) {
            iterate(ctx, n, comps);
        }
    }
    for (const int n : sizes) {
        churn(ctx, n);
    }
}

} // namespace sbx::bench
