#include "script/api/HostApi.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "foundation/hash/Fnv1a.hpp"
#include "script/api/EngineComponents.hpp"

namespace sbx::script::api {

std::string_view abiTypeName(AbiType t) noexcept {
    switch (t) {
    case AbiType::Void:
        return "void";
    case AbiType::Bool:
        return "boolean";
    case AbiType::I32:
        return "i32";
    case AbiType::Number:
        return "number";
    case AbiType::String:
        return "string";
    case AbiType::Entity:
        return "Entity";
    case AbiType::ServerContext:
        return "ServerContext";
    case AbiType::Component:
        return "ComponentRef<T>";
    case AbiType::ComponentData:
        return "T";
    case AbiType::ComponentDataOrNull:
        return "T | null";
    case AbiType::EntityList:
        return "Entity[]";
    case AbiType::ClientContext:
        return "ClientContext";
    }
    return "?";
}

const HostFunction* findHostFunction(std::string_view id) noexcept {
    for (const HostFunction& f : kHostFunctions) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

const HostFunction* findHostMember(AbiType owner, std::string_view member) noexcept {
    for (const HostFunction& f : kHostFunctions) {
        if (f.owner == owner && f.member == member) {
            return &f;
        }
    }
    return nullptr;
}

usize hostFunctionIndex(const HostFunction& f) noexcept {
    return static_cast<usize>(&f - kHostFunctions.data());
}

namespace {
std::string_view domainName(HostDomain d) noexcept {
    return d == HostDomain::Server ? "server" : "client";
}
std::string_view effectName(HostEffect e) noexcept {
    switch (e) {
    case HostEffect::Read:
        return "read";
    case HostEffect::Write:
        return "write";
    case HostEffect::Structural:
        return "structural";
    case HostEffect::Random:
        return "random";
    case HostEffect::Log:
        return "log";
    case HostEffect::Display:
        return "display";
    }
    return "?";
}
} // namespace

std::string canonicalSignature(const HostFunction& f) {
    std::string s = std::format("{} {}.{}", f.id, abiTypeName(f.owner), f.member);
    if (!f.property) {
        s += '(';
        for (u8 i = 0; i < f.paramCount; ++i) {
            if (i > 0) {
                s += ", ";
            }
            s += abiTypeName(f.params[i]);
        }
        s += ')';
    }
    s += std::format(": {} {} {} {}", abiTypeName(f.result), domainName(f.domain), effectName(f.effect), f.fuelBase);
    return s;
}

u64 hostApiFingerprint() {
    std::vector<std::string> lines;
    lines.reserve(kHostFunctions.size());
    for (const HostFunction& f : kHostFunctions) {
        lines.push_back(canonicalSignature(f));
    }
    // 엔진 컴포넌트 접근 표 (v0.5 M2-T1, ADR-0080 9) — 항목 · 필드 · 읽기 도메인이 바뀌면 지문이 바뀐다
    for (const EngineComponentAccess& e : kEngineComponents) {
        std::string l = std::format("engine {} {} {}", e.id, e.scriptName, engineReadName(e.read));
        for (const EngineFieldAccess& f : e.fields) {
            l += std::format(" {}:{}", f.name, componentFieldKindName(f.kind));
        }
        lines.push_back(std::move(l));
    }
    std::sort(lines.begin(), lines.end());
    Fnv1a64 h;
    h.string("SBXHOST").u64le(kHostApiVersion);
    for (const std::string& l : lines) {
        h.string(l).byte(0x0A);
    }
    return h.value();
}

} // namespace sbx::script::api
