#include "core/content/ContentLoader.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <queue>
#include <set>

#include "foundation/container/FixedString.hpp"
#include "foundation/hash/Fnv1a.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::content {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Severity = ContentIssue::Severity;

constexpr std::array kEngineManaged{"persist.persistence", "net.identity", "core.tags", "core.prefab"};

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

    ContentDatabase run(std::span<const std::string> packIds);

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
    void loadPrefab(const PackDir& p, const SourceFile& f, const json& j, const std::string& at);
    void loadRules(const PackDir& p, const SourceFile& f);
    void loadBehavior(const PackDir& p, const SourceFile& f);
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
    for (const PackDir& p : order) {
        std::set<std::string> vis{p.info.id};
        vis.insert(p.info.dependencies.begin(), p.info.dependencies.end());
        m_visible[p.info.id] = std::move(vis);
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
        loadPrefab(p, f, j, "");
        return;
    }
    for (usize i = 0; i < j.size(); ++i) {
        loadPrefab(p, f, j[i], ptr("", i));
    }
}

void ContentLoaderImpl::loadPrefab(const PackDir& p, const SourceFile& f, const json& j, const std::string& at) {
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
        const json& r = j[i];
        const std::string at = ptr("", i);
        if (!r.is_object()) {
            error(f.display, at, "JSON", "Rule 은 객체");
            continue;
        }
        bool ok = onlyKeys(
            r, {"id", "action", "source", "target", "range", "priority", "exclusive", "conditions", "effects"}, f, at);
        Rule rule;
        rule.file = f.display;
        rule.order = m_ruleOrder++;
        if (!r.contains("id") || !checkId(p, f, ptr(at, "id"), r["id"], rule.id)) {
            continue;
        }
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
        static const std::map<std::string, CmpOp, std::less<>> kOps{{"<", CmpOp::Lt},  {"<=", CmpOp::Le},
                                                                    {">", CmpOp::Gt},  {">=", CmpOp::Ge},
                                                                    {"==", CmpOp::Eq}, {"!=", CmpOp::Ne}};
        if (r.contains("conditions")) {
            const json& cs = r["conditions"];
            if (!cs.is_array()) {
                error(f.display, ptr(at, "conditions"), "V3", "conditions 는 배열");
                ok = false;
            } else {
                for (usize k = 0; k < cs.size(); ++k) {
                    const std::string cat = ptr(ptr(at, "conditions"), k);
                    const json& c = cs[k];
                    if (!c.is_object() || !onlyKeys(c, {"field", "op", "value"}, f, cat) || !c.contains("field") ||
                        !c["field"].is_string() || !c.contains("op") || !c["op"].is_string() || !c.contains("value") ||
                        !c["value"].is_number()) {
                        error(f.display, cat, "V3",
                              R"(조건은 {"field": "source.<컴포넌트>.<필드>", "op": "<", "value": 수})");
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
                    const auto ref =
                        parseFieldRef(std::string_view(path).substr(path.find('.') + 1), f, ptr(cat, "field"));
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
                    rule.conditions.push_back(std::move(rc));
                }
            }
        }
        if (!r.contains("effects") || !r["effects"].is_array() || r["effects"].empty()) {
            error(f.display, ptr(at, "effects"), "V3", "effects 는 비어 있지 않은 배열");
            ok = false;
        } else {
            const json& es = r["effects"];
            for (usize k = 0; k < es.size(); ++k) {
                const std::string eat = ptr(ptr(at, "effects"), k);
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
                } else if (op != "spawn" && op != "event") {
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
                        rule.exclusive = true; // 03 6.2: destroy(target) 이 있으면 배타
                    }
                } else if (op == "spawn") {
                    fx.op = EffectOp::Spawn;
                    fx.who = e.contains("who") ? fx.who : Who::Source;
                    if (!onlyKeys(e, {"op", "who", "prefab", "count"}, f, eat) || !e.contains("prefab") ||
                        !e["prefab"].is_string()) {
                        error(f.display, eat, "V3", R"({"op": "spawn", "prefab": id, "count": 1~16, "who"})");
                        ok = false;
                        continue;
                    }
                    fx.prefab = e["prefab"].get<std::string>();
                    noteRef(true, fx.prefab, p, f, ptr(eat, "prefab"));
                    if (e.contains("count")) {
                        if (!e["count"].is_number_unsigned() || e["count"].get<u64>() < 1 ||
                            e["count"].get<u64>() > 16) {
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
                } else {
                    error(f.display, ptr(eat, "op"), "V3",
                          std::format("모르는 효과 '{}' (field.add field.set destroy spawn tag.add tag.remove event)",
                                      op));
                    ok = false;
                    continue;
                }
                rule.effects.push_back(std::move(fx));
            }
        }
        if (ok) {
            m_db.m_rules.push_back(std::move(rule));
        }
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
    bool ok = onlyKeys(j, {"id", "initial", "states", "transitions"}, f, "");
    BehaviorGraph g;
    g.file = f.display;
    if (!j.contains("id") || !checkId(p, f, "/id", j["id"], g.id)) {
        return;
    }
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

ContentDatabase ContentLoaderImpl::run(std::span<const std::string> packIds) {
    m_order = resolveOrder(discover(), packIds);

    std::vector<std::vector<SourceFile>> files;
    files.reserve(m_order.size());
    for (const PackDir& p : m_order) {
        files.push_back(readPack(p));
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
            } else if (f.rel == "actions.json") {
                warn(f.display, "", "V3", "actions.json (PlayerAction) 은 Phase 12 — 지금은 읽지 않는다");
            } else {
                warn(f.display, "", "IO", "알 수 없는 파일 — 읽지 않는다");
            }
        }
    }

    std::sort(m_db.m_materials.begin(), m_db.m_materials.end(),
              [](const TerrainMaterial& a, const TerrainMaterial& b) { return a.id < b.id; });
    for (usize i = 1; i < m_db.m_materials.size(); ++i) {
        if (m_db.m_materials[i].id == m_db.m_materials[i - 1].id) {
            error("terrain.json", "", "V1", std::format("머티리얼 id '{}' 중복", m_db.m_materials[i].id));
        }
    }
    crossCheck();
    link();

    // contentHash = 팩(id, version) + 파일 (경로, 바이트) 매니페스트 + 내장 머티리얼 (09 7장)
    Fnv1a64 h;
    h.string("SBXCONTENT2").u64le(ContentDatabase::builtin().contentHash());
    for (usize i = 0; i < m_order.size(); ++i) {
        h.string(m_order[i].info.id).byte(0).string(m_order[i].info.version).byte(0);
        for (const SourceFile& f : files[i]) {
            h.string(f.rel).byte(0).u64le(fnv1a64(f.text));
        }
    }
    m_db.m_hash = h.value();
    return std::move(m_db);
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
    result.db = loader.run(packIds);
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
