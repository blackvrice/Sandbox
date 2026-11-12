#include "core/script/ScriptHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <optional>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Movement.hpp"
#include "core/script/EngineComponentAccess.hpp"
#include "core/script/ScriptValues.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/assert/Assert.hpp"
#include "foundation/log/Log.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::script {

namespace {

using api::AbiType;

// ctx 핸들: 위쪽 16 비트 표지 + 호출 번호 (작은 정수 · 다른 핸들과 섞이지 않게)
constexpr u64 kCtxTag = 0x5C78'0000'0000'0000ull;
// 개체 핸들: 위쪽 16 비트 표지 + 호출 번호 아래 16 비트 + 자리 + 1 (0 = null 과 섞이지 않게)
constexpr u64 kEntityTag = 0x5E00'0000'0000'0000ull;
// 이 호출에서 spawn 한 개체의 핸들 (v0.5 M2-T3): 다른 표지 + 호출 번호 아래 16 비트 + 새 개체 번호
constexpr u64 kNewEntityTag = 0x5E01'0000'0000'0000ull;

vm::HostResult fail(std::string message) {
    return {false, {}, std::move(message), {}, 0};
}

vm::HostResult ctxTick(ScriptHost& host, std::span<const vm::HostArg>) {
    return {true, vm::Value::i32v(static_cast<i32>(host.tick())), {}, {}, 0};
}

vm::HostResult ctxLog(ScriptHost& host, std::span<const vm::HostArg> args) {
    host.log(args[1].text);
    return {};
}

vm::HostResult ctxRead(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.read(args);
}
vm::HostResult ctxHas(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.has(args);
}
vm::HostResult ctxQuery(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.query(args);
}
vm::HostResult ctxWrite(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.write(args);
}
vm::HostResult ctxSpawn(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.spawn(args);
}
vm::HostResult ctxDestroy(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.destroy(args);
}
vm::HostResult ctxAdd(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.add(args);
}
vm::HostResult ctxRemove(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.remove(args);
}
vm::HostResult ctxTime(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.time(args);
}
vm::HostResult ctxSetOwner(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.setOwner(args);
}
vm::HostResult ctxPlayerCount(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.playerCount(args);
}
vm::HostResult ctxPlayerId(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.playerId(args);
}
vm::HostResult ctxPlayerRole(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.playerRole(args);
}
vm::HostResult ctxRandom(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.random(args);
}
vm::HostResult ctxRandomInt(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.randomInt(args);
}
vm::HostResult ctxNear(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.near(args, nullptr);
}
vm::HostResult ctxNearWith(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.nearWithArgs(args);
}
vm::HostResult ctxNearest(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.nearest(args, false);
}
vm::HostResult ctxKNearest(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.nearest(args, true);
}
vm::HostResult ctxInBox(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.inBox(args);
}
vm::HostResult ctxRequestPath(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.requestPath(args);
}
vm::HostResult ctxPathState(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.pathState(args);
}
vm::HostResult ctxPathPoints(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.pathPoints(args);
}
vm::HostResult ctxCancelPath(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.cancelPath(args);
}
vm::HostResult ctxRaycast(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.raycast(args);
}
vm::HostResult ctxOverlap(ScriptHost& host, std::span<const vm::HostArg> args) {
    return host.overlap(args);
}

// 표 (script/api/HostApi) 의 항목마다 하나 — 순서는 표와 같지 않아도 된다 (id 로 찾는다)
constexpr HostBinding kBindings[] = {
    {"ctx.tick", AbiType::I32, {AbiType::ServerContext}, 1, &ctxTick},
    {"ctx.log", AbiType::Void, {AbiType::ServerContext, AbiType::String}, 2, &ctxLog},
    {"ctx.read",
     AbiType::ComponentDataOrNull,
     {AbiType::ServerContext, AbiType::Entity, AbiType::Component},
     3,
     &ctxRead},
    {"ctx.has", AbiType::Bool, {AbiType::ServerContext, AbiType::Entity, AbiType::Component}, 3, &ctxHas},
    {"ctx.query", AbiType::EntityList, {AbiType::ServerContext, AbiType::Component}, 2, &ctxQuery},
    {"ctx.write",
     AbiType::Void,
     {AbiType::ServerContext, AbiType::Entity, AbiType::Component, AbiType::ComponentData},
     4,
     &ctxWrite},
    {"ctx.spawn",
     AbiType::EntityOrNull,
     {AbiType::ServerContext, AbiType::String, AbiType::Number, AbiType::Number},
     4,
     &ctxSpawn},
    {"ctx.destroy", AbiType::Void, {AbiType::ServerContext, AbiType::Entity}, 2, &ctxDestroy},
    {"ctx.add",
     AbiType::Void,
     {AbiType::ServerContext, AbiType::Entity, AbiType::Component, AbiType::ComponentData},
     4,
     &ctxAdd},
    {"ctx.remove", AbiType::Void, {AbiType::ServerContext, AbiType::Entity, AbiType::Component}, 3, &ctxRemove},
    {"ctx.time", AbiType::Number, {AbiType::ServerContext}, 1, &ctxTime},
    {"ctx.setOwner", AbiType::Void, {AbiType::ServerContext, AbiType::Entity, AbiType::I32}, 3, &ctxSetOwner},
    {"ctx.playerCount", AbiType::I32, {AbiType::ServerContext}, 1, &ctxPlayerCount},
    {"ctx.playerId", AbiType::I32, {AbiType::ServerContext, AbiType::I32}, 2, &ctxPlayerId},
    {"ctx.playerRole", AbiType::I32, {AbiType::ServerContext, AbiType::I32}, 2, &ctxPlayerRole},
    {"ctx.random", AbiType::Number, {AbiType::ServerContext}, 1, &ctxRandom},
    {"ctx.randomInt", AbiType::I32, {AbiType::ServerContext, AbiType::I32, AbiType::I32}, 3, &ctxRandomInt},
    {"ctx.near",
     AbiType::EntityList,
     {AbiType::ServerContext, AbiType::Number, AbiType::Number, AbiType::Number},
     4,
     &ctxNear},
    {"ctx.nearWith",
     AbiType::EntityList,
     {AbiType::ServerContext, AbiType::Number, AbiType::Number, AbiType::Number, AbiType::Component},
     5,
     &ctxNearWith},
    {"ctx.nearest",
     AbiType::EntityOrNull,
     {AbiType::ServerContext, AbiType::Number, AbiType::Number, AbiType::Number},
     4,
     &ctxNearest},
    {"ctx.kNearest",
     AbiType::EntityList,
     {AbiType::ServerContext, AbiType::Number, AbiType::Number, AbiType::Number, AbiType::I32},
     5,
     &ctxKNearest},
    {"ctx.inBox",
     AbiType::EntityList,
     {AbiType::ServerContext, AbiType::Number, AbiType::Number, AbiType::Number, AbiType::Number},
     5,
     &ctxInBox},
    {"ctx.requestPath",
     AbiType::Bool,
     {AbiType::ServerContext, AbiType::Entity, AbiType::Number, AbiType::Number},
     4,
     &ctxRequestPath},
    {"ctx.pathState", AbiType::I32, {AbiType::ServerContext, AbiType::Entity}, 2, &ctxPathState},
    {"ctx.pathPoints", AbiType::NumberList, {AbiType::ServerContext, AbiType::Entity}, 2, &ctxPathPoints},
    {"ctx.cancelPath", AbiType::Void, {AbiType::ServerContext, AbiType::Entity}, 2, &ctxCancelPath},
    {"ctx.raycast",
     AbiType::EntityOrNull,
     {AbiType::ServerContext, AbiType::Number, AbiType::Number, AbiType::Number, AbiType::Number},
     5,
     &ctxRaycast},
    {"ctx.overlap",
     AbiType::EntityList,
     {AbiType::ServerContext, AbiType::Number, AbiType::Number, AbiType::Number},
     4,
     &ctxOverlap},
};

template <class F>
void store(std::byte* p, const F& v) noexcept {
    std::memcpy(p, &v, sizeof(F));
}

// 유한하고 f32 로 나타낼 수 있다
bool fitsF32(f64 v) noexcept {
    return std::isfinite(v) && std::fabs(v) <= static_cast<f64>(std::numeric_limits<f32>::max());
}

} // namespace

