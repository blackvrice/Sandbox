#include "core/content/ContentLoader.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <queue>
#include <set>

#include "core/components/game/GameScore.hpp"
#include "core/simulation/SimConstants.hpp"
#include "core/trigger/TriggerCompiler.hpp"
#include "core/trigger/TriggerSchema.hpp"
#include "foundation/container/FixedString.hpp"
#include "foundation/hash/Fnv1a.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::content {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Severity = ContentIssue::Severity;

constexpr std::array kEngineManaged{"persist.persistence", "net.identity", "core.tags",
                                    "core.prefab",         "game.session", "game.score"};

bool isLowerIdent(std::string_view s) noexcept {
    if (s.empty()) {
        return false;
    }
    return std::all_of(s.begin(), s.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::string packOf(std::string_view id) {
    return std::string(id.substr(0, id.find('.')));
}

// RFC 6901: ~ → ~0, / → ~1
std::string ptr(std::string_view base, std::string_view key) {
    std::string out(base);
    out += '/';
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}
std::string ptr(std::string_view base, usize index) {
    return std::format("{}/{}", base, index);
}

struct PackDir {
    PackInfo info;
    fs::path dir;
    std::string folder;
};

struct SourceFile {
    std::string rel;     // 팩 안 상대 경로, '/' 구분
    std::string display; // "<폴더>/<rel>"
    std::string text;
};

} // namespace

class ContentLoaderImpl {
public:
    ContentLoaderImpl(const fs::path& root, const ecs::ComponentCatalog& catalog, std::vector<ContentIssue>& issues)
        : m_root(root), m_catalog(catalog), m_issues(issues) {}

    // 디스크에서 팩을 찾아 읽는다 (로드 순서, 파일 텍스트)
    std::shared_ptr<ContentSources> readSources(std::span<const std::string> packIds);
    // 원본 (+ 오버레이) → DB. 디스크를 보지 않는다
    ContentDatabase build(const ContentSources& sources, const ContentOverlay* overlay);

private:
    // --- 진단 -------------------------------------------------------------------------------------------------
    void issue(Severity s, std::string_view file, std::string pointer, std::string_view rule, std::string message) {
        m_issues.push_back(
            ContentIssue{s, std::string(rule), std::string(file), std::move(pointer), std::move(message)});
    }
    void error(std::string_view file, std::string pointer, std::string_view rule, std::string message) {
        issue(Severity::Error, file, std::move(pointer), rule, std::move(message));
    }
    void warn(std::string_view file, std::string pointer, std::string_view rule, std::string message) {
        issue(Severity::Warning, file, std::move(pointer), rule, std::move(message));
    }

    // 모르는 키 검사
    bool onlyKeys(const json& j, std::initializer_list<std::string_view> keys, const SourceFile& f,
                  const std::string& at) {
        bool ok = true;
        for (const auto& [k, _] : j.items()) {
            if (std::find(keys.begin(), keys.end(), k) == keys.end()) {
                error(f.display, ptr(at, k), "V3", std::format("모르는 키 '{}'", k));
                ok = false;
            }
        }
        return ok;
    }

    // --- 단계 -------------------------------------------------------------------------------------------------
    std::vector<PackDir> discover();
    std::vector<PackDir> resolveOrder(std::vector<PackDir> all, std::span<const std::string> packIds);
    std::vector<SourceFile> readPack(const PackDir& p);
    void loadTags(const PackDir& p, const SourceFile& f);
    void loadTerrain(const PackDir& p, const SourceFile& f);
    void loadPrefabs(const PackDir& p, const SourceFile& f);
    void loadPrefab(const PackDir& p, const SourceFile& f, const json& j, const std::string& at, bool fromOverlay);
    void loadRules(const PackDir& p, const SourceFile& f);
    void loadRule(const PackDir& p, const SourceFile& f, const json& r, const std::string& at, bool fromOverlay);
    bool parseRuleConditions(const json& cs, const SourceFile& f, const std::string& at,
                             std::vector<RuleCondition>& out);
    bool parseRuleEffects(const PackDir& p, const SourceFile& f, const json& es, const std::string& at,
                          Who spawnDefault, std::vector<RuleEffect>& out, bool& destroysTarget);
    void loadActions(const PackDir& p, const SourceFile& f);
    void loadBehavior(const PackDir& p, const SourceFile& f);
    void loadBehaviorJson(const PackDir& p, const SourceFile& f, const json& j, bool fromOverlay);
    void loadMode(const PackDir& p, const SourceFile& f);
    void loadTrigger(const PackDir& p, const SourceFile& f);
    // 트리거 노드 값 하나 (표의 타입 · 범위 — V3 · V7 · V2). 맞으면 true
    bool checkTriggerParam(const PackDir& p, const SourceFile& f, const trigger::ParamSpec& spec, const json& v,
                           const std::string& at);
    // 초 → 틱 (0 ≤ s ≤ maxSeconds, 반올림). allowZero = false 면 1 틱 이상
    std::optional<u64> secondsToTicks(const json& j, const SourceFile& f, const std::string& at, f64 maxSeconds,
                                      bool allowZero);
    void loadOverlay(const ContentOverlay& overlay);
    // 팩 항목이 오버레이에 있으면 팩 쪽을 건너뛴다 (오버레이가 바꾼다)
    [[nodiscard]] bool overridden(ContentKind k, std::string_view id, bool fromOverlay) const {
        return !fromOverlay && m_overlay != nullptr && m_overlay->find(k, id) != nullptr;
    }
    void crossCheck();
    void link(); // action 번호 · Rule 색인 · Behavior 감지 질의 (모든 파일을 읽은 뒤)

    // --- 조각 -------------------------------------------------------------------------------------------------
    bool checkId(const PackDir& p, const SourceFile& f, const std::string& at, const json& idJson, std::string& out);
    std::optional<TagSet> parseTagList(const json& j, const SourceFile& f, const std::string& at);
    std::optional<TagExpr> parseTagExpr(const json& j, const SourceFile& f, const std::string& at);
    std::optional<FieldRef> parseFieldRef(std::string_view path, const SourceFile& f, const std::string& at);
    std::optional<Condition> parseCondition(const json& j, const SourceFile& f, const std::string& at);
    std::optional<ActionNode> parseAction(const json& j, const SourceFile& f, const std::string& at);
    void noteRef(bool prefab, std::string id, const PackDir& p, const SourceFile& f, std::string at) {
        m_refs.push_back(Ref{prefab, std::move(id), p.info.id, f.display, std::move(at)});
    }

    struct Ref {
        bool prefab; // false = behavior
        std::string id;
        std::string fromPack;
        std::string file;
        std::string pointer;
    };

    fs::path m_root;
    const ecs::ComponentCatalog& m_catalog;
    std::vector<ContentIssue>& m_issues;
    ContentDatabase m_db;
    std::vector<PackDir> m_order;
    std::map<std::string, std::set<std::string>, std::less<>> m_visible; // 팩 → 참조 가능한 팩 (자신 + requires)
    std::vector<Ref> m_refs;
    std::map<std::string, std::string, std::less<>> m_interactUsers; // action → 처음 쓴 behavior (V4)
    u32 m_ruleOrder = 0;
    u32 m_triggerOrder = 0;
    const ContentOverlay* m_overlay = nullptr;
    std::map<std::string, u32, std::less<>> m_replacedRuleOrder; // 오버레이가 바꾼 팩 Rule 의 정의 순서
};

// =====================================================================================================================
std::vector<PackDir> ContentLoaderImpl::discover() {
    std::vector<PackDir> out;
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(m_root, ec)) {
        if (e.is_directory(ec)) {
            dirs.push_back(e.path());
        }
    }
    if (ec) {
        error(io::displayPath(m_root), "", "IO", std::format("콘텐츠 폴더를 읽을 수 없다: {}", ec.message()));
        return out;
    }
    std::sort(dirs.begin(), dirs.end());
    std::set<std::string> seen;
    for (const fs::path& d : dirs) {
        const fs::path packFile = d / "pack.json";
        if (!fs::exists(packFile, ec)) {
            continue;
        }
        const std::string folder = d.filename().string();
        const std::string display = folder + "/pack.json";
        auto text = io::readFile(packFile);
        if (!text) {
            error(display, "", "IO", text.error().describe());
            continue;
        }
        const json j = json::parse(*text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            error(display, "", "JSON", "JSON 객체가 아니다");
            continue;
        }
        PackDir p{{}, d, folder};
        bool ok = true;
        for (const auto& [k, _] : j.items()) {
            if (k != "id" && k != "version" && k != "requires" && k != "description") {
                error(display, ptr("", k), "V1", std::format("모르는 키 '{}'", k));
                ok = false;
            }
        }
        if (!j.contains("id") || !j["id"].is_string() || !isLowerIdent(j["id"].get<std::string>())) {
            error(display, "/id", "V1", "팩 id 는 소문자·숫자·'_' 문자열");
            continue;
        }
        p.info.id = j["id"].get<std::string>();
        if (j.contains("version")) {
            if (!j["version"].is_string()) {
                error(display, "/version", "V1", "version 은 문자열");
                ok = false;
            } else {
                p.info.version = j["version"].get<std::string>();
            }
        }
        if (j.contains("description") && j["description"].is_string()) {
            p.info.description = j["description"].get<std::string>();
        }
        if (j.contains("requires")) {
            if (!j["requires"].is_array()) {
                error(display, "/requires", "V2", "requires 는 팩 id 배열");
                ok = false;
            } else {
                for (usize i = 0; i < j["requires"].size(); ++i) {
                    const json& r = j["requires"][i];
                    if (!r.is_string() || !isLowerIdent(r.get<std::string>())) {
                        error(display, ptr("/requires", i), "V2", "팩 id 가 아니다");
                        ok = false;
                    } else {
                        p.info.dependencies.push_back(r.get<std::string>());
                    }
                }
            }
        }
        if (p.info.id == "core") {
            error(display, "/id", "V1", "'core' 는 엔진 예약 팩이다");
            continue;
        }
        if (!seen.insert(p.info.id).second) {
            error(display, "/id", "V1", std::format("팩 id '{}' 가 두 폴더에 있다", p.info.id));
            continue;
        }
        if (ok) {
            out.push_back(std::move(p));
        }
    }
    return out;
}

std::vector<PackDir> ContentLoaderImpl::resolveOrder(std::vector<PackDir> all, std::span<const std::string> packIds) {
    std::map<std::string, const PackDir*, std::less<>> byId;
    for (const PackDir& p : all) {
        byId[p.info.id] = &p;
    }
    // 필요한 팩 = 요청 + requires 의 닫힘
    std::set<std::string> needed;
    std::vector<std::string> stack(packIds.begin(), packIds.end());
    while (!stack.empty()) {
        const std::string id = stack.back();
        stack.pop_back();
        if (!needed.insert(id).second) {
            continue;
        }
        const auto it = byId.find(id);
        if (it == byId.end()) {
            error(io::displayPath(m_root), "", "V2", std::format("팩 '{}' 을 찾을 수 없다", id));
            continue;
        }
        for (const std::string& r : it->second->info.dependencies) {
            stack.push_back(r);
        }
    }
    // Kahn 위상 정렬, 동순위는 id 오름차순
    std::map<std::string, usize, std::less<>> indeg;
    std::map<std::string, std::vector<std::string>, std::less<>> users;
    for (const std::string& id : needed) {
        const auto it = byId.find(id);
        if (it == byId.end()) {
            continue;
        }
        indeg.try_emplace(id, 0);
        for (const std::string& r : it->second->info.dependencies) {
            if (byId.contains(r)) {
                ++indeg[id];
                users[r].push_back(id);
            }
        }
    }
    std::priority_queue<std::string, std::vector<std::string>, std::greater<>> ready;
    for (const auto& [id, d] : indeg) {
        if (d == 0) {
            ready.push(id);
        }
    }
    std::vector<PackDir> order;
    while (!ready.empty()) {
        const std::string id = ready.top();
        ready.pop();
        order.push_back(*byId[id]);
        for (const std::string& u : users[id]) {
            if (--indeg[u] == 0) {
                ready.push(u);
            }
        }
    }
    if (order.size() != indeg.size()) {
        error(io::displayPath(m_root), "", "V2", "팩 requires 에 순환이 있다");
    }
    return order;
}

std::vector<SourceFile> ContentLoaderImpl::readPack(const PackDir& p) {
    std::vector<SourceFile> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(p.dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        std::string rel = fs::relative(it->path(), p.dir, ec).generic_string();
        if (rel.starts_with("presentation/")) {
            continue; // 클라 전용, contentHash 제외
        }
        SourceFile f{rel, p.folder + "/" + rel, {}};
        auto text = io::readFile(it->path());
        if (!text) {
            error(f.display, "", "IO", text.error().describe());
            continue;
        }
        // 줄 끝 정규화 — Windows 체크아웃(CRLF)에서도 같은 contentHash
        std::string t;
        t.reserve(text->size());
        for (usize i = 0; i < text->size(); ++i) {
            if ((*text)[i] == '\r' && i + 1 < text->size() && (*text)[i + 1] == '\n') {
                continue;
            }
            t += (*text)[i];
        }
        f.text = std::move(t);
        files.push_back(std::move(f));
    }
    if (ec) {
        error(p.folder, "", "IO", ec.message());
    }
    std::sort(files.begin(), files.end(), [](const SourceFile& a, const SourceFile& b) { return a.rel < b.rel; });
    return files;
}

