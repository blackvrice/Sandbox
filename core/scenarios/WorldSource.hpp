#pragma once
// 서버가 돌릴 월드를 이름으로 연다: 시나리오 이름(ecosystem_small …) 또는 세이브 폴더(world.json 이 있는 곳).
// SandboxServer --world 와 SandboxClient --world(LocalServerHost, Phase 10B)가 같은 규칙을 쓴다. docs/15-BUILD.md 7장.
//
//   시나리오   loadScenarioContent 로 팩을 읽고 ScenarioRunner(catalog, content, 시나리오, seed)
//   세이브     world.json 의 content.packs 를 contentRoot 에서 읽고 (없으면 내장) persist::loadWorld →
//              아무 명령도 넣지 않는 "save" 시나리오로 이어 돌린다. 세이브 경고는 warnings 에
//   둘 다 아님 NotFound — 있는 시나리오 이름을 알려 준다

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/scenarios/Scenario.hpp"

namespace sbx::scenario {

struct WorldSource {
    // runner 의 월드가 content 를 가리킨다 — content 가 runner 보다 오래 살아야 한다 (선언 순서 = 소멸 역순)
    std::unique_ptr<content::ContentDatabase> content;
    std::unique_ptr<ScenarioRunner> runner;
    std::vector<std::string> packs; // 읽은 콘텐츠 팩 (비면 내장)
    std::string name;               // 시나리오 이름 또는 세이브 폴더 이름
    std::vector<std::string> warnings;
};

[[nodiscard]] Expected<WorldSource> openWorldSource(std::string_view spec, const std::filesystem::path& contentRoot,
                                                    u64 seed, const ecs::ComponentCatalog& catalog);

// "random_walk_1k, ecosystem_small, …" (오류 문구용)
[[nodiscard]] std::string scenarioNames();

} // namespace sbx::scenario