std::span<const HostBinding> hostBindings() {
    return kBindings;
}

std::vector<std::string> checkHostBindings(std::span<const HostBinding> bindings) {
    const std::vector<HostBindingShape> shapes = hostBindingShapes(bindings);
    return checkHostBindingShapes(shapes, api::HostDomain::Server);
}

std::vector<std::string> checkHostBindingShapes(std::span<const HostBindingShape> bindings, api::HostDomain domain) {
    std::vector<std::string> problems;
    for (const api::HostFunction& f : api::kHostFunctions) {
        if (f.domain != domain) {
            continue;
        }
        usize count = 0;
        for (const HostBindingShape& b : bindings) {
            if (b.id != f.id) {
                continue;
            }
            ++count;
            bool same = b.result == f.result && b.paramCount == f.paramCount + 1 && b.params[0] == f.owner;
            for (u8 i = 0; same && i < f.paramCount; ++i) {
                same = b.params[i + 1] == f.params[i];
            }
            if (!same) {
                problems.push_back(
                    std::format("{}: 구현의 서명이 표와 다르다 (표: {})", f.id, api::canonicalSignature(f)));
            }
            if (!b.hasImpl) {
                problems.push_back(std::format("{}: 구현 함수가 없다", f.id));
            }
        }
        if (count == 0) {
            problems.push_back(std::format("{}: 구현이 없다", f.id));
        } else if (count > 1) {
            problems.push_back(std::format("{}: 구현이 {} 개다", f.id, count));
        }
    }
    for (const HostBindingShape& b : bindings) {
        const api::HostFunction* f = api::findHostFunction(b.id);
        if (f == nullptr) {
            problems.push_back(std::format("{}: 표에 없는 구현", b.id));
        } else if (f->domain != domain) {
            problems.push_back(std::format("{}: 다른 도메인 항목의 구현", b.id));
        }
    }
    return problems;
}

ScriptHost::ScriptHost() {
    SBX_ASSERT(checkHostBindings(hostBindings()).empty(),
               "Host 바인딩이 Host API 표와 맞지 않는다 (checkHostBindings)");
    m_byHostIndex.assign(api::kHostFunctions.size(), nullptr);
    for (const HostBinding& b : hostBindings()) {
        if (const api::HostFunction* f = api::findHostFunction(b.id)) {
            m_byHostIndex[api::hostFunctionIndex(*f)] = &b;
        }
    }
}

