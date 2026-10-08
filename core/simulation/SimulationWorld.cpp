#include "core/simulation/SimulationWorld.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

#include "core/command/CommandJson.hpp"
#include "core/components/ai/Ai.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/replay/WorldHash.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::sim {
namespace {

using cmd::CommandPayload;
using ecs::Json;

bool isFinite(Vec2 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

// 정체성 컴포넌트는 SimulationWorld 만 붙이고 뗀다
bool isProtected(ecs::StableId id) noexcept {
    return id == ecs::stableIdOf<comp::Persistence> || id == ecs::stableIdOf<comp::NetIdentity>;
}

std::string hexId(ecs::StableId id) {
    return std::format("0x{:016x}", id);
}

world::WorldGrid makeGrid(const WorldDesc& desc, const content::ContentDatabase& content) {
    const auto fill = content.findMaterial(desc.fillMaterial);
    SBX_VERIFY(fill.has_value(), "WorldDesc.fillMaterial 이 콘텐츠에 없다 (validateWorldDesc 를 먼저 부르십시오)");
    auto grid = world::WorldGrid::create(desc.bounds, content, *fill);
    SBX_VERIFY(grid.has_value(), "WorldDesc.bounds 가 잘못되었다 (validateWorldDesc 를 먼저 부르십시오)");
    return std::move(*grid);
}

// 브러시 → 타일 목록 (행 우선, 경계로 자름)
std::vector<Vec2i> brushTiles(const cmd::PaintTerrain& p, const world::WorldGrid& grid) {
    std::vector<Vec2i> out;
    const i64 r = p.radius;
    for (i64 dy = -r; dy <= r; ++dy) {
        for (i64 dx = -r; dx <= r; ++dx) {
            if (p.shape == cmd::BrushShape::Circle && dx * dx + dy * dy > r * r) {
                continue;
            }
            const i64 x = static_cast<i64>(p.center.x) + dx;
            const i64 y = static_cast<i64>(p.center.y) + dy;
            if (x < std::numeric_limits<i32>::min() || x > std::numeric_limits<i32>::max() ||
                y < std::numeric_limits<i32>::min() || y > std::numeric_limits<i32>::max()) {
                continue;
            }
            const Vec2i t{static_cast<i32>(x), static_cast<i32>(y)};
            if (grid.containsTile(t)) {
                out.push_back(t);
            }
        }
    }
    return out;
}

} // namespace

Expected<void> validateWorldDesc(const WorldDesc& desc, const content::ContentDatabase& content) {
    const auto fill = content.findMaterial(desc.fillMaterial);
    if (!fill) {
        return makeError(ErrorCode::NotFound, std::format("머티리얼 '{}' 가 콘텐츠에 없다", desc.fillMaterial));
    }
    if (auto g = world::WorldGrid::create(desc.bounds, content, *fill); !g) {
        return std::unexpected(g.error());
    }
    return {};
}

SimulationWorld::SimulationWorld(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content,
                                 const WorldDesc& desc)
    : m_catalog(catalog), m_content(content), m_desc(desc), m_grid(makeGrid(desc, content)), m_spatial(desc.bounds),
      m_random(desc.seed) {
    m_clock.reset(0, desc.startPaused);
    if (desc.registerDefaultSystems) {
        registerDefaultSystems(m_scheduler);
    }
}

SimulationWorld::~SimulationWorld() = default;

void SimulationWorld::enqueue(cmd::SimCommand command) {
    m_commands.push(std::move(command));
}

void SimulationWorld::tick(ISystemProfiler* profiler) {
    // 이번 호출의 모드는 시작할 때 정한다 — 이번 틱에 적용되는 Pause/Step 은 다음 호출부터 효과가 있다.
    const bool run = !m_clock.paused() || m_clock.consumeStep();
    m_editStep = !run;

    // --- 0 BeginTick ---------------------------------------------------------
    assignPendingIdentities(); // 틱 밖에서 직접 만든 엔티티
    m_registry.clearTickLogs();
    m_createdCursor = 0;
    m_destroyedCursor = 0;
    m_events.clear();
    m_results.clear();
    m_intents.clear();
    if (run) {
        m_clock.advance();
    }
    const Tick now = m_clock.tick();
    m_changeStamp = std::max(m_changeStamp + 1, now);
    m_registry.setCurrentTick(m_changeStamp);
    m_random.setTick(now);

    // --- 2·3 ApplyCommands + Structural① --------------------------------------
    // 일시정지 편집 단계는 "다음 틱" 몫의 명령을 지금 적용한다 (서버는 항상 현재 틱 + 1 로 스탬프한다).
    const Tick target = run ? now : now + 1;
    for (const cmd::SimCommand& c : m_commands.takeUpTo(target)) {
        applyCommand(c);
    }

    // --- 4 UpdateSpatialIndex ------------------------------------------------
    m_spatial.rebuild(m_registry);

    m_lastRanSystems = run;
    if (!run) {
        return;
    }

    // --- 5~16 Systems, 17 Structural② ----------------------------------------
    const SystemContext base{m_registry, m_baseEcb, m_spatial,           m_grid,    m_random,
                             m_events,   m_spawns,  m_content,           m_catalog, m_saves,
                             m_intents,  m_paths,   m_paths.jobSystem(), now,       kFixedDt};
    m_scheduler.run(base, profiler);
    m_scheduler.applyBuffers(m_registry, [this] { syncIdentityLogs(); });
    applySpawns(); // Prefab 생성 — ECB 다음, (parent saveId, 순서) 정렬
}

Expected<u64> SimulationWorld::worldHash() const {
    return replay::computeWorldHash(m_registry, m_catalog,
                                    replay::WorldHashHeader{m_clock.tick(), m_random.worldSeed(), m_clock.paused()},
                                    m_grid, m_content);
}

ecs::EntityId SimulationWorld::resolveSave(SaveId id) const noexcept {
    const ecs::EntityId e = m_saves.find(id);
    return m_registry.alive(e) ? e : ecs::kNullEntity;
}

ecs::EntityId SimulationWorld::resolve(NetEntityId id) const noexcept {
    const auto it = m_netToEntity.find(id);
    if (it == m_netToEntity.end() || !m_registry.alive(it->second)) {
        return ecs::kNullEntity;
    }
    return it->second;
}

void SimulationWorld::assignPendingIdentities() {
    syncIdentityLogs();
}

void SimulationWorld::syncIdentityLogs() {
    // 파괴 먼저: 같은 구간에서 파괴된 슬롯을 새 엔티티가 재사용했을 수 있다 (LIFO 자유 목록).
    const auto destroyed = m_registry.destroyedThisTick();
    for (; m_destroyedCursor < destroyed.size(); ++m_destroyedCursor) {
        const ecs::EntityId e = destroyed[m_destroyedCursor];
        const u32 idx = e.index();
        if (idx >= m_slots.size() || m_slots[idx].entity != e) {
            continue; // 정체성을 받기 전에 사라진 엔티티
        }
        m_events.push(SimEvent{EventKind::EntityDestroyed, e, m_slots[idx].saveId, 0, 0});
        m_netToEntity.erase(m_slots[idx].netId);
        m_saves.erase(m_slots[idx].saveId);
        m_opaque.erase(m_slots[idx].saveId);
        m_slots[idx] = SlotIdentity{};
    }

    const auto created = m_registry.createdThisTick();
    for (; m_createdCursor < created.size(); ++m_createdCursor) {
        const ecs::EntityId e = created[m_createdCursor];
        if (!m_registry.alive(e) || m_registry.has<comp::Persistence>(e)) {
            continue;
        }
        const SaveId saveId = m_nextSaveId++;
        const NetEntityId netId = m_nextNetId++;
        SBX_VERIFY(netId != kInvalidNetEntityId, "NetEntityId 고갈 (2^32)");
        m_registry.emplace<comp::Persistence>(e, comp::Persistence{saveId});
        m_registry.emplace<comp::NetIdentity>(e, comp::NetIdentity{netId});
        const u32 idx = e.index();
        if (idx >= m_slots.size()) {
            m_slots.resize(static_cast<usize>(idx) + 1);
        }
        m_slots[idx] = SlotIdentity{e, saveId, netId};
        m_netToEntity[netId] = e;
        m_saves.set(saveId, e);
        m_events.push(SimEvent{EventKind::EntitySpawned, e, saveId, 0, 0});
    }
}

void SimulationWorld::beginRestore(Tick tick) {
    SBX_VERIFY(m_registry.aliveCount() == 0 && m_nextSaveId == 1, "restore 는 빈 월드에서만");
    m_changeStamp = tick;
    m_registry.setCurrentTick(tick);
}

ecs::EntityId SimulationWorld::restoreEntity(SaveId saveId) {
    SBX_VERIFY(saveId != kInvalidSaveId, "restoreEntity: saveId 0");
    const ecs::EntityId e = m_registry.create();
    const NetEntityId netId = m_nextNetId++;
    m_registry.emplace<comp::Persistence>(e, comp::Persistence{saveId});
    m_registry.emplace<comp::NetIdentity>(e, comp::NetIdentity{netId});
    const u32 idx = e.index();
    if (idx >= m_slots.size()) {
        m_slots.resize(static_cast<usize>(idx) + 1);
    }
    m_slots[idx] = SlotIdentity{e, saveId, netId};
    m_netToEntity[netId] = e;
    m_saves.set(saveId, e);
    return e;
}

void SimulationWorld::restoreOpaque(SaveId saveId, ecs::Json components) {
    m_opaque[saveId] = std::move(components);
}

void SimulationWorld::finishRestore(const RestoreState& state) {
    m_clock.restore(state.tick, state.paused, state.speed, state.pendingSteps, state.editSequence);
    m_nextSaveId = state.nextSaveId;
    m_changeStamp = std::max(m_changeStamp, state.tick);
    m_registry.setCurrentTick(m_changeStamp);
    m_random.setTick(state.tick);
    m_registry.clearTickLogs();
    m_createdCursor = 0;
    m_destroyedCursor = 0;
    m_events.clear();
    m_results.clear();
    m_intents.clear();
    m_spatial.rebuild(m_registry);

    // 저장 시점에 진행 중이던 경로 Job (03 7.1): 같은 입력(ai.path 의 start·goal)과 같은 지형(다음 틱 명령 전)으로
    // saveId 순서로 다시 제출한다 → 다음 틱 Stage 5 가 저장하지 않은 실행과 같은 결과를 받는다.
    m_paths.discard();
    std::vector<std::pair<SaveId, ecs::EntityId>> submitted;
    for (auto [e, p, id] : m_registry.view<ecs::Read<comp::Path>, ecs::Read<comp::Persistence>>()) {
        if (p.state == comp::PathState::Submitted) {
            submitted.emplace_back(id.saveId, e);
        }
    }
    std::sort(submitted.begin(), submitted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (!submitted.empty()) {
        m_paths.refreshSnapshot(m_grid);
        for (const auto& [saveId, e] : submitted) {
            const comp::Path& p = m_registry.read<comp::Path>(e);
            m_paths.submit(saveId, e, p.submittedTick, p.start, p.goal);
        }
    }
}

ecs::ComponentPoolBase& SimulationWorld::poolFor(const ecs::ComponentInfo& info) {
    if (ecs::ComponentPoolBase* p = m_registry.poolByStableId(info.stableId)) {
        return *p;
    }
    return m_registry.adoptPool(info.makePool());
}

Expected<ecs::EntityId> SimulationWorld::instantiate(const content::Prefab* prefab, Vec2 position,
                                                     std::span<const cmd::ComponentValue> overrides) {
    if (!isFinite(position)) {
        return makeError(ErrorCode::InvalidArgument, "position 이 유한하지 않다");
    }
    if (overrides.size() > kMaxCommandComponents) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("컴포넌트 {}개 (최대 {})", overrides.size(), kMaxCommandComponents));
    }
    struct Pending {
        const ecs::ComponentInfo* info;
        Json value;
    };
    std::vector<Pending> pending;
    const auto findPending = [&](const ecs::ComponentInfo* info) {
        return std::find_if(pending.begin(), pending.end(), [&](const Pending& p) { return p.info == info; });
    };
    // 1) Prefab 의 컴포넌트 (P2: 생략한 필드는 기본값)
    if (prefab != nullptr) {
        for (const content::PrefabComponent& pc : prefab->components) {
            if (pc.opaque) {
                continue;
            }
            const ecs::ComponentInfo* info = m_catalog.find(pc.stableId);
            if (info == nullptr) {
                return makeError(ErrorCode::NotFound,
                                 std::format("Prefab '{}' 의 컴포넌트 '{}' 가 카탈로그에 없다", prefab->id, pc.name));
            }
            pending.push_back(Pending{info, pc.value});
        }
    }
    // 2) 명령의 컴포넌트 — 같은 컴포넌트면 키 단위로 덮어쓴다
    std::vector<const ecs::ComponentInfo*> seen;
    bool overridePosition = false;
    for (const cmd::ComponentValue& cv : overrides) {
        const ecs::ComponentInfo* info = m_catalog.find(cv.stableId);
        if (info == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("모르는 컴포넌트 stableId {}", hexId(cv.stableId)));
        }
        if (isProtected(cv.stableId)) {
            return makeError(ErrorCode::PermissionDenied,
                             std::format("'{}' 는 엔진이 관리한다 — 명령으로 바꿀 수 없다", info->name));
        }
        if (std::find(seen.begin(), seen.end(), info) != seen.end()) {
            return makeError(ErrorCode::InvalidArgument, std::format("'{}' 가 두 번 나온다", info->name));
        }
        seen.push_back(info);
        if (!cv.value.is_object()) {
            return makeError(ErrorCode::InvalidArgument, std::format("'{}' 값은 JSON 객체여야 한다", info->name));
        }
        if (info->stableId == ecs::stableIdOf<comp::Transform> && cv.value.contains("position")) {
            overridePosition = true;
        }
        if (const auto it = findPending(info); it != pending.end()) {
            for (const auto& [k, v] : cv.value.items()) {
                it->value[k] = v;
            }
        } else {
            pending.push_back(Pending{info, cv.value});
        }
    }
    // 3) 위치 (P4): 명령의 core.transform.position > position 인자 > Prefab 의 값
    const ecs::ComponentInfo* trInfo = m_catalog.find(ecs::stableIdOf<comp::Transform>);
    SBX_VERIFY(trInfo != nullptr, "core.transform 이 카탈로그에 없다");
    auto tr = findPending(trInfo);
    if (tr == pending.end()) {
        pending.push_back(Pending{trInfo, Json::object()});
        tr = pending.end() - 1;
    }
    if (!overridePosition) {
        tr->value["position"] = Json::array({static_cast<f64>(position.x), static_cast<f64>(position.y)});
    }
    // 4) 전부 검증 (원자성: 하나라도 틀리면 아무것도 만들지 않는다)
    for (const Pending& p : pending) {
        if (auto r = p.info->validateJson(p.value, p.info->name); !r) {
            return std::unexpected(r.error());
        }
    }
    comp::Transform probe{};
    (void)ecs::componentFromJson(probe, tr->value, "core.transform");
    if (!m_grid.containsPoint(probe.position)) {
        return makeError(ErrorCode::OutOfRange,
                         std::format("위치 ({}, {}) 가 월드 경계 밖이다", probe.position.x, probe.position.y));
    }
    // 5) 적용 — 검증을 통과했으므로 실패하지 않는다
    const Tick tick = m_clock.tick();
    const ecs::EntityId e = m_registry.create();
    for (const Pending& p : pending) {
        void* raw = poolFor(*p.info).emplaceDefaultRaw(e, tick);
        const auto r = p.info->readJson(raw, p.value, p.info->name);
        SBX_VERIFY(r.has_value(), "검증을 통과한 컴포넌트 값의 적용이 실패했다");
    }
    if (prefab != nullptr) {
        if (!m_registry.has<comp::Tags>(e)) {
            m_registry.emplace<comp::Tags>(e, comp::Tags{prefab->tags});
        }
        if (!m_registry.has<comp::PrefabSource>(e)) {
            comp::PrefabSource src;
            SBX_VERIFY(src.prefab.assign(prefab->id), "Prefab id 가 ContentId 용량을 넘는다 (로더가 막아야 한다)");
            m_registry.emplace<comp::PrefabSource>(e, src);
        }
    }
    syncIdentityLogs();
    if (prefab != nullptr) {
        Json opaque = Json::object();
        for (const content::PrefabComponent& pc : prefab->components) {
            if (pc.opaque) {
                opaque[pc.name] = Json{{"version", 1}, {"value", pc.value}};
            }
        }
        if (!opaque.empty()) {
            m_opaque[m_registry.read<comp::Persistence>(e).saveId] = std::move(opaque);
        }
    }
    return e;
}

