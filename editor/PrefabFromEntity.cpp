#include "editor/PrefabFromEntity.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace sbx::editor {

namespace {

// 개체에만 뜻이 있는 컴포넌트 — Prefab 에 넣지 않는다. core.tags 는 엔진이 관리한다 (V3 — Prefab 은 "tags" 배열, 바탕의 것)
constexpr std::array<std::string_view, 5> kInstanceOnly = {"core.prefab", "persist.persistence", "core.contacts",
                                                          "core.velocity", "core.tags"};

bool idChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

} // namespace

std::optional<std::string> checkNewPrefabId(const content::ContentDatabase& db, std::string_view id) {
    const usize dot = id.find('.');
    if (id.empty() || dot == std::string_view::npos || dot == 0 || dot + 1 >= id.size() || id.back() == '.' ||
        !std::ranges::all_of(id, idChar) || id.find("..") != std::string_view::npos) {
        return std::format("'{}' — Prefab id 는 \"<팩>.<이름>\" (소문자 · 숫자 · '_', 예: game.enemy_fast)", id);
    }
    const std::string_view pack = id.substr(0, dot);
    if (std::ranges::none_of(db.packs(), [&](const content::PackInfo& p) { return p.id == pack; })) {
        return std::format("팩 '{}' 은 이 월드가 읽은 팩이 아니다", pack);
    }
    if (db.findPrefab(id) != nullptr) {
        return std::format("Prefab '{}' 가 이미 있다", id);
    }
    return std::nullopt;
}

ecs::Json prefabFromComponents(std::string_view id, const content::Prefab* base,
                               std::span<const ComponentState> components, const ecs::ComponentCatalog& catalog) {
    ecs::Json comps = ecs::Json::object();
    ecs::Json tags = ecs::Json::array();
    std::string category = "Objects";
    if (base != nullptr && base->raw.is_object()) {
        if (base->raw.contains("components") && base->raw["components"].is_object()) {
            comps = base->raw["components"];
        }
        if (base->raw.contains("tags") && base->raw["tags"].is_array()) {
            tags = base->raw["tags"];
        }
        if (!base->category.empty()) {
            category = base->category;
        }
    }
    comps.erase("core.prefab");
    for (const ComponentState& s : components) {
        if (std::ranges::find(kInstanceOnly, s.name) != kInstanceOnly.end()) {
            continue;
        }
        const ecs::ComponentInfo* info = catalog.find(s.stableId);
        if (info == nullptr || hasFlag(info->flags, ecs::ComponentFlags::WorldState) || !s.value.is_object()) {
            continue;
        }
        ecs::Json value = s.value;
        for (const ecs::FieldDesc& f : info->fields) {
            if (f.hint == ecs::Hint::EntityRef) {
                value.erase(std::string(f.name));
            }
        }
        if (s.name == "core.transform") {
            value.erase("position"); // 놓을 때 정한다
        }
        comps[std::string(s.name)] = std::move(value);
    }
    if (!comps.contains("core.transform")) {
        comps["core.transform"] = ecs::Json::object();
    }
    const std::string name(id.substr(id.find('.') + 1));
    return ecs::Json{{"id", std::string(id)}, {"name", name}, {"category", category}, {"tags", tags},
                     {"components", comps}};
}

std::string suggestPrefabId(const content::ContentDatabase& db, const content::Prefab* base) {
    std::string stem;
    if (base != nullptr) {
        stem = base->id + "_variant";
    } else {
        std::string pack = "game";
        for (const content::PackInfo& p : db.packs()) {
            pack = p.id; // 마지막 (프로젝트 팩은 보통 엔진 팩 뒤에 읽힌다)
        }
        stem = pack + ".object";
    }
    std::string id = stem;
    for (u32 n = 2; n < 100 && db.findPrefab(id) != nullptr; ++n) {
        id = std::format("{}_{}", stem, n);
    }
    return id;
}

} // namespace sbx::editor