vm::Value ScriptHost::beginCall(sim::Tick tick, std::string script, sim::SimulationWorld* world,
                                std::string_view callback) {
    m_token = kCtxTag | (m_nextToken++ & 0x0000'FFFF'FFFF'FFFFull);
    m_callback = std::string(callback);
    m_capabilities = api::callbackCapabilities(callback, false);
    m_tick = tick;
    m_script = std::move(script);
    m_world = world;
    m_handles.clear();
    m_handleOf.clear();
    m_writes.clear();
    m_writeOf.clear();
    m_pathOps.clear();
    m_ops = {};
    m_newEntities = 0;
    m_destroysThisCall = 0;
    m_addRemovesThisCall = 0;
    m_droppedThisCall = 0;
    return vm::Value::opaque(m_token);
}

void ScriptHost::endCall(bool commit) {
    if (commit && m_world != nullptr) {
        ecs::Registry& reg = m_world->registry();
        // 쓴 차례대로 — 호출 안에는 구조 변경이 없으므로 개체 · 컴포넌트는 그대로 있다 (없으면 건너뛴다)
        for (const PendingWrite& w : m_writes) {
            ecs::ComponentPoolBase* pool = reg.poolByStableId(w.info->stableId);
            void* raw = pool != nullptr ? pool->getRawForWrite(w.entity, reg.currentTick()) : nullptr;
            if (raw != nullptr) {
                std::memcpy(raw, w.bytes.data(), w.bytes.size());
            }
        }
        // 경로 요청 · 취소 (v0.5 M8-T8) — ai.path 를 직접. 요청은 Pending (다음 틱 단계 8 이 saveId 순 · 예산으로
        // 제출), 취소는 None. 경유점은 비운다 (옛 경로를 따라가지 않게). 개체 · ai.path 가 없어졌으면 건너뛴다
        for (const PathOp& op : m_pathOps) {
            if (!reg.alive(op.entity) || reg.tryRead<comp::Path>(op.entity) == nullptr) {
                continue;
            }
            comp::Path& path = reg.write<comp::Path>(op.entity);
            path.state = op.cancel ? comp::PathState::None : comp::PathState::Pending;
            if (!op.cancel) {
                path.goal = op.goal;
            }
            path.waypoints.clear();
            path.cursor = 0;
            path.partial = false;
        }
        // 구조 변경은 월드 큐로 — 단계 끝에 넣은 차례로 적용 (v0.5 M2-T3). 트랩이면 아래에서 버린다
        m_world->countScriptStructuralDropped(m_droppedThisCall);
        m_world->queueScriptStructural(std::move(m_ops));
    }
    m_ops = {};
    m_pathOps.clear();
    m_token = 0;
    m_capabilities = 0;
    m_world = nullptr;
    m_handles.clear();
    m_handleOf.clear();
    m_writes.clear();
    m_writeOf.clear();
}

vm::HostResult ScriptHost::call(const api::HostFunction& fn, std::span<const vm::HostArg> args) {
    const usize index = api::hostFunctionIndex(fn);
    const HostBinding* b = index < m_byHostIndex.size() ? m_byHostIndex[index] : nullptr;
    if (b == nullptr) {
        return fail(std::format("{} 의 구현이 없다", fn.id));
    }
    // 첫 인자는 주인 — 지금 열린 호출의 ctx 여야 한다
    if (m_token == 0 || args.empty() || args[0].value.bits != m_token) {
        return fail("ctx 가 지금 호출의 것이 아니다 (지난 호출의 ctx · 위조한 핸들)");
    }
    // 호출 토큰의 콜백이 이 함수의 Capability 를 허락하는가 (v0.10 RT-06-1 — 검사기를 거치지 않은 · 고친 바이트코드
    // 대비)
    if ((m_capabilities & api::capBit(fn.capability)) == 0) {
        return fail(std::format("{} — {} 콜백에서는 쓸 수 없다 ({} 권한이 없다 — docs/22-RUNTIME-V2.md 3.2)", fn.id,
                                m_callback, api::capabilityName(fn.capability)));
    }
    return b->impl(*this, args);
}

void ScriptHost::log(std::string_view text) {
    if (m_tick != m_logTick) {
        m_logTick = m_tick;
        m_logLinesThisTick = 0;
    }
    if (m_logLinesThisTick >= kMaxLogLinesPerTick) {
        ++m_droppedLogs; // 로그는 시뮬레이션 상태가 아니다 — 버려도 결과가 같다
        return;
    }
    ++m_logLinesThisTick;
    std::string line(text.substr(0, kMaxLogBytes));
    if (text.size() > kMaxLogBytes) {
        // UTF-8 글자 가운데를 자르지 않는다
        while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80) {
            line.pop_back();
        }
        if (!line.empty() && static_cast<unsigned char>(line.back()) >= 0xC0) {
            line.pop_back();
        }
        line += "…";
    }
    log::info("script", "{} t{}: {}", m_script, m_tick, line);
    if (m_logs.size() >= kKeptLogLines) {
        m_logs.erase(m_logs.begin());
    }
    m_logs.push_back({m_tick, m_script, std::move(line)});
}

// ---- 개체 핸들 · 컴포넌트 (ADR-0060)
// ---------------------------------------------------------------------------------

vm::Value ScriptHost::entityHandle(ecs::EntityId e) {
    if (m_token == 0 || !e.valid()) {
        return vm::Value::null();
    }
    u32 slot = 0;
    if (const auto it = m_handleOf.find(e.raw); it != m_handleOf.end()) {
        slot = it->second;
    } else {
        slot = static_cast<u32>(m_handles.size());
        m_handles.push_back(e);
        m_handleOf.emplace(e.raw, slot);
    }
    return vm::Value::opaque(kEntityTag | ((m_token & 0xFFFFull) << 32) | (static_cast<u64>(slot) + 1));
}

ecs::EntityId ScriptHost::entityOf(vm::Value handle) const {
    const u64 h = handle.bits;
    if ((h & 0xFFFF'0000'0000'0000ull) != kEntityTag || ((h >> 32) & 0xFFFFull) != (m_token & 0xFFFFull)) {
        return ecs::kNullEntity;
    }
    const u64 slot = (h & 0xFFFF'FFFFull);
    if (slot == 0 || slot > m_handles.size() || m_world == nullptr) {
        return ecs::kNullEntity;
    }
    const ecs::EntityId e = m_handles[static_cast<usize>(slot - 1)];
    return m_world->registry().alive(e) ? e : ecs::kNullEntity;
}

const ecs::ComponentInfo* ScriptHost::componentOf(vm::Value ref, std::string& error) const {
    if (m_world == nullptr) {
        error = "이 호출에는 월드가 없다";
        return nullptr;
    }
    const ecs::ComponentInfo* info = m_world->catalog().find(ref.bits);
    if (info == nullptr) {
        error = std::format("모르는 컴포넌트 ({:016x}) — 이 월드의 콘텐츠가 정의하지 않았다", ref.bits);
        return nullptr;
    }
    // 엔진 컴포넌트는 접근 표에 있는 것만 (v0.5 M2-T1 — 읽기 · has · query, 쓰기는 write 가 막는다)
    if (info->dynamicLayout == nullptr && engineAccessOf(*info) == nullptr) {
        error = std::format("엔진 컴포넌트 '{}' 는 스크립트 접근 표에 없다 (docs/20-CREATOR-ALPHA.md 3.1)", info->name);
        return nullptr;
    }
    return info;
}

const std::byte* ScriptHost::currentBytes(ecs::EntityId e, const ecs::ComponentInfo& info) const {
    if (const auto it = m_writeOf.find({e.raw, info.stableId}); it != m_writeOf.end()) {
        return m_writes[it->second].bytes.data();
    }
    const ecs::ComponentPoolBase* pool = m_world->registry().poolByStableId(info.stableId);
    return pool != nullptr ? static_cast<const std::byte*>(pool->getRaw(e)) : nullptr;
}

vm::HostResult ScriptHost::read(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    if (newIndexOf(args[1].value) != 0) {
        return {true, vm::Value::null(), {}, {}, 0}; // 이 호출에서 spawn 한 개체 — 이 틱 끝에 생긴다 (v0.5 M2-T3)
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 죽은 개체)");
    }
    const std::byte* bytes = currentBytes(e, *info);
    if (bytes == nullptr) {
        return {true, vm::Value::null(), {}, {}, 0};
    }
    vm::HostResult r;
    r.value = vm::Value::boolean(true);
    if (const api::EngineComponentAccess* engine = engineAccessOf(*info)) {
        std::string why;
        if (!appendEngineFieldValues(*engine, *info, bytes, r.values, why)) {
            return fail(std::move(why));
        }
        return r;
    }
    appendFieldValues(*info->dynamicLayout, bytes, r.values, [&](SaveId id) {
        const ecs::EntityId target = id == kInvalidSaveId ? ecs::kNullEntity : m_world->resolveSave(id);
        return target.valid() && m_world->registry().alive(target) ? entityHandle(target) : vm::Value::null();
    });
    return r;
}

vm::HostResult ScriptHost::has(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    if (newIndexOf(args[1].value) != 0) {
        return {true, vm::Value::boolean(false), {}, {}, 0}; // 이 호출에서 spawn 한 개체 (v0.5 M2-T3)
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 죽은 개체)");
    }
    const ecs::ComponentPoolBase* pool = m_world->registry().poolByStableId(info->stableId);
    return {true, vm::Value::boolean(pool != nullptr && pool->contains(e)), {}, {}, 0};
}

