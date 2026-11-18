#include "core/project/ProjectTemplate.hpp"

#include <algorithm>
#include <format>

#include <nlohmann/json.hpp>

#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/io/FileIo.hpp"
#include "script/sdk/SdkGenerator.hpp"

namespace sbx::project {

namespace fs = std::filesystem;

namespace {

[[nodiscard]] bool validPackId(std::string_view id) noexcept {
    return !id.empty() && id.size() <= 32 && id != "core" &&
           std::ranges::all_of(id, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// 글에서 팩 id 낱말 from 을 to 로 — 앞이 id 글자 · '.' 가 아니고 뒤가 '.' · '/' · '"' 인 것만 ("ttt.board" · "ttt/x" ·
// "ttt"). 설명 글 안의 같은 글자 (다른 낱말의 일부) 는 건드리지 않는다
std::string renamePackWord(std::string_view text, std::string_view from, std::string_view to) {
    const auto idChar = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; };
    std::string out;
    out.reserve(text.size());
    usize i = 0;
    while (i < text.size()) {
        const usize end = i + from.size();
        if (text.compare(i, from.size(), from) == 0 && (i == 0 || !idChar(text[i - 1])) && end < text.size() &&
            (text[end] == '.' || text[end] == '/' || text[end] == '"')) {
            out += to;
            i = end;
        } else {
            out += text[i++];
        }
    }
    return out;
}

// 팩 바꾸기 (createProjectFromTemplate 의 packId) — 시작 월드의 다시 저장은 매니페스트를 쓴 뒤 (아래 reopenWorlds)
Expected<void> renameTemplatePack(const fs::path& staging, std::string_view fromView, std::string_view to,
                                  GameManifest& m) {
    const std::string from(fromView); // 부르는 쪽이 m.contentPacks 의 칸을 넘긴다 — 아래에서 그 칸을 바꾸므로 복사
    std::error_code ec;
    fs::path folder;
    for (fs::directory_iterator it(staging / "content", ec), end; !ec && it != end; it.increment(ec)) {
        auto text = io::readFile(it->path() / "pack.json");
        if (!text) {
            continue;
        }
        const nlohmann::json j = nlohmann::json::parse(*text, nullptr, false);
        if (j.is_object() && j.value("id", "") == from) {
            folder = it->path();
        }
    }
    if (folder.empty()) {
        return makeError(ErrorCode::NotFound, std::format("템플릿에 팩 '{}' 의 폴더가 없다", from),
                         io::displayPath(staging));
    }
    const fs::path target = staging / "content" / std::string(to);
    if (fs::exists(target, ec)) {
        return makeError(ErrorCode::AlreadyExists, std::format("팩 폴더 '{}' 가 이미 있다", to),
                         io::displayPath(target));
    }
    fs::rename(folder, target, ec);
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("팩 폴더를 옮기지 못했다: {}", ec.message()),
                         io::displayPath(folder));
    }
    // 글 파일 — 팩 안 (json · ts · md), 자동 플레이 (playthrough.json · tests/*.json), 시작 월드 (world.json ·
    // entities.jsonl)
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec)) {
        const auto ext = it->path().extension();
        if (it->is_regular_file(ec) && (ext == ".json" || ext == ".ts" || ext == ".md")) {
            files.push_back(it->path());
        }
    }
    files.push_back(staging / "playthrough.json");
    for (fs::directory_iterator it(staging / "tests", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    for (fs::directory_iterator it(staging / "worlds", ec), end; !ec && it != end; it.increment(ec)) {
        files.push_back(it->path() / "world.json");
        files.push_back(it->path() / "entities.jsonl");
    }
    for (const fs::path& f : files) {
        if (!fs::is_regular_file(f, ec)) {
            continue;
        }
        auto text = io::readFile(f);
        if (!text) {
            return std::unexpected(text.error());
        }
        std::string next = renamePackWord(*text, from, to);
        // 자동 플레이의 expect.hash 는 뺀다 — 컴포넌트 이름 (stableId) 이 바뀌어 같은 판도 해시가 달라진다 (결과 · 틱은
        // 그대로)
        if (f.parent_path() == staging || f.parent_path() == staging / "tests") {
            nlohmann::ordered_json j = nlohmann::ordered_json::parse(next, nullptr, false);
            if (j.is_object() && j.contains("expect") && j["expect"].is_object() && j["expect"].erase("hash") > 0) {
                next = j.dump(2) + "\n";
            }
        }
        if (next != *text) {
            if (auto w = io::writeFileAtomic(f, next); !w) {
                return std::unexpected(w.error());
            }
        }
    }
    std::ranges::replace(m.contentPacks, std::string(from), std::string(to));
    m.gameMode = renamePackWord("\"" + m.gameMode, from, to).substr(1); // "<옛>.<모드>" — 앞 따옴표가 낱말 경계
    return {};
}

// 바꾼 이름으로 시작 월드들을 다시 저장 — 컴포넌트 이름이 바뀌어 옛 worldHash 와 맞지 않으므로 해시 확인 없이 열고 새로
// 쓴다
Expected<void> reopenWorlds(const fs::path& staging, const GameManifest& m, const ecs::ComponentCatalog& catalog) {
    content::ContentLoadResult loaded = content::ContentLoader::load(staging / "content", m.contentPacks, catalog);
    if (!loaded.ok()) {
        std::string msg = "이름을 바꾼 팩을 읽지 못했다";
        for (const content::ContentIssue& i : loaded.issues) {
            msg += "\n  " + i.describe();
        }
        return makeError(ErrorCode::ValidationFailed, msg, io::displayPath(staging));
    }
    std::error_code ec;
    for (fs::directory_iterator it(staging / "worlds", ec), end; !ec && it != end; it.increment(ec)) {
        if (!fs::is_regular_file(it->path() / "world.json", ec)) {
            continue;
        }
        persist::LoadOptions lo;
        lo.verifyHash = false;
        auto w = persist::loadWorld(catalog, loaded.db, it->path(), lo);
        if (!w) {
            return std::unexpected(w.error());
        }
        persist::SaveOptions so{it->path().filename().string()};
        if (const auto summary = scenario::readSaveSummary(it->path()); summary && summary->game) {
            so.game = summary->game;
        }
        if (auto s = persist::saveWorld(*w->world, it->path(), so); !s) {
            return std::unexpected(s.error());
        }
    }
    return {};
}

[[nodiscard]] bool validTemplateId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 32) {
        return false;
    }
    return std::ranges::all_of(
        id, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}

