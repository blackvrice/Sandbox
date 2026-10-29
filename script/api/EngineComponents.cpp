#include "script/api/EngineComponents.hpp"

#include <algorithm>

namespace sbx::script::api {

const EngineComponentAccess* findEngineComponent(std::string_view id) noexcept {
    for (const EngineComponentAccess& e : kEngineComponents) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const EngineComponentAccess* findEngineComponentByScriptName(std::string_view name) noexcept {
    for (const EngineComponentAccess& e : kEngineComponents) {
        if (e.scriptName == name) {
            return &e;
        }
    }
    return nullptr;
}

ComponentSchema engineComponentSchema(const EngineComponentAccess& entry) {
    ComponentSchema s;
    s.id = std::string(entry.id);
    s.version = 1;
    s.persistent = false; // 스크립트가 정의하지 않는다 — 카탈로그 쪽 플래그가 진짜 (등록하지 않는다)
    s.replicated = entry.read == EngineRead::Both;
    s.editorVisible = false;
    s.serverOnly = entry.read == EngineRead::Server;
    for (const EngineFieldAccess& f : entry.fields) {
        ComponentFieldSchema fs;
        fs.name = std::string(f.name);
        fs.kind = f.kind;
        s.fields.push_back(std::move(fs));
    }
    std::ranges::sort(s.fields, [](const auto& a, const auto& b) { return a.name < b.name; });
    s.module = "sandbox";
    return s;
}

std::string_view engineReadName(EngineRead r) noexcept {
    return r == EngineRead::Both ? "server · client" : "server";
}

} // namespace sbx::script::api