bool ContentLoaderImpl::checkId(const PackDir& p, const SourceFile& f, const std::string& at, const json& idJson,
                                std::string& out) {
    if (!idJson.is_string() || !isValidContentId(idJson.get<std::string>())) {
        error(f.display, at, "V1", "id 는 '<pack>.<name>' (소문자·숫자·'_')");
        return false;
    }
    out = idJson.get<std::string>();
    if (out.size() > ContentId::kCapacity) {
        error(f.display, at, "V1",
              std::format("id 는 {}바이트 이하 (컴포넌트의 ContentId 필드에 담긴다)", ContentId::kCapacity));
        return false;
    }
    if (packOf(out) != p.info.id) {
        error(f.display, at, "V1", std::format("id '{}' 는 팩 접두사 '{}.' 로 시작해야 한다", out, p.info.id));
        return false;
    }
    return true;
}

std::optional<TagSet> ContentLoaderImpl::parseTagList(const json& j, const SourceFile& f, const std::string& at) {
    if (!j.is_array()) {
        error(f.display, at, "V7", "태그 이름 배열이어야 한다");
        return std::nullopt;
    }
    TagSet set;
    bool ok = true;
    for (usize i = 0; i < j.size(); ++i) {
        const auto t = j[i].is_string() ? m_db.findTag(j[i].get<std::string>()) : std::nullopt;
        if (!t) {
            error(f.display, ptr(at, i), "V7", std::format("선언되지 않은 태그 {}", j[i].dump()));
            ok = false;
        } else {
            set.set(*t);
        }
    }
    return ok ? std::optional(set) : std::nullopt;
}

std::optional<TagExpr> ContentLoaderImpl::parseTagExpr(const json& j, const SourceFile& f, const std::string& at) {
    if (!j.is_object()) {
        error(f.display, at, "V7", R"(태그 식은 {"all": [...], "any": [...], "none": [...]})");
        return std::nullopt;
    }
    if (!onlyKeys(j, {"all", "any", "none"}, f, at)) {
        return std::nullopt;
    }
    TagExpr e;
    for (const auto& [key, dst] : {std::pair{"all", &e.all}, std::pair{"any", &e.any}, std::pair{"none", &e.none}}) {
        if (j.contains(key)) {
            const auto s = parseTagList(j[key], f, ptr(at, key));
            if (!s) {
                return std::nullopt;
            }
            *dst = *s;
        }
    }
    return e;
}

std::optional<FieldRef> ContentLoaderImpl::parseFieldRef(std::string_view path, const SourceFile& f,
                                                         const std::string& at) {
    const usize dot = path.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        error(f.display, at, "V3", std::format("필드 경로 '{}' 는 '<컴포넌트>.<필드>'", path));
        return std::nullopt;
    }
    FieldRef r;
    r.componentName = std::string(path.substr(0, dot));
    r.field = std::string(path.substr(dot + 1));
    const ecs::ComponentInfo* info = m_catalog.find(std::string_view(r.componentName));
    if (info == nullptr) {
        error(f.display, at, "V3", std::format("모르는 컴포넌트 '{}'", r.componentName));
        return std::nullopt;
    }
    const ecs::FieldDesc* fd = info->findField(r.field);
    if (fd == nullptr || !fd->numeric) {
        error(f.display, at, "V3", std::format("'{}' 에 수치 필드 '{}' 가 없다", r.componentName, r.field));
        return std::nullopt;
    }
    r.component = info->stableId;
    return r;
}

// =====================================================================================================================
void ContentLoaderImpl::loadTags(const PackDir& /*p*/, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        error(f.display, "", "JSON", "태그 이름 배열이어야 한다");
        return;
    }
    for (usize i = 0; i < j.size(); ++i) {
        if (!j[i].is_string() || !isLowerIdent(j[i].get<std::string>())) {
            error(f.display, ptr("", i), "V7", "태그 이름은 소문자·숫자·'_'");
            continue;
        }
        m_db.m_tags.push_back(j[i].get<std::string>()); // 여러 팩이 같은 태그를 선언해도 된다 (나중에 합친다)
    }
}

void ContentLoaderImpl::loadTerrain(const PackDir& p, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        error(f.display, "", "JSON", "머티리얼 배열이어야 한다");
        return;
    }
    for (usize i = 0; i < j.size(); ++i) {
        auto m = parseTerrainMaterial(j[i], f.display);
        if (!m) {
            error(f.display, ptr("", i), "V6", m.error().message);
            continue;
        }
        if (packOf(m->id) != p.info.id) {
            error(f.display, ptr(ptr("", i), "id"), "V1",
                  std::format("id '{}' 는 팩 접두사 '{}.' 로 시작해야 한다", m->id, p.info.id));
            continue;
        }
        m_db.m_materials.push_back(std::move(*m));
    }
}

void ContentLoaderImpl::loadPrefabs(const PackDir& p, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || (!j.is_object() && !j.is_array())) {
        error(f.display, "", "JSON", "Prefab 객체 또는 배열이어야 한다");
        return;
    }
    if (j.is_object()) {
        loadPrefab(p, f, j, "", false);
        return;
    }
    for (usize i = 0; i < j.size(); ++i) {
        loadPrefab(p, f, j[i], ptr("", i), false);
    }
}

void ContentLoaderImpl::loadPrefab(const PackDir& p, const SourceFile& f, const json& j, const std::string& at,
                                   bool fromOverlay) {
    if (!j.is_object()) {
        error(f.display, at, "JSON", "Prefab 은 객체");
        return;
    }
    bool ok = onlyKeys(j, {"id", "name", "category", "tags", "components", "inherits"}, f, at);
    Prefab pf;
    pf.file = f.display;
    if (!j.contains("id") || !checkId(p, f, ptr(at, "id"), j["id"], pf.id)) {
        return;
    }
    if (overridden(ContentKind::Prefab, pf.id, fromOverlay)) {
        return;
    }
    pf.raw = j;
    pf.overlay = fromOverlay;
    pf.stableId = fnv1a64(pf.id);
    if (j.contains("name") && j["name"].is_string()) {
        pf.name = j["name"].get<std::string>();
    }
    if (j.contains("category") && j["category"].is_string()) {
        pf.category = j["category"].get<std::string>();
    }
    if (j.contains("inherits") && !j["inherits"].is_null()) {
        error(f.display, ptr(at, "inherits"), "V2", "Prefab 상속은 [계획] — null 이거나 없어야 한다");
        ok = false;
    }
    if (j.contains("tags")) {
        const auto t = parseTagList(j["tags"], f, ptr(at, "tags"));
        ok = ok && t.has_value();
        if (t) {
            pf.tags = *t;
        }
    }
    if (j.contains("components")) {
        const json& comps = j["components"];
        if (!comps.is_object()) {
            error(f.display, ptr(at, "components"), "V3", "components 는 {이름: 값} 객체");
            return;
        }
        for (const auto& [name, value] : comps.items()) {
            const std::string cat = ptr(ptr(at, "components"), name);
            if (isEngineManagedComponent(name)) {
                error(f.display, cat, "V3",
                      std::format("'{}' 는 엔진이 관리한다{}", name,
                                  name == "core.tags" ? " — Prefab 의 \"tags\" 를 쓴다" : ""));
                ok = false;
                continue;
            }
            const ecs::ComponentInfo* info = m_catalog.find(std::string_view(name));
            if (info == nullptr) {
                if (isOpaqueAllowedComponent(name) && value.is_object()) {
                    pf.components.push_back(PrefabComponent{name, 0, true, value});
                } else {
                    error(f.display, cat, "V3", std::format("모르는 컴포넌트 '{}'", name));
                    ok = false;
                }
                continue;
            }
            if (auto r = info->validateJson(value, name); !r) {
                error(f.display, cat, "V3",
                      r.error().message + (r.error().context.empty() ? "" : " [" + r.error().context + "]"));
                ok = false;
                continue;
            }
            for (const ecs::FieldDesc& fd : info->fields) {
                if ((fd.hint == ecs::Hint::PrefabRef || fd.hint == ecs::Hint::BehaviorRef) && value.contains(fd.name) &&
                    value[std::string(fd.name)].is_string() &&
                    !value[std::string(fd.name)].get<std::string>().empty()) {
                    noteRef(fd.hint == ecs::Hint::PrefabRef, value[std::string(fd.name)].get<std::string>(), p, f,
                            ptr(cat, fd.name));
                }
            }
            pf.components.push_back(PrefabComponent{name, info->stableId, false, value});
        }
    }
    std::sort(pf.components.begin(), pf.components.end(),
              [](const PrefabComponent& a, const PrefabComponent& b) { return a.name < b.name; });
    if (ok) {
        m_db.m_prefabs.push_back(std::move(pf));
    }
}

void ContentLoaderImpl::loadRules(const PackDir& p, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        error(f.display, "", "JSON", "Rule 배열이어야 한다");
        return;
    }
    for (usize i = 0; i < j.size(); ++i) {
        loadRule(p, f, j[i], ptr("", i), false);
    }
}

// Rule · PlayerAction 의 conditions 배열 (11 3장). 문제가 있으면 false (모두 보고한다)
bool ContentLoaderImpl::parseRuleConditions(const json& cs, const SourceFile& f, const std::string& at,
                                            std::vector<RuleCondition>& out) {
    bool ok = true;
    static const std::map<std::string, CmpOp, std::less<>> kOps{
        {"<", CmpOp::Lt}, {"<=", CmpOp::Le}, {">", CmpOp::Gt}, {">=", CmpOp::Ge}, {"==", CmpOp::Eq}, {"!=", CmpOp::Ne}};
    if (!cs.is_array()) {
        error(f.display, at, "V3", "conditions 는 배열");
        ok = false;
    } else {
        for (usize k = 0; k < cs.size(); ++k) {
            const std::string cat = ptr(at, k);
            const json& c = cs[k];
            if (!c.is_object() || !onlyKeys(c, {"field", "op", "value"}, f, cat) || !c.contains("field") ||
                !c["field"].is_string() || !c.contains("op") || !c["op"].is_string() || !c.contains("value") ||
                !c["value"].is_number()) {
                error(f.display, cat, "V3", R"(조건은 {"field": "source.<컴포넌트>.<필드>", "op": "<", "value": 수})");
                ok = false;
                continue;
            }
            const std::string path = c["field"].get<std::string>();
            RuleCondition rc;
            if (path.starts_with("source.")) {
                rc.who = Who::Source;
            } else if (path.starts_with("target.")) {
                rc.who = Who::Target;
            } else {
                error(f.display, ptr(cat, "field"), "V3", "조건 경로는 source. 또는 target. 으로 시작한다");
                ok = false;
                continue;
            }
            const auto ref = parseFieldRef(std::string_view(path).substr(path.find('.') + 1), f, ptr(cat, "field"));
            const auto op = kOps.find(c["op"].get<std::string>());
            if (op == kOps.end()) {
                error(f.display, ptr(cat, "op"), "V3", "op 는 < <= > >= == !=");
                ok = false;
                continue;
            }
            if (!ref) {
                ok = false;
                continue;
            }
            rc.ref = *ref;
            rc.op = op->second;
            rc.value = c["value"].get<f64>();
            out.push_back(std::move(rc));
        }
    }
    return ok;
}

