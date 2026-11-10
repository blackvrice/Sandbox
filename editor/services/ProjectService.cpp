#include "editor/services/ProjectService.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
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

// 매니페스트만 (검사 없이 — 홈의 카드 · 예제 목록). 읽지 못하면 nullopt
std::optional<project::GameManifest> readManifestOf(const fs::path& dir) {
    const Expected<std::string> text = io::readFile(dir / std::string(project::kManifestFile));
    if (!text) {
        return std::nullopt;
    }
    project::ManifestParseResult parsed = project::parseManifest(*text);
    if (!parsed.ok()) {
        return std::nullopt;
    }
    return std::move(parsed.manifest);
}

std::string utcNow() {
    // 날짜 · 시각을 정수로 (chrono 형식 지정자는 macOS libc++ 에서 아직 고르지 않다)
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto day = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day ymd{day};
    const std::chrono::hh_mm_ss hms{now - day};
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()), hms.hours().count(),
                       hms.minutes().count(), hms.seconds().count());
}

} // namespace

ProjectService::ProjectService(const ecs::ComponentCatalog& catalog, SemVer engine, fs::path contentRoot,
                               fs::path recentFile, fs::path engineAssets, fs::path samplesRoot)
    : m_catalog(catalog), m_engine(std::move(engine)), m_contentRoot(std::move(contentRoot)),
      m_recentFile(std::move(recentFile)), m_engineAssets(std::move(engineAssets)) {
    // 예제 (v0.8 UI-2): samples/<이름>/ 중 게임 프로젝트 — 이름 순
    std::error_code ec;
    if (!samplesRoot.empty() && fs::is_directory(samplesRoot, ec)) {
        for (fs::directory_iterator it(samplesRoot, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec) || !project::isProjectFolder(it->path())) {
                continue;
            }
            ExampleProject e{it->path(), it->path().filename().string(), {}, {}, {}};
            if (const auto m = readManifestOf(it->path())) {
                e.name = m->name.empty() ? e.name : m->name;
                e.gameId = m->gameId;
                e.version = m->version.toString();
                e.description = m->description;
            }
            m_examples.push_back(std::move(e));
        }
        std::ranges::sort(m_examples, {}, &ExampleProject::name);
    }
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

Expected<ProjectSummary> ProjectService::copyExample(const fs::path& example, const fs::path& dest) {
    if (!project::isProjectFolder(example)) {
        return makeError(ErrorCode::NotFound, "예제가 게임 프로젝트 폴더가 아니다", io::displayPath(example));
    }
    std::error_code ec;
    if (fs::exists(dest, ec) && !fs::is_empty(dest, ec)) {
        return makeError(ErrorCode::AlreadyExists, "복사할 폴더가 비어 있지 않다 — 없거나 빈 폴더를 고른다",
                         io::displayPath(dest));
    }
    fs::create_directories(dest, ec);
    // 캐시 · 빌드 출력은 빼고 복사 (예제 폴더는 고치지 않는다)
    for (fs::recursive_directory_iterator it(example, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), example, ec);
        const std::string top = rel.begin() != rel.end() ? rel.begin()->string() : std::string();
        if (top == ".sandbox" || top == "build") {
            if (it->is_directory(ec)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        const fs::path to = dest / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(to, ec);
        } else {
            fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            return makeError(ErrorCode::IoError, std::format("예제를 복사하지 못했다: {}", ec.message()),
                             io::displayPath(to));
        }
    }
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("예제를 읽지 못했다: {}", ec.message()),
                         io::displayPath(example));
    }
    return open(dest);
}

void ProjectService::remember(const ProjectSummary& s) {
    std::erase_if(m_recent, [&](const RecentProject& r) { return r.dir == s.dir; });
    m_recent.insert(m_recent.begin(), RecentProject{s.dir, s.name, s.gameId, false, s.version, utcNow()});
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
        r.version = e.value("version", std::string());
        r.openedAt = e.value("openedAt", std::string());
        r.missing = !project::isProjectFolder(r.dir);
        if (!r.missing) {
            if (const auto m = readManifestOf(r.dir)) { // 지금 판 · 이름 (다른 곳에서 바뀌었을 수 있다)
                r.version = m->version.toString();
                r.name = m->name.empty() ? r.name : m->name;
            }
        }
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
        list.push_back({{"path", std::string(p.begin(), p.end())},
                        {"name", r.name},
                        {"gameId", r.gameId},
                        {"version", r.version},
                        {"openedAt", r.openedAt}});
    }
    if (const Expected<void> w = writeStudioSetting(m_recentFile, "recentProjects", list.dump()); !w) {
        m_warnings.push_back(std::format("최근 프로젝트를 저장하지 못했다 — {}", w.error().describe()));
    }
}

} // namespace sbx::editor::services
