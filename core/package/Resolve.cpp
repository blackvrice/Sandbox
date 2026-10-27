#include "core/package/Resolve.hpp"

#include <algorithm>
#include <deque>
#include <format>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"

namespace sbx::package {
namespace {

namespace fs = std::filesystem;
using content::PackDependency;
using content::PackSource;
using nlohmann::json;

constexpr std::string_view kProject = "project";

// 소스 하나를 사람이 읽는 글로
std::string describe(const ResolvedSource& s) {
    switch (s.kind) {
    case PackSource::Root:
        return "content/";
    case PackSource::Path:
        return std::format("path {}", s.path);
    case PackSource::Git:
        return std::format("git {} @ {}", s.git, s.rev);
    }
    return "?";
}

// <프로젝트>/content 에서 팩 id → 폴더 (pack.json 의 id — 폴더 이름은 자유)
std::map<std::string, fs::path, std::less<>> contentPacks(const fs::path& contentDir) {
    std::map<std::string, fs::path, std::less<>> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(contentDir, ec)) {
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

// 받은 팩 (아직 의존을 따라가기 전)
struct Node {
    ResolvedPack pack;
    fs::path base; // 이 팩의 path 의존을 풀 기준 폴더 (Root · Path 는 원래 폴더, Git 은 없음)
    bool fromGit = false;
};

// 요구 하나 — 누가 (요구자) 무엇을 (dep)
struct Want {
    std::string requester;
    PackDependency dep;
    fs::path base; // requester 의 path 기준
    bool requesterGit = false;
};

bool sameSource(const ResolvedSource& a, const ResolvedSource& b) {
    return a.kind == b.kind && a.path == b.path && a.git == b.git && a.rev == b.rev;
}

} // namespace

const ResolvedPack* Resolution::find(std::string_view id) const noexcept {
    for (const ResolvedPack& p : packs) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<PackDependency> readPackDependencies(const fs::path& packDir, std::vector<std::string>& errors) {
    std::vector<PackDependency> out;
    const std::string where = io::displayPath(packDir / "pack.json");
    const auto text = io::readFile(packDir / "pack.json");
    if (!text) {
        errors.push_back(std::format("{}: 읽을 수 없다", where));
        return out;
    }
    const json j = json::parse(*text, nullptr, false);
    if (!j.is_object()) {
        errors.push_back(std::format("{}: JSON 객체가 아니다", where));
        return out;
    }
    std::set<std::string> seen;
    if (const auto d = j.find("dependencies"); d != j.end()) {
        if (!d->is_object()) {
            errors.push_back(std::format("{}: dependencies 는 객체", where));
        } else {
            for (const auto& [id, value] : d->items()) {
                PackDependency dep;
                dep.pack = id;
                if (auto bad = content::parsePackDependency(value, dep)) {
                    errors.push_back(std::format("{}:/dependencies/{}{}: {}", where, id, bad->pointer, bad->message));
                    continue;
                }
                seen.insert(id);
                out.push_back(std::move(dep));
            }
        }
    }
    if (const auto r = j.find("requires"); r != j.end() && r->is_array()) {
        for (const json& id : *r) {
            if (id.is_string() && !seen.contains(id.get<std::string>())) {
                PackDependency dep;
                dep.pack = id.get<std::string>(); // 범위 · 소스 없음 = 콘텐츠 루트 (지금 requires 의 뜻)
                seen.insert(dep.pack);
                out.push_back(std::move(dep));
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const PackDependency& a, const PackDependency& b) { return a.pack < b.pack; });
    return out;
}

Resolution resolveDependencies(const fs::path& projectDir, std::span<const PackDependency> roots,
                               const ResolveOptions& options) {
    Resolution res;
    const auto content = contentPacks(projectDir / "content");
    std::map<std::string, Node, std::less<>> nodes; // 팩 id → 받은 팩
    std::set<std::string, std::less<>> pinned;      // 프로젝트가 직접 적은 팩 (고정)
    std::vector<Want> ranges;                       // 범위 확인 (모두 받은 뒤)
    std::deque<Want> queue;
    for (const PackDependency& d : roots) {
        pinned.insert(d.pack);
        queue.push_back(Want{std::string(kProject), d, projectDir, false});
    }

    // 소스 하나를 받는다 — 실패하면 오류를 남기고 nullopt
    const auto fetch = [&](const Want& w) -> std::optional<Node> {
        const PackDependency& d = w.dep;
        const std::string who = std::format("{} → {}", w.requester, d.pack);
        Node n;
        n.pack.id = d.pack;
        n.pack.source.kind = d.source;
        PackSnapshot snap;
        switch (d.source) {
        case PackSource::Root: {
            const auto it = content.find(d.pack);
            if (it == content.end()) {
                res.errors.push_back(std::format("{}: content/ 에 그 팩이 없다 (소스를 적거나 팩을 content/ 에)", who));
                return std::nullopt;
            }
            auto s = snapshotPack(it->second);
            if (!s) {
                res.errors.push_back(std::format("{}: {}", who, s.error().describe()));
                return std::nullopt;
            }
            snap = std::move(*s);
            n.pack.dir = it->second;
            n.base = it->second;
            break;
        }
        case PackSource::Path: {
            if (w.requesterGit) {
                res.errors.push_back(std::format(
                    "{}: Git 패키지의 path 의존은 쓸 수 없다 (저장소 밖을 가리킨다) — git 소스로 적는다", who));
                return std::nullopt;
            }
            const fs::path src = resolveLocalSource(w.base, d.path);
            auto c = cacheLocalPack(src, d.pack, options.cacheRoot);
            if (!c) {
                res.errors.push_back(std::format("{}: path {} — {}", who, d.path, c.error().describe()));
                return std::nullopt;
            }
            snap = std::move(c->snapshot);
            n.pack.dir = c->dir;
            n.pack.source.path = d.path;
            n.base = src;
            break;
        }
        case PackSource::Git: {
            if (!options.run) {
                res.errors.push_back(std::format("{}: Git 소스를 받을 실행기가 없다", who));
                return std::nullopt;
            }
            auto g = fetchGitPack(d.git, d.rev, d.pack, options.cacheRoot, options.run);
            if (!g) {
                res.errors.push_back(std::format("{}: git {} @ {} — {}", who, d.git, d.rev, g.error().describe()));
                return std::nullopt;
            }
            snap = std::move(g->pack.snapshot);
            n.pack.dir = g->pack.dir;
            n.pack.source.git = d.git;
            n.pack.source.rev = d.rev;
            n.pack.source.commit = g->commit;
            n.fromGit = true;
            break;
        }
        }
        n.pack.version = snap.version;
        n.pack.digest = snap.digest;
        return n;
    };

    while (!queue.empty()) {
        Want w = std::move(queue.front());
        queue.pop_front();
        if (w.dep.range) {
            ranges.push_back(w);
        }
        const bool fromProject = w.requester == kProject;
        if (const auto it = nodes.find(w.dep.pack); it != nodes.end()) {
            Node& have = it->second;
            have.pack.requiredBy.push_back(w.requester);
            // 같은 팩을 다른 소스로 — 프로젝트 고정이면 그대로 두고, 아니면 받아 digest 를 견준다
            const ResolvedSource asked{w.dep.source, w.dep.path, w.dep.git, w.dep.rev, {}};
            if (pinned.contains(w.dep.pack) && !fromProject) {
                continue; // 프로젝트가 정했다 — 다른 소스는 받지 않는다 (범위는 아래에서 본다)
            }
            if (sameSource(asked, have.pack.source) && (asked.kind != PackSource::Path || w.base == have.base)) {
                continue;
            }
            const auto other = fetch(w);
            if (other && other->pack.digest != have.pack.digest) {
                res.errors.push_back(std::format("팩 '{}' 충돌: {} 는 {} ({}), {} 는 {} ({}) — 한 버전만 쓸 수 있다 "
                                                 "(프로젝트 dependencies 에 적어 고정한다)",
                                                 w.dep.pack, have.pack.requiredBy.front(), describe(have.pack.source),
                                                 have.pack.version, w.requester, describe(other->pack.source),
                                                 other->pack.version));
            }
            continue;
        }
        auto n = fetch(w);
        if (!n) {
            continue;
        }
        n->pack.requiredBy.push_back(w.requester);
        std::vector<std::string> shapeErrors;
        const std::vector<PackDependency> deps = readPackDependencies(n->pack.dir, shapeErrors);
        for (const std::string& e : shapeErrors) {
            res.errors.push_back(e);
        }
        for (const PackDependency& d : deps) {
            if (d.pack == n->pack.id) {
                res.errors.push_back(std::format("팩 '{}' 가 자기 자신을 요구한다", d.pack));
                continue;
            }
            n->pack.dependencies.push_back(d.pack);
            queue.push_back(Want{n->pack.id, d, n->base, n->fromGit});
        }
        // pack.json 의 license (요약 · 잠금 파일)
        if (const auto text = io::readFile(n->pack.dir / "pack.json")) {
            const json j = json::parse(*text, nullptr, false);
            if (j.is_object() && j.contains("license") && j["license"].is_string()) {
                n->pack.license = j["license"].get<std::string>();
            }
        }
        const std::string id = n->pack.id;
        nodes.emplace(id, std::move(*n));
    }

    // 범위 — 그 팩을 가리키는 모든 범위를 version 이 만족해야 한다
    for (const Want& w : ranges) {
        const auto it = nodes.find(w.dep.pack);
        if (it == nodes.end()) {
            continue; // 받지 못했다 — 위에서 말했다
        }
        const ResolvedPack& p = it->second.pack;
        const auto v = parseSemVer(p.version);
        if (!v) {
            res.errors.push_back(
                std::format("{} 는 '{}' {} 을 요구하는데 그 팩의 version '{}' 이 SemVer 가 아니다 ({})", w.requester,
                            p.id, w.dep.range->text, p.version, describe(p.source)));
        } else if (!w.dep.range->contains(*v)) {
            res.errors.push_back(std::format("{} 는 '{}' {} 을 요구하는데 {} 는 {} 이다 ({})", w.requester, p.id,
                                             w.dep.range->text, p.id, p.version, describe(p.source)));
        }
    }

    // 순환 — 깊이 우선, 고리를 찾으면 그 팩들을 말한다
    std::map<std::string, int, std::less<>> state; // 0 안 봄 · 1 보는 중 · 2 끝
    std::vector<std::string> stack;
    std::set<std::string> reported;
    const auto visit = [&](auto&& self, const std::string& id) -> void {
        state[id] = 1;
        stack.push_back(id);
        const auto it = nodes.find(id);
        if (it != nodes.end()) {
            for (const std::string& d : it->second.pack.dependencies) {
                if (!nodes.contains(d)) {
                    continue;
                }
                if (state[d] == 1) {
                    const auto from = std::find(stack.begin(), stack.end(), d);
                    std::string ring;
                    for (auto p = from; p != stack.end(); ++p) {
                        ring += *p + " → ";
                    }
                    ring += d;
                    if (reported.insert(d).second) {
                        res.errors.push_back(std::format("의존에 순환이 있다: {}", ring));
                    }
                } else if (state[d] == 0) {
                    self(self, d);
                }
            }
        }
        stack.pop_back();
        state[id] = 2;
    };
    for (const auto& [id, _] : nodes) {
        if (state[id] == 0) {
            visit(visit, id);
        }
    }

    for (auto& [id, n] : nodes) {
        std::sort(n.pack.dependencies.begin(), n.pack.dependencies.end());
        std::sort(n.pack.requiredBy.begin(), n.pack.requiredBy.end());
        n.pack.requiredBy.erase(std::unique(n.pack.requiredBy.begin(), n.pack.requiredBy.end()),
                                n.pack.requiredBy.end());
        res.packs.push_back(std::move(n.pack));
    }
    return res;
}

} // namespace sbx::package
