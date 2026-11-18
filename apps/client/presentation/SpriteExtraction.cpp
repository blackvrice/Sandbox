#include "apps/client/presentation/SpriteExtraction.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
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

std::vector<std::string> SpriteExtraction::loadAnimations(const content::ContentDatabase& content) {
    m_animations.clear();
    m_anim.clear();
    m_cache.clear(); // 애니메이터 번호가 바뀐다
    std::vector<std::string> warnings;
    for (const content::PackInfo& p : content.packs()) {
        if (!p.animations.empty()) {
            const std::vector<std::string> w =
                m_animations.load(p.animations, std::format("{}/presentation/animations.json", p.id));
            warnings.insert(warnings.end(), w.begin(), w.end());
        }
    }
    const std::vector<std::string> left = m_animations.unresolved();
    warnings.insert(warnings.end(), left.begin(), left.end());
    return warnings;
}

bool SpriteExtraction::playAnimation(NetEntityId id, std::string_view state, f64 renderTick) {
    const auto cached = m_cache.find(id);
    if (cached == m_cache.end() || !cached->second.animator) {
        return false;
    }
    const Animator* an = m_animations.animator(*cached->second.animator);
    const std::optional<u32> s = an != nullptr ? an->stateOf(state) : std::nullopt;
    if (!s) {
        return false;
    }
    AnimationState& st =
        m_anim.try_emplace(id, AnimationState{*cached->second.animator, *s, renderTick, false}).first->second;
    st.state = *s;
    st.start = renderTick;
    return true;
}

void SpriteExtraction::reset() {
    m_cache.clear();
    m_anim.clear();
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
        // {"render.animator": {"version": 1, "value": {"animator": "hero"}}} — v0.12 RT-06-14
        if (comps.is_object() && comps.contains("render.animator") && comps["render.animator"].contains("value")) {
            const ecs::Json& v = comps["render.animator"]["value"];
            if (v.contains("animator") && v["animator"].is_string()) {
                r.animator = m_animations.findAnimator(v["animator"].get<std::string>());
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
        auto tiles = std::make_shared<std::vector<TileSprite>>();
        bool anyTile = false;
        for (const content::TerrainMaterial& m : world.content().terrainMaterials()) {
            const render::Material mat = m_materials.find("terrain/" + m.id);
            palette->push_back(mat.color);
            tiles->push_back({mat.textured, mat.sprite, mat.textured ? 0xFFFF'FFFFu : mat.color, mat.sub});
            anyTile = anyTile || mat.textured;
        }
        m_palette = std::move(palette);
        if (anyTile) { // 타일 그림 (v0.12 NG-15) — sprite 를 적은 지형 머티리얼이 하나라도 있을 때
            m_tiles = std::move(tiles);
        }
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
    out.tiles = m_tiles;
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
        render::Material material = r.material;
        u32 flags = 0;
        if (r.animator) { // 애니메이션 (v0.12 RT-06-14) — 지금 프레임의 머티리얼 (시트) · 칸 · 뒤집기
            auto [it, fresh] = m_anim.try_emplace(ni->netId, AnimationState{*r.animator, ~0u, renderTick, false});
            const auto* vel = reg.tryRead<comp::Velocity>(e);
            const Vec2 v = vel != nullptr ? vel->value : Vec2{};
            const AnimationFrame frame =
                stepAnimation(m_animations, it->second, renderTick, std::sqrt(v.x * v.x + v.y * v.y), v.x);
            if (frame.clip != nullptr) {
                const render::Material sheet = m_materials.find(frame.clip->material);
                material.sprite = sheet.sprite;
                material.color = sheet.color;
                material.sub = frame.sub;
                flags = frame.flipX ? render::kSpriteFlipX : 0u;
            }
        }
        out.sprites.push_back({.id = ni->netId,
                               .position = position,
                               .size = r.size,
                               .rotation = rotation,
                               .sprite = material.sprite,
                               .color = material.color,
                               .layer = r.layer,
                               .sub = material.sub,
                               .flags = flags});
        ++out.stats.entities;
        out.stats.withSprite += r.fromContent ? 1 : 0;
    });
    // 사라진 엔티티의 캐시를 가끔 정리 (netId 는 재사용되지 않으므로 틀린 값을 쓸 일은 없다 — 메모리만)
    if (m_captures % 600 == 0 && m_cache.size() > static_cast<usize>(out.stats.entities) * 2) {
        std::erase_if(m_cache, [&](const auto& kv) { return world.find(kv.first) == ecs::kNullEntity; });
    }
    if (m_captures % 600 == 0 && m_anim.size() > static_cast<usize>(out.stats.entities) * 2) {
        std::erase_if(m_anim, [&](const auto& kv) { return world.find(kv.first) == ecs::kNullEntity; });
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
                               .flags = s.flags,
                               .layer = s.layer,
                               .depth = -s.position.y,
                               .sub = s.sub});
    }
    emitTiles(snapshot, out);
}