// Rule · PlayerAction 의 effects 배열 (03 6.3 의 op). spawn 의 who 가 없으면 spawnDefault (Rule = source)
bool ContentLoaderImpl::parseRuleEffects(const PackDir& p, const SourceFile& f, const json& es, const std::string& at,
                                         Who spawnDefault, std::vector<RuleEffect>& out, bool& destroysTarget) {
    bool ok = true;
    if (!es.is_array() || es.empty()) {
        error(f.display, at, "V3", "effects 는 비어 있지 않은 배열");
        ok = false;
    } else {
        for (usize k = 0; k < es.size(); ++k) {
            const std::string eat = ptr(at, k);
            const json& e = es[k];
            if (!e.is_object() || !e.contains("op") || !e["op"].is_string()) {
                error(f.display, eat, "V3", R"(효과는 {"op": ...} 객체)");
                ok = false;
                continue;
            }
            RuleEffect fx;
            const std::string op = e["op"].get<std::string>();
            if (e.contains("who")) {
                const json& w = e["who"];
                if (w == "source") {
                    fx.who = Who::Source;
                } else if (w == "target") {
                    fx.who = Who::Target;
                } else {
                    error(f.display, ptr(eat, "who"), "V3", "who 는 source 또는 target");
                    ok = false;
                    continue;
                }
            } else if (op != "spawn" && op != "event" && op != "score.add") {
                error(f.display, eat, "V3", "who 가 필요하다");
                ok = false;
                continue;
            }
            if (op == "field.add" || op == "field.set") {
                fx.op = op == "field.add" ? EffectOp::FieldAdd : EffectOp::FieldSet;
                if (!onlyKeys(e, {"op", "who", "field", "value"}, f, eat) || !e.contains("field") ||
                    !e["field"].is_string() || !e.contains("value") || !e["value"].is_number()) {
                    error(f.display, eat, "V3", R"({"op", "who", "field": "<컴포넌트>.<필드>", "value": 수})");
                    ok = false;
                    continue;
                }
                const auto ref = parseFieldRef(e["field"].get<std::string>(), f, ptr(eat, "field"));
                if (!ref) {
                    ok = false;
                    continue;
                }
                fx.field = *ref;
                fx.value = e["value"].get<f64>();
            } else if (op == "destroy") {
                fx.op = EffectOp::Destroy;
                ok = onlyKeys(e, {"op", "who"}, f, eat) && ok;
                if (fx.who == Who::Target) {
                    destroysTarget = true; // 03 6.2: destroy(target) 이 있으면 배타 (Rule)
                }
            } else if (op == "spawn") {
                fx.op = EffectOp::Spawn;
                fx.who = e.contains("who") ? fx.who : spawnDefault;
                if (!onlyKeys(e, {"op", "who", "prefab", "count"}, f, eat) || !e.contains("prefab") ||
                    !e["prefab"].is_string()) {
                    error(f.display, eat, "V3", R"({"op": "spawn", "prefab": id, "count": 1~16, "who"})");
                    ok = false;
                    continue;
                }
                fx.prefab = e["prefab"].get<std::string>();
                noteRef(true, fx.prefab, p, f, ptr(eat, "prefab"));
                if (e.contains("count")) {
                    if (!e["count"].is_number_unsigned() || e["count"].get<u64>() < 1 || e["count"].get<u64>() > 16) {
                        error(f.display, ptr(eat, "count"), "V3", "count 는 1~16");
                        ok = false;
                        continue;
                    }
                    fx.count = static_cast<u32>(e["count"].get<u64>());
                }
            } else if (op == "tag.add" || op == "tag.remove") {
                fx.op = op == "tag.add" ? EffectOp::TagAdd : EffectOp::TagRemove;
                const auto t = e.contains("tag") && e["tag"].is_string() ? m_db.findTag(e["tag"].get<std::string>())
                                                                         : std::nullopt;
                if (!onlyKeys(e, {"op", "who", "tag"}, f, eat) || !t) {
                    error(f.display, ptr(eat, "tag"), "V7", "선언된 태그 이름이 필요하다");
                    ok = false;
                    continue;
                }
                fx.tag = *t;
            } else if (op == "event") {
                fx.op = EffectOp::Event;
                fx.who = e.contains("who") ? fx.who : Who::Target;
                if (!onlyKeys(e, {"op", "who", "name"}, f, eat) || !e.contains("name") || !e["name"].is_string() ||
                    !isValidContentId(e["name"].get<std::string>())) {
                    error(f.display, ptr(eat, "name"), "V3", "이벤트 이름은 '<pack>.<name>'");
                    ok = false;
                    continue;
                }
                fx.event = e["name"].get<std::string>();
                fx.eventCode = fnv1a64(fx.event);
            } else if (op == "score.add") {
                // v0.2 M3-T2 (ADR-0048): 게임 점수. team 이 없으면 who(기본 target) 의 팀 태그로, 팀이 없으면 공용 칸
                fx.op = EffectOp::ScoreAdd;
                fx.who = e.contains("who") ? fx.who : Who::Target;
                const bool valueOk = e.contains("value") && e["value"].is_number_integer() &&
                                     e["value"].get<i64>() >= -kMaxScoreDelta &&
                                     e["value"].get<i64>() <= kMaxScoreDelta;
                const bool teamOk =
                    !e.contains("team") || (e["team"].is_string() && isLowerIdent(e["team"].get<std::string>()));
                if (!onlyKeys(e, {"op", "who", "value", "team"}, f, eat) || !valueOk || !teamOk) {
                    error(f.display, eat, "V3",
                          std::format(R"({{"op": "score.add", "value": 정수 ±{}, "team": 팀 id (선택), "who"}})",
                                      kMaxScoreDelta));
                    ok = false;
                    continue;
                }
                fx.value = static_cast<f64>(e["value"].get<i64>());
                if (e.contains("team")) {
                    fx.team = e["team"].get<std::string>();
                }
            } else {
                error(f.display, ptr(eat, "op"), "V3",
                      std::format("모르는 효과 '{}' (field.add field.set destroy spawn tag.add tag.remove event "
                                  "score.add)",
                                  op));
                ok = false;
                continue;
            }
            out.push_back(std::move(fx));
        }
    }
    return ok;
}

void ContentLoaderImpl::loadRule(const PackDir& p, const SourceFile& f, const json& r, const std::string& at,
                                 bool fromOverlay) {
    if (!r.is_object()) {
        error(f.display, at, "JSON", "Rule 은 객체");
        return;
    }
    bool ok = onlyKeys(
        r, {"id", "action", "source", "target", "range", "priority", "exclusive", "conditions", "effects"}, f, at);
    Rule rule;
    rule.file = f.display;
    rule.order = m_ruleOrder++;
    if (!r.contains("id") || !checkId(p, f, ptr(at, "id"), r["id"], rule.id)) {
        return;
    }
    if (overridden(ContentKind::Rule, rule.id, fromOverlay)) {
        m_replacedRuleOrder[rule.id] = rule.order; // 오버레이가 같은 자리(동점 정렬 순서)를 이어받는다
        return;
    }
    if (fromOverlay) {
        if (const auto it = m_replacedRuleOrder.find(rule.id); it != m_replacedRuleOrder.end()) {
            rule.order = it->second;
        }
    }
    rule.raw = r;
    rule.overlay = fromOverlay;
    if (!r.contains("action") || !r["action"].is_string() || !isLowerIdent(r["action"].get<std::string>())) {
        error(f.display, ptr(at, "action"), "V3", "action 은 소문자·숫자·'_' 문자열");
        ok = false;
    } else {
        rule.action = r["action"].get<std::string>();
    }
    for (const auto& [key, dst] : {std::pair{"source", &rule.source}, std::pair{"target", &rule.target}}) {
        if (!r.contains(key)) {
            continue; // 빈 식 = 무엇이든
        }
        const json& side = r[key];
        if (!side.is_object() || !onlyKeys(side, {"tags"}, f, ptr(at, key))) {
            ok = false;
            continue;
        }
        if (side.contains("tags")) {
            const auto e = parseTagExpr(side["tags"], f, ptr(ptr(at, key), "tags"));
            ok = ok && e.has_value();
            if (e) {
                *dst = *e;
            }
        }
    }
    if (r.contains("range")) {
        if (!r["range"].is_number() || r["range"].get<f64>() < 0 || r["range"].get<f64>() > 64) {
            error(f.display, ptr(at, "range"), "V3", "range 는 0~64");
            ok = false;
        } else {
            rule.range = static_cast<f32>(r["range"].get<f64>());
        }
    }
    if (r.contains("priority")) {
        if (!r["priority"].is_number_integer()) {
            error(f.display, ptr(at, "priority"), "V3", "priority 는 정수");
            ok = false;
        } else {
            rule.priority = static_cast<i32>(std::clamp<i64>(r["priority"].get<i64>(), -1000000, 1000000));
        }
    }
    if (r.contains("exclusive")) {
        if (!r["exclusive"].is_boolean()) {
            error(f.display, ptr(at, "exclusive"), "V3", "exclusive 는 bool");
            ok = false;
        } else {
            rule.exclusive = r["exclusive"].get<bool>();
        }
    }
    if (r.contains("conditions")) {
        ok = parseRuleConditions(r["conditions"], f, ptr(at, "conditions"), rule.conditions) && ok;
    }
    bool destroysTarget = false;
    ok = parseRuleEffects(p, f, r.contains("effects") ? r["effects"] : json(), ptr(at, "effects"), Who::Source,
                          rule.effects, destroysTarget) &&
         ok;
    if (destroysTarget) {
        rule.exclusive = true; // 03 6.2: destroy(target) 이 있으면 배타
    }
    if (ok) {
        m_db.m_rules.push_back(std::move(rule));
    }
}

std::optional<Condition> ContentLoaderImpl::parseCondition(const json& j, const SourceFile& f, const std::string& at) {
    Condition c;
    // 인자 없는 노드는 문자열로: "targetValid"
    if (j.is_string()) {
        if (j == "targetValid") {
            c.kind = Condition::Kind::TargetValid;
            return c;
        }
        if (j == "true") {
            return c;
        }
        error(f.display, at, "V5", std::format("모르는 조건 {}", j.dump()));
        return std::nullopt;
    }
    if (!j.is_object() || j.size() != 1) {
        error(f.display, at, "V5", R"(조건은 {"<노드>": 인자} 하나)");
        return std::nullopt;
    }
    // items().begin() 의 프록시는 임시 객체라 구조적 바인딩이 매달린다 (최적화 빌드에서 실제로 깨졌다) — 반복자에서
    // 꺼낸다
    const auto first = j.begin();
    const std::string key = first.key();
    const json& arg = first.value();
    const std::string here = ptr(at, key);
    const auto number = [&](f64 lo, f64 hi) -> std::optional<f64> {
        if (!arg.is_number() || arg.get<f64>() < lo || arg.get<f64>() > hi) {
            error(f.display, here, "V5", std::format("'{}' 의 인자는 {}~{} 수", key, lo, hi));
            return std::nullopt;
        }
        return arg.get<f64>();
    };
    using K = Condition::Kind;
    if (key == "energyBelow" || key == "energyAbove" || key == "healthBelow") {
        c.kind = key == "energyBelow" ? K::EnergyBelow : key == "energyAbove" ? K::EnergyAbove : K::HealthBelow;
        const auto v = number(-1e6, 1e6);
        if (!v) {
            return std::nullopt;
        }
        c.value = *v;
    } else if (key == "targetInRange") {
        c.kind = K::TargetInRange;
        const auto v = number(0, 64);
        if (!v) {
            return std::nullopt;
        }
        c.value = *v;
    } else if (key == "random") {
        c.kind = K::Random;
        const auto v = number(0, 1);
        if (!v) {
            return std::nullopt;
        }
        c.value = *v;
    } else if (key == "targetValid") {
        c.kind = K::TargetValid;
    } else if (key == "sensed") {
        c.kind = K::Sensed;
        const auto e = parseTagExpr(arg, f, here);
        if (!e) {
            return std::nullopt;
        }
        c.tags = *e;
    } else if (key == "stateTime") {
        // {"stateTime": {"op": ">=", "seconds": 3}}
        c.kind = K::StateTime;
        if (!arg.is_object() || !arg.contains("seconds") || !arg["seconds"].is_number() ||
            arg["seconds"].get<f64>() < 0) {
            error(f.display, here, "V5", R"({"stateTime": {"op": ">=", "seconds": 수}})");
            return std::nullopt;
        }
        const std::string op = arg.contains("op") && arg["op"].is_string() ? arg["op"].get<std::string>() : ">=";
        static const std::map<std::string, CmpOp, std::less<>> kOps{
            {"<", CmpOp::Lt}, {"<=", CmpOp::Le}, {">", CmpOp::Gt}, {">=", CmpOp::Ge}};
        const auto o = kOps.find(op);
        if (o == kOps.end()) {
            error(f.display, ptr(here, "op"), "V5", "op 는 < <= > >=");
            return std::nullopt;
        }
        c.op = o->second;
        c.value = arg["seconds"].get<f64>();
    } else if (key == "and" || key == "or") {
        c.kind = key == "and" ? K::And : K::Or;
        if (!arg.is_array() || arg.empty()) {
            error(f.display, here, "V5", "and/or 는 조건 배열");
            return std::nullopt;
        }
        for (usize i = 0; i < arg.size(); ++i) {
            auto child = parseCondition(arg[i], f, ptr(here, i));
            if (!child) {
                return std::nullopt;
            }
            c.children.push_back(std::move(*child));
        }
    } else if (key == "not") {
        c.kind = K::Not;
        auto child = parseCondition(arg, f, here);
        if (!child) {
            return std::nullopt;
        }
        c.children.push_back(std::move(*child));
    } else {
        error(f.display, here, "V5", std::format("모르는 조건 노드 '{}'", key));
        return std::nullopt;
    }
    return c;
}

