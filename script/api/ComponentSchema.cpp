#include "script/api/ComponentSchema.hpp"

namespace sbx::script::api {

namespace {

constexpr std::string_view kEngineNamespaces[] = {
    "core",   "ai",     "life",   "game",   "net",    "persist", "debug",
    "render", "editor", "client", "server", "script", "sandbox", "sbx",
};

} // namespace

const ComponentFieldSchema* ComponentSchema::find(std::string_view name) const noexcept {
    for (const ComponentFieldSchema& f : fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

bool isValidComponentId(std::string_view n) noexcept {
    if (n.empty() || n.front() == '.' || n.back() == '.') {
        return false;
    }
    bool dot = false;
    char prev = '\0';
    for (const char c : n) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok || (c == '.' && prev == '.')) {
            return false;
        }
        dot = dot || c == '.';
        prev = c;
    }
    return dot;
}

bool isEngineComponentNamespace(std::string_view id) noexcept {
    const usize dot = id.find('.');
    const std::string_view ns = id.substr(0, dot);
    for (const std::string_view e : kEngineNamespaces) {
        if (ns == e) {
            return true;
        }
    }
    return false;
}

bool isValidFieldName(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    for (usize i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (!(alpha || (digit && i > 0))) {
            return false;
        }
    }
    return true;
}

std::string_view componentFieldKindName(ComponentFieldKind k) noexcept {
    switch (k) {
    case ComponentFieldKind::I32:
        return "i32";
    case ComponentFieldKind::F32:
        return "f32";
    case ComponentFieldKind::F64:
        return "f64";
    case ComponentFieldKind::Bool:
        return "bool";
    case ComponentFieldKind::Vec2:
        return "vec2";
    case ComponentFieldKind::Entity:
        return "entity";
    case ComponentFieldKind::Enum:
        return "enumOf";
    }
    return "?";
}

f64 roundToF32(f64 v) noexcept {
    return static_cast<f64>(static_cast<f32>(v));
}

} // namespace sbx::script::api
