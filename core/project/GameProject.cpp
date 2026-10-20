#include "core/project/GameProject.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <set>

#include "core/persist/WorldSave.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::project {
namespace {

namespace fs = std::filesystem;
using content::ContentIssue;
using nlohmann::json;
using nlohmann::ordered_json;
using Severity = ContentIssue::Severity;

constexpr usize kMaxGameIdBytes = 64;
constexpr usize kMaxNameChars = 64;
constexpr usize kMaxDescriptionChars = 1024;
constexpr usize kMaxPathBytes = 200;

constexpr std::array kManifestKeys{"schemaVersion", "gameId",      "name",    "version",
                                   "engine",        "description", "content", "entryWorld"};

bool isLowerIdentChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}
bool isLowerIdent(std::string_view s) noexcept {
    return !s.empty() && std::all_of(s.begin(), s.end(), isLowerIdentChar);
}

// "studio.arena": 소문자 식별자 마디 둘 이상
bool isGameId(std::string_view s) noexcept {
    if (s.size() > kMaxGameIdBytes) {
        return false;
    }
    usize segments = 0;
    while (true) {
        const usize dot = s.find('.');
        if (!isLowerIdent(s.substr(0, dot))) {
            return false;
        }
        ++segments;
        if (dot == std::string_view::npos) {
            return segments >= 2;
        }
        s.remove_prefix(dot + 1);
    }
}

// 1~max 글자, 제어 문자(U+0000~U+001F, U+007F) 없음. JSON 파서가 UTF-8 유효성은 이미 검사했다
bool isDisplayText(std::string_view s, usize maxChars, bool allowEmpty) noexcept {
    if (s.empty()) {
        return allowEmpty;
    }
    if (utf8::length(s) > maxChars) {
        return false;
    }
    return std::none_of(s.begin(), s.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7F;
    });
}

bool isWindowsReserved(std::string_view segment) noexcept {
    std::string stem(segment.substr(0, segment.find('.')));
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
}

class Collector {
public:
    explicit Collector(std::vector<ContentIssue>& out) : m_out(out) {}

    void error(std::string file, std::string pointer, std::string rule, std::string message) {
        m_out.push_back({Severity::Error, std::move(rule), std::move(file), std::move(pointer), std::move(message)});
    }
    void warning(std::string file, std::string pointer, std::string rule, std::string message) {
        m_out.push_back({Severity::Warning, std::move(rule), std::move(file), std::move(pointer), std::move(message)});
    }

private:
    std::vector<ContentIssue>& m_out;
};

usize countSeverity(const std::vector<ContentIssue>& issues, Severity s) noexcept {
    return static_cast<usize>(
        std::count_if(issues.begin(), issues.end(), [s](const ContentIssue& i) { return i.severity == s; }));
}

// 매니페스트의 문자열 필드 하나 (필수면 없을 때 P1)
const std::string* stringField(const json& j, const char* key, bool required, Collector& c) {
    const std::string file(kManifestFile);
    const auto it = j.find(key);
    if (it == j.end()) {
        if (required) {
            c.error(file, std::format("/{}", key), "P1", std::format("필수 키 '{}' 가 없다", key));
        }
        return nullptr;
    }
    if (!it->is_string()) {
        c.error(file, std::format("/{}", key), "P1", std::format("'{}' 는 문자열", key));
        return nullptr;
    }
    return it->get_ptr<const std::string*>();
}

} // namespace

bool ManifestParseResult::ok() const noexcept {
    return countSeverity(issues, Severity::Error) == 0;
}

bool ProjectReport::ok() const noexcept {
    return errorCount() == 0;
}
usize ProjectReport::errorCount() const noexcept {
    return countSeverity(issues, Severity::Error);
}
usize ProjectReport::warningCount() const noexcept {
    return countSeverity(issues, Severity::Warning);
}