std::optional<ActionNode> ContentLoaderImpl::parseAction(const json& j, const SourceFile& f, const std::string& at) {
    if (!j.is_object() || !j.contains("action") || !j["action"].is_string()) {
        error(f.display, at, "V5", R"(행동은 {"action": "<노드>", ...})");
        return std::nullopt;
    }
    const std::string kind = j["action"].get<std::string>();
    ActionNode a;
    using K = ActionNode::Kind;
    const auto positive = [&](const char* key, f32& out, f64 hi) {
        if (!j.contains(key) || !j[key].is_number() || j[key].get<f64>() <= 0 || j[key].get<f64>() > hi) {
            error(f.display, ptr(at, key), "V5", std::format("'{}' 는 0 초과 {} 이하 수", key, hi));
            return false;
        }
        out = static_cast<f32>(j[key].get<f64>());
        return true;
    };
    if (kind == "wander") {
        a.kind = K::Wander;
        if (!onlyKeys(j, {"action", "radius", "interval"}, f, at) || !positive("radius", a.radius, 64) ||
            !positive("interval", a.interval, 3600)) {
            return std::nullopt;
        }
    } else if (kind == "seek" || kind == "flee") {
        a.kind = kind == "seek" ? K::Seek : K::Flee;
        if (!onlyKeys(j, {"action", "tags", "strategy", "distance"}, f, at)) {
            return std::nullopt;
        }
        if (!j.contains("tags")) {
            error(f.display, at, "V5", std::format("'{}' 에는 tags 가 필요하다", kind));
            return std::nullopt;
        }
        const auto e = parseTagExpr(j["tags"], f, ptr(at, "tags"));
        if (!e) {
            return std::nullopt;
        }
        a.tags = *e;
        if (a.kind == K::Seek && j.contains("strategy") && j["strategy"] != "nearest") {
            error(f.display, ptr(at, "strategy"), "V5", "strategy 는 지금 nearest 만");
            return std::nullopt;
        }
        if (a.kind == K::Flee && !positive("distance", a.distance, 64)) {
            return std::nullopt;
        }
    } else if (kind == "interact") {
        a.kind = K::Interact;
        if (!onlyKeys(j, {"action", "name"}, f, at) || !j.contains("name") || !j["name"].is_string() ||
            !isLowerIdent(j["name"].get<std::string>())) {
            error(f.display, ptr(at, "name"), "V5", "interact 의 name 은 Rule action 이름");
            return std::nullopt;
        }
        a.name = j["name"].get<std::string>();
    } else if (kind == "idle") {
        a.kind = K::Idle;
        if (!onlyKeys(j, {"action"}, f, at)) {
            return std::nullopt;
        }
    } else if (kind == "setBlackboard") {
        a.kind = K::SetBlackboard;
        if (!onlyKeys(j, {"action", "slot", "value"}, f, at) || !j.contains("slot") ||
            !j["slot"].is_number_unsigned() || j["slot"].get<u64>() > 7 || !j.contains("value") ||
            !j["value"].is_number()) {
            error(f.display, at, "V5", R"({"action": "setBlackboard", "slot": 0~7, "value": 수})");
            return std::nullopt;
        }
        a.slot = static_cast<u8>(j["slot"].get<u64>());
        a.value = j["value"].get<f64>();
    } else {
        error(f.display, ptr(at, "action"), "V5",
              std::format("모르는 행동 '{}' (seek flee wander interact idle setBlackboard)", kind));
        return std::nullopt;
    }
    return a;
}

void ContentLoaderImpl::loadBehavior(const PackDir& p, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error(f.display, "", "JSON", "BehaviorGraph 객체여야 한다");
        return;
    }
    loadBehaviorJson(p, f, j, false);
}

void ContentLoaderImpl::loadBehaviorJson(const PackDir& p, const SourceFile& f, const json& j, bool fromOverlay) {
    bool ok = onlyKeys(j, {"id", "initial", "states", "transitions"}, f, "");
    BehaviorGraph g;
    g.file = f.display;
    if (!j.contains("id") || !checkId(p, f, "/id", j["id"], g.id)) {
        return;
    }
    if (overridden(ContentKind::Behavior, g.id, fromOverlay)) {
        return;
    }
    g.raw = j;
    g.overlay = fromOverlay;
    g.stableId = fnv1a64(g.id);
    if (!j.contains("states") || !j["states"].is_array() || j["states"].empty()) {
        error(f.display, "/states", "V5", "states 는 비어 있지 않은 배열");
        return;
    }
    std::map<std::string, u16, std::less<>> index;
    for (usize i = 0; i < j["states"].size(); ++i) {
        const json& s = j["states"][i];
        const std::string at = ptr("/states", i);
        if (!s.is_object() || !s.contains("id") || !s["id"].is_string() || !isLowerIdent(s["id"].get<std::string>())) {
            error(f.display, at, "V5", "상태에는 id(소문자·숫자·'_')가 필요하다");
            ok = false;
            continue;
        }
        ok = onlyKeys(s, {"id", "onEnter", "onTick", "onExit"}, f, at) && ok;
        BehaviorState st;
        st.id = s["id"].get<std::string>();
        if (!index.try_emplace(st.id, static_cast<u16>(g.states.size())).second) {
            error(f.display, ptr(at, "id"), "V5", std::format("상태 id '{}' 중복", st.id));
            ok = false;
            continue;
        }
        for (const auto& [key, dst] :
             {std::pair{"onEnter", &st.onEnter}, std::pair{"onTick", &st.onTick}, std::pair{"onExit", &st.onExit}}) {
            if (!s.contains(key)) {
                continue;
            }
            if (!s[key].is_array()) {
                error(f.display, ptr(at, key), "V5", "행동 배열");
                ok = false;
                continue;
            }
            for (usize k = 0; k < s[key].size(); ++k) {
                auto a = parseAction(s[key][k], f, ptr(ptr(at, key), k));
                if (!a) {
                    ok = false;
                    continue;
                }
                if (a->kind == ActionNode::Kind::Interact) {
                    m_interactUsers.try_emplace(a->name, g.id);
                }
                dst->push_back(std::move(*a));
            }
        }
        g.states.push_back(std::move(st));
    }
    if (!j.contains("initial") || !j["initial"].is_string() || !index.contains(j["initial"].get<std::string>())) {
        error(f.display, "/initial", "V5", "initial 은 존재하는 상태 id");
        ok = false;
    } else {
        g.initial = index[j["initial"].get<std::string>()];
    }
    if (j.contains("transitions")) {
        if (!j["transitions"].is_array()) {
            error(f.display, "/transitions", "V5", "transitions 는 배열");
            ok = false;
        } else {
            for (usize i = 0; i < j["transitions"].size(); ++i) {
                const json& t = j["transitions"][i];
                const std::string at = ptr("/transitions", i);
                if (!t.is_object() || !onlyKeys(t, {"from", "to", "priority", "when"}, f, at)) {
                    ok = false;
                    continue;
                }
                Transition tr;
                tr.order = static_cast<u32>(i);
                const auto stateRef = [&](const char* key, bool allowAny, u16& out) {
                    if (!t.contains(key) || !t[key].is_string()) {
                        error(f.display, ptr(at, key), "V5", "상태 id 가 필요하다");
                        return false;
                    }
                    const std::string name = t[key].get<std::string>();
                    if (allowAny && name == "*") {
                        out = kAnyState;
                        return true;
                    }
                    const auto it = index.find(name);
                    if (it == index.end()) {
                        error(f.display, ptr(at, key), "V5", std::format("없는 상태 '{}'", name));
                        return false;
                    }
                    out = it->second;
                    return true;
                };
                if (!stateRef("from", true, tr.from) || !stateRef("to", false, tr.to)) {
                    ok = false;
                    continue;
                }
                if (t.contains("priority")) {
                    if (!t["priority"].is_number_integer()) {
                        error(f.display, ptr(at, "priority"), "V5", "priority 는 정수");
                        ok = false;
                        continue;
                    }
                    tr.priority = static_cast<i32>(std::clamp<i64>(t["priority"].get<i64>(), -1000000, 1000000));
                }
                if (!t.contains("when")) {
                    error(f.display, at, "V5", "when 조건이 필요하다 (항상이면 \"true\")");
                    ok = false;
                    continue;
                }
                auto c = parseCondition(t["when"], f, ptr(at, "when"));
                if (!c) {
                    ok = false;
                    continue;
                }
                tr.when = std::move(*c);
                g.transitions.push_back(std::move(tr));
            }
        }
    }
    if (!ok) {
        return;
    }
    // 평가 순서: priority 내림, 정의 순서 오름 (03 5.1)
    std::stable_sort(g.transitions.begin(), g.transitions.end(), [](const Transition& a, const Transition& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.order < b.order;
    });
    // V5 도달 불가 상태 (경고): initial 에서 전이로 닿는 상태. "*" 전이는 어느 상태에서나 출발한다
    std::vector<bool> reach(g.states.size(), false);
    reach[g.initial] = true;
    for (bool changed = true; changed;) {
        changed = false;
        for (const Transition& tr : g.transitions) {
            const bool fromReached = tr.from == kAnyState || reach[tr.from];
            if (fromReached && !reach[tr.to]) {
                reach[tr.to] = true;
                changed = true;
            }
        }
    }
    for (usize i = 0; i < reach.size(); ++i) {
        if (!reach[i]) {
            warn(f.display, ptr("/states", i), "V5", std::format("상태 '{}' 에 도달할 수 없다", g.states[i].id));
        }
    }
    m_db.m_behaviors.push_back(std::move(g));
}

std::optional<u64> ContentLoaderImpl::secondsToTicks(const json& j, const SourceFile& f, const std::string& at,
                                                     f64 maxSeconds, bool allowZero) {
    if (!j.is_number() || !(j.get<f64>() >= 0) || j.get<f64>() > maxSeconds) {
        error(f.display, at, "V8", std::format("초는 0 ~ {} 의 수", maxSeconds));
        return std::nullopt;
    }
    const auto ticks = static_cast<u64>(j.get<f64>() * static_cast<f64>(sim::kTickRate) + 0.5);
    if (!allowZero && ticks == 0) {
        error(f.display, at, "V8", std::format("1 틱(1/{} 초) 이상", sim::kTickRate));
        return std::nullopt;
    }
    return ticks;
}

// actions.json — PlayerAction 배열 (11 6장, v0.2 M3-T1, ADR-0047). 형식 V3 · 태그 V7 · PlayerAction 고유 규칙 V9
// (who 는 target 뿐, range · maxTargets 범위). 문제가 있는 항목은 넣지 않는다
void ContentLoaderImpl::loadActions(const PackDir& p, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        error(f.display, "", "JSON", "PlayerAction 배열이어야 한다");
        return;
    }
    for (usize i = 0; i < j.size(); ++i) {
        const std::string at = ptr("", i);
        const json& a = j[i];
        if (!a.is_object()) {
            error(f.display, at, "JSON", "PlayerAction 은 객체");
            continue;
        }
        bool ok = onlyKeys(a, {"id", "name", "targets", "range", "maxTargets", "conditions", "effects"}, f, at);
        PlayerActionDef def;
        def.file = f.display;
        def.raw = a;
        if (!a.contains("id") || !checkId(p, f, ptr(at, "id"), a["id"], def.id)) {
            continue;
        }
        def.name = def.id;
        if (a.contains("name")) {
            const json& n = a["name"];
            if (!n.is_string() || n.get<std::string>().empty() || n.get<std::string>().size() > 128) {
                error(f.display, ptr(at, "name"), "V3", "name 은 1~128 바이트 문자열");
                ok = false;
            } else {
                def.name = n.get<std::string>();
            }
        }
        if (a.contains("targets")) {
            const json& t = a["targets"];
            if (!t.is_object() || !onlyKeys(t, {"tags"}, f, ptr(at, "targets"))) {
                ok = false;
            } else if (t.contains("tags")) {
                const auto e = parseTagExpr(t["tags"], f, ptr(ptr(at, "targets"), "tags"));
                ok = ok && e.has_value();
                if (e) {
                    def.targets = *e;
                }
            }
        }
        if (a.contains("range")) {
            const json& r = a["range"];
            if (!r.is_number() || r.get<f64>() < 0 || r.get<f64>() > kMaxActionRange) {
                error(f.display, ptr(at, "range"), "V9", std::format("range 는 0~{}", kMaxActionRange));
                ok = false;
            } else {
                def.range = static_cast<f32>(r.get<f64>());
            }
        }
        if (a.contains("maxTargets")) {
            const json& m = a["maxTargets"];
            if (!m.is_number_unsigned() || m.get<u64>() < 1 || m.get<u64>() > kMaxActionTargets) {
                error(f.display, ptr(at, "maxTargets"), "V9", std::format("maxTargets 는 1~{}", kMaxActionTargets));
                ok = false;
            } else {
                def.maxTargets = static_cast<u32>(m.get<u64>());
            }
        }
        if (a.contains("conditions")) {
            ok = parseRuleConditions(a["conditions"], f, ptr(at, "conditions"), def.conditions) && ok;
            for (usize k = 0; k < def.conditions.size(); ++k) {
                if (def.conditions[k].who != Who::Target) {
                    error(f.display, ptr(ptr(at, "conditions"), k), "V9",
                          "PlayerAction 의 조건은 target. 만 (source 가 없다)");
                    ok = false;
                }
            }
        }
        bool destroysTarget = false;
        ok = parseRuleEffects(p, f, a.contains("effects") ? a["effects"] : json(), ptr(at, "effects"), Who::Target,
                              def.effects, destroysTarget) &&
             ok;
        for (usize k = 0; k < def.effects.size(); ++k) {
            if (def.effects[k].who != Who::Target) {
                error(f.display, ptr(ptr(at, "effects"), k), "V9",
                      "PlayerAction 효과의 who 는 target 뿐 (source 가 없다)");
                ok = false;
            }
        }
        if (ok) {
            m_db.m_playerActions.push_back(std::move(def));
        }
    }
}

