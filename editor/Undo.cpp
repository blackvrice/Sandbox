#include "editor/Undo.hpp"

#include <type_traits>

#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/world/WorldGrid.hpp"

namespace sbx::editor {

void IdRemap::set(NetEntityId from, NetEntityId to) {
    if (from != to && from != kInvalidNetEntityId && to != kInvalidNetEntityId) {
        m_map[from] = to;
    }
}

NetEntityId IdRemap::resolve(NetEntityId id) const {
    // 다시 만든 것을 또 다시 만들면 사슬이 된다 (a → b → c). netId 는 재사용되지 않으니 고리가 없다 — 그래도 상한
    for (usize guard = 0; guard < m_map.size() + 1; ++guard) {
        const auto it = m_map.find(id);
        if (it == m_map.end()) {
            return id;
        }
        id = it->second;
    }
    return id;
}

void IdRemap::apply(cmd::CommandPayload& payload) const {
    std::visit(
        [&](auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, cmd::DeleteEntity> || std::is_same_v<T, cmd::MoveEntity>) {
                for (NetEntityId& id : p.targets) {
                    id = resolve(id);
                }
            } else if constexpr (std::is_same_v<T, cmd::AddComponent> || std::is_same_v<T, cmd::RemoveComponent> ||
                                 std::is_same_v<T, cmd::ChangeComponent>) {
                p.target = resolve(p.target);
            }
        },
        payload);
}

ecs::Json withoutEntityRefs(const ecs::Json& value, const ecs::ComponentInfo& info) {
    ecs::Json out = value;
    if (!out.is_object()) {
        return out;
    }
    for (const ecs::FieldDesc& f : info.fields) {
        if (f.hint == ecs::Hint::EntityRef) {
            out.erase(std::string(f.name));
        }
    }
    return out;
}

UndoCmd recreateCommand(NetEntityId id, std::span<const ComponentState> components,
                        const ecs::ComponentCatalog& catalog) {
    cmd::CreateEntity c;
    for (const ComponentState& s : components) {
        const ecs::ComponentInfo* info = catalog.find(s.stableId);
        if (info == nullptr || info->name == "net.identity") {
            continue;
        }
        if (info->name == "core.prefab") {
            if (s.value.is_object() && s.value.contains("id") && s.value["id"].is_string()) {
                c.prefab = s.value["id"].get<std::string>();
            }
            continue;
        }
        if (info->name == "core.transform" && s.value.is_object() && s.value.contains("position")) {
            const ecs::Json& p = s.value["position"];
            if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number()) {
                c.position = {p[0].get<f32>(), p[1].get<f32>()};
            }
        }
        c.components.push_back({s.stableId, withoutEntityRefs(s.value, *info)});
    }
    return {std::move(c), {id}};
}

std::vector<UndoCmd> paintInverse(const cmd::PaintTerrain& paint, const world::WorldGrid& grid,
                                  const content::ContentDatabase& content) {
    const auto mats = content.terrainMaterials();
    std::vector<cmd::PaintTerrain> byMaterial(mats.size());
    for (const Vec2i t : paint.cells) {
        if (!grid.containsTile(t)) {
            continue;
        }
        const usize m = grid.materialAt(t);
        if (m < mats.size()) {
            byMaterial[m].cells.push_back(t);
        }
    }
    std::vector<UndoCmd> out;
    for (usize m = 0; m < byMaterial.size(); ++m) {
        if (byMaterial[m].cells.empty() || mats[m].id == paint.materialId) {
            continue;
        }
        byMaterial[m].materialId = mats[m].id;
        out.push_back({std::move(byMaterial[m]), {}});
    }
    return out;
}

} // namespace sbx::editor
