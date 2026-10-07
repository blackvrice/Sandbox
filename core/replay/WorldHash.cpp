#include "core/replay/WorldHash.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include "core/content/ContentDatabase.hpp"
#include "core/simulation/SimVersion.hpp"

namespace sbx::replay {
namespace {

constexpr u64 kEntityEndMarker = 0xE0E0'E0E0'E0E0'E0E0ull;

struct HashedPool {
    const ecs::ComponentPoolBase* pool;
    const ecs::ComponentInfo* info;
};

struct Keyed {
    SaveId saveId;
    ecs::EntityId entity;
};

// Hashed 풀을 stableId 오름차순으로. 카탈로그에 없으면 오류.
Expected<std::vector<HashedPool>> hashedPools(const ecs::Registry& registry, const ecs::ComponentCatalog& catalog) {
    std::vector<HashedPool> out;
    for (const ecs::ComponentPoolBase* pool : registry.poolsByStableId()) {
        if (!ecs::hasFlag(pool->flags(), ecs::ComponentFlags::Hashed)) {
            continue;
        }
        const ecs::ComponentInfo* info = catalog.find(pool->stableId());
        if (info == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("Hashed 컴포넌트 '{}' 가 카탈로그에 등록되지 않았다", pool->name()),
                             "WorldHash");
        }
        out.push_back(HashedPool{pool, info});
    }
    return out;
}

// persist.persistence 를 가진 엔티티를 saveId 오름차순으로. 정체성 없는 엔티티가 있으면 오류.
Expected<std::vector<Keyed>> entitiesBySaveId(const ecs::Registry& registry) {
    std::vector<Keyed> keyed;
    const auto* ids = registry.findPool<comp::Persistence>();
    if (ids != nullptr) {
        keyed.reserve(ids->size());
        for (usize i = 0; i < ids->size(); ++i) {
            keyed.push_back(Keyed{ids->dataAt(i).saveId, ids->entityAt(i)});
        }
    }
    if (keyed.size() != registry.aliveCount()) {
        return makeError(ErrorCode::ValidationFailed,
                         std::format("정체성(saveId) 없는 엔티티 {}개 — 동기화 지점 밖에서 만든 엔티티는 "
                                     "SimulationWorld::assignPendingIdentities() 를 거쳐야 한다",
                                     registry.aliveCount() - keyed.size()),
                         "WorldHash");
    }
    std::sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b) { return a.saveId < b.saveId; });
    for (usize i = 1; i < keyed.size(); ++i) {
        if (keyed[i].saveId == keyed[i - 1].saveId) {
            return makeError(ErrorCode::ValidationFailed, std::format("saveId {} 중복", keyed[i].saveId), "WorldHash");
        }
    }
    return keyed;
}

void hashEntity(Fnv1a64& h, const Keyed& k, const std::vector<HashedPool>& pools) {
    h.u64le(k.saveId);
    for (const HashedPool& hp : pools) {
        const void* raw = hp.pool->getRaw(k.entity);
        if (raw == nullptr) {
            continue;
        }
        h.u64le(hp.info->stableId);
        h.u64le(hp.info->version);
        hp.info->hash(raw, h);
    }
    h.u64le(kEntityEndMarker);
}

} // namespace

Expected<u64> computeWorldHash(const ecs::Registry& registry, const ecs::ComponentCatalog& catalog,
                               const WorldHashHeader& header, const world::WorldGrid& grid,
                               const content::ContentDatabase& content) {
    auto pools = hashedPools(registry, catalog);
    if (!pools) {
        return std::unexpected(pools.error());
    }
    auto entities = entitiesBySaveId(registry);
    if (!entities) {
        return std::unexpected(entities.error());
    }
    Fnv1a64 h;
    h.string("SBXWH");
    h.u64le(sim::kSimVersion);
    h.u64le(header.tick);
    h.u64le(header.worldSeed);
    h.byte(header.paused ? 1 : 0);
    h.u64le(entities->size());
    for (const Keyed& k : *entities) {
        hashEntity(h, k, *pools);
    }
    h.string("TERRAIN");
    grid.hashInto(h, world::materialStableIds(content));
    return h.value();
}

Expected<std::vector<EntityHash>> computeEntityHashes(const ecs::Registry& registry,
                                                      const ecs::ComponentCatalog& catalog) {
    auto pools = hashedPools(registry, catalog);
    if (!pools) {
        return std::unexpected(pools.error());
    }
    auto entities = entitiesBySaveId(registry);
    if (!entities) {
        return std::unexpected(entities.error());
    }
    std::vector<EntityHash> out;
    out.reserve(entities->size());
    for (const Keyed& k : *entities) {
        Fnv1a64 h;
        hashEntity(h, k, *pools);
        out.push_back(EntityHash{k.saveId, h.value()});
    }
    return out;
}

ecs::Json describeEntity(const ecs::Registry& registry, const ecs::ComponentCatalog& catalog, SaveId saveId) {
    ecs::Json out = ecs::Json::object();
    const auto* ids = registry.findPool<comp::Persistence>();
    if (ids == nullptr) {
        return out;
    }
    ecs::EntityId entity = ecs::kNullEntity;
    for (usize i = 0; i < ids->size(); ++i) {
        if (ids->dataAt(i).saveId == saveId) {
            entity = ids->entityAt(i);
            break;
        }
    }
    if (!entity.valid()) {
        return out;
    }
    for (const ecs::ComponentPoolBase* pool : registry.poolsByStableId()) {
        const ecs::ComponentInfo* info = catalog.find(pool->stableId());
        const void* raw = pool->getRaw(entity);
        if (info == nullptr || raw == nullptr || !ecs::hasFlag(pool->flags(), ecs::ComponentFlags::Hashed)) {
            continue;
        }
        ecs::Json value;
        info->writeJson(raw, value);
        out[std::string(info->name)] = std::move(value);
    }
    return out;
}

std::string formatHash(u64 hash) {
    return std::format("0x{:016x}", hash);
}

Expected<u64> parseHash(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
    }
    u64 value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (ec != std::errc{} || ptr != text.data() + text.size() || text.empty()) {
        return makeError(ErrorCode::ParseError, std::format("16진수 해시가 아니다: '{}'", text));
    }
    return value;
}

} // namespace sbx::replay
