#include "core/script/ClientScriptHost.hpp"

#include <algorithm>
#include <format>

#include "core/components/core/Identity.hpp"
#include "core/script/EngineComponentAccess.hpp"
#include "core/script/ScriptValues.hpp"
#include "foundation/assert/Assert.hpp"

namespace sbx::script {

namespace {

using api::AbiType;

// 서버 ScriptHost 와 다른 표지 — 서버 핸들을 클라이언트에 넘겨도 (또는 그 반대) 맞지 않는다
constexpr u64 kCtxTag = 0xC1C7'0000'0000'0000ull;
constexpr u64 kEntityTag = 0xC1E0'0000'0000'0000ull;

vm::HostResult fail(std::string message) {
    return {false, {}, std::move(message), {}, 0};
}

// UTF-8 글자 가운데를 자르지 않고 max 바이트까지
std::string clip(std::string_view text, usize max) {
    if (text.size() <= max) {
        return std::string(text);
    }
    usize n = max;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) {
        --n; // text[n] 이 이어지는 바이트면 그 글자의 처음까지 물린다
    }
    return std::string(text.substr(0, n)) + "…";
}

vm::HostResult clientTick(ClientScriptHost& host, std::span<const vm::HostArg>) {
    return {true, vm::Value::i32v(static_cast<i32>(host.frame().tick)), {}, {}, 0};
}
vm::HostResult clientFrame(ClientScriptHost& host, std::span<const vm::HostArg>) {
    return {true, vm::Value::i32v(static_cast<i32>(host.frame().frame)), {}, {}, 0};
}
vm::HostResult clientDt(ClientScriptHost& host, std::span<const vm::HostArg>) {
    return {true, vm::Value::f64v(host.frame().dt), {}, {}, 0};
}
vm::HostResult clientRead(ClientScriptHost& host, std::span<const vm::HostArg> args) {
    return host.read(args);
}
vm::HostResult clientHas(ClientScriptHost& host, std::span<const vm::HostArg> args) {
    return host.has(args);
}
vm::HostResult clientQuery(ClientScriptHost& host, std::span<const vm::HostArg> args) {
    return host.query(args);
}
vm::HostResult clientLabel(ClientScriptHost& host, std::span<const vm::HostArg> args) {
    return host.label(args);
}
vm::HostResult clientPrint(ClientScriptHost& host, std::span<const vm::HostArg> args) {
    return host.print(args);
}

constexpr ClientHostBinding kBindings[] = {
    {"client.tick", AbiType::I32, {AbiType::ClientContext}, 1, &clientTick},
    {"client.frame", AbiType::I32, {AbiType::ClientContext}, 1, &clientFrame},
    {"client.dt", AbiType::Number, {AbiType::ClientContext}, 1, &clientDt},
    {"client.read",
     AbiType::ComponentDataOrNull,
     {AbiType::ClientContext, AbiType::Entity, AbiType::Component},
     3,
     &clientRead},
    {"client.has", AbiType::Bool, {AbiType::ClientContext, AbiType::Entity, AbiType::Component}, 3, &clientHas},
    {"client.query", AbiType::EntityList, {AbiType::ClientContext, AbiType::Component}, 2, &clientQuery},
    {"client.label", AbiType::Void, {AbiType::ClientContext, AbiType::Entity, AbiType::String}, 3, &clientLabel},
    {"client.print", AbiType::Void, {AbiType::ClientContext, AbiType::String}, 2, &clientPrint},
};

} // namespace

std::span<const ClientHostBinding> clientHostBindings() {
    return kBindings;
}

std::vector<std::string> checkClientHostBindings(std::span<const ClientHostBinding> bindings) {
    const std::vector<HostBindingShape> shapes = hostBindingShapes(bindings);
    return checkHostBindingShapes(shapes, api::HostDomain::Client);
}

ClientScriptHost::ClientScriptHost() {
    SBX_ASSERT(checkClientHostBindings(clientHostBindings()).empty(),
               "클라이언트 Host 바인딩이 Host API 표와 맞지 않는다 (checkClientHostBindings)");
    m_byHostIndex.assign(api::kHostFunctions.size(), nullptr);
    for (const ClientHostBinding& b : clientHostBindings()) {
        if (const api::HostFunction* f = api::findHostFunction(b.id)) {
            m_byHostIndex[api::hostFunctionIndex(*f)] = &b;
        }
    }
}

void ClientScriptHost::beginFrame(const ClientScriptFrame& frame) {
    m_frame = frame;
    m_labels.clear();
    m_lines.clear();
}