std::optional<std::string> checkRelativePath(std::string_view path) {
    if (path.empty()) {
        return "빈 경로";
    }
    if (path.size() > kMaxPathBytes) {
        return std::format("{} 바이트를 넘는다", kMaxPathBytes);
    }
    if (path.find('\\') != std::string_view::npos) {
        return "구분자는 '/' (역슬래시 금지)";
    }
    if (path.front() == '/' || (path.size() >= 2 && path[1] == ':')) {
        return "프로젝트 폴더 기준 상대 경로여야 한다";
    }
    std::string_view rest = path;
    while (true) {
        const usize slash = rest.find('/');
        const std::string_view seg = rest.substr(0, slash);
        if (seg.empty()) {
            return "빈 마디 ('//' 또는 '/' 로 끝남)";
        }
        if (seg == "." || seg == "..") {
            return "'.' · '..' 마디는 쓸 수 없다 (프로젝트 밖을 가리키지 않는다)";
        }
        const bool charsOk = std::all_of(seg.begin(), seg.end(), [](char ch) {
            return isLowerIdentChar(ch) || (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '.';
        });
        if (!charsOk) {
            return std::format("마디 '{}' 에 [A-Za-z0-9_.-] 밖의 문자가 있다", seg);
        }
        if (seg.back() == '.') {
            return std::format("마디 '{}' 가 '.' 로 끝난다 (Windows 에서 다른 이름이 된다)", seg);
        }
        if (isWindowsReserved(seg)) {
            return std::format("마디 '{}' 는 Windows 예약 이름", seg);
        }
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        rest.remove_prefix(slash + 1);
    }
}

ManifestParseResult parseManifest(std::string_view jsonText) {
    ManifestParseResult out;
    Collector c(out.issues);
    const std::string file(kManifestFile);
    const json j = json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        c.error(file, "", "P1", "JSON 객체가 아니다 (문법 · UTF-8 확인)");
        return out;
    }
    GameManifest& m = out.manifest;

    // schemaVersion 이 다르면 나머지 키의 뜻이 다를 수 있다 — 여기서 멈춘다
    const auto sv = j.find("schemaVersion");
    if (sv == j.end() || !sv->is_number_unsigned()) {
        c.error(file, "/schemaVersion", "P1", "필수 키 'schemaVersion' (양의 정수)");
        return out;
    }
    if (sv->get<u64>() != kManifestSchemaVersion) {
        c.error(file, "/schemaVersion", "P3",
                std::format("schemaVersion {} 은 이 엔진이 모른다 (지원: {})", sv->get<u64>(), kManifestSchemaVersion));
        return out;
    }
    m.schemaVersion = kManifestSchemaVersion;

    for (const auto& [k, _] : j.items()) {
        if (std::find(kManifestKeys.begin(), kManifestKeys.end(), k) == kManifestKeys.end()) {
            c.error(file, std::format("/{}", k), "P1", std::format("모르는 키 '{}'", k));
        }
    }
    if (const std::string* s = stringField(j, "gameId", true, c)) {
        if (isGameId(*s)) {
            m.gameId = *s;
        } else {
            c.error(file, "/gameId", "P2",
                    std::format("gameId 는 소문자·숫자·'_' 마디 둘 이상을 '.' 로 이은 것 (≤ {} 바이트, 예: "
                                "\"studio.arena\")",
                                kMaxGameIdBytes));
        }
    }
    if (const std::string* s = stringField(j, "name", true, c)) {
        if (isDisplayText(*s, kMaxNameChars, false)) {
            m.name = *s;
        } else {
            c.error(file, "/name", "P1", std::format("name 은 1~{} 글자, 제어 문자 없이", kMaxNameChars));
        }
    }
    if (const std::string* s = stringField(j, "version", true, c)) {
        if (auto v = parseSemVer(*s)) {
            m.version = std::move(*v);
        } else {
            c.error(file, "/version", "P3", std::format("SemVer 가 아니다 — {}", v.error().message));
        }
    }
    if (const std::string* s = stringField(j, "engine", true, c)) {
        if (auto r = parseVersionRange(*s)) {
            m.engine = std::move(*r);
        } else {
            c.error(file, "/engine", "P3", std::format("엔진 버전 범위 — {}", r.error().message));
        }
    }
    if (const std::string* s = stringField(j, "description", false, c)) {
        if (isDisplayText(*s, kMaxDescriptionChars, true)) {
            m.description = *s;
        } else {
            c.error(file, "/description", "P1",
                    std::format("description 은 {} 글자 이하, 제어 문자 없이", kMaxDescriptionChars));
        }
    }
    if (const auto ct = j.find("content"); ct != j.end()) {
        if (!ct->is_object()) {
            c.error(file, "/content", "P1", "content 는 객체 {\"packs\": [...]}");
        } else {
            for (const auto& [k, _] : ct->items()) {
                if (k != "packs") {
                    c.error(file, std::format("/content/{}", k), "P1", std::format("모르는 키 '{}'", k));
                }
            }
            if (const auto p = ct->find("packs"); p != ct->end()) {
                if (!p->is_array()) {
                    c.error(file, "/content/packs", "P1", "packs 는 팩 id 배열");
                } else if (p->size() > kMaxContentPacks) {
                    c.error(file, "/content/packs", "P2", std::format("팩은 {} 개까지", kMaxContentPacks));
                } else {
                    std::set<std::string> seen;
                    for (usize i = 0; i < p->size(); ++i) {
                        const json& id = (*p)[i];
                        const std::string at = std::format("/content/packs/{}", i);
                        if (!id.is_string() || !isLowerIdent(id.get<std::string>())) {
                            c.error(file, at, "P2", "팩 id 는 소문자·숫자·'_' 문자열");
                        } else if (id.get<std::string>() == "core") {
                            c.error(file, at, "P2", "'core' 는 엔진 예약 팩이다");
                        } else if (!seen.insert(id.get<std::string>()).second) {
                            c.error(file, at, "P2", std::format("팩 '{}' 가 두 번 있다", id.get<std::string>()));
                        } else {
                            m.contentPacks.push_back(id.get<std::string>());
                        }
                    }
                }
            }
        }
    }
    if (const std::string* s = stringField(j, "entryWorld", true, c)) {
        if (const auto why = checkRelativePath(*s)) {
            c.error(file, "/entryWorld", "P4", std::format("entryWorld 경로 — {}", *why));
        } else {
            m.entryWorld = *s;
        }
    }
    return out;
}

