#include "core/script/EngineComponentAccess.hpp"

#include <algorithm>
#include <format>
#include <limits>

#include "foundation/hash/Fnv1a.hpp"

namespace sbx::script {

namespace {

// JSON 값 → 칸 (종류가 맞지 않으면 false). 엔진의 f32 는 JSON 에 f64 로 정확히 들어간다 (JsonVisitor)
bool appendValue(api::ComponentFieldKind kind, const ecs::Json& j, std::vector<vm::Value>& out) {
    switch (kind) {
    case api::ComponentFieldKind::I32:
    case api::ComponentFieldKind::Enum:
        // u8 · u32 · Tick — i32 범위만 (Tick 이 2^31 을 넘는 월드는 [계획] — 30 TPS 로 2 년)
        if (!j.is_number_integer()) {
            return false;
        }
        {
            const i64 v = j.get<i64>();
            if (v < std::numeric_limits<i32>::min() || v > std::numeric_limits<i32>::max()) {
                return false;
            }
            out.push_back(vm::Value::i32v(static_cast<i32>(v)));
        }
        return true;
    case api::ComponentFieldKind::F32:
    case api::ComponentFieldKind::F64:
        if (!j.is_number()) {
            return false;
        }
        out.push_back(vm::Value::f64v(j.get<f64>()));
        return true;
    case api::ComponentFieldKind::Bool:
        if (!j.is_boolean()) {
            return false;
        }
        out.push_back(vm::Value::boolean(j.get<bool>()));
        return true;
    case api::ComponentFieldKind::Vec2:
        if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
            return false;
        }
        out.push_back(vm::Value::f64v(j[0].get<f64>()));
        out.push_back(vm::Value::f64v(j[1].get<f64>()));
        return true;
    case api::ComponentFieldKind::Entity:
        break;
    }
    return false;
}

} // namespace

const api::EngineComponentAccess* engineAccessOf(const ecs::ComponentInfo& info) noexcept {
    return info.dynamicLayout == nullptr ? api::findEngineComponent(info.name) : nullptr;
}

bool appendEngineFieldValues(const api::EngineComponentAccess& entry, const ecs::ComponentInfo& info,
                             const void* component, std::vector<vm::Value>& out, std::string& error) {
    ecs::Json j;
    info.writeJson(component, j);
    // 표의 필드는 이름 순 (EngineComponents.hpp) — 칸 순서 = 스키마 순서
    for (const api::EngineFieldAccess& f : entry.fields) {
        const auto it = j.find(f.name);
        if (it == j.end() || !appendValue(f.kind, *it, out)) {
            error = std::format("엔진 컴포넌트 {} 의 필드 '{}' 를 {} 로 읽을 수 없다", entry.id, f.name,
                                api::componentFieldKindName(f.kind));
            return false;
        }
    }
    return true;
}

std::vector<std::string> checkEngineAccessTable(const ecs::ComponentCatalog& catalog) {
    std::vector<std::string> problems;
    std::string_view previousId;
    for (const api::EngineComponentAccess& e : api::kEngineComponents) {
        if (!previousId.empty() && !(previousId < e.id)) {
            problems.push_back(std::format("{}: 표가 id 순이 아니다", e.id));
        }
        previousId = e.id;
        for (usize i = 1; i < e.fields.size(); ++i) {
            if (!(e.fields[i - 1].name < e.fields[i].name)) {
                problems.push_back(std::format("{}: 필드가 이름 순이 아니다 ('{}')", e.id, e.fields[i].name));
            }
        }
        const ecs::ComponentInfo* info = catalog.find(e.id);
        if (info == nullptr) {
            problems.push_back(std::format("{}: 카탈로그에 없다", e.id));
            continue;
        }
        if (info->dynamicLayout != nullptr) {
            problems.push_back(std::format("{}: 내장 컴포넌트가 아니다", e.id));
            continue;
        }
        if (info->stableId != fnv1a64(e.id)) {
            problems.push_back(std::format("{}: stableId 가 fnv1a64(id) 가 아니다", e.id));
        }
        if (e.read == api::EngineRead::Both && !ecs::hasFlag(info->flags, ecs::ComponentFlags::Replicated)) {
            problems.push_back(std::format("{}: 클라이언트 읽기인데 복제하지 않는다", e.id));
        }
        // 필드가 카탈로그의 reflect 목록에 있고, 수치 종류 (I32 · F32) 는 수치 필드다 (Bool · Vec2 는 아니다). 값으로
        // 읽히는지는 시험이 실제 월드로 본다 (appendEngineFieldValues)
        for (const api::EngineFieldAccess& f : e.fields) {
            const auto it = std::ranges::find(info->fields, f.name, &ecs::FieldDesc::name);
            if (it == info->fields.end()) {
                problems.push_back(std::format("{}: 필드 '{}' 가 카탈로그에 없다", e.id, f.name));
                continue;
            }
            const bool numericKind = f.kind == api::ComponentFieldKind::I32 || f.kind == api::ComponentFieldKind::F32 ||
                                     f.kind == api::ComponentFieldKind::F64;
            if (numericKind != it->numeric) {
                problems.push_back(std::format("{}: 필드 '{}' 의 종류 {} 가 카탈로그와 다르다", e.id, f.name,
                                               api::componentFieldKindName(f.kind)));
            }
        }
    }
    return problems;
}

} // namespace sbx::script
