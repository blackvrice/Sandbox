#include "apps/client/presentation/SpriteExtraction.hpp"

#include <algorithm>
#include <atomic>
#include <string>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/world/ChunkCoord.hpp"

namespace sbx::client {
namespace {

u64 nextWorldId() {
    static std::atomic<u64> counter{0};
    return ++counter;
}

} // namespace

SpriteExtraction::SpriteExtraction(render::MaterialLibrary& materials)
    : m_materials(materials), m_worldId(nextWorldId()) {}

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

void SpriteExtraction::captureTerrain(const sim::SimulationWorld& world, WorldSnapshot& out) {
    const world::WorldGrid& grid = world.grid();
    if (!m_palette) {
        // 콘텐츠는 월드가 사는 동안 그대로 — 처음 한 번 (머티리얼 번호 = 콘텐츠 순서)
        auto palette = std::make_shared<std::vector<u32>>();
        for (const content::TerrainMaterial& m : world.content().terrainMaterials()) {
            palette->push_back(m_materials.find("terrain/" + m.id).color);
        }
        m_palette = std::move(palette);
    }
    const std::span<const world::Chunk> chunks = grid.chunks();
    if (m_terrain.size() != chunks.size()) {
        m_terrain.assign(chunks.size(), {});
    }
    for (usize i = 0; i < chunks.size(); ++i) {
        const world::Chunk& c = chunks[i];
        render::TerrainChunk& t = m_terrain[i];
        if (t.tiles && t.revision == c.terrainRevision() && t.x == c.coord().x && t.y == c.coord().y) {
            continue; // 그대로 — 이전 스냅숏과 같은 버퍼를 공유한다
        }
        const auto& m = c.layers().material; // 행 우선, 행 = 청크 안 y (render::TerrainChunk 와 같은 배치)
        t.x = c.coord().x;
        t.y = c.coord().y;
        t.revision = c.terrainRevision();
        t.tiles = std::make_shared<const std::vector<u16>>(m.begin(), m.end());
        ++out.stats.terrainCopied;
    }
    render::TerrainView& v = out.terrain;
    v.worldId = m_worldId;
    v.chunkSize = world::kChunkSize;
    v.minChunkX = grid.bounds().minChunk.x;
    v.minChunkY = grid.bounds().minChunk.y;
    v.chunksX = grid.chunksX();
    v.chunksY = grid.chunksY();
    v.chunks = m_terrain;
    v.palette = m_palette;
    v.paletteVersion = 1;
}

void SpriteExtraction::captureSelected(sim::SimulationWorld& world, std::span<const SaveId> selection,
                                       WorldSnapshot& out) {
    const ecs::Registry& reg = world.registry();
    const content::ContentDatabase& content = world.content();
    for (const SaveId id : selection) {
        if (out.selected.size() >= kMaxSelectedDetails) {
            break;
        }
        const ecs::EntityId e = world.resolveSave(id);
        if (!reg.alive(e)) {
            continue; // 죽었다 — 선택은 다음 프레임에 Main 쪽이 정리한다
        }
        SelectedDetail d;
        d.id = id;
        if (const auto* t = reg.tryRead<comp::Transform>(e)) {
            d.position = t->position;
        }
        if (const auto* v = reg.tryRead<comp::Velocity>(e)) {
            d.velocity = v->value;
        }
        if (const auto* p = reg.tryRead<comp::PrefabSource>(e)) {
            d.prefab = std::string(p->prefab.view());
        }
        if (const auto* b = reg.tryRead<comp::Behavior>(e)) {
            if (const content::BehaviorGraph* g = content.findBehavior(b->graph.view());
                g != nullptr && b->state < g->states.size()) {
                d.state = g->states[b->state].id;
            }
            if (b->target != kInvalidSaveId) {
                if (const ecs::EntityId te = world.resolveSave(b->target); reg.alive(te)) {
                    if (const auto* tt = reg.tryRead<comp::Transform>(te)) {
                        d.target = tt->position;
                    }
                }
            }
        }
        if (const auto* en = reg.tryRead<comp::Energy>(e)) {
            d.energy = en->value;
            d.energyMax = en->max;
        }
        if (const auto* h = reg.tryRead<comp::Health>(e)) {
            d.health = h->value;
            d.healthMax = h->max;
        }
        if (const auto* s = reg.tryRead<comp::Sensor>(e)) {
            d.sensorRadius = s->radius;
        }
        if (const auto* p = reg.tryRead<comp::Path>(e)) {
            if (p->state == comp::PathState::Following) {
                for (usize i = p->cursor; i < p->waypoints.size(); ++i) {
                    d.path.push_back(p->waypoints[i]);
                }
            }
            if (p->state == comp::PathState::Following || p->state == comp::PathState::Pending ||
                p->state == comp::PathState::Submitted) {
                d.goal = p->goal;
            }
        }
        out.selected.push_back(std::move(d));
    }
}

void SpriteExtraction::capture(sim::SimulationWorld& world, std::span<const SaveId> selection, WorldSnapshot& out) {
    ++m_captures;
    out.tick = world.currentTick();
    out.sprites.clear();
    out.selected.clear();
    out.stats = {};

    captureTerrain(world, out);

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
        out.sprites.push_back({.id = p.saveId,
                               .previous = previous,
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

    captureSelected(world, selection, out);
}

void SpriteExtraction::emit(const WorldSnapshot& snapshot, f32 alpha, render::RenderWorld& out) {
    alpha = std::clamp(alpha, 0.f, 1.f);
    out.terrain = snapshot.terrain;
    out.sprites.reserve(out.sprites.size() + snapshot.sprites.size());
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
