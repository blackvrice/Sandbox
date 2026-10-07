#pragma once
// ContentDatabase — 불변 콘텐츠 테이블. docs/03-SIMULATION.md 1장, docs/11-CONTENT-SCHEMA.md.
//
// Phase 4: TerrainMaterial. Phase 5A: 팩 로더(ContentLoader)가 Tag · Prefab · Rule · BehaviorGraph 를 채운다.
// 내장 "core" 머티리얼(core.grass/rock/sand/water)은 팩으로 만든 DB 에도 항상 들어 있다 (빈 월드·테스트의 기본 지형).
//
// 머티리얼 인덱스 = id 정렬 순서 (05-WORLD 3.2). 인덱스는 이 프로세스 안에서만 의미가 있고,
// 세이브에는 id 테이블을 함께 적어 로드할 때 재매핑한다. 해시에는 인덱스가 아니라 id 의 stableId 를 먹인다.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/content/ContentModel.hpp"
#include "core/world/Terrain.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::content {

class ContentLoaderImpl;

struct TerrainMaterial {
    std::string id;   // "core.grass"
    u64 stableId = 0; // fnv1a64(id) — 해시용
    u8 moveCost = 10; // 0 = 통행 불가
    u8 flags = 0;     // world::TerrainFlags 비트
};

class ContentDatabase {
public:
    // JSON: {"terrainMaterials": [{"id": "...", "moveCost": 10, "flags": ["Blocked", "Water", "NoBuild"]}, ...]}
    // id 규칙(11-CONTENT-SCHEMA 1.1)·중복·범위·모르는 키를 검사한다.
    [[nodiscard]] static Expected<ContentDatabase> fromJson(const nlohmann::json& doc, std::string_view context);
    // 엔진 내장 기본 콘텐츠 (core.grass, core.rock, core.sand, core.water). 테스트·시나리오·빈 월드용.
    [[nodiscard]] static const ContentDatabase& builtin();

    [[nodiscard]] std::span<const TerrainMaterial> terrainMaterials() const noexcept { return m_materials; }
    [[nodiscard]] std::optional<world::MaterialIndex> findMaterial(std::string_view id) const noexcept;
    [[nodiscard]] const TerrainMaterial& material(world::MaterialIndex index) const noexcept;
    [[nodiscard]] usize materialCount() const noexcept { return m_materials.size(); }

    // 태그 (이름 정렬 = 비트 인덱스)
    [[nodiscard]] std::span<const std::string> tags() const noexcept { return m_tags; }
    [[nodiscard]] std::optional<TagIndex> findTag(std::string_view name) const noexcept;
    // "animal|prey" — 로그·진단용
    [[nodiscard]] std::string describeTags(const TagSet& set) const;

    [[nodiscard]] std::span<const Prefab> prefabs() const noexcept { return m_prefabs; } // id 정렬
    [[nodiscard]] const Prefab* findPrefab(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const Rule> rules() const noexcept { return m_rules; }                  // 정의 순서
    [[nodiscard]] std::span<const BehaviorGraph> behaviors() const noexcept { return m_behaviors; } // id 정렬
    [[nodiscard]] const BehaviorGraph* findBehavior(std::string_view id) const noexcept;
    // Rule·interact 가 쓰는 action 이름 (정렬). 번호 = 인덱스 + 1 (0 = 없음)
    [[nodiscard]] std::span<const std::string> actions() const noexcept { return m_actions; }
    [[nodiscard]] u16 findAction(std::string_view name) const noexcept;
    // action 번호(1 부터)의 Rule 인덱스들 — (priority 내림, 정의 순) 정렬. 03 6.2
    [[nodiscard]] std::span<const u32> rulesForAction(u16 actionId) const noexcept;
    [[nodiscard]] std::span<const PackInfo> packs() const noexcept { return m_packs; } // 로드 순서

    // 콘텐츠 지문. 머티리얼만 있으면 (id·moveCost·flags) 지문, 팩을 읽었으면 매니페스트 지문 (09 7장).
    [[nodiscard]] u64 contentHash() const noexcept { return m_hash; }

private:
    friend class ContentLoaderImpl; // core/content/ContentLoader.cpp

    std::vector<TerrainMaterial> m_materials; // id 정렬
    std::vector<std::string> m_tags;          // 이름 정렬
    std::vector<Prefab> m_prefabs;
    std::vector<Rule> m_rules;
    std::vector<BehaviorGraph> m_behaviors;
    std::vector<std::string> m_actions;            // 이름 정렬
    std::vector<std::vector<u32>> m_rulesByAction; // m_actions 와 같은 순서
    std::vector<PackInfo> m_packs;
    u64 m_hash = 0;
};

[[nodiscard]] std::string_view terrainFlagName(world::TerrainFlags f) noexcept;
// 11-CONTENT-SCHEMA 1.1: <pack>.<name>, 소문자·숫자·'_', 점으로 구분
[[nodiscard]] bool isValidContentId(std::string_view id) noexcept;
// {"id", "moveCost", "flags"} 하나 (오류 문맥 ctx)
[[nodiscard]] Expected<TerrainMaterial> parseTerrainMaterial(const nlohmann::json& j, const std::string& ctx);

} // namespace sbx::content
