#pragma once
// GameProject — 게임 하나의 편집 원본 폴더와 그 매니페스트(sandbox.project.json). docs/18-PLATFORM.md 5장, ADR-0040.
//
//   MyGame/
//     sandbox.project.json   매니페스트 (이 파일)
//     content/<팩>/          콘텐츠 팩 — 11-CONTENT-SCHEMA 그대로 (pack.json 의 id 로 찾는다)
//     worlds/<이름>/         월드 = 세이브 폴더 형식 그대로 (world.json · entities.jsonl · chunks/, 09 6장)
//
// 매니페스트 schemaVersion 1 (모르는 키는 오류 — 새 키는 schemaVersion 을 올려 더한다):
//   {"schemaVersion": 1, "gameId": "studio.arena", "name": "Arena", "version": "0.1.0",
//    "engine": ">=0.1.0 <1.0.0", "description": "...", "content": {"packs": ["arena"]}, "entryWorld": "worlds/main"}
//
// 문제는 ContentLoader 와 같은 ContentIssue 로 모두 모은다 (첫 오류에서 멈추지 않는다). 규칙 이름:
//   P1 형식 (JSON · 모르는 키 · 타입 · 필수 키)   P2 식별자 (gameId · 팩 id)   P3 버전 (schemaVersion · version ·
//   engine 범위 · 이 엔진이 범위 밖)   P4 경로 (entryWorld 형식 · 폴더 · 월드 열기)   P5 콘텐츠 참조 (팩 폴더 ·
//   월드가 쓰는 팩 · contentHash)   + 팩의 V1~V7 (file 은 "content/<팩 폴더>/…")

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
inline constexpr u32 kManifestSchemaVersion = 1;

struct GameManifest {
    u32 schemaVersion = kManifestSchemaVersion;
    std::string gameId;                    // "studio.arena" — 소문자·숫자·'_' 마디 둘 이상을 '.' 로 (≤ 64 바이트)
    std::string name;                      // 표시 이름 (1~64 글자, 제어 문자 없음)
    SemVer version;                        // 게임 버전 (SemVer)
    VersionRange engine;                   // 이 게임을 여는 엔진 버전 범위
    std::string description;               // 선택 (≤ 1024 글자)
    std::vector<std::string> contentPacks; // <프로젝트>/content 에서 찾을 팩 id (비면 내장 콘텐츠만)
    std::string entryWorld;                // 시작 월드 폴더 — 프로젝트 기준 상대 경로, '/' 구분 ("worlds/main")

    bool operator==(const GameManifest&) const = default;
};

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

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] usize errorCount() const noexcept;
    [[nodiscard]] usize warningCount() const noexcept;
};

// engine = 지금 엔진의 버전 (BuildInfo). 매니페스트 → 엔진 범위 → 팩 (V1~V7) → 시작 월드 (형식 · 팩 · 열기) 순서
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

// dir 에 매니페스트 파일이 있나 (서버 · 클라이언트 --world 가 프로젝트와 세이브 폴더를 가른다)
[[nodiscard]] bool isProjectFolder(const std::filesystem::path& dir);

// 경로 형식 검사 (P4): 비지 않은 상대 경로, '/' 구분, 마디는 [A-Za-z0-9_.-] 이고 "." · ".." · '.' 로 끝남 ·
// Windows 예약 이름(con, nul, com1 …)이 아니다. 문제가 없으면 빈 optional, 있으면 이유
[[nodiscard]] std::optional<std::string> checkRelativePath(std::string_view path);

} // namespace sbx::project