void SimulationWorld::applySpawns() {
    std::set<std::tuple<i32, i32, u64>> usedTiles;
    for (const SpawnRequest& req : m_spawns.take()) {
        if (req.prefab == nullptr) {
            continue;
        }
        const Vec2 pos = m_grid.clampPoint(req.position);
        if (req.uniqueTile) {
            const auto key = std::make_tuple(static_cast<i32>(std::floor(pos.x)), static_cast<i32>(std::floor(pos.y)),
                                             req.prefab->stableId);
            if (!usedTiles.insert(key).second) {
                continue;
            }
        }
        if (auto e = instantiate(req.prefab, pos, {}); !e) {
            log::warn("sim", "Prefab '{}' 생성 실패 (parent saveId {}): {}", req.prefab->id, req.parent,
                      e.error().describe());
        }
    }
}

SaveId SimulationWorld::saveIdOfNet(NetEntityId id) const noexcept {
    const ecs::EntityId e = resolve(id);
    const comp::Persistence* p = e.valid() ? m_registry.tryRead<comp::Persistence>(e) : nullptr;
    return p != nullptr ? p->saveId : kInvalidSaveId;
}

void SimulationWorld::applyCommand(const cmd::SimCommand& command) {
    ecs::Json recorded;
    if (m_commandObserver) {
        recorded = cmd::payloadToJson(command.payload, [this](NetEntityId n) { return u64{saveIdOfNet(n)}; });
    }
    cmd::CommandResult result;
    result.issuer = command.header.issuer;
    result.sequence = command.header.sequence;
    result.appliedTick = m_clock.tick();
    auto applied = applyPayload(command.payload);
    syncIdentityLogs();
    if (applied) {
        result.accepted = true;
        result.created = std::move(*applied);
        if (m_commandObserver) {
            m_commandObserver(recorded);
        }
        if (m_editStep) {
            m_clock.bumpEditSequence();
        }
    } else {
        result.error = std::move(applied.error());
        log::debug("sim", "명령 거절 tick {} issuer {} seq {} {}: {}", m_clock.tick(), command.header.issuer,
                   command.header.sequence, cmd::commandName(command.payload), result.error.describe());
    }
    m_results.push_back(std::move(result));
}