// 지형 타일 그림 (v0.12 NG-15): 보이는 칸 (카메라 사각형 ∩ 월드) 중 그림 있는 머티리얼의 칸마다 스프라이트 하나 —
// 레이어 0 · 깊이 가장 아래 (개체 밑). 지형 색 (TerrainPass) 위에 덮는다. 보이는 칸이 kMaxTileSprites 를 넘으면 그리지
// 않는다
void SpriteExtraction::emitTiles(const WorldSnapshot& snapshot, render::RenderWorld& out) {
    const render::TerrainView& v = snapshot.terrain;
    if (!snapshot.tiles || v.empty()) {
        return;
    }
    const render::WorldRect view = out.camera.visibleRect();
    const Vec2 wmin = v.worldMin();
    const Vec2 wmax = wmin + v.worldSize();
    const i32 x0 = static_cast<i32>(std::floor(std::max(view.min.x, wmin.x)));
    const i32 y0 = static_cast<i32>(std::floor(std::max(view.min.y, wmin.y)));
    const i32 x1 = static_cast<i32>(std::ceil(std::min(view.max.x, wmax.x)));
    const i32 y1 = static_cast<i32>(std::ceil(std::min(view.max.y, wmax.y)));
    if (x1 <= x0 || y1 <= y0 ||
        static_cast<u64>(x1 - x0) * static_cast<u64>(y1 - y0) > static_cast<u64>(kMaxTileSprites)) {
        return;
    }
    const std::vector<TileSprite>& tiles = *snapshot.tiles;
    for (const render::TerrainChunk& c : v.chunks) {
        if (!c.tiles) {
            continue;
        }
        const i32 ox = c.x * v.chunkSize;
        const i32 oy = c.y * v.chunkSize;
        if (ox + v.chunkSize <= x0 || ox >= x1 || oy + v.chunkSize <= y0 || oy >= y1) {
            continue;
        }
        for (i32 ly = std::max(0, y0 - oy); ly < std::min(v.chunkSize, y1 - oy); ++ly) {
            for (i32 lx = std::max(0, x0 - ox); lx < std::min(v.chunkSize, x1 - ox); ++lx) {
                const u16 m = (*c.tiles)[static_cast<usize>(ly * v.chunkSize + lx)];
                if (m >= tiles.size() || !tiles[m].textured) {
                    continue;
                }
                const TileSprite& t = tiles[m];
                out.sprites.push_back({.position = {static_cast<f32>(ox + lx) + 0.5f, static_cast<f32>(oy + ly) + 0.5f},
                                       .size = {1.f, 1.f},
                                       .sprite = t.sprite,
                                       .color = t.color,
                                       .layer = 0,
                                       .depth = -1.0e9f, // 개체 밑
                                       .sub = t.sub});
            }
        }
    }
}

} // namespace sbx::client
