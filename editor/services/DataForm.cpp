#include "editor/services/DataForm.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/content/ContentLoader.hpp"
#include "core/content/PrefabDiff.hpp"

namespace sbx::editor {

namespace {

FormFieldKind kindOf(const ecs::FieldDesc& f, const ecs::Json& v) {
    switch (f.hint) {
    case ecs::Hint::EntityRef:
        return FormFieldKind::EntityRef;
    case ecs::Hint::PrefabRef:
        return FormFieldKind::PrefabRef;
    case ecs::Hint::BehaviorRef:
        return FormFieldKind::BehaviorRef;
    default:
        break;
    }
    if (v.is_boolean()) {
        return FormFieldKind::Bool;
    }
    if (v.is_number_integer() || v.is_number_unsigned()) {
        return FormFieldKind::Int;
    }
    if (v.is_number_float()) {
        return FormFieldKind::Number;
    }
    if (v.is_string()) {
        return FormFieldKind::Text;
    }
    if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
        return v[0].is_number_integer() && v[1].is_number_integer() ? FormFieldKind::Vec2i : FormFieldKind::Vec2;
    }
    return FormFieldKind::Other;
}

// 종류에 맞게 고르고 범위로 자른다. 맞지 않으면 nullopt
std::optional<ecs::Json> coerce(const FormField& f, const ecs::Json& v) {
    const auto clamp = [&](f64 x) { return f.meta.hasRange ? std::clamp(x, f.meta.min, f.meta.max) : x; };
    switch (f.kind) {
    case FormFieldKind::Bool:
        return v.is_boolean() ? std::optional<ecs::Json>(v) : std::nullopt;
    case FormFieldKind::Int:
        if (!v.is_number() || !std::isfinite(v.get<f64>())) {
            return std::nullopt;
        }
        return ecs::Json(static_cast<i64>(std::llround(clamp(v.get<f64>()))));
    case FormFieldKind::Number:
        if (!v.is_number() || !std::isfinite(v.get<f64>())) {
            return std::nullopt;
        }
        return ecs::Json(clamp(v.get<f64>()));
    case FormFieldKind::Vec2:
    case FormFieldKind::Vec2i:
        if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number() ||
            !std::isfinite(v[0].get<f64>()) || !std::isfinite(v[1].get<f64>())) {
            return std::nullopt;
        }
        if (f.kind == FormFieldKind::Vec2i) {
            return ecs::Json::array({std::llround(v[0].get<f64>()), std::llround(v[1].get<f64>())});
        }
        return ecs::Json::array({v[0].get<f64>(), v[1].get<f64>()});
    case FormFieldKind::Text:
    case FormFieldKind::PrefabRef:
    case FormFieldKind::BehaviorRef:
        return v.is_string() ? std::optional<ecs::Json>(v) : std::nullopt;
    case FormFieldKind::EntityRef:
    case FormFieldKind::Other:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

Expected<FormSection> componentForm(const ecs::ComponentInfo& info, const ecs::Json& partial) {
    if (!partial.is_null() && !partial.is_object()) {
        return makeError(ErrorCode::InvalidArgument, std::format("'{}' 값은 객체", info.name));
    }
    auto canon = content::canonicalComponentJson(info, partial);
    if (!canon) {
        return std::unexpected(canon.error());
    }
    FormSection s;
    s.component = std::string(info.name);
    s.stableId = info.stableId;
    for (const ecs::FieldDesc& f : info.fields) {
        const std::string key(f.name);
        FormField ff;
        ff.name = key;
        ff.hint = f.hint;
        ff.meta = f.meta;
        ff.value = canon->contains(key) ? (*canon)[key] : ecs::Json();
        ff.kind = kindOf(f, ff.value);
        ff.set = partial.is_object() && partial.contains(key);
        s.fields.push_back(std::move(ff));
    }
    return s;
}

Expected<void> setFormField(const ecs::ComponentInfo& info, ecs::Json& partial, std::string_view field,
                            const ecs::Json& value) {
    auto form = componentForm(info, partial);
    if (!form) {
        return std::unexpected(form.error());
    }
    const auto it = std::ranges::find(form->fields, field, &FormField::name);
    if (it == form->fields.end()) {
        return makeError(ErrorCode::NotFound, std::format("'{}' 에 필드 '{}' 가 없다", info.name, field));
    }
    if (!it->editable()) {
        return makeError(ErrorCode::PermissionDenied, std::format("'{}.{}' 는 폼에서 바꿀 수 없다", info.name, field));
    }
    const auto v = coerce(*it, value);
    if (!v) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}.{}' 의 값 {} 이 종류에 맞지 않다", info.name, field, value.dump()));
    }
    ecs::Json next = partial.is_object() ? partial : ecs::Json::object();
    next[std::string(field)] = *v;
    if (auto r = info.validateJson(next, info.name); !r) {
        return std::unexpected(r.error());
    }
    // 저장되는 모양으로 (f32 필드의 1.005 → 1.00499999…) — 원본 → 폼 → 원본이 같게
    if (auto canon = content::canonicalComponentJson(info, next); canon && canon->contains(std::string(field))) {
        next[std::string(field)] = (*canon)[std::string(field)];
    }
    partial = std::move(next);
    return {};
}

bool resetFormField(ecs::Json& partial, std::string_view field) {
    return partial.is_object() && partial.erase(std::string(field)) > 0;
}

ecs::Json partialFromForm(const FormSection& form) {
    if (form.stableId == 0) {
        return form.raw;
    }
    ecs::Json out = ecs::Json::object();
    for (const FormField& f : form.fields) {
        if (f.set) {
            out[f.name] = f.value;
        }
    }
    return out;
}

std::vector<FormSection> prefabForm(const ecs::Json& prefab, const ecs::ComponentCatalog& catalog,
                                    std::vector<std::string>* warnings) {
    std::vector<FormSection> out;
    if (!prefab.is_object() || !prefab.contains("components") || !prefab["components"].is_object()) {
        return out;
    }
    for (const auto& [name, value] : prefab["components"].items()) {
        const ecs::ComponentInfo* info = catalog.find(std::string_view(name));
        if (info != nullptr) {
            if (auto form = componentForm(*info, value)) {
                out.push_back(std::move(*form));
                continue;
            } else if (warnings != nullptr) {
                warnings->push_back(std::format("{}: {}", name, form.error().describe()));
            }
        }
        FormSection raw; // opaque (render.* · client.*) · 모르는 · 틀린 값 — 통째로
        raw.component = name;
        raw.raw = value;
        out.push_back(std::move(raw));
    }
    std::ranges::sort(out, {}, &FormSection::component);
    return out;
}

bool addPrefabComponent(ecs::Json& prefab, std::string_view name, const ecs::ComponentCatalog& catalog) {
    if (!prefab.is_object() || catalog.find(name) == nullptr || content::isEngineManagedComponent(name)) {
        return false;
    }
    if (!prefab.contains("components") || !prefab["components"].is_object()) {
        prefab["components"] = ecs::Json::object();
    }
    if (prefab["components"].contains(std::string(name))) {
        return false;
    }
    prefab["components"][std::string(name)] = ecs::Json::object();
    return true;
}

bool removePrefabComponent(ecs::Json& prefab, std::string_view name) {
    return prefab.is_object() && prefab.contains("components") && prefab["components"].is_object() &&
           prefab["components"].erase(std::string(name)) > 0;
}

} // namespace sbx::editor
