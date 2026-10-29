#include "core/script/ScriptHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <optional>

#include "core/components/core/Identity.hpp"
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

vm::Value ScriptHost::beginCall(sim::Tick tick, std::string script, sim::SimulationWorld* world) {
    m_token = kCtxTag | (m_nextToken++ & 0x0000'FFFF'FFFF'FFFFull);
    m_tick = tick;
    m_script = std::move(script);
    m_world = world;
    m_handles.clear();
    m_handleOf.clear();
    m_writes.clear();
    m_writeOf.clear();
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
    }
    m_token = 0;
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

vm::HostResult ScriptHost::write(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 죽은 개체)");
    }
    if (info->dynamicLayout == nullptr) { // 검사기가 S404 로 막는다 — 손으로 만든 바이트코드를 위해 한 번 더
        return fail(std::format("엔진 컴포넌트 '{}' 는 아직 읽기만 한다 (쓰기 규칙은 v0.5 M2-T2)", info->name));
    }
    const std::byte* current = currentBytes(e, *info);
    if (current == nullptr) {
        return fail(std::format("개체에 '{}' 가 없다 — 쓰기는 있는 컴포넌트의 값만 (붙이기는 [계획])", info->name));
    }
    const ecs::DynamicComponentLayout& layout = *info->dynamicLayout;
    std::vector<std::byte> bytes(current, current + layout.stride());
    const std::span<const vm::Value> v = args[3].fields;
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
        std::byte* p = bytes.data() + f.offset;
        const std::string where = std::format("{}.{}", info->name, f.name);
        const auto a = next();
        if (!a) {
            return fail(std::format("{}: 값의 칸이 모자란다", info->name));
        }
        switch (f.kind) {
        case ecs::DynamicFieldKind::I32:
        case ecs::DynamicFieldKind::Enum: {
            const i32 x = a->asI32();
            if (!inRange(f, static_cast<f64>(x))) {
                return fail(std::format("{} = {} 이 범위 [{}, {}] 밖이다", where, x, f.meta.min, f.meta.max));
            }
            store(p, x);
            break;
        }
        case ecs::DynamicFieldKind::F32:
        case ecs::DynamicFieldKind::F64: {
            const f64 x = a->asF64();
            const bool f32Kind = f.kind == ecs::DynamicFieldKind::F32;
            if (f32Kind ? !fitsF32(x) : !std::isfinite(x)) {
                return fail(std::format("{} = {} — 유한한 {} 값이 아니다", where, x, f32Kind ? "f32" : "f64"));
            }
            const f64 stored = f32Kind ? static_cast<f64>(static_cast<f32>(x)) : x; // f32 는 한 번 반올림 (ADR-0059 2)
            if (!inRange(f, stored)) {
                return fail(std::format("{} = {} 이 범위 [{}, {}] 밖이다", where, stored, f.meta.min, f.meta.max));
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
                return fail(std::format("{}: 값의 칸이 모자란다", info->name));
            }
            const f64 x = a->asF64();
            const f64 y = b->asF64();
            if (!fitsF32(x) || !fitsF32(y)) {
                return fail(std::format("{} = ({}, {}) — 유한한 f32 값이 아니다", where, x, y));
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
                    return fail(
                        std::format("{}: 개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 죽은 개체)", where));
                }
                id = pid->saveId;
            }
            store(p, id);
            break;
        }
        }
    }
    if (k != v.size()) {
        return fail(std::format("{}: 값의 칸이 남는다 (모양이 컴포넌트와 다르다)", info->name));
    }
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
}

} // namespace sbx::script
