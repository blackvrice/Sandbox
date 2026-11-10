#include "editor/services/ProjectService.hpp"

#include <algorithm>
#include <format>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/project/GameProject.hpp"
#include "core/scenarios/Scenario.hpp"
#include "editor/services/StudioSettings.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::editor::services {
namespace {

namespace fs = std::filesystem;

constexpr usize kMaxProblems = 30;

fs::path normalized(const fs::path& dir) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(dir, ec);
    return ec ? fs::absolute(dir, ec).lexically_normal() : p;
}

ProjectSummary summarize(const fs::path& dir, const project::ProjectReport& r) {
    ProjectSummary s;
    s.dir = normalized(dir);
    if (r.manifestRead) {
        s.gameId = r.manifest.gameId;
        s.name = r.manifest.name;
        s.version = r.manifest.version.toString();
    }
    s.errors = r.errorCount();
    s.warnings = r.warningCount();
    for (const content::ContentIssue& i : r.issues) {
        if (s.problems.size() >= kMaxProblems) {
            break;
        }
        s.problems.push_back(i.describe());
    }
    return s;
}

} // namespace

ProjectService::ProjectService(const ecs::ComponentCatalog& catalog, SemVer engine, fs::path contentRoot,
                               fs::path recentFile, fs::path engineAssets)
    : m_catalog(catalog), m_engine(std::move(engine)), m_contentRoot(std::move(contentRoot)),
      m_recentFile(std::move(recentFile)), m_engineAssets(std::move(engineAssets)) {
    m_templates.push_back({"empty", "빈 월드", ""});
    for (const scenario::ScenarioEntry& e : scenario::scenarioList()) {
        m_templates.push_back({std::string(e.name), std::string(e.description), std::string(e.name)});
    }
    loadRecent();
}

Expected<ProjectSummary> ProjectService::inspect(const fs::path& dir) const {
    if (!project::isProjectFolder(dir)) {
        return makeError(ErrorCode::NotFound,
                         std::format("게임 프로젝트 폴더가 아니다 ({} 이 없다)", project::kManifestFile),
                         io::displayPath(dir));
    }
    const project::ProjectReport r =
        project::validateProject(dir, m_engine, m_catalog, {.toolFiles = true, .engineAssets = m_engineAssets});
    return summarize(dir, r);
}

Expected<ProjectSummary> ProjectService::open(const fs::path& dir) {
    Expected<ProjectSummary> s = inspect(dir);
    if (!s) {
        return s;
    }
    if (s->errors > 0) {
        std::string first;
        for (usize i = 0; i < std::min<usize>(5, s->problems.size()); ++i) {
            first += "\n  " + s->problems[i];
        }
        return makeError(
            ErrorCode::ValidationFailed,
            std::format("프로젝트에 오류 {} 개 — 열지 않았다 (sbx check 와 같은 검사){}", s->errors, first),
            io::displayPath(dir));
    }
    remember(*s);
    return s;
}

Expected<ProjectSummary> ProjectService::create(const NewProjectRequest& request) {
    const auto t = std::ranges::find(m_templates, request.templateId, &ProjectTemplate::id);
    if (t == m_templates.end()) {
        return makeError(ErrorCode::NotFound, std::format("템플릿 '{}' 가 없다", request.templateId));
    }
    project::NewProjectOptions o;
    o.gameId = request.gameId;
    o.name = request.name;
    o.fromScenario = t->scenario;
    o.contentRoot = m_contentRoot;
    const Expected<project::NewProjectResult> made = project::createProject(request.dir, o, m_engine, m_catalog);
    if (!made) {
        return std::unexpected(made.error());
    }
    Expected<ProjectSummary> s = inspect(request.dir);
    if (s) {
        remember(*s);
    }
    return s;
}

void ProjectService::remember(const ProjectSummary& s) {
    std::erase_if(m_recent, [&](const RecentProject& r) { return r.dir == s.dir; });
    m_recent.insert(m_recent.begin(), RecentProject{s.dir, s.name, s.gameId, false});
    if (m_recent.size() > kMaxRecent) {
        m_recent.resize(kMaxRecent);
    }
    saveRecent();
}

void ProjectService::forget(const fs::path& dir) {
    const fs::path key = normalized(dir);
    std::erase_if(m_recent, [&](const RecentProject& r) { return r.dir == key || r.dir == dir; });
    saveRecent();
}

std::vector<std::string> ProjectService::takeWarnings() {
    std::vector<std::string> out;
    out.swap(m_warnings);
    return out;
}

void ProjectService::loadRecent() {
    m_recent.clear();
    std::error_code ec;
    if (m_recentFile.empty() || !fs::exists(m_recentFile, ec)) {
        return;
    }
    const Expected<std::string> text = io::readFile(m_recentFile);
    const nlohmann::json j = text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
    if (!j.is_object()) {
        // 깨진 파일 — 옆으로 옮기고 빈 목록 (사용자가 고쳐 볼 수 있게 지우지 않는다). studio.json 은 다른 Studio 설정도
        // 든다 (v0.8 M6-T6 — StudioSettings)
        m_warnings.push_back(moveStudioFileAside(m_recentFile));
        return;
    }
    const auto list = j.find("recentProjects");
    if (list == j.end()) {
        return; // 아직 없다 (레이아웃 같은 다른 설정만 있는 파일)
    }
    if (!list->is_array()) {
        m_warnings.push_back("Studio 설정 recentProjects 는 배열이어야 한다 — 최근 목록을 비웠다");
        return;
    }
    for (const nlohmann::json& e : *list) {
        if (m_recent.size() >= kMaxRecent) {
            break;
        }
        if (!e.is_object() || !e.contains("path") || !e["path"].is_string()) {
            continue; // 모르는 칸 — 건너뛴다
        }
        RecentProject r;
        r.dir = fs::path(std::u8string(reinterpret_cast<const char8_t*>(e["path"].get_ref<const std::string&>().data()),
                                       e["path"].get_ref<const std::string&>().size()));
        r.name = e.value("name", std::string());
        r.gameId = e.value("gameId", std::string());
        r.missing = !project::isProjectFolder(r.dir);
        m_recent.push_back(std::move(r));
    }
}

void ProjectService::saveRecent() {
    if (m_recentFile.empty()) {
        return;
    }
    nlohmann::json list = nlohmann::json::array();
    for (const RecentProject& r : m_recent) {
        const std::u8string p = r.dir.generic_u8string();
        list.push_back({{"path", std::string(p.begin(), p.end())}, {"name", r.name}, {"gameId", r.gameId}});
    }
    if (const Expected<void> w = writeStudioSetting(m_recentFile, "recentProjects", list.dump()); !w) {
        m_warnings.push_back(std::format("최근 프로젝트를 저장하지 못했다 — {}", w.error().describe()));
    }
}

} // namespace sbx::editor::services
