#include "core/script/ScriptComponents.hpp"

#include <format>

namespace sbx::script {

namespace {

ecs::DynamicFieldKind toKind(api::ComponentFieldKind k) noexcept {
    switch (k) {
    case api::ComponentFieldKind::I32:
        return ecs::DynamicFieldKind::I32;
    case api::ComponentFieldKind::F32:
        return ecs::DynamicFieldKind::F32;
    case api::ComponentFieldKind::F64:
        return ecs::DynamicFieldKind::F64;
    case api::ComponentFieldKind::Bool:
        return ecs::DynamicFieldKind::Bool;
    case api::ComponentFieldKind::Vec2:
        return ecs::DynamicFieldKind::Vec2;
    case api::ComponentFieldKind::Entity:
        return ecs::DynamicFieldKind::EntityRef;
    case api::ComponentFieldKind::Enum:
        return ecs::DynamicFieldKind::Enum;
    }
    return ecs::DynamicFieldKind::I32;
}

} // namespace

ecs::DynamicComponentDesc toDynamicDesc(const api::ComponentSchema& schema) {
    using F = ecs::ComponentFlags;
    ecs::DynamicComponentDesc d;
    d.name = schema.id;
    d.version = schema.version;
    F flags = F::None;
    if (schema.persistent) {
        flags = flags | F::Persistent;
    }
    if (schema.replicated) {
        flags = flags | F::Replicated;
    }
    if (schema.editorVisible) {
        flags = flags | F::EditorVisible;
    }
    if (schema.serverOnly) {
        flags = flags | F::ServerOnly;
    }
    d.flags = flags;
    for (const api::ComponentFieldSchema& f : schema.fields) {
        ecs::DynamicFieldDesc fd;
        fd.name = f.name;
        fd.kind = toKind(f.kind);
        fd.defaultValue = f.defaultValue;
        fd.defaultY = f.defaultY;
        fd.hasRange = f.hasRange;
        fd.min = f.min;
        fd.max = f.max;
        d.fields.push_back(std::move(fd));
    }
    return d;
}

std::string ScriptComponentProblem::describe() const {
    return std::format("{}:{}: 컴포넌트 '{}' — {}", module, line, id, message);
}

std::vector<ScriptComponentProblem> registerScriptComponents(ecs::ComponentCatalog& catalog,
                                                             std::span<const api::ComponentSchema> schemas) {
    std::vector<ScriptComponentProblem> problems;
    auto problem = [&](const api::ComponentSchema& s, std::string message) {
        problems.push_back(ScriptComponentProblem{s.module, s.line, s.column, s.id, std::move(message)});
    };
    for (const api::ComponentSchema& s : schemas) {
        if (api::isEngineComponentNamespace(s.id)) {
            problem(s, "엔진 이름공간이다");
            continue;
        }
        if (const ecs::ComponentInfo* existing = catalog.find(s.id)) {
            problem(
                s, std::format("이미 있다 ({})", existing->dynamicLayout ? "다른 스크립트 컴포넌트" : "엔진 컴포넌트"));
            continue;
        }
        if (auto r = catalog.addDynamic(toDynamicDesc(s)); !r) {
            problem(s, r.error().message);
        }
    }
    return problems;
}

} // namespace sbx::script