[[nodiscard]] bool emptyOrMissing(const fs::path& dir) {
    std::error_code ec;
    return !fs::exists(dir, ec) || (fs::is_directory(dir, ec) && fs::is_empty(dir, ec));
}

// 세이브의 게임 결속 (world.json 의 "game".gameId — persist::GameBinding) 을 새 gameId 로. 결속이 없는 세이브는 그대로
Expected<void> rebindWorlds(const fs::path& projectDir, std::string_view gameId) {
    std::error_code ec;
    const fs::path worlds = projectDir / "worlds";
    if (!fs::is_directory(worlds, ec)) {
        return {};
    }
    for (fs::directory_iterator it(worlds, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path file = it->path() / "world.json";
        if (!fs::is_regular_file(file, ec)) {
            continue;
        }
        auto text = io::readFile(file);
        if (!text) {
            return std::unexpected(text.error());
        }
        nlohmann::ordered_json j = nlohmann::ordered_json::parse(*text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            return makeError(ErrorCode::ParseError, "world.json 을 읽지 못했습니다", io::displayPath(file));
        }
        const auto g = j.find("game");
        if (g == j.end() || !g->is_object()) {
            continue;
        }
        (*g)["gameId"] = std::string(gameId);
        if (auto w = io::writeFileAtomic(file, j.dump(2) + "\n"); !w) {
            return std::unexpected(w.error());
        }
    }
    return {};
}

} // namespace