// modes/<이름>.json — GameMode 하나 (11 9장, ADR-0045). 문제는 모두 모으고, 하나라도 있으면 모드를 넣지 않는다
void ContentLoaderImpl::loadMode(const PackDir& p, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error(f.display, "", "JSON", "GameMode 객체여야 한다");
        return;
    }
    bool ok = onlyKeys(j, {"id", "name", "description", "players", "teams", "start", "end", "restart"}, f, "");
    GameMode m;
    m.file = f.display;
    m.raw = j;
    if (!j.contains("id") || !checkId(p, f, "/id", j["id"], m.id)) {
        return;
    }
    const auto text = [&](const char* key, std::string& out, bool required) {
        if (!j.contains(key)) {
            if (required) {
                error(f.display, ptr("", key), "V8", std::format("'{}' 가 필요하다", key));
                ok = false;
            }
            return;
        }
        if (!j[key].is_string() || j[key].get<std::string>().size() > 256) {
            error(f.display, ptr("", key), "V8", std::format("'{}' 는 256 바이트 이하 문자열", key));
            ok = false;
            return;
        }
        out = j[key].get<std::string>();
    };
    text("name", m.name, true);
    text("description", m.description, false);

    // players {min, max}
    if (j.contains("players")) {
        const json& pl = j["players"];
        const auto count = [&](const char* key, u32& out) {
            if (!pl.contains(key)) {
                return;
            }
            if (!pl[key].is_number_unsigned() || pl[key].get<u64>() < 1 || pl[key].get<u64>() > kMaxGamePlayers) {
                error(f.display, ptr("/players", key), "V8", std::format("{} 는 1 ~ {} 의 정수", key, kMaxGamePlayers));
                ok = false;
                return;
            }
            out = static_cast<u32>(pl[key].get<u64>());
        };
        if (!pl.is_object() || !onlyKeys(pl, {"min", "max"}, f, "/players")) {
            error(f.display, "/players", "V8", R"(players 는 {"min": 1, "max": 4})");
            ok = false;
        } else {
            count("min", m.minPlayers);
            count("max", m.maxPlayers);
            if (m.minPlayers > m.maxPlayers) {
                error(f.display, "/players", "V8",
                      std::format("min {} 이 max {} 보다 크다", m.minPlayers, m.maxPlayers));
                ok = false;
            }
        }
    }

    // teams [{id, name}]
    if (j.contains("teams")) {
        const json& ts = j["teams"];
        if (!ts.is_array() || ts.size() > kMaxGameTeams) {
            error(f.display, "/teams", "V8", std::format("teams 는 {} 개까지의 배열", kMaxGameTeams));
            ok = false;
        } else {
            for (usize i = 0; i < ts.size(); ++i) {
                const std::string at = ptr("/teams", i);
                const json& t = ts[i];
                if (!t.is_object() || !onlyKeys(t, {"id", "name", "tag"}, f, at) || !t.contains("id") ||
                    !t["id"].is_string() || !isLowerIdent(t["id"].get<std::string>()) ||
                    t["id"].get<std::string>().size() > 32 || !t.contains("name") || !t["name"].is_string() ||
                    t["name"].get<std::string>().empty() || t["name"].get<std::string>().size() > 64) {
                    error(f.display, at, "V8",
                          R"(팀은 {"id": "red", "name": "빨강", "tag": "team_red"} — id 는 소문자·숫자·'_')");
                    ok = false;
                    continue;
                }
                GameTeam team{t["id"].get<std::string>(), t["name"].get<std::string>()};
                // 팀 태그 (선택, v0.2 M3-T2): 이 태그가 있는 개체가 이 팀 — 효과 score.add 의 기본 팀
                if (t.contains("tag")) {
                    const auto tag = t["tag"].is_string() ? m_db.findTag(t["tag"].get<std::string>()) : std::nullopt;
                    if (!tag) {
                        error(f.display, ptr(at, "tag"), "V7", "선언된 태그 이름이 필요하다");
                        ok = false;
                        continue;
                    }
                    if (std::any_of(m.teams.begin(), m.teams.end(),
                                    [&](const GameTeam& o) { return o.hasTag && o.tag == *tag; })) {
                        error(f.display, ptr(at, "tag"), "V8",
                              std::format("팀 태그 '{}' 를 두 팀이 쓴다", t["tag"].get<std::string>()));
                        ok = false;
                        continue;
                    }
                    team.hasTag = true;
                    team.tag = *tag;
                }
                if (std::any_of(m.teams.begin(), m.teams.end(), [&](const GameTeam& o) { return o.id == team.id; })) {
                    error(f.display, ptr(at, "id"), "V8", std::format("팀 id '{}' 중복", team.id));
                    ok = false;
                    continue;
                }
                m.teams.push_back(std::move(team));
            }
        }
    }

    // start {countdownSeconds}
    if (j.contains("start")) {
        const json& st = j["start"];
        if (!st.is_object() || !onlyKeys(st, {"countdownSeconds"}, f, "/start")) {
            error(f.display, "/start", "V8", R"(start 는 {"countdownSeconds": 3})");
            ok = false;
        } else if (st.contains("countdownSeconds")) {
            if (const auto t = secondsToTicks(st["countdownSeconds"], f, "/start/countdownSeconds", 60, true)) {
                m.countdownTicks = static_cast<u32>(*t);
            } else {
                ok = false;
            }
        }
    }

    // end [{when: {time?, count?}, result, team?}]
    static const std::map<std::string, CmpOp, std::less<>> kOps{
        {"<", CmpOp::Lt}, {"<=", CmpOp::Le}, {">", CmpOp::Gt}, {">=", CmpOp::Ge}, {"==", CmpOp::Eq}, {"!=", CmpOp::Ne}};
    const auto opOf = [&](const json& o, const std::string& at) -> std::optional<CmpOp> {
        const auto it = o.is_string() ? kOps.find(o.get<std::string>()) : kOps.end();
        if (it == kOps.end()) {
            error(f.display, at, "V8", "op 는 < <= > >= == != 중 하나");
            return std::nullopt;
        }
        return it->second;
    };
    if (j.contains("end")) {
        const json& es = j["end"];
        if (!es.is_array() || es.size() > kMaxGameEndRules) {
            error(f.display, "/end", "V8", std::format("end 는 {} 개까지의 배열", kMaxGameEndRules));
            ok = false;
        } else {
            for (usize i = 0; i < es.size(); ++i) {
                const std::string at = ptr("/end", i);
                const json& e = es[i];
                if (!e.is_object() || !onlyKeys(e, {"when", "result", "team"}, f, at)) {
                    error(f.display, at, "V8", R"(끝 규칙은 {"when": {...}, "result": "win", "team": "red"})");
                    ok = false;
                    continue;
                }
                GameEndRule rule;
                bool rok = true;
                // when
                const json* w = e.contains("when") ? &e["when"] : nullptr;
                const std::string wat = ptr(at, "when");
                if (w == nullptr || !w->is_object() || w->empty() ||
                    !onlyKeys(*w, {"time", "count", "score"}, f, wat)) {
                    error(f.display, wat, "V8",
                          R"(when 은 {"time": {...}} · {"count": {...}} · {"score": {...}} 중 하나 이상)");
                    rok = false;
                } else {
                    if (w->contains("time")) {
                        const json& tm = (*w)["time"];
                        const std::string tat = ptr(wat, "time");
                        if (!tm.is_object() || !onlyKeys(tm, {"op", "seconds"}, f, tat) || !tm.contains("op") ||
                            !tm.contains("seconds")) {
                            error(f.display, tat, "V8", R"(time 은 {"op": ">=", "seconds": 180})");
                            rok = false;
                        } else {
                            const auto op = opOf(tm["op"], ptr(tat, "op"));
                            const auto ticks = secondsToTicks(tm["seconds"], f, ptr(tat, "seconds"), 86400, true);
                            if (op && ticks) {
                                rule.when.hasTime = true;
                                rule.when.timeOp = *op;
                                rule.when.timeTicks = *ticks;
                            } else {
                                rok = false;
                            }
                        }
                    }
                    if (w->contains("count")) {
                        const json& cn = (*w)["count"];
                        const std::string cat = ptr(wat, "count");
                        if (!cn.is_object() || !onlyKeys(cn, {"tags", "op", "value"}, f, cat) || !cn.contains("tags") ||
                            !cn.contains("op") || !cn.contains("value")) {
                            error(f.display, cat, "V8", R"(count 는 {"tags": {"all": [...]}, "op": "<=", "value": 0})");
                            rok = false;
                        } else {
                            const auto tags = parseTagExpr(cn["tags"], f, ptr(cat, "tags"));
                            const auto op = opOf(cn["op"], ptr(cat, "op"));
                            const bool valueOk =
                                cn["value"].is_number_unsigned() && cn["value"].get<u64>() <= 1'000'000;
                            if (!valueOk) {
                                error(f.display, ptr(cat, "value"), "V8", "value 는 0 ~ 1,000,000 의 정수");
                            }
                            if (tags && op && valueOk) {
                                rule.when.hasCount = true;
                                rule.when.countTags = *tags;
                                rule.when.countOp = *op;
                                rule.when.countValue = static_cast<u32>(cn["value"].get<u64>());
                            } else {
                                rok = false;
                            }
                        }
                    }
                    // score {team?, op, value} — game.score 의 한 칸, team 이 없으면 공용 (v0.2 M3-T2)
                    if (w->contains("score")) {
                        const json& sc = (*w)["score"];
                        const std::string sat = ptr(wat, "score");
                        if (!sc.is_object() || !onlyKeys(sc, {"team", "op", "value"}, f, sat) || !sc.contains("op") ||
                            !sc.contains("value")) {
                            error(f.display, sat, "V8", R"(score 는 {"team": "red", "op": ">=", "value": 10})");
                            rok = false;
                        } else {
                            const auto op = opOf(sc["op"], ptr(sat, "op"));
                            const bool valueOk = sc["value"].is_number_integer() &&
                                                 sc["value"].get<i64>() >= -comp::kMaxScore &&
                                                 sc["value"].get<i64>() <= comp::kMaxScore;
                            if (!valueOk) {
                                error(f.display, ptr(sat, "value"), "V8",
                                      std::format("value 는 ±{} 의 정수", comp::kMaxScore));
                            }
                            u8 slot = 0;
                            bool teamOk = true;
                            if (sc.contains("team")) {
                                const auto tit = sc["team"].is_string()
                                                     ? std::find_if(m.teams.begin(), m.teams.end(),
                                                                    [&](const GameTeam& t) {
                                                                        return t.id == sc["team"].get<std::string>();
                                                                    })
                                                     : m.teams.end();
                                if (tit == m.teams.end()) {
                                    error(f.display, ptr(sat, "team"), "V8", "team 은 teams 에 있는 팀 id");
                                    teamOk = false;
                                } else {
                                    slot = static_cast<u8>(tit - m.teams.begin() + 1);
                                }
                            }
                            if (op && valueOk && teamOk) {
                                rule.when.hasScore = true;
                                rule.when.scoreSlot = slot;
                                rule.when.scoreOp = *op;
                                rule.when.scoreValue = sc["value"].get<i64>();
                            } else {
                                rok = false;
                            }
                        }
                    }
                }
                // result
                static const std::map<std::string, GameResult, std::less<>> kResults{
                    {"win", GameResult::Win}, {"lose", GameResult::Lose}, {"draw", GameResult::Draw}};
                const auto rit = e.contains("result") && e["result"].is_string()
                                     ? kResults.find(e["result"].get<std::string>())
                                     : kResults.end();
                if (rit == kResults.end()) {
                    error(f.display, ptr(at, "result"), "V8", "result 는 win · lose · draw");
                    rok = false;
                } else {
                    rule.result = rit->second;
                }
                // team (그 팀의 승리 · 패배)
                if (e.contains("team")) {
                    const auto tit =
                        e["team"].is_string()
                            ? std::find_if(m.teams.begin(), m.teams.end(),
                                           [&](const GameTeam& t) { return t.id == e["team"].get<std::string>(); })
                            : m.teams.end();
                    if (tit == m.teams.end()) {
                        error(f.display, ptr(at, "team"), "V8", "team 은 teams 에 있는 팀 id");
                        rok = false;
                    } else if (rule.result == GameResult::Draw) {
                        error(f.display, ptr(at, "team"), "V8", "draw 에는 team 을 쓰지 않는다");
                        rok = false;
                    } else {
                        rule.team = static_cast<u8>(tit - m.teams.begin() + 1);
                    }
                }
                if (rok) {
                    m.end.push_back(std::move(rule));
                } else {
                    ok = false;
                }
            }
        }
    }

    // restart {mode, delaySeconds}
    if (j.contains("restart")) {
        const json& rs = j["restart"];
        if (!rs.is_object() || !onlyKeys(rs, {"mode", "delaySeconds"}, f, "/restart") || !rs.contains("mode") ||
            !rs["mode"].is_string() ||
            (rs["mode"].get<std::string>() != "manual" && rs["mode"].get<std::string>() != "auto")) {
            error(f.display, "/restart", "V8", R"(restart 는 {"mode": "manual" | "auto", "delaySeconds": 5})");
            ok = false;
        } else {
            m.autoRestart = rs["mode"].get<std::string>() == "auto";
            if (rs.contains("delaySeconds")) {
                if (const auto t = secondsToTicks(rs["delaySeconds"], f, "/restart/delaySeconds", 60, true)) {
                    m.restartDelayTicks = static_cast<u32>(*t);
                } else {
                    ok = false;
                }
            }
        }
    }
    if (ok) {
        m_db.m_modes.push_back(std::move(m));
    }
}