Expected<std::vector<NetEntityId>> SimulationWorld::applyPayload(const CommandPayload& payload) {
    using Result = Expected<std::vector<NetEntityId>>;
    const Tick tick = m_clock.tick();

    // 공통 검사: 대상 해석
    const auto resolveTarget = [&](NetEntityId id) -> Expected<ecs::EntityId> {
        const ecs::EntityId e = resolve(id);
        if (!e.valid()) {
            return makeError(ErrorCode::NotFound, std::format("대상 엔티티가 없다 (netId {})", id));
        }
        return e;
    };
    const auto resolveTargets = [&](const std::vector<NetEntityId>& ids) -> Expected<std::vector<ecs::EntityId>> {
        if (ids.empty() || ids.size() > kMaxCommandTargets) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("대상 수 {} (1~{} 이어야 한다)", ids.size(), kMaxCommandTargets));
        }
        std::vector<ecs::EntityId> out;
        out.reserve(ids.size());
        for (const NetEntityId id : ids) {
            auto e = resolveTarget(id);
            if (!e) {
                return std::unexpected(e.error());
            }
            out.push_back(*e);
        }
        return out;
    };
    // 공통 검사: 사용자가 다룰 수 있는 컴포넌트인가
    const auto editableInfo = [&](ecs::StableId id) -> Expected<const ecs::ComponentInfo*> {
        const ecs::ComponentInfo* info = m_catalog.find(id);
        if (info == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("모르는 컴포넌트 stableId {}", hexId(id)));
        }
        if (isProtected(id)) {
            return makeError(ErrorCode::PermissionDenied,
                             std::format("'{}' 는 엔진이 관리한다 — 명령으로 바꿀 수 없다", info->name));
        }
        return info;
    };

    // --- 편집 ---------------------------------------------------------------
    if (const auto* c = std::get_if<cmd::CreateEntity>(&payload)) {
        const content::Prefab* prefab = nullptr;
        if (!c->prefab.empty()) {
            prefab = m_content.findPrefab(c->prefab);
            if (prefab == nullptr) {
                return makeError(ErrorCode::NotFound, std::format("Prefab '{}' 가 콘텐츠에 없다", c->prefab));
            }
        }
        auto e = instantiate(prefab, c->position, c->components);
        if (!e) {
            return std::unexpected(e.error());
        }
        return Result{std::in_place, std::vector<NetEntityId>{m_registry.read<comp::NetIdentity>(*e).netId}};
    }

    if (const auto* c = std::get_if<cmd::DeleteEntity>(&payload)) {
        auto targets = resolveTargets(c->targets);
        if (!targets) {
            return std::unexpected(targets.error());
        }
        for (const ecs::EntityId e : *targets) {
            (void)m_registry.destroy(e); // 같은 대상이 두 번 있으면 두 번째는 무시된다
        }
        return Result{};
    }

    if (const auto* c = std::get_if<cmd::MoveEntity>(&payload)) {
        if (!isFinite(c->value)) {
            return makeError(ErrorCode::InvalidArgument, "이동 값이 유한하지 않다");
        }
        auto targets = resolveTargets(c->targets);
        if (!targets) {
            return std::unexpected(targets.error());
        }
        for (const ecs::EntityId e : *targets) {
            const comp::Transform* tr = m_registry.tryRead<comp::Transform>(e);
            if (tr == nullptr) {
                return makeError(ErrorCode::ValidationFailed, "core.transform 이 없는 대상");
            }
            const Vec2 to = c->absolute ? c->value : tr->position + c->value;
            if (!m_grid.containsPoint(to)) {
                return makeError(ErrorCode::OutOfRange,
                                 std::format("이동 결과 ({}, {}) 가 월드 경계 밖이다", to.x, to.y));
            }
        }
        for (const ecs::EntityId e : *targets) {
            comp::Transform& tr = m_registry.write<comp::Transform>(e);
            tr.position = c->absolute ? c->value : tr.position + c->value;
        }
        return Result{};
    }

    if (const auto* c = std::get_if<cmd::AddComponent>(&payload)) {
        auto e = resolveTarget(c->target);
        if (!e) {
            return std::unexpected(e.error());
        }
        auto info = editableInfo(c->component.stableId);
        if (!info) {
            return std::unexpected(info.error());
        }
        ecs::ComponentPoolBase& pool = poolFor(**info);
        if (pool.contains(*e)) {
            return makeError(ErrorCode::AlreadyExists, std::format("'{}' 가 이미 있다", (*info)->name));
        }
        if (auto r = (*info)->validateJson(c->component.value, (*info)->name); !r) {
            return std::unexpected(r.error());
        }
        if ((*info)->stableId == ecs::stableIdOf<comp::Transform>) {
            comp::Transform probe{};
            (void)ecs::componentFromJson(probe, c->component.value, "core.transform");
            if (!m_grid.containsPoint(probe.position)) {
                return makeError(ErrorCode::OutOfRange, "core.transform 위치가 월드 경계 밖이다");
            }
        }
        void* raw = pool.emplaceDefaultRaw(*e, tick);
        const auto r = (*info)->readJson(raw, c->component.value, (*info)->name);
        SBX_VERIFY(r.has_value(), "검증을 통과한 컴포넌트 값의 적용이 실패했다");
        return Result{};
    }

    if (const auto* c = std::get_if<cmd::RemoveComponent>(&payload)) {
        auto e = resolveTarget(c->target);
        if (!e) {
            return std::unexpected(e.error());
        }
        auto info = editableInfo(c->stableId);
        if (!info) {
            return std::unexpected(info.error());
        }
        ecs::ComponentPoolBase* pool = m_registry.poolByStableId(c->stableId);
        if (pool == nullptr || !pool->remove(*e)) {
            return makeError(ErrorCode::NotFound, std::format("'{}' 가 없다", (*info)->name));
        }
        return Result{};
    }

    if (const auto* c = std::get_if<cmd::ChangeComponent>(&payload)) {
        auto e = resolveTarget(c->target);
        if (!e) {
            return std::unexpected(e.error());
        }
        auto info = editableInfo(c->stableId);
        if (!info) {
            return std::unexpected(info.error());
        }
        if (!c->patch.is_object()) {
            return makeError(ErrorCode::InvalidArgument, "patch 는 JSON 객체여야 한다");
        }
        if ((*info)->patchJson == nullptr) {
            return makeError(ErrorCode::Unsupported, std::format("'{}' 는 패치할 수 없다", (*info)->name));
        }
        ecs::ComponentPoolBase* pool = m_registry.poolByStableId(c->stableId);
        if (pool == nullptr || !pool->contains(*e)) {
            return makeError(ErrorCode::NotFound, std::format("'{}' 가 없다", (*info)->name));
        }
        // patchJson 은 원자적이다: 실패하면 아무것도 바뀌지 않는다. changed 틱은 성공했을 때만 올린다.
        if ((*info)->stableId == ecs::stableIdOf<comp::Transform>) {
            comp::Transform probe = m_registry.read<comp::Transform>(*e);
            if (auto r = (*info)->patchJson(&probe, c->patch, (*info)->name); !r) {
                return std::unexpected(r.error());
            }
            if (!m_grid.containsPoint(probe.position)) {
                return makeError(ErrorCode::OutOfRange, "core.transform 위치가 월드 경계 밖이다");
            }
        }
        void* raw = const_cast<void*>(pool->getRaw(*e)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
        if (auto r = (*info)->patchJson(raw, c->patch, (*info)->name); !r) {
            return std::unexpected(r.error());
        }
        (void)pool->getRawForWrite(*e, tick);
        return Result{};
    }

    if (const auto* c = std::get_if<cmd::PaintTerrain>(&payload)) {
        const auto m = m_content.findMaterial(c->materialId);
        if (!m) {
            return makeError(ErrorCode::NotFound, std::format("머티리얼 '{}' 가 콘텐츠에 없다", c->materialId));
        }
        std::vector<Vec2i> tiles;
        if (!c->cells.empty()) {
            if (c->cells.size() > kMaxPaintTiles) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("타일 {}개 (최대 {})", c->cells.size(), kMaxPaintTiles));
            }
            for (const Vec2i t : c->cells) {
                if (!m_grid.containsTile(t)) {
                    return makeError(ErrorCode::OutOfRange, std::format("타일 ({}, {}) 가 월드 경계 밖이다", t.x, t.y));
                }
            }
            tiles = c->cells;
        } else {
            if (c->radius > kMaxBrushRadius) {
                return makeError(ErrorCode::OutOfRange,
                                 std::format("브러시 반경 {} (최대 {})", c->radius, kMaxBrushRadius));
            }
            if (c->shape != cmd::BrushShape::Square && c->shape != cmd::BrushShape::Circle) {
                return makeError(ErrorCode::InvalidArgument, "모르는 브러시 모양");
            }
            tiles = brushTiles(*c, m_grid);
        }
        (void)m_grid.paint(tiles, *m, m_content.material(*m));
        return Result{};
    }

    // --- 실행 제어 -----------------------------------------------------------
    if (std::holds_alternative<cmd::PauseSimulation>(payload)) {
        m_clock.setPaused(true);
        return Result{};
    }
    if (std::holds_alternative<cmd::ResumeSimulation>(payload)) {
        m_clock.setPaused(false);
        return Result{};
    }
    if (const auto* c = std::get_if<cmd::StepSimulation>(&payload)) {
        if (!m_clock.paused()) {
            return makeError(ErrorCode::InvalidArgument, "StepSimulation 은 일시정지 상태에서만 쓸 수 있다");
        }
        if (c->ticks == 0 || c->ticks > kMaxStepTicks) {
            return makeError(ErrorCode::OutOfRange, std::format("ticks {} (1~{})", c->ticks, kMaxStepTicks));
        }
        m_clock.addSteps(c->ticks);
        return Result{};
    }
    if (const auto* c = std::get_if<cmd::SetSimulationSpeed>(&payload)) {
        if (!SimulationClock::isValidSpeed(c->speed)) {
            return makeError(
                ErrorCode::OutOfRange,
                std::format("speed {} ({}~{})", c->speed, SimulationClock::kMinSpeed, SimulationClock::kMaxSpeed));
        }
        m_clock.setSpeed(c->speed);
        return Result{};
    }
    SBX_VERIFY(false, "처리하지 않은 명령 종류");
    return makeError(ErrorCode::Unsupported, std::string(cmd::commandName(payload)));
}

} // namespace sbx::sim
