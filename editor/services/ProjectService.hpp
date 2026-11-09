#pragma once
// Studio 프로젝트 서비스 (v0.8 M6-T1 — docs/25-STUDIO-ARCHITECTURE.md 2 · 5장 ①②, docs/23-ROADMAP.md 1.1, ADR-0111). 새
// 프로젝트 · 열기 · 최근 — UI 를 모른다 (Studio 홈 화면이 부른다, 시험은 헤드리스).
//
// 새로 만들기 = core::createProject (sbx new 와 같은 규칙 — 폴더는 없거나 비어 있어야, 옆 임시 폴더에 만들고 검사를
// 지나면 제자리로) + 검사 요약. 템플릿 = 빈 월드 · 등록된 시나리오 (M6-T2 의 템플릿 셋이 나오면 그것으로). 열기 = 검사
// (validateProject — sbx check 와 같은 것) — 오류가 있으면 열지 않고 문제를 돌려준다 (경고는 요약에). 월드를 띄우는
// 것은 앱 (세션) 의 몫. 최근 = 사용자 설정 폴더의 studio.json {"recentProjects": [{path, name, gameId}]} — 새로
// 만들거나 연 것이 맨 앞, 같은 경로는 하나, 10 개까지. 읽을 때 없어진 폴더는 missing 으로 표시 (지우지 않는다 — 외장
// 디스크). 깨진 파일은 옆으로 옮기고 (studio.json.broken) 빈 목록 + 경고 (M1-T3 의 규칙). 게임 프로젝트 데이터를 사용자
// 폴더에 쓰지 않는다 (25 S4).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::ecs {
class ComponentCatalog;
}

namespace sbx::editor::services {

struct ProjectTemplate {
    std::string id;       // "empty" · 시나리오 이름
    std::string title;    // 화면 이름
    std::string scenario; // createProject 의 fromScenario (빈 월드 = "")
};

struct NewProjectRequest {
    std::filesystem::path dir;
    std::string gameId; // "<작성자>.<게임>"
    std::string name;   // 비면 gameId 의 마지막 마디
    std::string templateId = "empty";
};

struct ProjectSummary {
    std::filesystem::path dir;
    std::string gameId;
    std::string name;
    std::string version;
    usize errors = 0;
    usize warnings = 0;
    std::vector<std::string> problems; // 문제마다 한 줄 (규칙 · 위치 · 문장 — 30 개까지)
};

struct RecentProject {
    std::filesystem::path dir;
    std::string name;
    std::string gameId;
    bool missing = false; // 목록을 읽을 때 폴더 (sandbox.project.json) 가 없었다
};

class ProjectService {
public:
    static constexpr usize kMaxRecent = 10;

    // catalog 는 서비스보다 오래 산다. contentRoot = 시나리오 팩을 찾을 곳, recentFile 이 비면 최근 목록을 저장하지
    // 않는다, engineAssets = 엔진 에셋 루트 (게임 UI material 검사 U8 — sbx check 와 같게, 비면 보지 않는다)
    ProjectService(const ecs::ComponentCatalog& catalog, SemVer engine, std::filesystem::path contentRoot,
                   std::filesystem::path recentFile, std::filesystem::path engineAssets = {});

    [[nodiscard]] const std::vector<ProjectTemplate>& templates() const noexcept { return m_templates; }
    Expected<ProjectSummary> create(const NewProjectRequest& request);
    // 오류가 있으면 ValidationFailed (문제 처음 다섯을 메시지에 — 전부는 inspect 로)
    Expected<ProjectSummary> open(const std::filesystem::path& dir);
    // 검사만 (최근 목록을 바꾸지 않는다)
    Expected<ProjectSummary> inspect(const std::filesystem::path& dir) const;

    [[nodiscard]] const std::vector<RecentProject>& recent() const noexcept { return m_recent; }
    void forget(const std::filesystem::path& dir); // 최근 목록에서 뺀다 (저장한다)
    // 최근 목록을 읽을 때의 경고 (깨진 파일 · 옮김) — 꺼내면 비운다
    [[nodiscard]] std::vector<std::string> takeWarnings();

private:
    void remember(const ProjectSummary& s);
    void loadRecent();
    void saveRecent();

    const ecs::ComponentCatalog& m_catalog;
    SemVer m_engine;
    std::filesystem::path m_contentRoot;
    std::filesystem::path m_recentFile;
    std::filesystem::path m_engineAssets;
    std::vector<ProjectTemplate> m_templates;
    std::vector<RecentProject> m_recent;
    std::vector<std::string> m_warnings;
};

} // namespace sbx::editor::services
