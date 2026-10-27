#include "core/package/Lockfile.hpp"

#include <algorithm>
#include <format>
#include <set>

#include <nlohmann/json.hpp>

#include "core/package/GitSource.hpp"
#include "core/package/PackSource.hpp"
#include "foundation/hash/Sha256.hpp"

namespace sbx::package {
namespace {

namespace fs = std::filesystem;
using content::PackSource;
using nlohmann::json;
using nlohmann::ordered_json;

bool isPackId(std::string_view s) noexcept {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// 소스를 사람이 읽는 글로 (매니페스트와 잠금을 견줄 때)
std::string sourceText(PackSource kind, std::string_view path, std::string_view git, std::string_view rev) {
    switch (kind) {
    case PackSource::Root:
        return "content/";
    case PackSource::Path:
        return std::format("path {}", path);
    case PackSource::Git:
        return std::format("git {} @ {}", git, rev);
    }
    return "?";
}

bool isCommit(std::string_view s) noexcept {
    return s.size() == 40 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// 객체 j 의 키가 모두 allowed 안에 있는가 — 아니면 오류 하나씩
void onlyKeys(const json& j, std::initializer_list<std::string_view> allowed, const std::string& at,
              std::vector<std::string>& errors) {
    for (const auto& [k, _] : j.items()) {
        if (std::find(allowed.begin(), allowed.end(), k) == allowed.end()) {
            errors.push_back(std::format("{}/{}: 모르는 키", at, k));
        }
    }
}

// 문자열 키 하나 — 없거나 문자열이 아니면 오류
const std::string* stringKey(const json& j, std::string_view key, const std::string& at,
                             std::vector<std::string>& errors) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        errors.push_back(std::format("{}/{}: 문자열이어야 한다", at, key));
        return nullptr;
    }
    return it->get_ptr<const std::string*>();
}

void parseSource(const json& j, const std::string& at, LockedPack& p, std::vector<std::string>& errors) {
    if (!j.is_object()) {
        errors.push_back(
            std::format("{}: {{\"content\": true}} · {{\"path\"}} · {{\"git\", \"rev\", \"commit\"}}", at));
        return;
    }
    if (j.contains("content")) {
        onlyKeys(j, {"content"}, at, errors);
        if (j["content"] != true) {
            errors.push_back(std::format("{}/content: true 여야 한다", at));
        }
        p.source.kind = PackSource::Root;
    } else if (j.contains("path")) {
        onlyKeys(j, {"path"}, at, errors);
        p.source.kind = PackSource::Path;
        if (const std::string* s = stringKey(j, "path", at, errors)) {
            if (s->empty() || s->find('\\') != std::string::npos) {
                errors.push_back(std::format("{}/path: 빈 경로 · 역슬래시는 쓸 수 없다", at));
            }
            p.source.path = *s;
        }
    } else if (j.contains("git")) {
        onlyKeys(j, {"git", "rev", "commit"}, at, errors);
        p.source.kind = PackSource::Git;
        if (const std::string* s = stringKey(j, "git", at, errors)) {
            if (auto bad = checkGitUrl(*s)) {
                errors.push_back(std::format("{}/git: {}", at, *bad));
            }
            p.source.git = *s;
        }
        if (const std::string* s = stringKey(j, "rev", at, errors)) {
            if (auto bad = checkGitRev(*s)) {
                errors.push_back(std::format("{}/rev: {}", at, *bad));
            }
            p.source.rev = *s;
        }
        if (const std::string* s = stringKey(j, "commit", at, errors)) {
            if (!isCommit(*s)) {
                errors.push_back(std::format("{}/commit: 40 글자 소문자 16 진수 커밋이어야 한다", at));
            }
            p.source.commit = *s;
        }
    } else {
        errors.push_back(std::format("{}: content · path · git 중 하나가 있어야 한다", at));
    }
}

} // namespace

const LockedPack* LockFile::find(std::string_view id) const noexcept {
    const auto it = std::find_if(packs.begin(), packs.end(), [&](const LockedPack& p) { return p.id == id; });
    return it == packs.end() ? nullptr : &*it;
}

LockFile lockFromResolution(const Resolution& resolution) {
    LockFile lock;
    for (const ResolvedPack& r : resolution.packs) {
        LockedPack p;
        p.id = r.id;
        p.version = r.version;
        p.license = r.license;
        p.source = r.source;
        if (p.installed()) {
            p.digest = r.digest;
        } else {
            p.source = ResolvedSource{}; // content/ 팩 — 소스는 종류뿐
        }
        p.dependencies = r.dependencies;
        lock.packs.push_back(std::move(p));
    }
    std::sort(lock.packs.begin(), lock.packs.end(),
              [](const LockedPack& a, const LockedPack& b) { return a.id < b.id; });
    return lock;
}

std::string writeLockFile(const LockFile& lock) {
    ordered_json j;
    j["lockVersion"] = kLockVersion;
    j["packs"] = ordered_json::object();
    for (const LockedPack& p : lock.packs) {
        ordered_json e;
        e["version"] = p.version;
        e["license"] = p.license;
        ordered_json s;
        switch (p.source.kind) {
        case PackSource::Root:
            s["content"] = true;
            break;
        case PackSource::Path:
            s["path"] = p.source.path;
            break;
        case PackSource::Git:
            s["git"] = p.source.git;
            s["rev"] = p.source.rev;
            s["commit"] = p.source.commit;
            break;
        }
        e["source"] = std::move(s);
        if (p.installed()) {
            e["digest"] = p.digest;
        }
        e["dependencies"] = p.dependencies;
        j["packs"][p.id] = std::move(e);
    }
    return j.dump(2) + "\n";
}

LockParseResult parseLockFile(std::string_view text) {
    LockParseResult r;
    std::vector<std::string>& errors = r.errors;
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        errors.push_back("JSON 객체가 아니다");
        return r;
    }
    onlyKeys(j, {"lockVersion", "packs"}, "", errors);
    const auto v = j.find("lockVersion");
    if (v == j.end() || !v->is_number_unsigned() || v->get<u64>() != kLockVersion) {
        errors.push_back(std::format("/lockVersion: {} 이어야 한다 (이 엔진이 읽는 판)", kLockVersion));
        return r; // 다른 판의 나머지는 읽지 않는다
    }
    const auto packs = j.find("packs");
    if (packs == j.end() || !packs->is_object()) {
        errors.push_back("/packs: {팩 id: {...}} 객체여야 한다");
        return r;
    }
    for (const auto& [id, e] : packs->items()) {
        const std::string at = std::format("/packs/{}", id);
        LockedPack p;
        p.id = id;
        if (!isPackId(id) || id == "core") {
            errors.push_back(std::format("{}: 팩 id 가 아니다", at));
        }
        if (!e.is_object()) {
            errors.push_back(std::format("{}: 객체여야 한다", at));
            continue;
        }
        onlyKeys(e, {"version", "license", "source", "digest", "dependencies"}, at, errors);
        if (const std::string* s = stringKey(e, "version", at, errors)) {
            p.version = *s;
        }
        if (const std::string* s = stringKey(e, "license", at, errors)) {
            p.license = *s;
        }
        if (const auto s = e.find("source"); s != e.end()) {
            parseSource(*s, at + "/source", p, errors);
        } else {
            errors.push_back(std::format("{}/source: 없다", at));
        }
        if (p.installed()) {
            if (const std::string* s = stringKey(e, "digest", at, errors)) {
                if (!isSha256Hex(*s)) {
                    errors.push_back(std::format("{}/digest: 64 글자 소문자 SHA-256 이어야 한다", at));
                }
                p.digest = *s;
            }
        } else if (e.contains("digest")) {
            errors.push_back(std::format("{}/digest: content/ 팩은 잠그지 않는다", at));
        }
        const auto deps = e.find("dependencies");
        if (deps == e.end() || !deps->is_array()) {
            errors.push_back(std::format("{}/dependencies: 팩 id 배열이어야 한다", at));
        } else {
            for (const json& d : *deps) {
                if (!d.is_string() || !isPackId(d.get<std::string>())) {
                    errors.push_back(std::format("{}/dependencies: 팩 id 가 아닌 값", at));
                } else {
                    p.dependencies.push_back(d.get<std::string>());
                }
            }
            if (!std::is_sorted(p.dependencies.begin(), p.dependencies.end())) {
                errors.push_back(std::format("{}/dependencies: 팩 id 순이어야 한다", at));
            }
        }
        r.lock.packs.push_back(std::move(p));
    }
    // nlohmann::json 의 객체는 키 순 — packs 는 이미 팩 id 순이다
    for (const LockedPack& p : r.lock.packs) {
        for (const std::string& d : p.dependencies) {
            if (r.lock.find(d) == nullptr) {
                errors.push_back(std::format("/packs/{}/dependencies: '{}' 가 잠금에 없다", p.id, d));
            }
        }
    }
    return r;
}

