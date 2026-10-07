#include "render/renderer/SpriteBatcher.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace sbx::render {

void SpriteBatcher::radixSortByKey() {
    const usize n = m_order.size();
    m_scratch.resize(n);
    for (u32 pass = 0; pass < 8; ++pass) {
        const u32 shift = pass * 8;
        std::array<u32, 256> count{};
        for (const Keyed& k : m_order) {
            ++count[(k.key >> shift) & 0xFFu];
        }
        if (std::ranges::any_of(count, [n](u32 c) { return c == n; })) {
            continue; // 이 바이트는 모두 같다
        }
        u32 sum = 0;
        for (u32& c : count) {
            const u32 t = c;
            c = sum;
            sum += t;
        }
        for (const Keyed& k : m_order) {
            m_scratch[count[(k.key >> shift) & 0xFFu]++] = k;
        }
        m_order.swap(m_scratch);
    }
}

u64 spriteSortKey(RenderPassId pass, u8 layer, u32 pipeline, u32 material, f32 depth) noexcept {
    // float → 순서를 지키는 u32 (음수는 전부 뒤집고, 양수는 부호 비트를 세운다)
    u32 bits = std::bit_cast<u32>(depth);
    bits = (bits & 0x8000'0000u) != 0 ? ~bits : (bits | 0x8000'0000u);
    const u64 d = bits >> 12; // 상위 20 비트
    return (static_cast<u64>(static_cast<u8>(pass)) & 0xFu) << 60 | static_cast<u64>(layer) << 52 |
           (static_cast<u64>(pipeline) & 0xFFFu) << 40 | (static_cast<u64>(material) & 0xF'FFFFu) << 20 | d;
}

void SpriteBatcher::build(std::span<const SpriteDraw> sprites, std::span<const SpriteRegion> regions,
                          const Camera2D& camera, u32 maxPerBatch) {
    m_order.clear();
    m_instances.clear();
    m_batches.clear();
    m_stats = {};
    m_stats.submitted = static_cast<u32>(sprites.size());
    if (maxPerBatch == 0) {
        maxPerBatch = 1;
    }
    static const SpriteRegion kFallback{};
    const auto regionOf = [&](SpriteId id) -> const SpriteRegion& {
        if (id < regions.size()) {
            return regions[id];
        }
        return regions.empty() ? kFallback : regions[kWhiteSprite];
    };

    const WorldRect view = camera.visibleRect();
    m_order.reserve(sprites.size());
    for (u32 i = 0; i < sprites.size(); ++i) {
        const SpriteDraw& s = sprites[i];
        // 회전해도 들어가는 외접원 반경으로 컬링
        const f32 r = 0.5f * std::sqrt(s.size.x * s.size.x + s.size.y * s.size.y);
        const WorldRect bounds{{s.position.x - r, s.position.y - r}, {s.position.x + r, s.position.y + r}};
        if (!view.overlaps(bounds)) {
            ++m_stats.culled;
            continue;
        }
        const SpriteRegion& reg = regionOf(s.sprite);
        m_order.push_back({spriteSortKey(RenderPassId::WorldSprite, s.layer, 0, reg.atlas, s.depth), i});
    }
    // 정렬: 64비트 키의 LSD 기수 정렬 (바이트 8단계, 모든 키가 같은 바이트인 단계는 건너뛴다 — pass · pipeline 등).
    // 안정 정렬이라 같은 키는 제출 순서 그대로다 (결과가 비교 정렬 구현과 무관). 5만 개에서 비교 정렬의 수 배 빠르다
    // (14-PERFORMANCE 7장 render.sprite_batch).
    radixSortByKey();

    m_instances.reserve(m_order.size());
    for (const Keyed& k : m_order) {
        const SpriteDraw& s = sprites[k.index];
        const SpriteRegion& reg = regionOf(s.sprite);
        SpriteInstanceGpu g{};
        g.position[0] = s.position.x;
        g.position[1] = s.position.y;
        g.size[0] = s.size.x;
        g.size[1] = s.size.y;
        g.rotation = s.rotation;
        g.page = reg.page;
        f32 u0 = reg.uv[0], v0 = reg.uv[1], u1 = reg.uv[2], v1 = reg.uv[3];
        if ((s.flags & kSpriteFlipX) != 0) {
            std::swap(u0, u1);
        }
        if ((s.flags & kSpriteFlipY) != 0) {
            std::swap(v0, v1);
        }
        g.uv[0] = u0;
        g.uv[1] = v0;
        g.uv[2] = u1;
        g.uv[3] = v1;
        g.color = s.color;
        g.flags = s.flags;

        const u32 atlas = reg.atlas;
        if (m_batches.empty() || m_batches.back().atlas != atlas || m_batches.back().instanceCount >= maxPerBatch) {
            m_batches.push_back({atlas, static_cast<u32>(m_instances.size()), 0});
        }
        ++m_batches.back().instanceCount;
        m_instances.push_back(g);
    }
    m_stats.drawn = static_cast<u32>(m_instances.size());
    m_stats.batches = static_cast<u32>(m_batches.size());
}

} // namespace sbx::render