vm::HostResult ScriptHost::query(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[1].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    vm::HostResult r;
    const ecs::Registry& reg = m_world->registry();
    const ecs::ComponentPoolBase* pool = reg.poolByStableId(info->stableId);
    if (pool == nullptr) {
        return r;
    }
    if (pool->size() > kMaxQueryEntities) {
        return fail(std::format("질의 결과가 {} 개를 넘는다 ({} 개)", kMaxQueryEntities, pool->size()));
    }
    // saveId 오름차순 — 풀 순서는 세이브 → 로드 뒤에 다를 수 있다 (ADR-0057 5, 04 D2)
    std::vector<std::pair<SaveId, ecs::EntityId>> keyed;
    keyed.reserve(pool->size());
    for (const ecs::EntityId e : pool->entities()) {
        const auto* id = reg.tryRead<comp::Persistence>(e);
        if (id != nullptr && id->saveId != kInvalidSaveId) {
            keyed.emplace_back(id->saveId, e);
        }
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    r.values.reserve(keyed.size());
    for (const auto& [id, e] : keyed) {
        r.values.push_back(entityHandle(e));
    }
    r.extraFuel = keyed.size();
    return r;
}

// 스크립트 컴포넌트 값 (칸 — 필드 이름 순) 을 bytes (배치 그대로) 에 쓴다. 범위 · 유한 · f32 · 개체 핸들 검사 — 문제면
// 그 설명 (write · add 가 같이 쓴다, v0.5 M2-T3 에 write 에서 나눔)
std::optional<std::string> ScriptHost::encodeDynamic(const ecs::ComponentInfo& info, std::byte* bytes,
                                                     std::span<const vm::Value> v) const {
    const ecs::DynamicComponentLayout& layout = *info.dynamicLayout;
    usize k = 0;
    auto next = [&]() -> std::optional<vm::Value> {
        if (k >= v.size()) {
            return std::nullopt;
        }
        return v[k++];
    };
    auto inRange = [](const ecs::DynamicComponentLayout::Field& f, f64 x) {
        return !f.meta.hasRange || (x >= f.meta.min && x <= f.meta.max);
    };
    for (const ecs::DynamicComponentLayout::Field& f : layout.fields()) {
        std::byte* p = bytes + f.offset;
        const std::string where = std::format("{}.{}", info.name, f.name);
        const auto a = next();
        if (!a) {
            return (std::format("{}: 값의 칸이 모자란다", info.name));
        }
        switch (f.kind) {
        case ecs::DynamicFieldKind::I32:
        case ecs::DynamicFieldKind::Enum: {
            const i32 x = a->asI32();
            if (!inRange(f, static_cast<f64>(x))) {
                return (std::format("{} = {} 이 범위 [{}, {}] 밖이다", where, x, f.meta.min, f.meta.max));
            }
            store(p, x);
            break;
        }
        case ecs::DynamicFieldKind::F32:
        case ecs::DynamicFieldKind::F64: {
            const f64 x = a->asF64();
            const bool f32Kind = f.kind == ecs::DynamicFieldKind::F32;
            if (f32Kind ? !fitsF32(x) : !std::isfinite(x)) {
                return (std::format("{} = {} — 유한한 {} 값이 아니다", where, x, f32Kind ? "f32" : "f64"));
            }
            const f64 stored = f32Kind ? static_cast<f64>(static_cast<f32>(x)) : x; // f32 는 한 번 반올림 (ADR-0059 2)
            if (!inRange(f, stored)) {
                return (std::format("{} = {} 이 범위 [{}, {}] 밖이다", where, stored, f.meta.min, f.meta.max));
            }
            if (f32Kind) {
                store(p, static_cast<f32>(stored));
            } else {
                store(p, stored);
            }
            break;
        }
        case ecs::DynamicFieldKind::Bool:
            store(p, a->asBool());
            break;
        case ecs::DynamicFieldKind::Vec2: {
            const auto b = next();
            if (!b) {
                return (std::format("{}: 값의 칸이 모자란다", info.name));
            }
            const f64 x = a->asF64();
            const f64 y = b->asF64();
            if (!fitsF32(x) || !fitsF32(y)) {
                return (std::format("{} = ({}, {}) — 유한한 f32 값이 아니다", where, x, y));
            }
            store(p, Vec2{static_cast<f32>(x), static_cast<f32>(y)});
            break;
        }
        case ecs::DynamicFieldKind::EntityRef: {
            SaveId id = kInvalidSaveId;
            if (!a->isNull()) {
                const ecs::EntityId target = entityOf(*a);
                const auto* pid = target.valid() ? m_world->registry().tryRead<comp::Persistence>(target) : nullptr;
                if (pid == nullptr) {
                    return (std::format("{}: 개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 죽은 개체)", where));
                }
                id = pid->saveId;
            }
            store(p, id);
            break;
        }
        }
    }
    if (k != v.size()) {
        return (std::format("{}: 값의 칸이 남는다 (모양이 컴포넌트와 다르다)", info.name));
    }
    return std::nullopt;
}

vm::HostResult ScriptHost::write(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    if (newIndexOf(args[1].value) != 0) {
        return fail("이 호출에서 spawn 한 개체는 아직 없다 — 값은 ctx.add 로 (v0.5 M2-T3)");
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 죽은 개체)");
    }
    // 엔진 관리 컴포넌트 (접근 표의 쓰기 None) 는 개체에 있든 없든 거절 — 권한이 먼저 (v0.5 M2-T2)
    if (const api::EngineComponentAccess* engine = engineAccessOf(*info);
        engine != nullptr && engine->write != api::EngineWrite::Server) {
        return fail(std::format("엔진 컴포넌트 '{}' 는 스크립트가 쓰지 않는다 (엔진 관리 — 접근 표)", info->name));
    }
    const std::byte* current = currentBytes(e, *info);
    if (current == nullptr) {
        return fail(std::format("개체에 '{}' 가 없다 — 쓰기는 있는 컴포넌트의 값만 (붙이기는 [계획])", info->name));
    }
    // 쓰기 버퍼 — (개체, 컴포넌트) 마다 마지막 값 하나, 호출이 끝나면 적용 (트랩이면 버림)
    const auto buffer = [&](std::vector<std::byte>&& bytes) -> vm::HostResult {
        const std::pair<u64, u64> key{e.raw, info->stableId};
        if (const auto it = m_writeOf.find(key); it != m_writeOf.end()) {
            m_writes[it->second].bytes = std::move(bytes);
            return {};
        }
        if (m_writes.size() >= kMaxPendingWrites) {
            return fail(std::format("호출 하나의 쓰기가 {} 개를 넘는다", kMaxPendingWrites));
        }
        m_writeOf.emplace(key, m_writes.size());
        m_writes.push_back(PendingWrite{e, info, std::move(bytes)});
        return {};
    };
    // 엔진 컴포넌트 (v0.5 M2-T2): 접근 표가 쓰기를 허락한 것만 — 범위 · 경계 자름 (EngineComponentAccess). 바이트
    // 복사로 버퍼에 둔다 (쓸 수 있는 내장 컴포넌트는 trivially copyable — EngineComponentAccess.cpp 의 static_assert)
    if (const api::EngineComponentAccess* engine = engineAccessOf(*info)) {
        std::vector<std::byte> bytes(current, current + info->size);
        std::string why;
        if (!applyEngineFieldValues(*engine, *info, bytes.data(), args[3].fields, &m_world->grid(), why)) {
            return fail(std::move(why));
        }
        return buffer(std::move(bytes));
    }
    std::vector<std::byte> bytes(current, current + info->dynamicLayout->stride());
    if (auto bad = encodeDynamic(*info, bytes.data(), args[3].fields)) {
        return fail(std::move(*bad));
    }
    return buffer(std::move(bytes));
}

