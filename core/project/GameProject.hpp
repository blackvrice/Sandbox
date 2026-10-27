#pragma once
// GameProject — 게임 하나의 편집 원본 폴더와 그 매니페스트(sandbox.project.json). docs/18-PLATFORM.md 5장, ADR-0040.
//
//   MyGame/
//     sandbox.project.json   매니페스트 (이 파일)
//     content/<팩>/          콘텐츠 팩 — 11-CONTENT-SCHEMA 그대로 (pack.json 의 id 로 찾는다)
//     worlds/<이름>/         월드 = 세이브 폴더 형식 그대로 (world.json · entities.jsonl · chunks/, 09 6장)
//
// 매니페스트 schemaVersion 2 (1 도 읽는다 — 모르는 키는 오류, 새 키는 schemaVersion 을 올려 더한다):
//   {"schemaVersion": 2, "gameId": "studio.arena", "name": "Arena", "version": "0.1.0",
//    "engine": ">=0.1.0 <1.0.0", "description": "...", "content": {"packs": ["arena"]}, "entryWorld": "worlds/main",
//    "gameMode": "arena.duel"}
//   2 에서 더한 키: gameMode (콘텐츠의 GameMode id — v0.2 M2, ADR-0045. 없으면 끝 없는 샌드박스)
//
// 문제는 ContentLoader 와 같은 ContentIssue 로 모두 모은다 (첫 오류에서 멈추지 않는다). 규칙 이름:
//   P1 형식 (JSON · 모르는 키 · 타입 · 필수 키)   P2 식별자 (gameId · 팩 id)   P3 버전 (schemaVersion · version ·
//   engine 범위 · 이 엔진이 범위 밖)   P4 경로 (entryWorld 형식 · 폴더 · 월드 열기)   P5 콘텐츠 참조 (팩 폴더 ·
//   월드가 쓰는 팩 · contentHash · gameMode 가 콘텐츠에 없음)   + 팩의 V1~V7 (file 은 "content/<팩 폴더>/…")

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentLoader.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::project {

inline constexpr std::string_view kManifestFile = "sandbox.project.json";
inline constexpr u32 kManifestSchemaVersion = 3;    // 읽을 수 있는 가장 새 판 (3 = dependencies — v0.4 M1-T2)
inline constexpr u32 kMinManifestSchemaVersion = 1; // 읽을 수 있는 가장 옛 판
// content.packs 상한 — 서버가 Reject 로 팩 목록을 알려 줄 수 있는 수 (network kMaxPacks 와 같다, static_assert 가
// 지킨다)
inline constexpr usize kMaxContentPacks = 16;

struct GameManifest {
    u32 schemaVersion = kManifestSchemaVersion;
    std::string gameId;                    // "studio.arena" — 소문자·숫자·'_' 마디 둘 이상을 '.' 로 (≤ 64 바이트)
    std::string name;                      // 표시 이름 (1~64 글자, 제어 문자 없음)
    SemVer version;                        // 게임 버전 (SemVer)
    VersionRange engine;                   // 이 게임을 여는 엔진 버전 범위
    std::string description;               // 선택 (≤ 1024 글자)
    std::vector<std::string> contentPacks; // <프로젝트>/content 에서 찾을 팩 id (비면 내장 콘텐츠만)
    std::string entryWorld;                // 시작 월드 폴더 — 프로젝트 기준 상대 경로, '/' 구분 ("worlds/main")
    std::string gameMode;                  // schemaVersion 2: GameMode id (비면 없음)
    // schemaVersion 3 (v0.4 M1-T2, ADR-0071): 의존 팩 — 팩 id 순. content.packs 와 함께 불러온다 (loadedPacks). 소스
    // (path · git) 는 기록만 — 받기는 M2 [계획], 지금은 <프로젝트>/content 에서 찾는다
    std::vector<content::PackDependency> dependencies;

    bool operator==(const GameManifest&) const = default;
};

// 불러올 팩 = content.packs (그 차례) + 거기 없는 dependencies (팩 id 순)
[[nodiscard]] std::vector<std::string> loadedPacks(const GameManifest& manifest);

struct ManifestParseResult {
    GameManifest manifest; // ok() 일 때만 믿을 수 있다
    std::vector<content::ContentIssue> issues;

    [[nodiscard]] bool ok() const noexcept;
};

// 텍스트를 읽어 검사한다 (디스크를 보지 않는다 — 경로의 형식만). issues 의 file 은 kManifestFile
[[nodiscard]] ManifestParseResult parseManifest(std::string_view jsonText);

// 정규형 JSON (키 순서 고정, 들여쓰기 2, 끝 줄바꿈). parseManifest(writeManifest(m)).manifest == m
[[nodiscard]] std::string writeManifest(const GameManifest& manifest);

// <projectDir>/sandbox.project.json 에 원자적으로 쓴다 (폴더는 있어야 한다)
[[nodiscard]] Expected<void> saveManifest(const std::filesystem::path& projectDir, const GameManifest& manifest);

// 프로젝트 폴더 검사 결과. 시작 월드를 실제로 열어 본다 (콘텐츠에 오류가 없을 때)
struct ProjectReport {
    GameManifest manifest;
    bool manifestRead = false; // 매니페스트가 오류 없이 읽혔다
    std::vector<content::ContentIssue> issues;
    u64 contentHash = 0; // 프로젝트 콘텐츠 (팩이 없으면 내장)의 해시 — 콘텐츠를 읽었을 때만
    usize prefabs = 0;
    bool worldOpened = false;
    u64 worldTick = 0;
    usize worldEntities = 0;
    // 스크립트 (v0.3 M5-T1, ADR-0065) — 콘텐츠를 오류 없이 읽었을 때. 시스템마다 한 줄 ("id — 파일 · 콜백")
    usize scriptPacks = 0;
    usize scriptComponents = 0;
    std::vector<std::string> scriptSystems;
    // 의존 (v0.4 M1-T2) — 매니페스트 dependencies 마다 한 줄 ("inv ^0.1 → 0.1.0 · MIT (content/inv)")
    std::vector<std::string> dependencies;
    enum class SdkState : u8 { Missing, Current, Stale };
    SdkState sdk = SdkState::Missing; // <프로젝트>/sdk/sandbox.d.ts 가 이 엔진의 SDK 와 같은가

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] usize errorCount() const noexcept;
    [[nodiscard]] usize warningCount() const noexcept;
};