std::string writeManifest(const GameManifest& m) {
    ordered_json j;
    j["schemaVersion"] = m.schemaVersion;
    j["gameId"] = m.gameId;
    j["name"] = m.name;
    j["version"] = m.version.toString();
    j["engine"] = m.engine.text;
    if (!m.description.empty()) {
        j["description"] = m.description;
    }
    if (!m.contentPacks.empty()) {
        j["content"]["packs"] = m.contentPacks;
    }
    j["entryWorld"] = m.entryWorld;
    return j.dump(2) + "\n";
}

Expected<void> saveManifest(const std::filesystem::path& projectDir, const GameManifest& manifest) {
    return io::writeFileAtomic(projectDir / std::string(kManifestFile), writeManifest(manifest));
}

namespace {

// 검사가 연 콘텐츠 · 월드를 openProject 가 가져가려고 남긴다 (validateProject 는 버린다)
struct Opened {
    std::unique_ptr<content::ContentDatabase> db; // world 가 가리킨다 — world 보다 먼저 만들고 늦게 없앤다
    std::unique_ptr<sim::SimulationWorld> world;
};

// validateProject · openProject 공용: 매니페스트 → 엔진 범위 → 팩 → 시작 월드. 오류가 있어도 가능한 데까지 본다
void checkProject(const std::filesystem::path& projectDir, const SemVer& engine, const ecs::ComponentCatalog& catalog,
                  ProjectReport& r, Opened& out) {
    Collector c(r.issues);
    const std::string file(kManifestFile);

    // 1. 매니페스트
    const auto text = io::readFile(projectDir / std::string(kManifestFile));
    if (!text) {
        c.error(file, "", "P1", std::format("매니페스트를 읽을 수 없다 — {}", text.error().describe()));
        return;
    }
    ManifestParseResult parsed = parseManifest(*text);
    r.issues.insert(r.issues.end(), parsed.issues.begin(), parsed.issues.end());
    if (!parsed.ok()) {
        return; // 필드가 비었을 수 있다 — 디스크 검사로 넘어가지 않는다
    }
    r.manifest = std::move(parsed.manifest);
    r.manifestRead = true;
    const GameManifest& m = r.manifest;

    // 2. 엔진 범위
    if (!m.engine.contains(engine)) {
        c.error(file, "/engine", "P3",
                std::format("이 엔진 {} 는 범위 \"{}\" 밖이다", engine.toString(), m.engine.text));
    }

    // 3. 콘텐츠 — 팩이 없으면 내장 콘텐츠 (세이브 폴더를 여는 규칙과 같다, WorldSource)
    const fs::path contentRoot = projectDir / "content";
    if (m.contentPacks.empty()) {
        out.db = std::make_unique<content::ContentDatabase>(content::ContentDatabase::builtin());
    } else {
        std::error_code ec;
        if (!fs::is_directory(contentRoot, ec)) {
            c.error("content", "", "P5", "content.packs 가 있는데 content/ 폴더가 없다");
        } else {
            content::ContentLoadResult loaded = content::ContentLoader::load(contentRoot, m.contentPacks, catalog);
            const std::string rootDisplay = io::displayPath(contentRoot);
            for (ContentIssue& i : loaded.issues) {
                // 팩 파일은 "<팩 폴더>/…", 루트 자체의 문제(팩을 찾을 수 없다 등)는 루트의 전체 경로로 온다
                i.file = i.file == rootDisplay ? std::string("content") : "content/" + i.file;
                r.issues.push_back(std::move(i));
            }
            if (loaded.ok()) {
                out.db = std::make_unique<content::ContentDatabase>(std::move(loaded.db));
            }
        }
    }
    if (out.db) {
        r.contentHash = out.db->contentHash();
        r.prefabs = out.db->prefabs().size();
    }

    // 4. 시작 월드: 폴더 · 요약 · 팩 · 실제로 열기
    const fs::path worldDir = projectDir / fs::path(m.entryWorld);
    const std::string worldFile = m.entryWorld + "/world.json";
    std::error_code ec;
    if (!fs::is_directory(worldDir, ec)) {
        c.error(m.entryWorld, "", "P4", "entryWorld 폴더가 없다");
        return;
    }
    auto summary = scenario::readSaveSummary(worldDir);
    if (!summary) {
        c.error(worldFile, "", "P4", std::format("월드 요약을 읽을 수 없다 — {}", summary.error().describe()));
        return;
    }
    bool packsOk = true;
    for (usize i = 0; i < summary->packs.size(); ++i) {
        const std::string& p = summary->packs[i];
        if (std::find(m.contentPacks.begin(), m.contentPacks.end(), p) == m.contentPacks.end()) {
            c.error(worldFile, std::format("/content/packs/{}", i), "P5",
                    std::format("월드가 쓰는 팩 '{}' 가 매니페스트 content.packs 에 없다", p));
            packsOk = false;
        }
    }
    if (!out.db || !packsOk) {
        return; // 콘텐츠가 없으면 월드를 열어 볼 수 없다 (오류는 이미 있다)
    }
    if (summary->contentHash != out.db->contentHash()) {
        c.warning(worldFile, "/content/contentHash", "P5",
                  std::format("월드는 다른 콘텐츠로 저장됐다 (월드 {}, 프로젝트 {}) — 열리지만 다시 저장 권장",
                              replay::formatHash(summary->contentHash), replay::formatHash(out.db->contentHash())));
    }
    auto loaded = persist::loadWorld(catalog, *out.db, worldDir);
    if (!loaded) {
        c.error(worldFile, "", "P4", std::format("월드를 열 수 없다 — {}", loaded.error().describe()));
        return;
    }
    for (const std::string& w : loaded->warnings) {
        c.warning(worldFile, "", "P4", w);
    }
    r.worldOpened = true;
    r.worldTick = loaded->world->clock().tick();
    r.worldEntities = summary->entities;
    out.world = std::move(loaded->world);
}

} // namespace

