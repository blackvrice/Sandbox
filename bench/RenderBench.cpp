// render.sprite_batch — SpriteBatcher(컬링 · 정렬 · 묶음)의 CPU 비용. docs/14-PERFORMANCE.md 2장, 06 8.5 (목표: 50k
// 가시 스프라이트에서 CPU 렌더 ≤ 4 ms — 이 벤치는 그중 배치 만들기만 잰다. GPU 비용은 사용자 PC 의 실제 실행으로).
#include <random>
#include <vector>

#include "BenchUtil.hpp"
#include "render/renderer/SpriteBatcher.hpp"

namespace sbx::bench {

void runRenderBenchmarks(Context& ctx) {
    if (!wants(ctx, "render.sprite_batch")) {
        return;
    }
    const std::vector<render::SpriteRegion> regions = {
        {0, 0, {0, 0, 1, 1}}, {0, 1, {0.1f, 0.1f, 0.2f, 0.2f}}, {0, 2, {0.3f, 0.3f, 0.4f, 0.4f}}};
    for (const u32 n : ctx.quick ? std::vector<u32>{10'000} : std::vector<u32>{10'000, 50'000}) {
        std::mt19937 rng(5);
        std::uniform_real_distribution<f32> pos(-100.f, 100.f);
        std::vector<render::SpriteDraw> sprites(n);
        for (render::SpriteDraw& s : sprites) {
            s.position = {pos(rng), pos(rng)};
            s.size = {1, 1};
            s.sprite = static_cast<render::SpriteId>(rng() % 3);
            s.layer = static_cast<u8>(rng() % 3 == 0 ? 0 : 10);
            s.depth = -s.position.y;
        }
        render::Camera2D cam;
        cam.viewportWidth = 1920;
        cam.viewportHeight = 1080;
        cam.fit({{-100, -100}, {100, 100}}, 0); // 전부 보인다 (컬링이 버릴 것이 없는 최악)
        render::SpriteBatcher batcher;
        const auto ns = measure(ctx.repeats, [&] {
            batcher.build(sprites, regions, cam);
            doNotOptimize(batcher.instances().data());
        });
        const double ms = median(ns) / 1e6;
        ctx.results.push_back({{"scenario", "render.sprite_batch"},
                               {"params", {{"sprites", n}, {"visible", batcher.stats().drawn}}},
                               {"metrics", {{"build_ms", ms}, {"batches", batcher.stats().batches}}}});
        std::printf("  render.sprite_batch  sprites=%-6u  build %.3f ms  batches %u\n", n, ms, batcher.stats().batches);
    }
}

} // namespace sbx::bench