std::vector<std::string> checkLockMatches(const LockFile& lock, std::span<const content::PackDependency> dependencies) {
    std::vector<std::string> errors;
    std::set<std::string> reached;
    std::vector<std::string> stack;
    for (const content::PackDependency& d : dependencies) {
        const LockedPack* p = lock.find(d.pack);
        if (p == nullptr) {
            errors.push_back(std::format("매니페스트의 의존 '{}' 가 잠금에 없다", d.pack));
            continue;
        }
        const std::string want = sourceText(d.source, d.path, d.git, d.rev);
        const std::string have = sourceText(p->source.kind, p->source.path, p->source.git, p->source.rev);
        if (want != have) {
            errors.push_back(
                std::format("'{}' 의 소스가 매니페스트 ({}) 와 잠금 ({}) 에서 다르다", d.pack, want, have));
        }
        stack.push_back(d.pack);
    }
    while (!stack.empty()) {
        const std::string id = std::move(stack.back());
        stack.pop_back();
        if (!reached.insert(id).second) {
            continue;
        }
        if (const LockedPack* p = lock.find(id)) {
            stack.insert(stack.end(), p->dependencies.begin(), p->dependencies.end());
        }
    }
    for (const LockedPack& p : lock.packs) {
        if (!reached.contains(p.id)) {
            errors.push_back(std::format("잠금의 '{}' 를 매니페스트의 의존이 쓰지 않는다", p.id));
        }
    }
    return errors;
}