// --- Trigger (11 10장, v0.2 M4-T1, ADR-0050) ---------------------------------------------------------------------
namespace {

const std::map<std::string, CmpOp, std::less<>>& cmpOps() {
    static const std::map<std::string, CmpOp, std::less<>> ops{{"<", CmpOp::Lt},  {"<=", CmpOp::Le}, {">", CmpOp::Gt},
                                                               {">=", CmpOp::Ge}, {"==", CmpOp::Eq}, {"!=", CmpOp::Ne}};
    return ops;
}

std::optional<GameResult> resultOf(const json& v) {
    if (v == "win") {
        return GameResult::Win;
    }
    if (v == "lose") {
        return GameResult::Lose;
    }
    if (v == "draw") {
        return GameResult::Draw;
    }
    return std::nullopt;
}

} // namespace

bool ContentLoaderImpl::checkTriggerParam(const PackDir& p, const SourceFile& f, const trigger::ParamSpec& spec,
                                          const json& v, const std::string& at) {
    using trigger::ParamType;
    const auto bad = [&](std::string what) {
        error(f.display, at, "V3", std::format("{} 는 {}", spec.key, what));
        return false;
    };
    switch (spec.type) {
    case ParamType::Number:
        if (!v.is_number() || !std::isfinite(v.get<f64>()) || v.get<f64>() < spec.min || v.get<f64>() > spec.max) {
            return bad(std::format("{} ~ {} 의 수", spec.min, spec.max));
        }
        return true;
    case ParamType::Integer:
        if (!v.is_number_integer() || static_cast<f64>(v.get<i64>()) < spec.min ||
            static_cast<f64>(v.get<i64>()) > spec.max) {
            return bad(std::format("{} ~ {} 의 정수", spec.min, spec.max));
        }
        return true;
    case ParamType::Bool:
        return v.is_boolean() || bad("true · false");
    case ParamType::Tags:
        return parseTagExpr(v, f, at).has_value();
    case ParamType::Op:
        return (v.is_string() && cmpOps().contains(v.get<std::string>())) || bad("< <= > >= == != 중 하나");
    case ParamType::Vec2:
        if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number() ||
            !std::isfinite(v[0].get<f64>()) || !std::isfinite(v[1].get<f64>()) || std::abs(v[0].get<f64>()) > 1e6 ||
            std::abs(v[1].get<f64>()) > 1e6) {
            return bad("[x, y] (유한한 수, |값| ≤ 1,000,000)");
        }
        return true;
    case ParamType::Field:
        if (!v.is_string()) {
            return bad(R"("<컴포넌트>.<수치 필드>")");
        }
        return parseFieldRef(v.get<std::string>(), f, at).has_value();
    case ParamType::Prefab:
        if (!v.is_string() || !isValidContentId(v.get<std::string>())) {
            return bad("Prefab id");
        }
        noteRef(true, v.get<std::string>(), p, f, at);
        return true;
    case ParamType::Tag:
        if (!v.is_string() || !m_db.findTag(v.get<std::string>())) {
            error(f.display, at, "V7", "선언된 태그 이름이 필요하다");
            return false;
        }
        return true;
    case ParamType::Team:
        return (v.is_string() && isLowerIdent(v.get<std::string>()) && v.get<std::string>().size() <= 32) ||
               bad("팀 id (소문자 · 숫자 · '_')");
    case ParamType::Result:
        return resultOf(v).has_value() || bad("win · lose · draw");
    case ParamType::EventName:
        return (v.is_string() && isValidContentId(v.get<std::string>())) || bad("'<pack>.<name>'");
    }
    return false;
}

// triggers/<이름>.json — Visual Trigger 하나. 노드 값은 TriggerSchema 표로 (V3 · V7 · V2), 모양은 trigger::compileGraph
// (V10). 문제가 하나라도 있으면 트리거를 넣지 않는다
void ContentLoaderImpl::loadTrigger(const PackDir& p, const SourceFile& f) {
    const json j = json::parse(f.text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error(f.display, "", "JSON", "Trigger 객체여야 한다");
        return;
    }
    bool ok = onlyKeys(j, {"id", "name", "limits", "nodes", "links"}, f, "");
    TriggerDef t;
    t.file = f.display;
    t.raw = j;
    t.order = m_triggerOrder++;
    if (!j.contains("id") || !checkId(p, f, "/id", j["id"], t.id)) {
        return;
    }
    t.name = t.id;
    if (j.contains("name")) {
        if (!j["name"].is_string() || j["name"].get<std::string>().empty() ||
            j["name"].get<std::string>().size() > 128) {
            error(f.display, "/name", "V3", "name 은 1~128 바이트 문자열");
            ok = false;
        } else {
            t.name = j["name"].get<std::string>();
        }
    }
    if (j.contains("limits")) {
        const json& l = j["limits"];
        if (!l.is_object() || !onlyKeys(l, {"perTick"}, f, "/limits")) {
            error(f.display, "/limits", "V10", R"(limits 는 {"perTick": 1})");
            ok = false;
        } else if (l.contains("perTick")) {
            if (!l["perTick"].is_number_unsigned() || l["perTick"].get<u64>() < 1 ||
                l["perTick"].get<u64>() > kMaxTriggerFiresPerTick) {
                error(f.display, "/limits/perTick", "V10",
                      std::format("perTick 은 1 ~ {} 의 정수", kMaxTriggerFiresPerTick));
                ok = false;
            } else {
                t.perTick = static_cast<u32>(l["perTick"].get<u64>());
            }
        }
    }

    // 노드 — 표로 키 · 값을 본 뒤 타입별 실행 표현으로
    const json* nodes = j.contains("nodes") ? &j["nodes"] : nullptr;
    if (nodes == nullptr || !nodes->is_array() || nodes->empty()) {
        error(f.display, "/nodes", "V10", "nodes 는 비어 있지 않은 배열 (이벤트 노드 하나에서 시작한다)");
        return;
    }
    std::vector<std::string> ids;
    std::vector<trigger::GraphNode> graph;
    std::vector<u16> slot; // 노드 → conditions · effects 번호 (이벤트는 0)
    for (usize i = 0; i < nodes->size(); ++i) {
        const std::string at = ptr("/nodes", i);
        const json& n = (*nodes)[i];
        ids.emplace_back();
        graph.emplace_back();
        slot.push_back(0);
        if (!n.is_object() || !n.contains("id") || !n["id"].is_string() || !isLowerIdent(n["id"].get<std::string>()) ||
            n["id"].get<std::string>().size() > 32) {
            error(f.display, at, "V10",
                  R"(노드는 {"id": "start", "type": "event.timer", ...} — id 는 소문자 · 숫자 · '_')");
            ok = false;
            continue;
        }
        ids.back() = n["id"].get<std::string>();
        if (std::find(ids.begin(), ids.end() - 1, ids.back()) != ids.end() - 1) {
            error(f.display, ptr(at, "id"), "V10", std::format("노드 id '{}' 중복", ids.back()));
            ok = false;
            continue;
        }
        const trigger::NodeTypeSpec* spec =
            n.contains("type") && n["type"].is_string() ? trigger::findNodeType(n["type"].get<std::string>()) : nullptr;
        if (spec == nullptr) {
            error(f.display, ptr(at, "type"), "V3", "모르는 노드 type (event.* · cond.* · effect.* — 11 10장의 표)");
            ok = false;
            continue;
        }
        bool nok = true;
        for (const auto& [key, value] : n.items()) {
            if (std::find(std::begin(trigger::kNodeReservedKeys), std::end(trigger::kNodeReservedKeys), key) !=
                std::end(trigger::kNodeReservedKeys)) {
                continue;
            }
            const trigger::ParamSpec* ps = trigger::findParam(*spec, key);
            if (ps == nullptr) {
                error(f.display, ptr(at, key), "V3", std::format("'{}' 에 모르는 키 '{}'", spec->type, key));
                nok = false;
            } else {
                nok = checkTriggerParam(p, f, *ps, value, ptr(at, key)) && nok;
            }
        }
        for (const trigger::ParamSpec& ps : spec->params) {
            if (ps.required && !n.contains(ps.key)) {
                error(f.display, at, "V3", std::format("'{}' 에 '{}' 가 필요하다", spec->type, ps.key));
                nok = false;
            }
        }
        graph.back().category = spec->category;
        graph.back().givesSubject = spec->givesSubject;
        graph.back().needsSubject = spec->needsSubject || (spec->type == "effect.spawn" && !n.contains("at"));
        if (!nok) {
            ok = false;
            continue;
        }
        const std::string_view type = spec->type;
        const auto num = [&](const char* k, f64 d = 0) { return n.contains(k) ? n[k].get<f64>() : d; };
        const auto op = [&] { return cmpOps().find(n["op"].get<std::string>())->second; };
        const auto tags = [&](const char* k) { return n.contains(k) ? *parseTagExpr(n[k], f, ptr(at, k)) : TagExpr{}; };
        const auto text = [&](const char* k) { return n.contains(k) ? n[k].get<std::string>() : std::string(); };
        switch (spec->category) {
        case trigger::NodeCategory::Event: {
            // 둘째 이벤트 노드는 compileGraph 가 알린다 (값은 마지막 것이 남지만 그때는 넣지 않는다)
            TriggerEvent& e = t.event;
            if (type == "event.game_start") {
                e.kind = TriggerEventKind::GameStart;
            } else if (type == "event.tick") {
                e.kind = TriggerEventKind::Tick;
            } else if (type == "event.timer") {
                e.kind = TriggerEventKind::Timer;
                const auto ticks = secondsToTicks(n["seconds"], f, ptr(at, "seconds"), 86400, false);
                ok = ok && ticks.has_value();
                e.ticks = ticks.value_or(1);
                e.repeat = n.contains("repeat") && n["repeat"].get<bool>();
            } else if (type == "event.area_enter") {
                e.kind = TriggerEventKind::AreaEnter;
                e.minX = n["min"][0].get<f32>();
                e.minY = n["min"][1].get<f32>();
                e.maxX = n["max"][0].get<f32>();
                e.maxY = n["max"][1].get<f32>();
                if (e.minX > e.maxX || e.minY > e.maxY) {
                    error(f.display, at, "V10", "영역의 min 이 max 보다 크다");
                    ok = false;
                }
                e.tags = tags("tags");
            } else if (type == "event.score") {
                e.kind = TriggerEventKind::Score;
                e.team = text("team");
                e.op = op();
                e.value = n["value"].get<i64>();
            } else {
                e.kind = TriggerEventKind::Death;
                e.tags = tags("tags");
            }
            break;
        }
        case trigger::NodeCategory::Condition: {
            TriggerCondition c;
            if (type == "cond.count") {
                c.kind = TriggerConditionKind::Count;
                c.tags = tags("tags");
                c.op = op();
                c.value = num("value");
            } else if (type == "cond.score") {
                c.kind = TriggerConditionKind::Score;
                c.team = text("team");
                c.op = op();
                c.value = num("value");
            } else if (type == "cond.time") {
                c.kind = TriggerConditionKind::Time;
                c.op = op();
                const auto ticks = secondsToTicks(n["seconds"], f, ptr(at, "seconds"), 86400, true);
                ok = ok && ticks.has_value();
                c.ticks = ticks.value_or(0);
            } else if (type == "cond.field") {
                c.kind = TriggerConditionKind::Field;
                c.field = *parseFieldRef(n["field"].get<std::string>(), f, ptr(at, "field"));
                c.op = op();
                c.value = num("value");
            } else if (type == "cond.tags") {
                c.kind = TriggerConditionKind::Tags;
                c.tags = tags("tags");
            } else {
                c.kind = TriggerConditionKind::Chance;
                c.chance = static_cast<f32>(num("p"));
            }
            slot.back() = static_cast<u16>(t.conditions.size());
            t.conditions.push_back(std::move(c));
            break;
        }
        case trigger::NodeCategory::Effect: {
            TriggerEffect x;
            RuleEffect& fx = x.fx;
            fx.who = Who::Target; // 대상 (subject)
            if (type == "effect.field.add" || type == "effect.field.set") {
                fx.op = type == "effect.field.add" ? EffectOp::FieldAdd : EffectOp::FieldSet;
                fx.field = *parseFieldRef(n["field"].get<std::string>(), f, ptr(at, "field"));
                fx.value = num("value");
            } else if (type == "effect.destroy") {
                fx.op = EffectOp::Destroy;
            } else if (type == "effect.spawn") {
                fx.op = EffectOp::Spawn;
                fx.prefab = n["prefab"].get<std::string>();
                fx.count = n.contains("count") ? static_cast<u32>(n["count"].get<i64>()) : 1;
                if (n.contains("at")) {
                    x.hasAt = true;
                    x.atX = n["at"][0].get<f32>();
                    x.atY = n["at"][1].get<f32>();
                }
            } else if (type == "effect.tag.add" || type == "effect.tag.remove") {
                fx.op = type == "effect.tag.add" ? EffectOp::TagAdd : EffectOp::TagRemove;
                fx.tag = *m_db.findTag(n["tag"].get<std::string>());
            } else if (type == "effect.event") {
                fx.op = EffectOp::Event;
                fx.event = n["name"].get<std::string>();
                fx.eventCode = fnv1a64(fx.event);
            } else if (type == "effect.score.add") {
                fx.op = EffectOp::ScoreAdd;
                fx.value = static_cast<f64>(n["value"].get<i64>());
                fx.team = text("team");
            } else {
                x.kind = TriggerEffectKind::GameEnd;
                x.result = *resultOf(n["result"]);
                x.team = text("team");
                if (x.result == GameResult::Draw && !x.team.empty()) {
                    error(f.display, ptr(at, "team"), "V10", "draw 에는 team 을 쓰지 않는다");
                    ok = false;
                }
            }
            slot.back() = static_cast<u16>(t.effects.size());
            t.effects.push_back(std::move(x));
            break;
        }
        }
    }

    // 연결 {from, to, port?}
    std::vector<trigger::GraphLink> links;
    if (j.contains("links")) {
        const json& ls = j["links"];
        if (!ls.is_array()) {
            error(f.display, "/links", "V10", R"(links 는 [{"from": "a", "to": "b", "port": "else"}, ...])");
            ok = false;
        } else {
            for (usize i = 0; i < ls.size(); ++i) {
                const std::string at = ptr("/links", i);
                const json& l = ls[i];
                const auto nodeOf = [&](const char* k) -> std::optional<u16> {
                    if (!l.contains(k) || !l[k].is_string()) {
                        return std::nullopt;
                    }
                    const auto it = std::find(ids.begin(), ids.end(), l[k].get<std::string>());
                    return it != ids.end() ? std::optional<u16>(static_cast<u16>(it - ids.begin())) : std::nullopt;
                };
                if (!l.is_object() || !onlyKeys(l, {"from", "to", "port"}, f, at)) {
                    ok = false;
                    continue;
                }
                const auto from = nodeOf("from");
                const auto to = nodeOf("to");
                const bool portOk = !l.contains("port") || l["port"] == "then" || l["port"] == "else";
                if (!from || !to || !portOk) {
                    error(f.display, at, "V10", R"(연결은 {"from": 노드 id, "to": 노드 id, "port": "then" | "else"})");
                    ok = false;
                    continue;
                }
                links.push_back(trigger::GraphLink{*from, *to, l.contains("port") && l["port"] == "else"});
            }
        }
    }
    if (!ok) {
        return; // 노드 값이 틀리면 모양 검사는 하지 않는다 (같은 문제를 두 번 알리지 않게)
    }

    const trigger::CompiledGraph g = trigger::compileGraph(graph, links);
    for (const trigger::GraphIssue& i : g.issues) {
        const std::string at = i.where == trigger::GraphIssue::Where::Node   ? ptr("/nodes", i.index)
                               : i.where == trigger::GraphIssue::Where::Link ? ptr("/links", i.index)
                                                                             : std::string();
        if (i.warning) {
            warn(f.display, at, "V10", i.message);
        } else {
            error(f.display, at, "V10", i.message);
        }
    }
    if (!g.ok) {
        return;
    }
    t.hasSubject = graph[g.eventNode].givesSubject;
    for (const trigger::CompiledStep& st : g.steps) {
        t.program.push_back(TriggerStep{st.op, st.op == TriggerStep::Op::Jump ? u16{0} : slot[st.node], st.jump});
    }
    m_db.m_triggers.push_back(std::move(t));
}

