#include "apps/client/presentation/SpriteExtraction.hpp"

#include <algorithm>
#include <string>

#include "core/components/core/Transform.hpp"
#include "core/world/ChunkCoord.hpp"

namespace sbx::client {

render::WorldRect SpriteExtraction::worldBounds(const sim::SimulationWorld& world) {
    const world::GridBounds b = world.desc().bounds;
    return {{static_cast<f32>(b.minChunk.x * world::kChunkSize), static_cast<f32>(b.minChunk.y * world::kChunkSize)},
            {static_cast<f32>((b.maxChunk.x + 1) * world::kChunkSize),
             static_cast<f32>((b.maxChunk.y + 1) * world::kChunkSize)}};
}

const SpriteExtraction::Resolved& SpriteExtraction::resolve(const sim::SimulationWorld& world, SaveId saveId) {
    if (const auto it = m_cache.find(saveId); it != m_cache.end()) {
        return it->second;
    }
    Resolved r;
    r.material = m_materials.find("debug/entity");
    r.size = {0.5f, 0.5f};
    r.layer = 5;
    const auto& opaque = world.opaqueComponents();
    if (const auto it = opaque.find(saveId); it != opaque.end()) {
        const ecs::Json& comps = it->second;
        // {"render.sprite": {"version": 1, "value": {"material": "eco/rabbit", "size": [0.8, 0.8], "layer": 10}}}
        if (comps.is_object() && comps.contains("render.sprite") && comps["render.sprite"].contains("value")) {
            const ecs::Json& v = comps["render.sprite"]["value"];
            r.fromContent = true;
            r.size = {1, 1};
            r.layer = 0;
            if (v.contains("material") && v["material"].is_string()) {
                r.material = m_materials.find(v["material"].get<std::string>());
            }
            if (v.contains("size") && v["size"].is_array() && v["size"].size() == 2 && v["size"][0].is_number() &&
                v["size"][1].is_number()) {
                r.size = {v["size"][0].get<f32>(), v["size"][1].get<f32>()};
            }
            if (v.contains("layer") && v["layer"].is_number_integer()) {
                r.layer = static_cast<u8>(std::clamp(v["layer"].get<int>(), 0, 255));
            }
        }
    }
    return m_cache.emplace(saveId, r).first->second;
}

void SpriteExtraction::capture(sim::SimulationWorld& world, WorldSnapshot& out) {
    ++m_captures;
    out.tick = world.currentTick();
    out.sprites.clear();
    out.stats = {};

    // 배경 (월드 경계)
    const render::WorldRect b = worldBounds(world);
    const render::Material ground = m_materials.find("terrain/" + world.desc().fillMaterial);
    out.background = {.position = (b.min + b.max) * 0.5f,
                      .size = b.max - b.min,
                      .sprite = ground.sprite,
                      .color = ground.color,
                      .layer = 0,
                      .depth = -1e30f};

    m_nextPositions.clear();
    m_nextPositions.reserve(m_lastPositions.size() + 64);
    for (auto [e, t, p] : world.registry().view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
        (void)e;
        const Resolved& r = resolve(world, p.saveId);
        Vec2 previous = t.position;
        if (const auto it = m_lastPositions.find(p.saveId); it != m_lastPositions.end()) {
            previous = it->second;
        }
        m_nextPositions.emplace(p.saveId, t.position);
        out.sprites.push_back({.previous = previous,
                               .current = t.position,
                               .size = r.size,
                               .rotation = t.rotation,
                               .sprite = r.material.sprite,
                               .color = r.material.color,
                               .layer = r.layer});
        ++out.stats.entities;
        out.stats.withSprite += r.fromContent ? 1 : 0;
    }
    m_lastPositions.swap(m_nextPositions);
    // 사라진 엔티티의 캐시를 가끔 정리 (saveId 는 재사용되지 않으므로 틀린 값을 쓸 일은 없다 — 메모리만)
    if (m_captures % 300 == 0 && m_cache.size() > static_cast<usize>(out.stats.entities) * 2) {
        std::erase_if(m_cache, [&](const auto& kv) { return !m_lastPositions.contains(kv.first); });
    }
    out.stats.cached = static_cast<u32>(m_cache.size());
}

void SpriteExtraction::emit(const WorldSnapshot& snapshot, f32 alpha, render::RenderWorld& out) {
    alpha = std::clamp(alpha, 0.f, 1.f);
    out.sprites.reserve(out.sprites.size() + snapshot.sprites.size() + 1);
    out.sprites.push_back(snapshot.background);
    for (const SnapshotSprite& s : snapshot.sprites) {
        const Vec2 pos = s.previous + (s.current - s.previous) * alpha;
        out.sprites.push_back({.position = pos,
                               .size = s.size,
                               .rotation = s.rotation,
                               .sprite = s.sprite,
                               .color = s.color,
                               .layer = s.layer,
                               .depth = -pos.y});
    }
}

} // namespace sbx::client