Expected<ProjectTemplateInfo> readProjectTemplate(const fs::path& projectDir) {
    const fs::path file = projectDir / kTemplateFile;
    const std::string where = io::displayPath(file);
    auto text = io::readFile(file);
    if (!text) {
        return std::unexpected(text.error());
    }
    const nlohmann::json j = nlohmann::json::parse(*text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return makeError(ErrorCode::ParseError, "template.json 은 JSON 객체다", where);
    }
    if (j.value("format", std::string()) != "sandbox.template" || !j.contains("version") ||
        !j["version"].is_number_integer() || j["version"].get<i64>() != 1) {
        return makeError(ErrorCode::ParseError, "\"format\": \"sandbox.template\", \"version\": 1 이 필요하다", where);
    }
    ProjectTemplateInfo t;
    t.id = j.value("id", std::string());
    if (!validTemplateId(t.id)) {
        return makeError(ErrorCode::ParseError,
                         std::format("템플릿 id '{}' — 소문자 · 숫자 · _ · - 로 1 ~ 32 글자", t.id), where);
    }
    t.title = j.value("title", t.id);
    t.description = j.value("description", std::string());
    if (const auto o = j.find("order"); o != j.end() && o->is_number_integer()) {
        t.order = o->get<i32>();
    }
    t.dir = projectDir;
    if (!isProjectFolder(projectDir)) {
        return makeError(ErrorCode::ParseError, "템플릿 폴더에 sandbox.project.json 이 없다", where);
    }
    return t;
}