void ContentLoaderImpl::crossCheck() {
    // V1 중복 id
    const auto dupCheck = [&](auto& items, const char* what) {
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        for (usize i = 1; i < items.size(); ++i) {
            if (items[i].id == items[i - 1].id) {
                error(items[i].file, "/id", "V1",
                      std::format("{} id '{}' 중복 (처음: {})", what, items[i].id, items[i - 1].file));
            }
        }
    };
    dupCheck(m_db.m_prefabs, "Prefab");
    dupCheck(m_db.m_behaviors, "Behavior");
    dupCheck(m_db.m_modes, "GameMode");
    dupCheck(m_db.m_playerActions, "PlayerAction");
    {
        // Trigger 는 정의 순서를 지킨다 (실행 순서) — 복사본으로 중복만 본다
        std::vector<std::pair<std::string, std::string>> ids;
        for (const TriggerDef& t : m_db.m_triggers) {
            ids.emplace_back(t.id, t.file);
        }
        std::sort(ids.begin(), ids.end());
        for (usize i = 1; i < ids.size(); ++i) {
            if (ids[i].first == ids[i - 1].first) {
                error(ids[i].second, "/id", "V1",
                      std::format("Trigger id '{}' 중복 (처음: {})", ids[i].first, ids[i - 1].second));
            }
        }
        if (m_db.m_triggers.size() > kMaxTriggers) {
            error(m_db.m_triggers.back().file, "", "V10",
                  std::format("Trigger 는 모든 팩을 합쳐 {} 개까지 ({} 개)", kMaxTriggers, m_db.m_triggers.size()));
        }
    }
    if (m_db.m_playerActions.size() > kMaxPlayerActions) {
        error(m_db.m_playerActions.back().file, "", "V9",
              std::format("PlayerAction 은 모든 팩을 합쳐 {} 개까지 ({} 개)", kMaxPlayerActions,
                          m_db.m_playerActions.size()));
    }
    {
        std::vector<const Rule*> rs;
        for (const Rule& r : m_db.m_rules) {
            rs.push_back(&r);
        }
        std::sort(rs.begin(), rs.end(), [](const Rule* a, const Rule* b) { return a->id < b->id; });
        for (usize i = 1; i < rs.size(); ++i) {
            if (rs[i]->id == rs[i - 1]->id) {
                error(rs[i]->file, "", "V1", std::format("Rule id '{}' 중복", rs[i]->id));
            }
        }
    }
    // V2 참조 무결성 + 팩 경계 (자신 또는 requires 의 팩만)
    for (const Ref& r : m_refs) {
        const bool exists = r.prefab ? m_db.findPrefab(r.id) != nullptr : m_db.findBehavior(r.id) != nullptr;
        if (!exists) {
            error(r.file, r.pointer, "V2", std::format("없는 {} '{}'", r.prefab ? "Prefab" : "Behavior", r.id));
            continue;
        }
        if (!m_visible[r.fromPack].contains(packOf(r.id))) {
            error(r.file, r.pointer, "V2",
                  std::format("'{}' 는 팩 '{}' 의 것 — requires 에 선언해야 참조할 수 있다", r.id, packOf(r.id)));
        }
    }
    // V4 고아 Rule (경고)
    for (const Rule& r : m_db.m_rules) {
        if (!m_interactUsers.contains(r.action)) {
            warn(r.file, "", "V4",
                 std::format("Rule '{}' 의 action '{}' 을 interact 하는 Behavior 가 없다", r.id, r.action));
        }
    }
    // V2 (경고) score.add 의 팀 id — 어느 GameMode 에도 없으면 언제나 효과가 없다. 팀은 모드마다라 실행 때 고른다
    const auto checkScoreTeams = [&](const std::vector<RuleEffect>& effects, const std::string& file,
                                     const std::string& owner) {
        for (const RuleEffect& fx : effects) {
            if (fx.op != EffectOp::ScoreAdd || fx.team.empty()) {
                continue;
            }
            const bool known = std::any_of(m_db.m_modes.begin(), m_db.m_modes.end(), [&](const GameMode& m) {
                return std::any_of(m.teams.begin(), m.teams.end(), [&](const GameTeam& t) { return t.id == fx.team; });
            });
            if (!known) {
                warn(file, "", "V2",
                     std::format("'{}' 의 score.add 팀 '{}' 가 어느 GameMode 에도 없다", owner, fx.team));
            }
        }
    };
    for (const Rule& r : m_db.m_rules) {
        checkScoreTeams(r.effects, r.file, r.id);
    }
    for (const PlayerActionDef& a : m_db.m_playerActions) {
        checkScoreTeams(a.effects, a.file, a.id);
    }

    // Trigger (v0.2 M4-T1): 팀 id (V2 경고 — score.add 와 같은 이유) · 재진입 고리 (V10 경고)
    const auto teamKnown = [&](const std::string& team) {
        return team.empty() || std::any_of(m_db.m_modes.begin(), m_db.m_modes.end(), [&](const GameMode& m) {
                   return std::any_of(m.teams.begin(), m.teams.end(), [&](const GameTeam& t) { return t.id == team; });
               });
    };
    for (const TriggerDef& t : m_db.m_triggers) {
        std::vector<std::string> teams{t.event.team};
        for (const TriggerCondition& c : t.conditions) {
            teams.push_back(c.team);
        }
        for (const TriggerEffect& x : t.effects) {
            teams.push_back(x.kind == TriggerEffectKind::GameEnd ? x.team : x.fx.team);
        }
        for (const std::string& team : teams) {
            if (!teamKnown(team)) {
                warn(t.file, "", "V2", std::format("Trigger '{}' 의 팀 '{}' 가 어느 GameMode 에도 없다", t.id, team));
            }
        }
    }
    // 재진입: 트리거의 효과가 내는 이벤트 (score.add → 점수, destroy → 죽음, spawn → 영역 진입) 를 다른 (또는 같은)
    // 트리거가 듣는다. 고리면 한 틱 안에서 서로를 부를 수 있다 — 실행 예산(perTick)이 끊지만 의도를 확인하라고 알린다
    const usize nt = m_db.m_triggers.size();
    const auto raises = [&](const TriggerDef& t, TriggerEventKind k) {
        return std::any_of(t.effects.begin(), t.effects.end(), [&](const TriggerEffect& x) {
            if (x.kind != TriggerEffectKind::Rule) {
                return false;
            }
            return (k == TriggerEventKind::Score && x.fx.op == EffectOp::ScoreAdd) ||
                   (k == TriggerEventKind::Death && x.fx.op == EffectOp::Destroy) ||
                   (k == TriggerEventKind::AreaEnter && x.fx.op == EffectOp::Spawn);
        });
    };
    std::vector<std::vector<usize>> next(nt);
    for (usize a = 0; a < nt; ++a) {
        for (usize b = 0; b < nt; ++b) {
            if (raises(m_db.m_triggers[a], m_db.m_triggers[b].event.kind)) {
                next[a].push_back(b);
            }
        }
    }
    std::vector<u8> color(nt, 0); // 0 아직 · 1 지나는 중 · 2 끝
    std::vector<usize> stack;
    const auto visit = [&](auto& self, usize a) -> void {
        color[a] = 1;
        stack.push_back(a);
        for (const usize b : next[a]) {
            if (color[b] == 1) {
                // stack 에서 b 부터 끝까지가 고리
                std::string path;
                for (auto it = std::find(stack.begin(), stack.end(), b); it != stack.end(); ++it) {
                    path += m_db.m_triggers[*it].id + " → ";
                }
                path += m_db.m_triggers[b].id;
                warn(m_db.m_triggers[b].file, "", "V10",
                     std::format("재진입 고리 {} — 한 틱에 perTick 번까지만 돈다", path));
            } else if (color[b] == 0) {
                self(self, b);
            }
        }
        stack.pop_back();
        color[a] = 2;
    };
    for (usize a = 0; a < nt; ++a) {
        if (color[a] == 0) {
            visit(visit, a);
        }
    }
}