ProjectReport validateProject(const std::filesystem::path& projectDir, const SemVer& engine,
                              const ecs::ComponentCatalog& catalog) {
    ProjectReport r;
    Opened opened;
    checkProject(projectDir, engine, catalog, r, opened);
    return r;
}

Expected<OpenedProject> openProject(const std::filesystem::path& projectDir, const SemVer& engine,
                                    const ecs::ComponentCatalog& catalog) {
    ProjectReport r;
    Opened opened;
    checkProject(projectDir, engine, catalog, r, opened);
    if (!r.ok() || !opened.world) {
        std::string msg = std::format("게임 프로젝트 오류 {}개", r.errorCount());
        usize shown = 0;
        for (const ContentIssue& i : r.issues) {
            if (i.severity == Severity::Error && shown++ < 5) {
                msg += "\n  " + i.describe();
            }
        }
        return makeError(ErrorCode::ValidationFailed, msg, io::displayPath(projectDir));
    }
    OpenedProject out;
    out.manifest = std::move(r.manifest);
    out.content = std::move(opened.db);
    out.world = std::move(opened.world);
    for (const ContentIssue& i : r.issues) {
        out.warnings.push_back(i.describe()); // ok() 이므로 모두 경고
    }
    return out;
}

bool isProjectFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / std::string(kManifestFile), ec);
}


