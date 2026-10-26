#pragma once
// 스크립트 컴포넌트 값 (바이트) → VM 칸 — 서버 ScriptHost · 클라이언트 ClientScriptHost 가 같이 쓴다 (ADR-0060 · 0067).
// 칸 차례 = VM 의 구조체 펼치기: 필드 이름 순, vec2 = x · y 둘, 개체 참조 = 핸들 · null (entityRef 가 정한다)

#include <cstddef>
#include <cstring>
#include <vector>

#include "core/ecs/DynamicComponent.hpp"
#include "script/vm/Interpreter.hpp"

namespace sbx::script {

template <class F>
F loadField(const std::byte* p) noexcept {
    F v{};
    std::memcpy(&v, p, sizeof(F));
    return v;
}

// entityRef(SaveId) → vm::Value (핸들 또는 null)
template <class EntityRef>
void appendFieldValues(const ecs::DynamicComponentLayout& layout, const std::byte* bytes, std::vector<vm::Value>& out,
                       EntityRef&& entityRef) {
    for (const ecs::DynamicComponentLayout::Field& f : layout.fields()) {
        const std::byte* p = bytes + f.offset;
        switch (f.kind) {
        case ecs::DynamicFieldKind::I32:
        case ecs::DynamicFieldKind::Enum:
            out.push_back(vm::Value::i32v(loadField<i32>(p)));
            break;
        case ecs::DynamicFieldKind::F32:
            out.push_back(vm::Value::f64v(static_cast<f64>(loadField<f32>(p))));
            break;
        case ecs::DynamicFieldKind::F64:
            out.push_back(vm::Value::f64v(loadField<f64>(p)));
            break;
        case ecs::DynamicFieldKind::Bool:
            out.push_back(vm::Value::boolean(loadField<bool>(p)));
            break;
        case ecs::DynamicFieldKind::Vec2: {
            const Vec2 v = loadField<Vec2>(p);
            out.push_back(vm::Value::f64v(static_cast<f64>(v.x)));
            out.push_back(vm::Value::f64v(static_cast<f64>(v.y)));
            break;
        }
        case ecs::DynamicFieldKind::EntityRef:
            out.push_back(entityRef(loadField<u64>(p)));
            break;
        }
    }
}

} // namespace sbx::script
