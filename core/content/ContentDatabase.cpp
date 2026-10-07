#include "core/content/ContentDatabase.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>

#include "foundation/assert/Assert.hpp"
#include "foundation/hash/Fnv1a.hpp"

namespace sbx::content {
namespace {

using nlohmann::json;
using world::TerrainFlags;

constexpr std::array kFlags{TerrainFlags::Blocked, TerrainFlags::Water, TerrainFlags::NoBuild};

} // namespace

// 11-CONTENT-SCHEMA 1.1: <pack>.<name>, 소문자·숫자·'_', 점으로 구분
bool isValidContentId(std::string_view id) noexcept {
    if (id.empty() || id.front() == '.' || id.back() == '.') {
        return false;
    }
    bool dot = false;
    char prev = '\0';
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok || (c == '.' && prev == '.')) {
            return false;
        }
        dot = dot || c == '.';
        prev = c;
    }
    return dot;
}

Expected<TerrainMaterial> parseTerrainMaterial(const json& j, const std::string& ctx) {
    if (!j.is_object()) {
        return makeError(ErrorCode::ParseError, "머티리얼은 객체여야 한다", ctx);
    }
    for (const auto& [key, _] : j.items()) {
        if (key != "id" && key != "moveCost" && key != "flags") {
            return makeError(ErrorCode::ValidationFailed, std::format("모르는 필드 '{}'", key), ctx);
        }
    }
    TerrainMaterial m;
    if (!j.contains("id") || !j["id"].is_string()) {
        return makeError(ErrorCode::ParseError, "id(문자열)가 필요하다", ctx);
    }
    m.id = j["id"].get<std::string>();
    if (!isValidContentId(m.id)) {
        return makeError(ErrorCode::ValidationFailed, std::format("id 규칙 위반 '{}'", m.id), ctx);
    }
    m.stableId = fnv1a64(m.id);
    if (j.contains("moveCost")) {
        const json& mc = j["moveCost"];
        if (!mc.is_number_integer() || mc.get<i64>() < 0 || mc.get<i64>() > 255) {
            return makeError(ErrorCode::ValidationFailed, "moveCost 는 0~255 정수", ctx + ".moveCost");
        }
        m.moveCost = static_cast<u8>(mc.get<i64>());
    }
    if (j.contains("flags")) {
        if (!j["flags"].is_array()) {
            return makeError(ErrorCode::ParseError, "flags 는 문자열 배열", ctx + ".flags");
        }
        for (const json& f : j["flags"]) {
            const auto it = std::find_if(kFlags.begin(), kFlags.end(), [&](TerrainFlags k) {
                return f.is_string() && f.get<std::string>() == terrainFlagName(k);
            });
            if (it == kFlags.end()) {
                return makeError(ErrorCode::ValidationFailed, std::format("모르는 플래그 {}", f.dump()),
                                 ctx + ".flags");
            }
            m.flags = static_cast<u8>(m.flags | static_cast<u8>(*it));
        }
    }
    return m;
}

std::string_view terrainFlagName(TerrainFlags f) noexcept {
    switch (f) {
    case TerrainFlags::None:
        return "None";
    case TerrainFlags::Blocked:
        return "Blocked";
    case TerrainFlags::Water:
        return "Water";
    case TerrainFlags::NoBuild:
        return "NoBuild";
    }
    return "?";
}

Expected<ContentDatabase> ContentDatabase::fromJson(const json& doc, std::string_view context) {
    const std::string ctx(context);
    if (!doc.is_object() || !doc.contains("terrainMaterials") || !doc["terrainMaterials"].is_array()) {
        return makeError(ErrorCode::ParseError, "terrainMaterials 배열이 필요하다", ctx);
    }
    ContentDatabase db;
    const json& arr = doc["terrainMaterials"];
    for (usize i = 0; i < arr.size(); ++i) {
        auto m = parseTerrainMaterial(arr[i], std::format("{}.terrainMaterials[{}]", ctx, i));
        if (!m) {
            return std::unexpected(m.error());
        }
        db.m_materials.push_back(std::move(*m));
    }
    if (db.m_materials.empty()) {
        return makeError(ErrorCode::ValidationFailed, "머티리얼이 하나 이상 필요하다", ctx);
    }
    if (db.m_materials.size() > std::numeric_limits<world::MaterialIndex>::max()) {
        return makeError(ErrorCode::OutOfRange, "머티리얼이 너무 많다", ctx);
    }
    std::sort(db.m_materials.begin(), db.m_materials.end(),
              [](const TerrainMaterial& a, const TerrainMaterial& b) { return a.id < b.id; });
    for (usize i = 1; i < db.m_materials.size(); ++i) {
        if (db.m_materials[i].id == db.m_materials[i - 1].id) {
            return makeError(ErrorCode::AlreadyExists, std::format("머티리얼 id 중복 '{}'", db.m_materials[i].id), ctx);
        }
    }
    Fnv1a64 h;
    h.string("SBXCONTENT");
    for (const TerrainMaterial& m : db.m_materials) {
        h.string(m.id).byte(0).byte(m.moveCost).byte(m.flags);
    }
    db.m_hash = h.value();
    return db;
}

