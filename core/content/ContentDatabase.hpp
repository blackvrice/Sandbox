#pragma once
// ContentDatabase — 불변 콘텐츠 테이블. docs/03-SIMULATION.md 1장, docs/11-CONTENT-SCHEMA.md.
//
// Phase 4: TerrainMaterial. Phase 5A: 팩 로더(ContentLoader)가 Tag · Prefab · Rule · BehaviorGraph 를 채운다.
// Phase 12C: 월드 오버레이 — 바꾸지 않고 새 DB 를 만든다 (ContentLoader::applyOverlay, 팩 원본 sources() 를 공유).
// 내장 "core" 머티리얼(core.grass/rock/sand/water)은 팩으로 만든 DB 에도 항상 들어 있다 (빈 월드·테스트의 기본 지형).
//
// 머티리얼 인덱스 = id 정렬 순서 (05-WORLD 3.2). 인덱스는 이 프로세스 안에서만 의미가 있고,
// 세이브에는 id 테이블을 함께 적어 로드할 때 재매핑한다. 해시에는 인덱스가 아니라 id 의 stableId 를 먹인다.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/content/ContentModel.hpp"
#include "core/content/ContentOverlay.hpp"
#include "core/world/Terrain.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::content {

class ContentLoaderImpl;

// 팩 원본 — 로더가 읽은 파일 텍스트 (presentation/ 빼고, 줄 끝 정규화). 오버레이를 적용할 때 디스크를 다시 읽지 않고
// 이것으로 다시 만든다 (ContentLoader::applyOverlay, ADR-0030). 불변, 오버레이를 적용한 DB 들이 함께 쓴다.
struct ContentSourceFile {
    std::string rel;     // 팩 안 상대 경로 "rules/eat.json"
    std::string display; // "<폴더>/<rel>"
    std::string text;
};
struct ContentSourcePack {
    PackInfo info;
    std::string folder;
    std::vector<ContentSourceFile> files; // rel 정렬
};
struct ContentSources {
    std::vector<ContentSourcePack> packs; // 로드 순서
};

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
    [[nodiscard]] std::span<const Rule> rules() const noexcept { return m_rules; } // 정의 순서
    [[nodiscard]] const Rule* findRule(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const BehaviorGraph> behaviors() const noexcept { return m_behaviors; } // id 정렬
    [[nodiscard]] const BehaviorGraph* findBehavior(std::string_view id) const noexcept;
    // Rule·interact 가 쓰는 action 이름 (정렬). 번호 = 인덱스 + 1 (0 = 없음)
    [[nodiscard]] std::span<const std::string> actions() const noexcept { return m_actions; }
    [[nodiscard]] u16 findAction(std::string_view name) const noexcept;
    // action 번호(1 부터)의 Rule 인덱스들 — (priority 내림, 정의 순) 정렬. 03 6.2
    [[nodiscard]] std::span<const u32> rulesForAction(u16 actionId) const noexcept;
    [[nodiscard]] std::span<const PackInfo> packs() const noexcept { return m_packs; } // 로드 순서
    // GameMode (v0.2 M2, ADR-0045) — id 정렬
    [[nodiscard]] std::span<const GameMode> modes() const noexcept { return m_modes; }
    [[nodiscard]] const GameMode* findMode(std::string_view id) const noexcept;
    // PlayerAction (v0.2 M3-T1 — 팩의 actions.json, ADR-0047) — id 정렬
    [[nodiscard]] std::span<const PlayerActionDef> playerActions() const noexcept { return m_playerActions; }
    [[nodiscard]] const PlayerActionDef* findPlayerAction(std::string_view id) const noexcept;

    // 콘텐츠 지문. 머티리얼만 있으면 (id·moveCost·flags) 지문, 팩을 읽었으면 매니페스트 지문 (09 7장).
    // 오버레이를 적용한 DB 도 팩의 지문 그대로다 (오버레이 지문은 overlay().hash() — WorldHash 가 먹는다)
    [[nodiscard]] u64 contentHash() const noexcept { return m_hash; }

    // 월드 오버레이 (ADR-0030): 이 DB 에 적용된 것 (팩만 읽었으면 비어 있다)과 팩 원본 (팩 없이 만든 DB 면 nullptr)
    [[nodiscard]] const ContentOverlay& overlay() const noexcept { return m_overlay; }
    [[nodiscard]] const std::shared_ptr<const ContentSources>& sources() const noexcept { return m_sources; }

private:
    friend class ContentLoaderImpl; // core/content/ContentLoader.cpp
    friend class ContentLoader;

    std::vector<TerrainMaterial> m_materials; // id 정렬
    std::vector<std::string> m_tags;          // 이름 정렬
    std::vector<Prefab> m_prefabs;
    std::vector<Rule> m_rules;
    std::vector<BehaviorGraph> m_behaviors;
    std::vector<GameMode> m_modes;
    std::vector<PlayerActionDef> m_playerActions;
    std::vector<std::string> m_actions;            // 이름 정렬
    std::vector<std::vector<u32>> m_rulesByAction; // m_actions 와 같은 순서
    std::vector<PackInfo> m_packs;
    u64 m_hash = 0;
    ContentOverlay m_overlay;
    std::shared_ptr<const ContentSources> m_sources;
};

[[nodiscard]] std::string_view terrainFlagName(world::TerrainFlags f) noexcept;
// 11-CONTENT-SCHEMA 1.1: <pack>.<name>, 소문자·숫자·'_', 점으로 구분
[[nodiscard]] bool isValidContentId(std::string_view id) noexcept;
// {"id", "moveCost", "flags"} 하나 (오류 문맥 ctx)
[[nodiscard]] Expected<TerrainMaterial> parseTerrainMaterial(const nlohmann::json& j, const std::string& ctx);

} // namespace sbx::content
