#include "apps/client/presentation/SpriteExtraction.hpp"

#include <algorithm>
#include <atomic>
#include <string>

#include "core/components/core/Identity.hpp"
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

render::WorldRect SpriteExtraction::worldBounds(const net::ClientWorld& world) {
    const world::GridBounds b = world.grid().bounds();
    return {{static_cast<f32>(b.minChunk.x * world::kChunkSize), static_cast<f32>(b.minChunk.y * world::kChunkSize)},
            {static_cast<f32>((b.maxChunk.x + 1) * world::kChunkSize),
             static_cast<f32>((b.maxChunk.y + 1) * world::kChunkSize)}};
}

void SpriteExtraction::reset() {
    m_cache.clear();
    m_terrain.clear();
    m_palette.reset();
    m_worldId = nextWorldId();
}

const SpriteExtraction::Resolved& SpriteExtraction::resolve(const net::ClientWorld& world, NetEntityId id) {
    if (const auto it = m_cache.find(id); it != m_cache.end()) {
        return it->second;
    }
    Resolved r;
    r.material = m_materials.find("debug/entity");
    r.size = {0.5f, 0.5f};
    r.layer = 5;
    if (const ecs::Json* opaque = world.opaque(id)) {
        const ecs::Json& comps = *opaque;
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
    return m_cache.emplace(id, r).first->second;
}

void SpriteExtraction::captureTerrain(const net::ClientWorld& world, WorldSnapshot& out) {
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

void SpriteExtraction::captureSelected(const net::ClientWorld& world, f64 renderTick,
                                       std::span<const NetEntityId> selection, const net::InspectResult* inspect,
                                       WorldSnapshot& out) {
    const ecs::Registry& reg = world.registry();
    auto positionOf = [&](NetEntityId id) -> std::optional<Vec2> {
        if (const net::TransformTrack* t = world.transformTrack(id)) {
            return net::sampleTransform(*t, renderTick).position;
        }
        return std::nullopt;
    };
    for (const NetEntityId id : selection) {
        if (out.selected.size() >= kMaxSelectedDetails) {
            break;
        }
        const ecs::EntityId e = world.find(id);
        if (e == ecs::kNullEntity) {
            continue; // 사라졌다 — 선택은 세션이 정리한다
        }
        SelectedDetail d;
        d.id = id;
        if (const auto p = positionOf(id)) {
            d.position = *p;
        }
        if (const auto* v = reg.tryRead<comp::Velocity>(e)) {
            d.velocity = v->value;
        }
        if (const auto* p = reg.tryRead<comp::PrefabSource>(e)) {
            d.prefab = std::string(p->prefab.view());
        }
        if (const auto* en = reg.tryRead<comp::Energy>(e)) {
            d.energy = en->value;
            d.energyMax = en->max;
        }
        if (const auto* h = reg.tryRead<comp::Health>(e)) {
            d.health = h->value;
            d.healthMax = h->max;
        }
        if (inspect != nullptr) {
            if (const auto it = std::ranges::find(inspect->entries, id, &net::InspectEntry::netId);
                it != inspect->entries.end()) {
                d.state = it->state;
                d.sensorRadius = it->sensorRadius;
                d.path = it->path;
                d.goal = it->goal;
                if (it->target != kInvalidNetEntityId) {
                    d.target = positionOf(it->target);
                }
            }
        }
        out.selected.push_back(std::move(d));
    }
}

void SpriteExtraction::capture(const net::ClientWorld& world, f64 renderTick, std::span<const NetEntityId> selection,
                               const net::InspectResult* inspect, WorldSnapshot& out) {
    ++m_captures;
    out.serverTick = world.serverTick();
    out.renderTick = renderTick;
    out.sprites.clear();
    out.selected.clear();
    out.stats = {};

    captureTerrain(world, out);

    out.sprites.reserve(world.entityCount());
    // 엔티티 index 순서 (프레임마다 같은 순서 — 같은 깊이끼리의 그리기 순서가 흔들리지 않게)
    const ecs::Registry& reg = world.registry();
    reg.forEachEntityByIndex([&](ecs::EntityId e) {
        const auto* t = reg.tryRead<comp::Transform>(e);
        const auto* ni = reg.tryRead<comp::NetIdentity>(e);
        if (t == nullptr || ni == nullptr) {
            return;
        }
        const Resolved& r = resolve(world, ni->netId);
        Vec2 position = t->position;
        f32 rotation = t->rotation;
        if (const net::TransformTrack* track = world.transformTrack(ni->netId)) {
            const net::TransformSample s = net::sampleTransform(*track, renderTick);
            position = s.position;
            rotation = s.rotation;
        }
        out.sprites.push_back({.id = ni->netId,
                               .position = position,
                               .size = r.size,
                               .rotation = rotation,
                               .sprite = r.material.sprite,
                               .color = r.material.color,
                               .layer = r.layer});
        ++out.stats.entities;
        out.stats.withSprite += r.fromContent ? 1 : 0;
    });
    // 사라진 엔티티의 캐시를 가끔 정리 (netId 는 재사용되지 않으므로 틀린 값을 쓸 일은 없다 — 메모리만)
    if (m_captures % 600 == 0 && m_cache.size() > static_cast<usize>(out.stats.entities) * 2) {
        std::erase_if(m_cache, [&](const auto& kv) { return world.find(kv.first) == ecs::kNullEntity; });
    }
    out.stats.cached = static_cast<u32>(m_cache.size());

    captureSelected(world, renderTick, selection, inspect, out);
}

void SpriteExtraction::emit(const WorldSnapshot& snapshot, render::RenderWorld& out) {
    out.terrain = snapshot.terrain;
    out.sprites.reserve(out.sprites.size() + snapshot.sprites.size());
    for (const SnapshotSprite& s : snapshot.sprites) {
        out.sprites.push_back({.position = s.position,
                               .size = s.size,
                               .rotation = s.rotation,
                               .sprite = s.sprite,
                               .color = s.color,
                               .layer = s.layer,
                               .depth = -s.position.y});
    }
}

} // namespace sbx::client