const ContentDatabase& ContentDatabase::builtin() {
    static const ContentDatabase db = [] {
        const json doc = json::parse(R"({"terrainMaterials": [
            {"id": "core.grass", "moveCost": 10},
            {"id": "core.rock",  "moveCost": 0,  "flags": ["Blocked", "NoBuild"]},
            {"id": "core.sand",  "moveCost": 14},
            {"id": "core.water", "moveCost": 0,  "flags": ["Water", "NoBuild"]}
        ]})");
        auto r = fromJson(doc, "builtin");
        SBX_VERIFY(r.has_value(), "내장 콘텐츠가 잘못되었다");
        return std::move(*r);
    }();
    return db;
}

std::optional<world::MaterialIndex> ContentDatabase::findMaterial(std::string_view id) const noexcept {
    const auto it = std::lower_bound(m_materials.begin(), m_materials.end(), id,
                                     [](const TerrainMaterial& m, std::string_view k) { return m.id < k; });
    if (it == m_materials.end() || it->id != id) {
        return std::nullopt;
    }
    return static_cast<world::MaterialIndex>(it - m_materials.begin());
}

std::optional<TagIndex> ContentDatabase::findTag(std::string_view name) const noexcept {
    const auto it = std::lower_bound(m_tags.begin(), m_tags.end(), name);
    if (it == m_tags.end() || *it != name) {
        return std::nullopt;
    }
    return static_cast<TagIndex>(it - m_tags.begin());
}

std::string ContentDatabase::describeTags(const TagSet& set) const {
    std::string out;
    for (usize i = 0; i < m_tags.size(); ++i) {
        if (set.test(static_cast<TagIndex>(i))) {
            out += out.empty() ? "" : "|";
            out += m_tags[i];
        }
    }
    return out;
}

const Prefab* ContentDatabase::findPrefab(std::string_view id) const noexcept {
    const auto it = std::lower_bound(m_prefabs.begin(), m_prefabs.end(), id,
                                     [](const Prefab& p, std::string_view k) { return p.id < k; });
    return (it != m_prefabs.end() && it->id == id) ? &*it : nullptr;
}

const BehaviorGraph* ContentDatabase::findBehavior(std::string_view id) const noexcept {
    const auto it = std::lower_bound(m_behaviors.begin(), m_behaviors.end(), id,
                                     [](const BehaviorGraph& b, std::string_view k) { return b.id < k; });
    return (it != m_behaviors.end() && it->id == id) ? &*it : nullptr;
}

u16 ContentDatabase::findAction(std::string_view name) const noexcept {
    const auto it = std::lower_bound(m_actions.begin(), m_actions.end(), name,
                                     [](const std::string& a, std::string_view k) { return a < k; });
    return (it != m_actions.end() && *it == name) ? static_cast<u16>(it - m_actions.begin() + 1) : u16{0};
}

std::span<const u32> ContentDatabase::rulesForAction(u16 actionId) const noexcept {
    if (actionId == 0 || actionId > m_rulesByAction.size()) {
        return {};
    }
    return m_rulesByAction[actionId - 1u];
}

const TerrainMaterial& ContentDatabase::material(world::MaterialIndex index) const noexcept {
    SBX_ASSERT(index < m_materials.size(), "머티리얼 인덱스 범위 밖");
    return m_materials[index];
}

} // namespace sbx::content
