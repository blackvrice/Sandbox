#pragma once
// 프로젝트 템플릿 (v0.10 M6-T2, ADR-0144). docs/15-BUILD.md 7장 · docs/18-PLATFORM.md.
//
// 템플릿 = template.json 을 가진 예제 게임 프로젝트 (samples/<폴더>/) — 예제 (Studio 홈) 와 같은 폴더를 쓴다. 새
// 프로젝트는 그 폴더를 복사하고 (.sandbox · build · template.json 은 빼고) 매니페스트의 gameId · name · version ·
// engine 을 새로 쓰고, SDK (sdk/sandbox.d.ts · tsconfig.json) 를 이 엔진으로 다시 만든 뒤, 검사 (오류 · 경고 0 · 시작
// 월드가 열린다) 를 통과하면 제자리로 옮긴다 (createProject 와 같은 임시 폴더 규칙). 세이브의 게임 결속 (world.json
// "game") 도 새 gameId 로.
//
//   template.json  { "format": "sandbox.template", "version": 1, "id": "minigame", "title": "…", "description": "…",
//                    "order": 1 }        — id = [a-z0-9_-] 1 ~ 32, 폴더 안에서 하나 (같은 id 는 뒤를 버리고 경고)

#include <filesystem>
#include <string>
#include <vector>

#include "core/project/GameProject.hpp"

namespace sbx::project {

inline constexpr std::string_view kTemplateFile = "template.json";

struct ProjectTemplateInfo {
    std::string id;          // sbx new --template · Studio 새 프로젝트의 고르기
    std::string title;       // 화면 이름
    std::string description; // 한두 줄
    i32 order = 0;           // 목록 차례 (작은 것 먼저, 같으면 id)
    std::filesystem::path dir;
};

// samplesRoot 아래 폴더 중 template.json 이 있는 게임 프로젝트 (order · id 차례). 읽지 못한 template.json 은 건너뛰고
// warnings 에 한 줄
[[nodiscard]] std::vector<ProjectTemplateInfo> listProjectTemplates(const std::filesystem::path& samplesRoot,
                                                                    std::vector<std::string>* warnings = nullptr);

// template.json 하나를 읽는다 (형식 · id 검사)
[[nodiscard]] Expected<ProjectTemplateInfo> readProjectTemplate(const std::filesystem::path& projectDir);

// 템플릿에서 새 프로젝트 — 폴더는 없거나 비어 있어야 한다 (AlreadyExists). name 이 비면 gameId 의 마지막 마디
[[nodiscard]] Expected<NewProjectResult> createProjectFromTemplate(const ProjectTemplateInfo& tmpl,
                                                                   const std::filesystem::path& projectDir,
                                                                   std::string_view gameId, std::string_view name,
                                                                   const SemVer& engine,
                                                                   const ecs::ComponentCatalog& catalog);

} // namespace sbx::project