// ---- 구조 변경 (v0.5 M2-T3, ADR-0080 2 · 3)
// -------------------------------------------------------------------------- 호출마다 m_ops 에 모으고 endCall(true) 가
// 월드 큐로 넘긴다. 상한 · 잘못된 연산은 트랩 없이 버리고 센다 (spawn 은 null). 값의 문제 (add 의 범위 · 유한) 는 write
// 와 같이 HostError

vm::Value ScriptHost::newEntityHandle(u32 newIndex) const {
    return vm::Value::opaque(kNewEntityTag | ((m_token & 0xFFFFull) << 32) | newIndex);
}

u32 ScriptHost::newIndexOf(vm::Value handle) const {
    const u64 h = handle.bits;
    if ((h & 0xFFFF'0000'0000'0000ull) != kNewEntityTag || ((h >> 32) & 0xFFFFull) != (m_token & 0xFFFFull)) {
        return 0;
    }
    const u64 index = h & 0xFFFF'FFFFull;
    return index >= 1 && index <= m_newEntities ? static_cast<u32>(index) : 0u;
}

vm::HostResult ScriptHost::spawn(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const auto drop = [&] {
        ++m_droppedThisCall;
        return vm::HostResult{true, vm::Value::null(), {}, {}, 0};
    };
    if (m_newEntities >= kMaxSpawnsPerCall || m_world->scriptSpawnsThisTick() + m_newEntities >= kMaxSpawnsPerTick) {
        return drop();
    }
    const content::Prefab* prefab = m_world->content().findPrefab(args[1].text);
    const f64 x = args[2].value.asF64();
    const f64 y = args[3].value.asF64();
    if (prefab == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return drop();
    }
    const Vec2 pos{static_cast<f32>(x), static_cast<f32>(y)};
    if (!m_world->grid().containsPoint(pos)) {
        return drop(); // 경계 밖 — 자르지 않는다 (만들 자리를 잘못 고른 것)
    }
    const u32 index = ++m_newEntities;
    sim::ScriptStructuralOp op;
    op.kind = sim::ScriptStructuralOp::Kind::Spawn;
    op.newIndex = index;
    op.prefab = prefab;
    op.position = pos;
    m_ops.ops.push_back(std::move(op));
    return {true, newEntityHandle(index), {}, {}, 0};
}

vm::HostResult ScriptHost::destroy(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    if (m_destroysThisCall >= kMaxDestroysPerCall) {
        ++m_droppedThisCall;
        return {};
    }
    sim::ScriptStructuralOp op;
    op.kind = sim::ScriptStructuralOp::Kind::Destroy;
    op.newIndex = newIndexOf(args[1].value);
    if (op.newIndex == 0) {
        op.target = entityOf(args[1].value);
        if (!op.target.valid()) {
            ++m_droppedThisCall; // 지난 호출 · 위조 · 이미 죽은 개체 — 버리고 센다
            return {};
        }
    }
    ++m_destroysThisCall;
    m_ops.ops.push_back(std::move(op));
    return {};
}

vm::HostResult ScriptHost::add(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    if (info->dynamicLayout == nullptr || m_addRemovesThisCall >= kMaxAddRemovesPerCall) {
        ++m_droppedThisCall; // 엔진 컴포넌트 붙이기는 [계획] (검사기 S404) · 상한
        return {};
    }
    sim::ScriptStructuralOp op;
    op.kind = sim::ScriptStructuralOp::Kind::Add;
    op.newIndex = newIndexOf(args[1].value);
    if (op.newIndex == 0) {
        op.target = entityOf(args[1].value);
        if (!op.target.valid()) {
            ++m_droppedThisCall;
            return {};
        }
    }
    // 기본값 칸 위에 값 — 모든 필드를 넘긴다 (T 통째)
    const ecs::DynamicComponentLayout& layout = *info->dynamicLayout;
    op.bytes.assign(layout.defaults(), layout.defaults() + layout.stride());
    if (auto bad = encodeDynamic(*info, op.bytes.data(), args[3].fields)) {
        return fail(std::move(*bad));
    }
    op.component = info;
    ++m_addRemovesThisCall;
    m_ops.ops.push_back(std::move(op));
    return {};
}

vm::HostResult ScriptHost::remove(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    if (info->dynamicLayout == nullptr || m_addRemovesThisCall >= kMaxAddRemovesPerCall) {
        ++m_droppedThisCall; // 엔진 컴포넌트 떼기는 [계획] · 상한
        return {};
    }
    sim::ScriptStructuralOp op;
    op.kind = sim::ScriptStructuralOp::Kind::Remove;
    op.newIndex = newIndexOf(args[1].value);
    if (op.newIndex == 0) {
        op.target = entityOf(args[1].value);
        if (!op.target.valid()) {
            ++m_droppedThisCall;
            return {};
        }
    }
    op.component = info;
    ++m_addRemovesThisCall;
    m_ops.ops.push_back(std::move(op));
    return {};
}

// ---- 시간 · 난수 · 반경 질의 (v0.5 M2-T4, ADR-0080 4 · 5)
// -------------------------------------------------------------

vm::HostResult ScriptHost::time(std::span<const vm::HostArg>) {
    return {true, vm::Value::f64v(static_cast<f64>(m_tick) / static_cast<f64>(sim::kTickRate)), {}, {}, 0};
}

vm::HostResult ScriptHost::setOwner(std::span<const vm::HostArg> args) {
    const i32 player = args[2].value.asI32();
    if (player < 0 || player > 0xFFFF) {
        return fail(std::format("setOwner — player {} 는 0 ~ 65535", player));
    }
    if (m_addRemovesThisCall >= kMaxAddRemovesPerCall) {
        ++m_droppedThisCall; // 붙이기 · 떼기와 같은 상한
        return {};
    }
    sim::ScriptStructuralOp op;
    op.kind = sim::ScriptStructuralOp::Kind::SetOwner;
    op.newIndex = newIndexOf(args[1].value);
    if (op.newIndex == 0) {
        op.target = entityOf(args[1].value);
        if (!op.target.valid()) {
            ++m_droppedThisCall;
            return {};
        }
    }
    op.player = static_cast<u32>(player);
    ++m_addRemovesThisCall;
    m_ops.ops.push_back(std::move(op));
    return {};
}

vm::HostResult ScriptHost::playerCount(std::span<const vm::HostArg>) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    return {true, vm::Value::i32v(static_cast<i32>(m_world->players().size())), {}, {}, 0};
}