std::vector<std::string> verifyInstalled(const fs::path& projectDir, const LockFile& lock) {
    std::vector<std::string> errors;
    const fs::path root = projectDir / fs::path(std::string(kInstallDir));
    std::set<std::string> expected;
    for (const LockedPack& p : lock.packs) {
        if (!p.installed()) {
            continue;
        }
        expected.insert(p.id);
        const fs::path dir = root / p.id;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            errors.push_back(std::format("팩 '{}' 가 {}/{} 에 설치되지 않았다", p.id, kInstallDir, p.id));
            continue;
        }
        const auto snap = snapshotPack(dir);
        if (!snap) {
            errors.push_back(std::format("{}/{}: {}", kInstallDir, p.id, snap.error().describe()));
            continue;
        }
        if (snap->id != p.id) {
            errors.push_back(std::format("{}/{}: pack.json id '{}' 가 폴더 · 잠금의 '{}' 와 다르다", kInstallDir, p.id,
                                         snap->id, p.id));
        } else if (snap->digest != p.digest) {
            errors.push_back(std::format("{}/{}: digest {} 가 잠금의 {} 와 다르다 (설치한 뒤 고쳐졌다 — 다시 설치)",
                                         kInstallDir, p.id, snap->digest.substr(0, 16), p.digest.substr(0, 16)));
        }
    }
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        std::vector<std::string> extra;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            const std::string name = e.path().filename().string();
            if (!expected.contains(name)) {
                extra.push_back(name);
            }
        }
        std::sort(extra.begin(), extra.end());
        for (const std::string& name : extra) {
            errors.push_back(std::format("{}/{}: 잠금에 없는 설치 폴더", kInstallDir, name));
        }
    }
    return errors;
}

} // namespace sbx::package