std::vector<ProjectTemplateInfo> listProjectTemplates(const fs::path& samplesRoot, std::vector<std::string>* warnings) {
    std::vector<ProjectTemplateInfo> out;
    std::error_code ec;
    if (samplesRoot.empty() || !fs::is_directory(samplesRoot, ec)) {
        return out;
    }
    std::vector<fs::path> dirs;
    for (fs::directory_iterator it(samplesRoot, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec) && fs::is_regular_file(it->path() / kTemplateFile, ec)) {
            dirs.push_back(it->path());
        }
    }
    std::ranges::sort(dirs); // 같은 id 면 앞 폴더가 이긴다 — 디렉터리 차례와 무관하게
    for (const fs::path& d : dirs) {
        auto t = readProjectTemplate(d);
        if (!t) {
            if (warnings != nullptr) {
                warnings->push_back(t.error().describe());
            }
            continue;
        }
        if (std::ranges::any_of(out, [&](const ProjectTemplateInfo& o) { return o.id == t->id; })) {
            if (warnings != nullptr) {
                warnings->push_back(std::format("템플릿 id '{}' 가 겹친다 — {} 를 버린다", t->id, io::displayPath(d)));
            }
            continue;
        }
        out.push_back(std::move(*t));
    }
    std::ranges::sort(out, [](const ProjectTemplateInfo& a, const ProjectTemplateInfo& b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return out;
}

Expected<NewProjectResult> createProjectFromTemplate(const ProjectTemplateInfo& tmpl, const fs::path& projectDirArg,
                                                     std::string_view gameId, std::string_view name,
                                                     const SemVer& engine, const ecs::ComponentCatalog& catalog,
                                                     std::string_view packId) {
    fs::path projectDir = projectDirArg.lexically_normal();
    if (projectDir.filename().empty()) {
        projectDir = projectDir.parent_path();
    }
    const std::string where = io::displayPath(projectDir);
    if (!emptyOrMissing(projectDir)) {
        return makeError(ErrorCode::AlreadyExists, "폴더가 비어 있지 않습니다 — 새 프로젝트는 빈 폴더에만 만든다",
                         where);
    }
    if (!isValidGameId(gameId)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("게임 id '{}' — \"<작성자>.<게임>\" (소문자 · 숫자 · _, 점으로 잇는다)", gameId),
                         where);
    }
    // 1. 템플릿 매니페스트
    auto manifestText = io::readFile(tmpl.dir / kManifestFile);
    if (!manifestText) {
        return std::unexpected(manifestText.error());
    }
    const ManifestParseResult parsed = parseManifest(*manifestText);
    if (!parsed.ok()) {
        return makeError(ErrorCode::ValidationFailed, "템플릿의 매니페스트를 읽지 못했다", io::displayPath(tmpl.dir));
    }
    GameManifest m = parsed.manifest;
    m.gameId = std::string(gameId);
    m.name = name.empty() ? m.gameId.substr(m.gameId.rfind('.') + 1) : std::string(name);
    m.version = SemVer{0, 1, 0, {}, {}};
    auto range = parseVersionRange(defaultEngineRange(engine));
    if (!range) {
        return std::unexpected(range.error());
    }
    m.engine = std::move(*range);

    // 2. 옆의 임시 폴더에 복사 (캐시 · 빌드 출력 · 템플릿 표시는 빼고)
    fs::path staging = projectDir;
    staging += ".sbx-new";
    std::error_code ec;
    fs::remove_all(staging, ec);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{staging};
    if (auto r = io::createDirectories(staging); !r) {
        return std::unexpected(r.error());
    }
    for (fs::recursive_directory_iterator it(tmpl.dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), tmpl.dir, ec);
        const std::string top = rel.begin() != rel.end() ? rel.begin()->string() : std::string();
        if (top == ".sandbox" || top == "build" || rel == fs::path(kTemplateFile)) {
            if (it->is_directory(ec)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        const fs::path to = staging / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(to, ec);
        } else {
            fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            return makeError(ErrorCode::IoError, std::format("템플릿을 복사하지 못했다: {}", ec.message()),
                             io::displayPath(to));
        }
    }
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("템플릿을 읽지 못했다: {}", ec.message()),
                         io::displayPath(tmpl.dir));
    }
    // 2b. 팩 이름 바꾸기 (--pack-id — v0.12)
    if (!packId.empty()) {
        if (!validPackId(packId)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("팩 id '{}' — 소문자 · 숫자 · '_' (32 바이트까지, core 는 엔진)", packId),
                             where);
        }
        if (m.contentPacks.size() != 1) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("템플릿 '{}' 의 팩이 하나가 아니다 ({} 개) — 팩 이름을 바꿀 수 없다", tmpl.id,
                                         m.contentPacks.size()),
                             where);
        }
        if (m.contentPacks.front() != packId) {
            if (auto r = renameTemplatePack(staging, m.contentPacks.front(), packId, m); !r) {
                return std::unexpected(r.error());
            }
        }
    }
    // 3. 매니페스트 · 세이브 결속 · SDK (이 엔진의 Host 표)
    if (auto r = saveManifest(staging, m); !r) {
        return std::unexpected(r.error());
    }
    if (!packId.empty()) {
        if (auto r = reopenWorlds(staging, m, catalog); !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = rebindWorlds(staging, m.gameId); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = io::createDirectories(staging / script::sdk::kProjectSdkDir); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = io::writeFileAtomic(staging / script::sdk::kProjectSdkDir / script::sdk::kDtsFileName,
                                     script::sdk::generateSandboxDts());
        !r) {
        return std::unexpected(r.error());
    }
    if (auto r = io::writeFileAtomic(staging / script::sdk::kTsconfigFileName, script::sdk::generateTsconfig()); !r) {
        return std::unexpected(r.error());
    }

    // 4. 검사 — 경고도 없어야 한다 (템플릿은 바로 도는 틀이다)
    const ProjectReport report = validateProject(staging, engine, catalog);
    if (!report.ok() || report.warningCount() != 0 || !report.worldOpened) {
        std::string msg = std::format("템플릿 '{}' 로 만든 프로젝트가 검사를 통과하지 못했습니다", tmpl.id);
        for (const content::ContentIssue& i : report.issues) {
            msg += "\n  " + i.describeWithHelp();
        }
        return makeError(ErrorCode::ValidationFailed, msg, where);
    }
    if (fs::exists(projectDir, ec)) {
        fs::remove(projectDir, ec); // 빈 폴더 (위에서 확인)
    }
    if (projectDir.has_parent_path()) {
        fs::create_directories(projectDir.parent_path(), ec);
    }
    ec = io::renameRetrying(staging, projectDir); // 백신 · 색인기가 잠깐 잡으면 다시 (Windows — createProject 와 같다)
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("프로젝트 폴더를 넣을 수 없습니다: {}", ec.message()), where);
    }
    NewProjectResult result;
    result.manifest = m;
    result.copiedPacks = m.contentPacks;
    result.worldTick = report.worldTick;
    result.worldEntities = report.worldEntities;
    return result;
}

} // namespace sbx::project
