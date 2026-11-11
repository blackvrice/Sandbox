#include "editor/services/BuildService.hpp"

#include <chrono>
#include <format>

#include "core/package/GamePackage.hpp"
#include "core/project/GameProject.hpp"

namespace sbx::editor::services {
namespace {

namespace fs = std::filesystem;

void addIssues(BuildReport& r, const std::vector<content::ContentIssue>& issues) {
    for (const content::ContentIssue& i : issues) {
        (i.severity == content::ContentIssue::Severity::Error ? r.errors : r.warnings).push_back(i.describeWithHelp());
    }
}

} // namespace

BuildService::BuildService(const ecs::ComponentCatalog& catalog, SemVer engine, fs::path engineAssets)
    : m_catalog(catalog), m_engine(std::move(engine)), m_engineAssets(std::move(engineAssets)) {}

BuildService::~BuildService() {
    join();
}

void BuildService::join() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

BuildReport BuildService::check(const fs::path& project) const {
    const auto t0 = std::chrono::steady_clock::now();
    BuildReport r;
    r.checkOnly = true;
    if (!project::isProjectFolder(project)) {
        r.errors.push_back(std::format("게임 프로젝트 폴더가 아니다 ({} 이 없다)", project::kManifestFile));
        return r;
    }
    const project::ProjectReport p =
        project::validateProject(project, m_engine, m_catalog, {.toolFiles = true, .engineAssets = m_engineAssets});
    addIssues(r, p.issues);
    if (p.manifestRead) {
        r.gameId = p.manifest.gameId;
        r.version = p.manifest.version.toString();
    }
    r.groups = p.assetGroups;
    r.ok = r.errors.empty();
    r.seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

BuildReport BuildService::buildNow(const BuildRequest& request) const {
    const auto t0 = std::chrono::steady_clock::now();
    BuildReport r = check(request.project);
    r.checkOnly = false;
    if (!r.ok) {
        r.errors.insert(
            r.errors.begin(),
            std::format("검사 오류 {} 개 — 빌드하지 않았다 (누락 에셋 A6 · id 없는 에셋 A2 …)", r.errors.size()));
        return r;
    }
    const Expected<package::BuildResult> built =
        package::buildPackage(request.project, request.out, m_engine, m_catalog, {.profile = request.profile});
    if (!built) {
        r.ok = false;
        r.errors.push_back(built.error().describe());
        return r;
    }
    r.file = built->file;
    r.gameId = built->index.gameId;
    r.version = built->index.version;
    r.files = built->index.files.size();
    r.bytes = built->bytes;
    r.digest = built->index.digest;
    addIssues(r, built->profileIssues);
    // 부르지 않는 에셋 (built->unreferencedAssets) 은 앞 검사의 A8 경고가 이미 말했다 — 두 번 적지 않는다
    r.groups.clear();
    for (const asset::GroupSummary& g : built->assetGroups) {
        r.groups.push_back(std::format("{} — {} 개 · {} 바이트", g.name, g.ids.size(), g.bytes));
    }
    r.seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

bool BuildService::start(BuildRequest request) {
    // 돌고 있거나, 끝났지만 보고를 아직 가져가지 않았으면 (m_running 이 false 면 일꾼은 m_done 을 다 썼다)
    if (m_running.load() || m_done) {
        return false;
    }
    join(); // 앞의 일꾼 (이미 끝났다)
    m_running.store(true);
    m_worker = std::thread([this, req = std::move(request)] {
        auto report = std::make_unique<BuildReport>(buildNow(req));
        m_done = std::move(report);
        m_running.store(false); // release — 메인이 running() 이 false 를 본 뒤 m_done 을 읽는다
    });
    return true;
}

std::optional<BuildReport> BuildService::poll() {
    if (m_running.load() || !m_done) {
        return std::nullopt;
    }
    join();
    std::optional<BuildReport> r(std::move(*m_done));
    m_done.reset();
    return r;
}

} // namespace sbx::editor::services