std::string defaultEngineRange(const SemVer& engine) {
    // 1.0 전에는 minor 마다 깨질 수 있다는 SemVer 의 뜻(^0.x)을 그대로 따르면 엔진이 0.2.0 이 되는 순간 지금 만든 프로젝트가
    // 모두 열리지 않는다 — 1.0 까지는 하한만 두고, 1.0 부터는 major 범위
    if (engine.major == 0) {
        return std::format(">={}.{}.{} <1.0.0", engine.major, engine.minor, engine.patch);
    }
    return std::format("^{}", engine.major);
}

namespace {

// contentRoot 아래 팩 폴더: pack.json 의 id → 폴더 (ContentLoader 와 같은 규칙 — 폴더 이름은 자유)
std::map<std::string, fs::path, std::less<>> packFolders(const fs::path& root) {
    std::map<std::string, fs::path, std::less<>> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) {
            continue;
        }
        const auto text = io::readFile(e.path() / "pack.json");
        if (!text) {
            continue;
        }
        const json j = json::parse(*text, nullptr, false);
        if (j.is_object() && j.contains("id") && j["id"].is_string()) {
            out.emplace(j["id"].get<std::string>(), e.path());
        }
    }
    return out;
}

bool isEmptyOrMissing(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return true;
    }
    return fs::is_directory(dir, ec) && fs::is_empty(dir, ec);
}

} // namespace