vm::HostResult ScriptHost::playerId(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const i32 index = args[1].value.asI32();
    const auto& players = m_world->players(); // std::map — id 순
    if (index < 0 || static_cast<usize>(index) >= players.size()) {
        return {true, vm::Value::i32v(-1), {}, {}, 0};
    }
    return {true, vm::Value::i32v(static_cast<i32>(std::next(players.begin(), index)->first)), {}, {}, 0};
}

vm::HostResult ScriptHost::playerRole(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const i32 id = args[1].value.asI32();
    if (id <= 0 || id > 0xFFFF) {
        return {true, vm::Value::i32v(-1), {}, {}, 0};
    }
    const auto& players = m_world->players();
    const auto it = players.find(static_cast<cmd::ClientId>(id));
    return {true, vm::Value::i32v(it == players.end() ? -1 : static_cast<i32>(it->second.role)), {}, {}, 0};
}

rnd::CounterRng* ScriptHost::stream() {
    if (m_world == nullptr) {
        return nullptr;
    }
    if (m_streamTick != m_tick) {
        m_streams.clear(); // 틱마다 새 흐름 — 세이브할 상태가 없다
        m_streamTick = m_tick;
    }
    // 키 = 시스템 id (beginCall 의 script — ScriptSystemAdapter 가 시스템 id 를 넘긴다). 시스템을 더하거나 빼도 다른
    // 시스템의 수열은 그대로
    const u64 key = fnv1a64(m_script);
    auto it = m_streams.find(key);
    if (it == m_streams.end()) {
        it = m_streams.emplace(key, m_world->random().stream(rnd::Purpose::Script, key)).first;
    }
    return &it->second;
}

vm::HostResult ScriptHost::random(std::span<const vm::HostArg>) {
    rnd::CounterRng* rng = stream();
    if (rng == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    // 53 비트 → [0, 1) 의 f64 (정확히 나타낼 수 있는 값만)
    const f64 x = static_cast<f64>(rng->nextU64() >> 11) * (1.0 / 9007199254740992.0);
    return {true, vm::Value::f64v(x), {}, {}, 0};
}

vm::HostResult ScriptHost::randomInt(std::span<const vm::HostArg> args) {
    const i32 lo = args[1].value.asI32();
    const i32 hi = args[2].value.asI32();
    if (lo > hi) {
        return fail(std::format("randomInt({}, {}) — lo 가 hi 보다 크다", lo, hi));
    }
    rnd::CounterRng* rng = stream();
    if (rng == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const u64 span = static_cast<u64>(static_cast<i64>(hi) - static_cast<i64>(lo)) + 1; // 1 ~ 2^32
    const u64 offset = span > 0xFFFFFFFFull ? rng->nextU32() : rng->below(static_cast<u32>(span));
    return {true, vm::Value::i32v(static_cast<i32>(static_cast<i64>(lo) + static_cast<i64>(offset))), {}, {}, 0};
}

vm::HostResult ScriptHost::nearWithArgs(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[4].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    return near(args, info);
}

vm::HostResult ScriptHost::near(std::span<const vm::HostArg> args, const ecs::ComponentInfo* with) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const f64 x = args[1].value.asF64();
    const f64 y = args[2].value.asF64();
    const f64 r = args[3].value.asF64();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(r) || r < 0 || r > kMaxNearRadius) {
        return fail(std::format("near({}, {}, {}) — 유한한 위치 · 반경 0 ~ {}", x, y, r, kMaxNearRadius));
    }
    const ecs::ComponentPoolBase* pool = with != nullptr ? m_world->registry().poolByStableId(with->stableId) : nullptr;
    if (with != nullptr && pool == nullptr) {
        return {}; // 그 컴포넌트를 가진 개체가 없다
    }
    // 단계 4 의 공간 색인 — 틱 처음의 위치 (이 틱에 쓴 위치 · 만든 개체는 다음 틱부터). saveId 오름차순 (세이브 → 로드
    // 뒤에도 같다 — query 와 같다)
    std::vector<std::pair<SaveId, ecs::EntityId>> found;
    bool tooMany = false;
    m_world->spatial().forEachInRadius(
        Vec2{static_cast<f32>(x), static_cast<f32>(y)}, static_cast<f32>(r), [&](const world::SpatialEntry& e) {
            if (!m_world->registry().alive(e.entity) || (pool != nullptr && !pool->contains(e.entity))) {
                return;
            }
            if (found.size() >= kMaxNearEntities) {
                tooMany = true;
                return;
            }
            found.emplace_back(e.saveId, e.entity);
        });
    if (tooMany) {
        return fail(std::format("near 결과가 {} 개를 넘는다 — 반경을 줄인다", kMaxNearEntities));
    }
    std::ranges::sort(found);
    vm::HostResult out;
    out.values.reserve(found.size());
    for (const auto& [id, e] : found) {
        out.values.push_back(entityHandle(e));
    }
    out.extraFuel = found.size(); // 개체마다 연료 1 (질의와 같다)
    return out;
}