// engine = 지금 엔진의 버전 (BuildInfo). 매니페스트 → 엔진 범위 → 팩 (V1~V7 · 스크립트 S*) → 시작 월드 (형식 · 팩 ·
// 열기) → 도구 파일 (P6 — sdk/sandbox.d.ts 가 이 엔진의 것과 다르다 · 스크립트가 있는데 없다, tsconfig.json 이 JSON 이
// 아니다) 순서
[[nodiscard]] ProjectReport validateProject(const std::filesystem::path& projectDir, const SemVer& engine,
                                            const ecs::ComponentCatalog& catalog);

// 프로젝트를 실행용으로 연다 (v0.2 M1-T2): validateProject 와 같은 검사를 하고, 오류가 하나라도 있으면 ValidationFailed
// (처음 5 개의 문제를 메시지에). 성공하면 콘텐츠와 시작 월드 (세이브 그대로 — 틱 · 일시정지 상태 포함)
struct OpenedProject {
    GameManifest manifest;
    std::unique_ptr<content::ContentDatabase> content; // world 가 가리킨다 — world 보다 오래 살아야 한다 (선언 순서)
    std::unique_ptr<sim::SimulationWorld> world;
    std::vector<std::string> warnings; // 검사 경고 (P5 해시 · 로드 경고)를 describe() 로
};
[[nodiscard]] Expected<OpenedProject> openProject(const std::filesystem::path& projectDir, const SemVer& engine,
                                                  const ecs::ComponentCatalog& catalog);

// 새 프로젝트 만들기 (v0.2 M1-T4, ADR-0043). 템플릿 = 시나리오 하나 또는 빈 월드:
//   빈 월드      내장 콘텐츠, 기본 경계 (16 × 16 청크), 일시정지로 시작 (편집하고 실행한다)
//   시나리오     그 시나리오가 요구하는 팩(+ requires)을 contentRoot 에서 <프로젝트>/content 로 복사하고, 시나리오를 한
//   틱
//                돌린 월드(초기 배치가 적용된 상태)를 시작 월드로 — 이후 시나리오의 보충 · 스크립트는 따라오지 않는다
// 폴더는 없거나 비어 있어야 한다 (덮어쓰지 않는다 — AlreadyExists). 옆의 임시 폴더에 만들고 validateProject 를 통과하면
// 제자리로 옮긴다. 엔진 범위는 1.0 전이면 ">=<엔진> <1.0.0", 그 뒤면 "^<major>"
struct NewProjectOptions {
    std::string gameId;
    std::string name;                  // 비면 gameId 의 마지막 마디
    std::string fromScenario;          // 비면 빈 월드
    std::filesystem::path contentRoot; // fromScenario 의 팩을 찾을 곳
    u64 seed = 1;
    // 비면 게임 모드 없음 (schemaVersion 1). 있으면 schemaVersion 2 + gameMode — 템플릿 콘텐츠에 있어야 한다
    // (없으면 NotFound, 있는 모드를 알려 준다). v0.2 M3-T3 — 데모 모드 (sbx new --mode)
    std::string gameMode;
};
struct NewProjectResult {
    GameManifest manifest;
    std::vector<std::string> copiedPacks; // 복사한 팩 id (로드 순서)
    u64 worldTick = 0;
    usize worldEntities = 0;
};
[[nodiscard]] Expected<NewProjectResult> createProject(const std::filesystem::path& projectDir,
                                                       const NewProjectOptions& options, const SemVer& engine,
                                                       const ecs::ComponentCatalog& catalog);
// 새 프로젝트의 엔진 범위 문자열 (위 규칙)
[[nodiscard]] std::string defaultEngineRange(const SemVer& engine);

// 프로젝트를 돌리는 서버 · 싱글플레이의 세이브 · 리플레이 폴더 기본값 (--saves 가 없을 때 — v0.2 M1-T5, ADR-0044):
// <프로젝트>/worlds. 에디터 "월드" 창의 저장 · 불러오기가 프로젝트의 월드를 다룬다
[[nodiscard]] std::filesystem::path projectSavesRoot(const std::filesystem::path& projectDir);
// 시작 월드의 세이브 이름: entryWorld 가 "worlds/<이름>" 이면 그 이름 (Ctrl+S 기본값 — 편집한 맵이 게임의 시작 월드가
// 된다), 아니면 nullopt (세이브 폴더 밖이다)
[[nodiscard]] std::optional<std::string> entryWorldSaveName(const GameManifest& manifest);

// dir 에 매니페스트 파일이 있나 (서버 · 클라이언트 --world 가 프로젝트와 세이브 폴더를 가른다)
[[nodiscard]] bool isProjectFolder(const std::filesystem::path& dir);

// 경로 형식 검사 (P4): 비지 않은 상대 경로, '/' 구분, 마디는 [A-Za-z0-9_.-] 이고 "." · ".." · '.' 로 끝남 ·
// Windows 예약 이름(con, nul, com1 …)이 아니다. 문제가 없으면 빈 optional, 있으면 이유
[[nodiscard]] std::optional<std::string> checkRelativePath(std::string_view path);

} // namespace sbx::project