Expected<NewProjectResult> createProject(const std::filesystem::path& projectDirArg, const NewProjectOptions& options,
                                         const SemVer& engine, const ecs::ComponentCatalog& catalog) {
    // "MyGame/" 처럼 끝에 구분자가 있으면 임시 폴더가 그 안("MyGame/.sbx-new")에 생긴다 — 마지막 마디로 맞춘다
    fs::path projectDir = projectDirArg.lexically_normal();
    if (projectDir.filename().empty()) {
        projectDir = projectDir.parent_path();
    }
    const std::string where = io::displayPath(projectDir);
    if (!isEmptyOrMissing(projectDir)) {
        return makeError(ErrorCode::AlreadyExists, "폴더가 비어 있지 않습니다 — 새 프로젝트는 빈 폴더에만 만든다",
                         where);
    }

    // 1. 매니페스트 (형식 검사는 parseManifest 와 같은 규칙 — 쓰고 다시 읽어 본다)
    GameManifest m;
    m.gameId = options.gameId;
    m.name = options.name.empty() ? options.gameId.substr(options.gameId.rfind('.') + 1) : options.name;
    m.version = SemVer{0, 1, 0, {}, {}};
    auto range = parseVersionRange(defaultEngineRange(engine));
    if (!range) {
        return std::unexpected(range.error());
    }
    m.engine = std::move(*range);
    m.entryWorld = "worlds/main";

    // 2. 템플릿: 콘텐츠와 시작 월드
    std::unique_ptr<scenario::IScenario> sc;
    if (!options.fromScenario.empty()) {
        sc = scenario::makeScenario(options.fromScenario);
        if (!sc) {
            return makeError(ErrorCode::NotFound,
                             std::format("그런 시나리오가 없습니다 (시나리오: {})", scenario::scenarioNames()),
                             options.fromScenario);
        }
        m.contentPacks = sc->requiredPacks();
    }
    const ManifestParseResult check = parseManifest(writeManifest(m));
    if (!check.ok()) {
        std::string msg = "매니페스트를 만들 수 없습니다";
        for (const ContentIssue& i : check.issues) {
            msg += "\n  " + i.describe();
        }
        return makeError(ErrorCode::ValidationFailed, msg, where);
    }
    content::ContentDatabase db = content::ContentDatabase::builtin();
    if (sc) {
        auto loaded = scenario::loadScenarioContent(*sc, options.contentRoot, catalog);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        db = std::move(*loaded);
    }

    // 3. 옆의 임시 폴더에 만든다 (실패하면 지운다 — 사용자 폴더에 반쯤 만든 프로젝트를 남기지 않는다)
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
    NewProjectResult result;
    if (sc) {
        const auto folders = packFolders(options.contentRoot);
        for (const content::PackInfo& p : db.packs()) {
            const auto it = folders.find(p.id);
            if (it == folders.end()) {
                return makeError(ErrorCode::NotFound, std::format("팩 '{}' 의 폴더를 찾을 수 없습니다", p.id),
                                 io::displayPath(options.contentRoot));
            }
            fs::create_directories(staging / "content", ec);
            fs::copy(it->second, staging / "content" / it->second.filename(), fs::copy_options::recursive, ec);
            if (ec) {
                return makeError(ErrorCode::IoError,
                                 std::format("팩 '{}' 을 복사할 수 없습니다: {}", p.id, ec.message()),
                                 io::displayPath(it->second));
            }
            result.copiedPacks.push_back(p.id);
        }
        scenario::ScenarioRunner runner(catalog, db, std::move(sc), options.seed);
        runner.step(); // setup() 의 초기 명령이 첫 틱에 적용된다
        if (auto r = persist::saveWorld(runner.world(), staging / "worlds" / "main", persist::SaveOptions{"main"});
            !r) {
            return std::unexpected(r.error());
        }
    } else {
        sim::WorldDesc desc;
        desc.seed = options.seed;
        desc.startPaused = true; // 빈 월드는 편집부터
        sim::SimulationWorld world(catalog, db, desc);
        if (auto r = persist::saveWorld(world, staging / "worlds" / "main", persist::SaveOptions{"main"}); !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = saveManifest(staging, m); !r) {
        return std::unexpected(r.error());
    }

    // 4. 만든 것이 검사를 통과해야 제자리로 (경고도 없어야 한다 — 방금 만든 프로젝트다)
    const ProjectReport report = validateProject(staging, engine, catalog);
    if (!report.ok() || report.warningCount() != 0 || !report.worldOpened) {
        std::string msg = "만든 프로젝트가 검사를 통과하지 못했습니다 (엔진 버그)";
        for (const ContentIssue& i : report.issues) {
            msg += "\n  " + i.describe();
        }
        return makeError(ErrorCode::ValidationFailed, msg, where);
    }
    if (fs::exists(projectDir, ec)) {
        fs::remove(projectDir, ec); // 빈 폴더 (위에서 확인)
    }
    if (projectDir.has_parent_path()) {
        fs::create_directories(projectDir.parent_path(), ec);
    }
    fs::rename(staging, projectDir, ec);
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("프로젝트 폴더를 넣을 수 없습니다: {}", ec.message()), where);
    }
    result.manifest = std::move(m);
    result.worldTick = report.worldTick;
    result.worldEntities = report.worldEntities;
    return result;
}

} // namespace sbx::project
