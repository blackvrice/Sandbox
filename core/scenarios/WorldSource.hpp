#pragma once
// 서버가 돌릴 월드를 이름으로 연다: 시나리오 이름(ecosystem_small …), 게임 프로젝트 폴더(sandbox.project.json 이 있는 곳)
// 또는 세이브 폴더(world.json 이 있는 곳). SandboxServer --world 와 SandboxClient --world(LocalServerHost, Phase 10B)가 같은
// 규칙을 쓴다. docs/15-BUILD.md 7장.
//
//   시나리오   loadScenarioContent 로 팩을 읽고 ScenarioRunner(catalog, content, 시나리오, seed)
//   프로젝트   project::openProject (v0.2 M1-T2, 18 5.2) — 매니페스트 · 이 엔진 버전(BuildInfo) · <프로젝트>/content 팩 ·
//              entryWorld 를 검사하고 연다 (contentRoot 인자는 쓰지 않는다). 검사 오류가 하나라도 있으면 열지 않는다.
//              name = 매니페스트 name, game = 매니페스트. 이어 돌리기는 세이브와 같다
//   패키지     (v0.2 M5-T2, ADR-0054) .sbpkg 파일 — 검증해 캐시에 풀고 (package::extractPackage) 프로젝트처럼 연다.
//              고친 패키지는 열지 않는다. package = index (digest · 게임), projectDir = 풀어 둔 폴더
//   세이브     world.json 의 content.packs 를 contentRoot 에서 읽고 (없으면 내장) persist::loadWorld →
//              아무 명령도 넣지 않는 "save" 시나리오로 이어 돌린다. 세이브 경고는 warnings 에
//   셋 다 아님 NotFound — 있는 시나리오 이름을 알려 준다

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/package/GamePackage.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/project/GameProject.hpp"
#include "core/scenarios/Scenario.hpp"

namespace sbx::scenario {

struct WorldSource {
    // runner 의 월드가 content 를 가리킨다 — content 가 runner 보다 오래 살아야 한다 (선언 순서 = 소멸 역순)
    std::unique_ptr<content::ContentDatabase> content;
    std::unique_ptr<ScenarioRunner> runner;
    std::vector<std::string> packs; // 읽은 콘텐츠 팩 (비면 내장)
    std::string name;               // 시나리오 이름, 게임 이름(매니페스트 name) 또는 세이브 폴더 이름
    std::vector<std::string> warnings;
    std::optional<project::GameManifest> game; // 게임 프로젝트 · 패키지를 열었을 때만
    std::filesystem::path projectDir;          // 프로젝트 폴더 (패키지면 풀어 둔 캐시 — 재시작의 시작 월드 기준)
    std::optional<package::PackageIndex> package; // 패키지를 열었을 때만 (digest · 게임 정체성)
};

[[nodiscard]] Expected<WorldSource> openWorldSource(std::string_view spec, const std::filesystem::path& contentRoot,
                                                    u64 seed, const ecs::ComponentCatalog& catalog);

// 이미 읽은 콘텐츠로 세이브 폴더를 연다 — 서버가 실행 중에 불러오기 (Phase 12D). 접속한 클라이언트가 그 콘텐츠를 가지고
// 있으므로 world.json 의 contentHash 가 content 의 것과 다르면 거절한다 (InvalidArgument). runner 의 월드가 content 를
// 가리킨다. allowOtherContent = 해시가 달라도 연다 (게임 재시작의 시작 월드 — 프로젝트를 열 때처럼 경고만, v0.2 M2-T3.
// 로드 경고가 warnings 에 든다)
struct OpenedSave {
    std::unique_ptr<ScenarioRunner> runner;
    std::vector<std::string> warnings;
};
[[nodiscard]] Expected<OpenedSave> openSave(const std::filesystem::path& dir, const content::ContentDatabase& content,
                                            const ecs::ComponentCatalog& catalog, bool allowOtherContent = false);

// 세이브 폴더 요약 (world.json 만 읽는다 — 목록 표시용)
struct SaveSummary {
    sim::Tick tick = 0;
    u64 entities = 0;
    u64 contentHash = 0;
    std::vector<std::string> packs;
    std::optional<persist::GameBinding> game; // world.json 의 "game" (v0.2 M5-T2 — 없으면 게임에 묶이지 않은 세이브)
};
[[nodiscard]] Expected<SaveSummary> readSaveSummary(const std::filesystem::path& dir);

// "random_walk_1k, ecosystem_small, …" (오류 문구용)
[[nodiscard]] std::string scenarioNames();

} // namespace sbx::scenario