void ContentLoaderImpl::link() {
    // action 이름 표: Rule 의 action ∪ interact 의 name (정렬)
    std::set<std::string, std::less<>> names;
    for (const Rule& r : m_db.m_rules) {
        names.insert(r.action);
    }
    for (const auto& [name, user] : m_interactUsers) {
        names.insert(name);
    }
    m_db.m_actions.assign(names.begin(), names.end());
    m_db.m_rulesByAction.assign(m_db.m_actions.size(), {});
    for (usize i = 0; i < m_db.m_rules.size(); ++i) {
        Rule& r = m_db.m_rules[i];
        r.actionId = m_db.findAction(r.action);
        m_db.m_rulesByAction[r.actionId - 1u].push_back(static_cast<u32>(i));
    }
    for (auto& list : m_db.m_rulesByAction) {
        std::stable_sort(list.begin(), list.end(), [&](u32 a, u32 b) {
            const Rule& ra = m_db.m_rules[a];
            const Rule& rb = m_db.m_rules[b];
            return ra.priority != rb.priority ? ra.priority > rb.priority : ra.order < rb.order;
        });
    }

    // Behavior: 감지 질의 번호 · interact action 번호
    for (BehaviorGraph& g : m_db.m_behaviors) {
        bool overflow = false;
        const auto queryOf = [&](const TagExpr& e) -> u8 {
            for (usize i = 0; i < g.queries.size(); ++i) {
                if (g.queries[i] == e) {
                    return static_cast<u8>(i);
                }
            }
            if (g.queries.size() >= kMaxBehaviorQueries) {
                overflow = true;
                return 0;
            }
            g.queries.push_back(e);
            return static_cast<u8>(g.queries.size() - 1);
        };
        const auto linkCondition = [&](auto& self, Condition& c) -> void {
            if (c.kind == Condition::Kind::Sensed) {
                c.query = queryOf(c.tags);
            }
            for (Condition& child : c.children) {
                self(self, child);
            }
        };
        const auto linkActions = [&](std::vector<ActionNode>& actions) {
            for (ActionNode& a : actions) {
                if (a.kind == ActionNode::Kind::Seek || a.kind == ActionNode::Kind::Flee) {
                    a.query = queryOf(a.tags);
                } else if (a.kind == ActionNode::Kind::Interact) {
                    a.actionId = m_db.findAction(a.name);
                }
            }
        };
        // 질의 번호는 상태(정의 순) → 전이(정렬 순) 순서로 매긴다 — 데이터만으로 정해진다
        for (BehaviorState& st : g.states) {
            linkActions(st.onEnter);
            linkActions(st.onTick);
            linkActions(st.onExit);
        }
        for (Transition& t : g.transitions) {
            linkCondition(linkCondition, t.when);
        }
        if (overflow) {
            error(g.file, "", "V7",
                  std::format("Behavior '{}' 의 감지 질의(sensed·seek·flee 의 서로 다른 tags)가 {}개를 넘는다", g.id,
                              kMaxBehaviorQueries));
        }
    }
}

std::shared_ptr<ContentSources> ContentLoaderImpl::readSources(std::span<const std::string> packIds) {
    auto sources = std::make_shared<ContentSources>();
    for (const PackDir& p : resolveOrder(discover(), packIds)) {
        ContentSourcePack sp;
        sp.info = p.info;
        sp.folder = p.folder;
        // 클라 전용 키 바인딩 — 원문만 들고 간다 (형식 검사는 클라이언트의 ActionMap, contentHash 제외)
        if (const fs::path input = p.dir / "presentation" / "input.json"; fs::is_regular_file(input)) {
            if (auto text = io::readFile(input)) {
                sp.info.inputBindings = std::move(*text);
            } else {
                warn(p.folder + "/presentation/input.json", "", "IO", text.error().describe());
            }
        }
        for (SourceFile& f : readPack(p)) {
            sp.files.push_back(ContentSourceFile{std::move(f.rel), std::move(f.display), std::move(f.text)});
        }
        sources->packs.push_back(std::move(sp));
    }
    return sources;
}

ContentDatabase ContentLoaderImpl::build(const ContentSources& sources, const ContentOverlay* overlay) {
    m_overlay = overlay;
    m_order.clear();
    std::vector<std::vector<SourceFile>> files;
    files.reserve(sources.packs.size());
    for (const ContentSourcePack& sp : sources.packs) {
        m_order.push_back(PackDir{sp.info, {}, sp.folder});
        std::vector<SourceFile>& out = files.emplace_back();
        for (const ContentSourceFile& f : sp.files) {
            out.push_back(SourceFile{f.rel, f.display, f.text});
        }
        std::set<std::string> vis{sp.info.id};
        vis.insert(sp.info.dependencies.begin(), sp.info.dependencies.end());
        m_visible[sp.info.id] = std::move(vis);
    }

    // 1) 태그 — 모든 팩의 선언을 합쳐 이름 정렬 (비트 인덱스)
    for (usize i = 0; i < m_order.size(); ++i) {
        for (const SourceFile& f : files[i]) {
            if (f.rel == "tags.json") {
                loadTags(m_order[i], f);
            }
        }
    }
    std::sort(m_db.m_tags.begin(), m_db.m_tags.end());
    m_db.m_tags.erase(std::unique(m_db.m_tags.begin(), m_db.m_tags.end()), m_db.m_tags.end());
    if (m_db.m_tags.size() > kMaxTags) {
        error("tags.json", "", "V7", std::format("태그 {}개 (최대 {})", m_db.m_tags.size(), kMaxTags));
    }

    // 2) 머티리얼 — 내장 core.* + 팩
    for (const TerrainMaterial& m : ContentDatabase::builtin().terrainMaterials()) {
        m_db.m_materials.push_back(m);
    }

    // 3) 파일별 — 팩 순서, 파일 경로 순서
    for (usize i = 0; i < m_order.size(); ++i) {
        const PackDir& p = m_order[i];
        m_db.m_packs.push_back(p.info);
        for (const SourceFile& f : files[i]) {
            if (f.rel == "pack.json" || f.rel == "tags.json") {
                continue;
            }
            if (f.rel == "terrain.json") {
                loadTerrain(p, f);
            } else if (f.rel.starts_with("prefabs/") && f.rel.ends_with(".json")) {
                loadPrefabs(p, f);
            } else if (f.rel.starts_with("rules/") && f.rel.ends_with(".json")) {
                loadRules(p, f);
            } else if (f.rel.starts_with("behaviors/") && f.rel.ends_with(".json")) {
                loadBehavior(p, f);
            } else if (f.rel.starts_with("modes/") && f.rel.ends_with(".json")) {
                loadMode(p, f);
            } else if (f.rel == "actions.json") {
                loadActions(p, f);
            } else if (f.rel.starts_with("triggers/") && f.rel.ends_with(".json")) {
                loadTrigger(p, f);
            } else {
                warn(f.display, "", "IO", "알 수 없는 파일 — 읽지 않는다");
            }
        }
    }
    // 4) 월드 오버레이 (ADR-0030) — 팩 항목을 바꾸거나 더한다
    if (overlay != nullptr) {
        loadOverlay(*overlay);
    }

    std::sort(m_db.m_materials.begin(), m_db.m_materials.end(),
              [](const TerrainMaterial& a, const TerrainMaterial& b) { return a.id < b.id; });
    for (usize i = 1; i < m_db.m_materials.size(); ++i) {
        if (m_db.m_materials[i].id == m_db.m_materials[i - 1].id) {
            error("terrain.json", "", "V1", std::format("머티리얼 id '{}' 중복", m_db.m_materials[i].id));
        }
    }
    // Rule 은 정의 순서 (오버레이가 바꾼 Rule 은 원래 자리 — 오버레이로 더한 것은 맨 뒤)
    std::stable_sort(m_db.m_rules.begin(), m_db.m_rules.end(),
                     [](const Rule& a, const Rule& b) { return a.order < b.order; });
    crossCheck();
    link();

    // contentHash = 팩(id, version) + 파일 (경로, 바이트) 매니페스트 + 내장 머티리얼 (09 7장). 오버레이는 넣지 않는다
    Fnv1a64 h;
    h.string("SBXCONTENT2").u64le(ContentDatabase::builtin().contentHash());
    for (usize i = 0; i < m_order.size(); ++i) {
        h.string(m_order[i].info.id).byte(0).string(m_order[i].info.version).byte(0);
        for (const SourceFile& f : files[i]) {
            h.string(f.rel).byte(0).u64le(fnv1a64(f.text));
        }
    }
    m_db.m_hash = h.value();
    if (overlay != nullptr) {
        m_db.m_overlay = *overlay;
    }
    return std::move(m_db);
}

void ContentLoaderImpl::loadOverlay(const ContentOverlay& overlay) {
    if (overlay.size() > kMaxOverlayItems) {
        error("overlay", "", "V7", std::format("오버레이 항목 {}개 (최대 {})", overlay.size(), kMaxOverlayItems));
        return;
    }
    for (u8 k = 0; k < kContentKindCount; ++k) {
        const auto kind = static_cast<ContentKind>(k);
        for (const auto& [id, value] : overlay.items(kind)) {
            const SourceFile f{std::format("overlay/{}/{}.json", contentKindFolder(kind), id),
                               std::format("overlay/{}/{}.json", contentKindFolder(kind), id),
                               {}};
            if (!value.is_object()) {
                error(f.display, "", "JSON", std::format("{} 은 JSON 객체여야 한다", contentKindName(kind)));
                continue;
            }
            if (!value.contains("id") || !value["id"].is_string() || value["id"].get<std::string>() != id) {
                error(f.display, "/id", "V1", std::format("\"id\" 가 오버레이 항목 id '{}' 와 같아야 한다", id));
                continue;
            }
            if (value.dump().size() > kMaxOverlayItemBytes) {
                error(f.display, "", "V7", std::format("JSON 이 {} 바이트를 넘는다", kMaxOverlayItemBytes));
                continue;
            }
            const auto pack =
                std::find_if(m_order.begin(), m_order.end(), [&](const PackDir& p) { return p.info.id == packOf(id); });
            if (pack == m_order.end()) {
                error(f.display, "/id", "V2",
                      std::format("'{}' 의 팩 '{}' 을 읽지 않았다 — 월드의 팩 id 를 접두사로 쓴다", id, packOf(id)));
                continue;
            }
            switch (kind) {
            case ContentKind::Prefab:
                loadPrefab(*pack, f, value, "", true);
                break;
            case ContentKind::Rule:
                loadRule(*pack, f, value, "", true);
                break;
            case ContentKind::Behavior:
                loadBehaviorJson(*pack, f, value, true);
                break;
            }
        }
    }
}

// =====================================================================================================================
std::string ContentIssue::describe() const {
    return std::format("{}:{}: {} {} — {}", file, pointer.empty() ? "/" : pointer,
                       severity == Severity::Error ? "오류" : "경고", rule, message);
}

bool ContentLoadResult::ok() const noexcept {
    return errorCount() == 0;
}
usize ContentLoadResult::errorCount() const noexcept {
    return static_cast<usize>(std::count_if(issues.begin(), issues.end(),
                                            [](const ContentIssue& i) { return i.severity == Severity::Error; }));
}
usize ContentLoadResult::warningCount() const noexcept {
    return issues.size() - errorCount();
}

ContentLoadResult ContentLoader::load(const std::filesystem::path& root, std::span<const std::string> packIds,
                                      const ecs::ComponentCatalog& catalog) {
    ContentLoadResult result;
    ContentLoaderImpl loader(root, catalog, result.issues);
    std::shared_ptr<ContentSources> sources = loader.readSources(packIds);
    result.db = loader.build(*sources, nullptr);
    result.db.m_sources = std::move(sources);
    return result;
}

ContentLoadResult ContentLoader::applyOverlay(const ContentDatabase& base, const ContentOverlay& overlay,
                                              const ecs::ComponentCatalog& catalog) {
    ContentLoadResult result;
    if (base.sources() == nullptr) {
        // 팩 없이 만든 DB (내장 · 머티리얼 JSON) — 오버레이할 팩이 없다
        result.db = base;
        if (!overlay.empty()) {
            result.issues.push_back(ContentIssue{Severity::Error, "V2", "overlay", "",
                                                 "콘텐츠 팩 없이 만든 월드에는 오버레이를 쓸 수 없다"});
        }
        return result;
    }
    ContentLoaderImpl loader({}, catalog, result.issues);
    result.db = loader.build(*base.sources(), &overlay);
    result.db.m_sources = base.sources();
    return result;
}

Expected<ContentDatabase> loadContent(const std::filesystem::path& root, std::span<const std::string> packIds,
                                      const ecs::ComponentCatalog& catalog) {
    ContentLoadResult r = ContentLoader::load(root, packIds, catalog);
    if (r.ok()) {
        return std::move(r.db);
    }
    std::string msg = std::format("콘텐츠 오류 {}개", r.errorCount());
    usize shown = 0;
    for (const ContentIssue& i : r.issues) {
        if (i.severity == ContentIssue::Severity::Error && shown++ < 5) {
            msg += "\n  " + i.describe();
        }
    }
    return makeError(ErrorCode::ValidationFailed, msg, io::displayPath(root));
}

bool isEngineManagedComponent(std::string_view name) noexcept {
    return std::find(kEngineManaged.begin(), kEngineManaged.end(), name) != kEngineManaged.end();
}

bool isOpaqueAllowedComponent(std::string_view name) noexcept {
    return name.starts_with("render.") || name.starts_with("client.");
}

} // namespace sbx::content
