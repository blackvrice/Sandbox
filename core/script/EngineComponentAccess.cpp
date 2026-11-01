#include "core/script/EngineComponentAccess.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <type_traits>

#include "core/components/core/Movement.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/life/Life.hpp"
#include "foundation/hash/Fnv1a.hpp"

namespace sbx::script {

// 쓰기 버퍼는 컴포넌트를 바이트로 복사한다 (ScriptHost::write) — 표에서 쓰기를 허락한 내장 컴포넌트는 그래도 된다
static_assert(std::is_trivially_copyable_v<comp::Transform> && std::is_trivially_copyable_v<comp::Velocity> &&
              std::is_trivially_copyable_v<comp::Movement> && std::is_trivially_copyable_v<comp::Health> &&
              std::is_trivially_copyable_v<comp::Energy>);
constexpr std::array<std::string_view, 5> kCopyableWritable{"core.movement", "core.transform", "core.velocity",
                                                            "life.energy", "life.health"};

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

bool applyEngineFieldValues(const api::EngineComponentAccess& entry, const ecs::ComponentInfo& info, void* component,
                            std::span<const vm::Value> slots, const world::WorldGrid* clampToWorld,
                            std::string& error) {
    if (entry.write != api::EngineWrite::Server) {
        error = std::format("엔진 컴포넌트 '{}' 는 스크립트가 쓰지 않는다 (엔진 관리 — 접근 표)", entry.id);
        return false;
    }
    // f32 로 저장되는 값 — 유한하고 f32 범위 안 (넘치면 inf 가 된다)
    const auto finiteF32 = [](f64 v) {
        return std::isfinite(v) && std::fabs(v) <= static_cast<f64>(std::numeric_limits<f32>::max());
    };
    ecs::Json patch = ecs::Json::object();
    usize k = 0;
    for (const api::EngineFieldAccess& f : entry.fields) {
        const usize need = f.kind == api::ComponentFieldKind::Vec2 ? 2 : 1;
        if (k + need > slots.size()) {
            error = std::format("{}: 값의 칸이 모자란다", entry.id);
            return false;
        }
        const std::string where = std::format("{}.{}", entry.id, f.name);
        switch (f.kind) {
        case api::ComponentFieldKind::I32:
        case api::ComponentFieldKind::Enum:
            patch[std::string(f.name)] = slots[k].asI32();
            break;
        case api::ComponentFieldKind::F32:
        case api::ComponentFieldKind::F64: {
            const f64 x = slots[k].asF64();
            if (!finiteF32(x)) {
                error = std::format("{} = {} — 유한한 f32 값이 아니다", where, x);
                return false;
            }
            patch[std::string(f.name)] = x;
            break;
        }
        case api::ComponentFieldKind::Bool:
            patch[std::string(f.name)] = slots[k].asBool();
            break;
        case api::ComponentFieldKind::Vec2: {
            const f64 x = slots[k].asF64();
            const f64 y = slots[k + 1].asF64();
            if (!finiteF32(x) || !finiteF32(y)) {
                error = std::format("{} = ({}, {}) — 유한한 f32 값이 아니다", where, x, y);
                return false;
            }
            Vec2 v{static_cast<f32>(x), static_cast<f32>(y)};
            if (f.rule == api::EngineFieldRule::ClampToWorld && clampToWorld != nullptr) {
                v = clampToWorld->clampPoint(v); // 경계 밖은 자른다 — 오류가 아니다 (20 3.1)
            }
            patch[std::string(f.name)] = ecs::Json::array({static_cast<f64>(v.x), static_cast<f64>(v.y)});
            break;
        }
        case api::ComponentFieldKind::Entity:
            error = std::format("{}: 개체 참조 필드는 표에 없다", where);
            return false;
        }
        k += need;
    }
    if (k != slots.size()) {
        error = std::format("{}: 값의 칸이 남는다 (모양이 컴포넌트와 다르다)", entry.id);
        return false;
    }
    // FieldMeta 범위 · 타입은 카탈로그가 본다 — 복사본에 적용해 전부 맞을 때만 덮어쓴다
    if (auto r = info.patchJson(component, patch, entry.id); !r) {
        error = r.error().context.empty() ? r.error().message
                                          : std::format("{} ({})", r.error().message, r.error().context);
        return false;
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
        if (e.write == api::EngineWrite::Server &&
            std::ranges::find(kCopyableWritable, e.id) == kCopyableWritable.end()) {
            problems.push_back(
                std::format("{}: 쓰기를 허락했는데 static_assert (trivially copyable) 목록에 없다", e.id));
        }
        for (const api::EngineFieldAccess& f : e.fields) {
            if (f.rule == api::EngineFieldRule::ClampToWorld && f.kind != api::ComponentFieldKind::Vec2) {
                problems.push_back(std::format("{}: ClampToWorld 는 Vec2 필드만 ('{}')", e.id, f.name));
            }
        }
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