// 가까운 차례 — (거리², saveId) 가 작은 것부터. 거리² 는 색인의 위치 (f32) 와 (x, y) 로 f64 에서 셈한다 (결정적)
vm::HostResult ScriptHost::nearest(std::span<const vm::HostArg> args, bool many) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const f64 x = args[1].value.asF64();
    const f64 y = args[2].value.asF64();
    const f64 r = args[3].value.asF64();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(r) || r < 0 || r > kMaxNearRadius) {
        return fail(std::format("nearest({}, {}, {}) — 유한한 위치 · 반경 0 ~ {}", x, y, r, kMaxNearRadius));
    }
    const i32 k = many ? args[4].value.asI32() : 1;
    if (k < 0 || static_cast<usize>(k) > kMaxNearEntities) {
        return fail(std::format("kNearest 의 k {} — 0 ~ {}", k, kMaxNearEntities));
    }
    struct Hit {
        f64 d2;
        SaveId saveId;
        ecs::EntityId entity;
    };
    std::vector<Hit> hits;
    m_world->spatial().forEachInRadius(Vec2{static_cast<f32>(x), static_cast<f32>(y)}, static_cast<f32>(r),
                                       [&](const world::SpatialEntry& e) {
                                           if (!m_world->registry().alive(e.entity)) {
                                               return;
                                           }
                                           const f64 dx = static_cast<f64>(e.position.x) - x;
                                           const f64 dy = static_cast<f64>(e.position.y) - y;
                                           hits.push_back({dx * dx + dy * dy, e.saveId, e.entity});
                                       });
    const usize take = std::min(hits.size(), static_cast<usize>(k));
    std::partial_sort(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(take), hits.end(),
                      [](const Hit& a, const Hit& b) { return a.d2 != b.d2 ? a.d2 < b.d2 : a.saveId < b.saveId; });
    vm::HostResult out;
    out.extraFuel = hits.size(); // 후보마다 연료 1
    if (!many) {
        out.value = take == 0 ? vm::Value::null() : entityHandle(hits[0].entity);
        return out;
    }
    out.values.reserve(take);
    for (usize i = 0; i < take; ++i) {
        out.values.push_back(entityHandle(hits[i].entity));
    }
    return out;
}

vm::HostResult ScriptHost::inBox(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const f64 x0 = args[1].value.asF64();
    const f64 y0 = args[2].value.asF64();
    const f64 x1 = args[3].value.asF64();
    const f64 y1 = args[4].value.asF64();
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) || x1 < x0 || y1 < y0 ||
        x1 - x0 > kMaxBoxExtent || y1 - y0 > kMaxBoxExtent) {
        return fail(
            std::format("inBox({}, {}, {}, {}) — 유한하고 min ≤ max, 한 변 {} 까지", x0, y0, x1, y1, kMaxBoxExtent));
    }
    std::vector<std::pair<SaveId, ecs::EntityId>> found;
    bool tooMany = false;
    m_world->spatial().forEachInAABB(Vec2{static_cast<f32>(x0), static_cast<f32>(y0)},
                                     Vec2{static_cast<f32>(x1), static_cast<f32>(y1)},
                                     [&](const world::SpatialEntry& e) {
                                         if (!m_world->registry().alive(e.entity)) {
                                             return;
                                         }
                                         if (found.size() >= kMaxNearEntities) {
                                             tooMany = true;
                                             return;
                                         }
                                         found.emplace_back(e.saveId, e.entity);
                                     });
    if (tooMany) {
        return fail(std::format("inBox 결과가 {} 개를 넘는다 — 사각형을 줄인다", kMaxNearEntities));
    }
    std::ranges::sort(found);
    vm::HostResult out;
    out.values.reserve(found.size());
    for (const auto& [id, e] : found) {
        out.values.push_back(entityHandle(e));
    }
    out.extraFuel = found.size();
    return out;
}

vm::HostResult ScriptHost::requestPath(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (이 호출에서 spawn 한 개체는 다음 틱부터)");
    }
    const f64 x = args[2].value.asF64();
    const f64 y = args[3].value.asF64();
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return fail(std::format("requestPath 의 목표 ({}, {}) 가 유한하지 않다", x, y));
    }
    if (m_world->registry().tryRead<comp::Path>(e) == nullptr) {
        return {true, vm::Value::boolean(false), {}, {}, 0}; // ai.path 가 없다 (Prefab 에 붙인다)
    }
    if (m_pathOps.size() >= kMaxPathOpsPerCall) {
        return fail(std::format("경로 요청 · 취소가 호출마다 {} 개를 넘는다", kMaxPathOpsPerCall));
    }
    m_pathOps.push_back({e, false, Vec2{static_cast<f32>(x), static_cast<f32>(y)}});
    return {true, vm::Value::boolean(true), {}, {}, 0};
}