vm::Value ClientScriptHost::beginCall(std::string system) {
    m_token = kCtxTag | (m_nextToken++ & 0x0000'FFFF'FFFF'FFFFull);
    m_system = std::move(system);
    m_labelsAtCall = m_labels.size();
    m_linesAtCall = m_lines.size();
    m_handles.clear();
    m_handleOf.clear();
    return vm::Value::opaque(m_token);
}

void ClientScriptHost::endCall(bool ok) {
    if (!ok) {
        m_labels.resize(m_labelsAtCall);
        m_lines.resize(m_linesAtCall);
    }
    m_token = 0;
    m_handles.clear();
    m_handleOf.clear();
}

vm::HostResult ClientScriptHost::call(const api::HostFunction& fn, std::span<const vm::HostArg> args) {
    const usize index = api::hostFunctionIndex(fn);
    const ClientHostBinding* b = index < m_byHostIndex.size() ? m_byHostIndex[index] : nullptr;
    if (b == nullptr) {
        return fail(std::format("{} 의 클라이언트 구현이 없다 (서버 항목은 client 시스템에서 부를 수 없다)", fn.id));
    }
    if (m_token == 0 || args.empty() || args[0].value.bits != m_token) {
        return fail("ctx 가 지금 호출의 것이 아니다 (지난 호출의 ctx · 위조한 핸들)");
    }
    return b->impl(*this, args);
}

vm::Value ClientScriptHost::entityHandle(ecs::EntityId e) {
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

ecs::EntityId ClientScriptHost::entityOf(vm::Value handle) const {
    const u64 h = handle.bits;
    if ((h & 0xFFFF'0000'0000'0000ull) != kEntityTag || ((h >> 32) & 0xFFFFull) != (m_token & 0xFFFFull)) {
        return ecs::kNullEntity;
    }
    const u64 slot = h & 0xFFFF'FFFFull;
    if (slot == 0 || slot > m_handles.size() || m_frame.registry == nullptr) {
        return ecs::kNullEntity;
    }
    const ecs::EntityId e = m_handles[static_cast<usize>(slot - 1)];
    return m_frame.registry->alive(e) ? e : ecs::kNullEntity;
}

const ecs::ComponentInfo* ClientScriptHost::componentOf(vm::Value ref, std::string& error) const {
    if (m_frame.registry == nullptr || m_frame.catalog == nullptr) {
        error = "이 프레임에는 복제 월드가 없다";
        return nullptr;
    }
    const ecs::ComponentInfo* info = m_frame.catalog->find(ref.bits);
    if (info == nullptr) {
        error = std::format("모르는 컴포넌트 ({:016x}) — 이 월드의 콘텐츠가 정의하지 않았다", ref.bits);
        return nullptr;
    }
    // 엔진 컴포넌트는 접근 표에서 클라이언트 읽기 (Both) 인 것만 (v0.5 M2-T1 — 복제하는 것)
    if (info->dynamicLayout == nullptr) {
        const api::EngineComponentAccess* engine = engineAccessOf(*info);
        if (engine == nullptr || engine->read != api::EngineRead::Both) {
            error = std::format("엔진 컴포넌트 '{}' 는 클라이언트 스크립트가 읽지 않는다 (접근 표 — 서버만 · 없음)",
                                info->name);
            return nullptr;
        }
    }
    return info;
}

vm::HostResult ClientScriptHost::read(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 사라진 개체)");
    }
    const ecs::ComponentPoolBase* pool = m_frame.registry->poolByStableId(info->stableId);
    const auto* bytes = pool != nullptr ? static_cast<const std::byte*>(pool->getRaw(e)) : nullptr;
    if (bytes == nullptr) {
        return {true, vm::Value::null(), {}, {}, 0}; // 복제되지 않는 컴포넌트 (serverOnly) 도 여기
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
    // 개체 참조 필드는 null — 복제 월드의 saveId → 개체 풀이는 [계획]
    appendFieldValues(*info->dynamicLayout, bytes, r.values, [](SaveId) { return vm::Value::null(); });
    return r;
}

vm::HostResult ClientScriptHost::has(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[2].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 사라진 개체)");
    }
    const ecs::ComponentPoolBase* pool = m_frame.registry->poolByStableId(info->stableId);
    return {true, vm::Value::boolean(pool != nullptr && pool->contains(e)), {}, {}, 0};
}

vm::HostResult ClientScriptHost::query(std::span<const vm::HostArg> args) {
    std::string error;
    const ecs::ComponentInfo* info = componentOf(args[1].value, error);
    if (info == nullptr) {
        return fail(std::move(error));
    }
    vm::HostResult r;
    const ecs::Registry& reg = *m_frame.registry;
    const ecs::ComponentPoolBase* pool = reg.poolByStableId(info->stableId);
    if (pool == nullptr) {
        return r;
    }
    if (pool->size() > kMaxQueryEntities) {
        return fail(std::format("질의 결과가 {} 개를 넘는다 ({} 개)", kMaxQueryEntities, pool->size()));
    }
    // netId 오름차순 — 복제 월드의 풀 순서는 받은 차례라 클라이언트마다 다르다
    std::vector<std::pair<NetEntityId, ecs::EntityId>> keyed;
    keyed.reserve(pool->size());
    for (const ecs::EntityId e : pool->entities()) {
        const auto* id = reg.tryRead<comp::NetIdentity>(e);
        if (id != nullptr && id->netId != kInvalidNetEntityId) {
            keyed.emplace_back(id->netId, e);
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

vm::HostResult ClientScriptHost::label(std::span<const vm::HostArg> args) {
    const ecs::EntityId e = entityOf(args[1].value);
    if (!e.valid()) {
        return fail("개체 핸들이 이 호출의 것이 아니다 (지난 호출 · 위조 · 사라진 개체)");
    }
    if (m_labels.size() >= kMaxLabels) {
        ++m_dropped; // 표시는 상태가 아니다 — 버려도 된다
        return {};
    }
    m_labels.push_back({e, clip(args[2].text, kMaxTextBytes)});
    return {};
}

vm::HostResult ClientScriptHost::print(std::span<const vm::HostArg> args) {
    if (m_lines.size() >= kMaxLines) {
        ++m_dropped;
        return {};
    }
    m_lines.push_back({m_system, clip(args[1].text, kMaxTextBytes)});
    return {};
}

} // namespace sbx::script