vm::HostResult ScriptHost::pathState(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다");
    }
    const comp::Path* p = m_world->registry().tryRead<comp::Path>(e);
    // 지금 월드의 상태 (이 호출의 requestPath 는 호출 끝에 반영 — 다음 호출부터 보인다)
    return {true, vm::Value::i32v(p == nullptr ? -1 : static_cast<i32>(p->state)), {}, {}, 0};
}

vm::HostResult ScriptHost::pathPoints(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다");
    }
    vm::HostResult out;
    if (const comp::Path* p = m_world->registry().tryRead<comp::Path>(e)) {
        for (const Vec2 w : p->waypoints) {
            out.values.push_back(vm::Value::f64v(static_cast<f64>(w.x)));
            out.values.push_back(vm::Value::f64v(static_cast<f64>(w.y)));
        }
    }
    out.extraFuel = out.values.size() / 2;
    return out;
}

vm::HostResult ScriptHost::cancelPath(std::span<const vm::HostArg> args) {
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다");
    }
    if (m_pathOps.size() >= kMaxPathOpsPerCall) {
        return fail(std::format("경로 요청 · 취소가 호출마다 {} 개를 넘는다", kMaxPathOpsPerCall));
    }
    m_pathOps.push_back({e, true, {}});
    return {};
}

// 선분 (x0, y0) → (x1, y1) 이 처음 닿는 충돌 원. 후보 = 선분의 사각형을 충돌 반지름 상한만큼 넓힌 색인 칸, 위치는
// 단계 4 색인 (틱 처음), 반지름은 지금 값. 매개변수 t (0 ~ 1) 가 작은 것, 같으면 saveId 작은 것 (결정적)
vm::HostResult ScriptHost::raycast(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const f64 x0 = args[1].value.asF64();
    const f64 y0 = args[2].value.asF64();
    const f64 x1 = args[3].value.asF64();
    const f64 y1 = args[4].value.asF64();
    const f64 dx = x1 - x0;
    const f64 dy = y1 - y0;
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) ||
        dx * dx + dy * dy > kMaxRayLength * kMaxRayLength) {
        return fail(std::format("raycast({}, {}, {}, {}) — 유한하고 길이 {} 까지", x0, y0, x1, y1, kMaxRayLength));
    }
    const f64 pad = comp::kMaxColliderRadius;
    const Vec2 lo{static_cast<f32>(std::min(x0, x1) - pad), static_cast<f32>(std::min(y0, y1) - pad)};
    const Vec2 hi{static_cast<f32>(std::max(x0, x1) + pad), static_cast<f32>(std::max(y0, y1) + pad)};
    const ecs::Registry& reg = m_world->registry();
    f64 bestT = 2.0;
    SaveId bestId = kInvalidSaveId;
    ecs::EntityId best{};
    u64 candidates = 0;
    m_world->spatial().forEachInAABB(lo, hi, [&](const world::SpatialEntry& e) {
        if (!reg.alive(e.entity)) {
            return;
        }
        const comp::Collider* c = reg.tryRead<comp::Collider>(e.entity);
        if (c == nullptr) {
            return;
        }
        ++candidates;
        // |p0 + t·d − c|² = r² 의 작은 근 (시작점이 원 안이면 t = 0)
        const f64 fx = x0 - static_cast<f64>(e.position.x);
        const f64 fy = y0 - static_cast<f64>(e.position.y);
        const f64 r = static_cast<f64>(c->radius);
        const f64 cc = fx * fx + fy * fy - r * r;
        f64 t = 0.0;
        if (cc > 0.0) {
            const f64 a = dx * dx + dy * dy;
            const f64 b = 2.0 * (fx * dx + fy * dy);
            const f64 disc = b * b - 4.0 * a * cc;
            if (a == 0.0 || disc < 0.0) {
                return;
            }
            t = (-b - std::sqrt(disc)) / (2.0 * a);
            if (t < 0.0 || t > 1.0) {
                return;
            }
        }
        if (t < bestT || (t == bestT && e.saveId < bestId)) {
            bestT = t;
            bestId = e.saveId;
            best = e.entity;
        }
    });
    vm::HostResult out;
    out.extraFuel = candidates;
    out.value = bestT <= 1.0 ? entityHandle(best) : vm::Value::null();
    return out;
}

vm::HostResult ScriptHost::overlap(std::span<const vm::HostArg> args) {
    if (m_world == nullptr) {
        return fail("이 호출에는 월드가 없다");
    }
    const f64 x = args[1].value.asF64();
    const f64 y = args[2].value.asF64();
    const f64 r = args[3].value.asF64();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(r) || r < 0 || r > kMaxNearRadius) {
        return fail(std::format("overlap({}, {}, {}) — 유한한 위치 · 반경 0 ~ {}", x, y, r, kMaxNearRadius));
    }
    const ecs::Registry& reg = m_world->registry();
    const f32 reach = static_cast<f32>(r + comp::kMaxColliderRadius);
    std::vector<std::pair<SaveId, ecs::EntityId>> found;
    bool tooMany = false;
    u64 candidates = 0;
    m_world->spatial().forEachInRadius(Vec2{static_cast<f32>(x), static_cast<f32>(y)}, reach,
                                       [&](const world::SpatialEntry& e) {
                                           if (!reg.alive(e.entity)) {
                                               return;
                                           }
                                           const comp::Collider* c = reg.tryRead<comp::Collider>(e.entity);
                                           if (c == nullptr) {
                                               return;
                                           }
                                           ++candidates;
                                           const f64 dx = static_cast<f64>(e.position.x) - x;
                                           const f64 dy = static_cast<f64>(e.position.y) - y;
                                           const f64 rr = r + static_cast<f64>(c->radius);
                                           if (dx * dx + dy * dy > rr * rr) {
                                               return;
                                           }
                                           if (found.size() >= kMaxNearEntities) {
                                               tooMany = true;
                                               return;
                                           }
                                           found.emplace_back(e.saveId, e.entity);
                                       });
    if (tooMany) {
        return fail(std::format("overlap 결과가 {} 개를 넘는다 — 반경을 줄인다", kMaxNearEntities));
    }
    std::ranges::sort(found);
    vm::HostResult out;
    out.values.reserve(found.size());
    for (const auto& [id, e] : found) {
        out.values.push_back(entityHandle(e));
    }
    out.extraFuel = candidates;
    return out;
}

} // namespace sbx::script
