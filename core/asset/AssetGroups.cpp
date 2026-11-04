#include "core/asset/AssetGroups.hpp"

#include <algorithm>
#include <format>
#include <set>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"

namespace sbx::asset {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;
using content::ContentIssue;
using Severity = ContentIssue::Severity;

constexpr std::string_view kLabelPrefix = "label:";
constexpr usize kMaxPatternBytes = 200;
constexpr usize kMaxGroupsFileBytes = 256 * 1024;

// glob 의 글자 — 경로 규칙 ([A-Za-z0-9_.-] · '/') 에 * · ? 를 더한 것. ".." 조각 · 앞 '/' 는 없다
bool validPattern(std::string_view p) {
    if (p.empty() || p.size() > kMaxPatternBytes || p.front() == '/' || p.back() == '/') {
        return false;
    }
    const bool chars = std::all_of(p.begin(), p.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
               c == '-' || c == '/' || c == '*' || c == '?';
    });
    if (!chars) {
        return false;
    }
    usize start = 0;
    while (start <= p.size()) {
        const usize end = std::min(p.find('/', start), p.size());
        const std::string_view seg = p.substr(start, end - start);
        if (seg.empty() || seg == "." || seg == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool globMatch(std::string_view p, std::string_view s) {
    while (!p.empty()) {
        if (p.starts_with("**")) {
            p.remove_prefix(2);
            if (p.starts_with('/') && globMatch(p.substr(1), s)) {
                return true; // "**/" = 폴더 0 개
            }
            for (usize i = 0; i <= s.size(); ++i) {
                if (globMatch(p, s.substr(i))) {
                    return true;
                }
            }
            return false;
        }
        if (p.front() == '*') {
            for (usize i = 0; i <= s.size(); ++i) {
                if (globMatch(p.substr(1), s.substr(i))) {
                    return true;
                }
                if (i < s.size() && s[i] == '/') {
                    break; // * 는 폴더를 넘지 않는다
                }
            }
            return false;
        }
        if (s.empty() || (p.front() == '?' ? s.front() == '/' : p.front() != s.front())) {
            return false;
        }
        p.remove_prefix(1);
        s.remove_prefix(1);
    }
    return s.empty();
}

bool entryMatches(const AssetEntry& e, std::string_view include) {
    if (include.starts_with(kLabelPrefix)) {
        return std::binary_search(e.labels.begin(), e.labels.end(), include.substr(kLabelPrefix.size()));
    }
    if (matchAssetGlob(include, e.path)) {
        return true;
    }
    return std::any_of(e.copies.begin(), e.copies.end(),
                       [&](const std::string& c) { return matchAssetGlob(include, c); });
}

} // namespace

bool AssetGroupsFile::ok() const noexcept {
    return std::none_of(issues.begin(), issues.end(),
                        [](const ContentIssue& i) { return i.severity == Severity::Error; });
}

AssetGroupsFile parseAssetGroups(std::string_view jsonText) {
    AssetGroupsFile r;
    r.present = true;
    const std::string file(kGroupsFile);
    const auto error = [&](std::string pointer, std::string message) {
        r.issues.push_back({Severity::Error, "A7", file, std::move(pointer), std::move(message)});
    };
    if (jsonText.size() > kMaxGroupsFileBytes) {
        error("", std::format("{} 바이트를 넘는다", kMaxGroupsFileBytes));
        return r;
    }
    const json j = json::parse(jsonText, nullptr, false);
    if (!j.is_object() || !j.contains("groups") || !j["groups"].is_array() || j.size() != 1) {
        error("", R"({"groups": [{"name", "include": [glob · "label:<라벨>"]}]} 객체 (다른 키 없음))");
        return r;
    }
    const json& groups = j["groups"];
    if (groups.size() > kMaxGroups) {
        error("/groups", std::format("그룹은 {} 개까지", kMaxGroups));
        return r;
    }
    std::vector<AssetGroup> out;
    for (usize gi = 0; gi < groups.size(); ++gi) {
        const json& g = groups[gi];
        const std::string where = std::format("/groups/{}", gi);
        if (!g.is_object() || !g.contains("name") || !g["name"].is_string() || !g.contains("include") ||
            !g["include"].is_array() || g.size() != 2) {
            error(where, "그룹은 {name, include} (다른 키 없음)");
            continue;
        }
        AssetGroup group{g["name"].get<std::string>(), {}};
        if (!isAssetLabel(group.name)) {
            error(where + "/name", "그룹 이름은 [a-z0-9_-] 1 ~ 32 글자");
        } else if (std::any_of(out.begin(), out.end(), [&](const AssetGroup& o) { return o.name == group.name; })) {
            error(where + "/name", std::format("그룹 '{}' 이 두 번 있다", group.name));
        }
        const json& inc = g["include"];
        if (inc.empty() || inc.size() > kMaxGroupIncludes) {
            error(where + "/include", std::format("include 는 1 ~ {} 개", kMaxGroupIncludes));
        }
        for (usize k = 0; k < inc.size() && k < kMaxGroupIncludes; ++k) {
            const std::string at = std::format("{}/include/{}", where, k);
            if (!inc[k].is_string()) {
                error(at, "문자열 (glob · \"label:<라벨>\")");
                continue;
            }
            std::string v = inc[k].get<std::string>();
            const bool ok = v.starts_with(kLabelPrefix) ? isAssetLabel(std::string_view(v).substr(kLabelPrefix.size()))
                                                        : validPattern(v);
            if (!ok) {
                error(at, std::format("'{}' — assets/ 기준 glob ([A-Za-z0-9_.-] · '/' · * · ** · ?, \"..\" 없음) 또는 "
                                      "\"label:<라벨>\"",
                                      v));
            }
            group.include.push_back(std::move(v));
        }
        out.push_back(std::move(group));
    }
    if (r.ok()) {
        r.groups = std::move(out);
    }
    return r;
}

AssetGroupsFile loadAssetGroups(const std::filesystem::path& projectDir) {
    const fs::path f = projectDir / fs::path(std::string(kGroupsFile));
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(f, ec))) {
        return {};
    }
    if (!fs::is_regular_file(fs::symlink_status(f, ec))) {
        AssetGroupsFile r;
        r.present = true;
        r.issues.push_back({Severity::Error, "A7", std::string(kGroupsFile), "", "일반 파일이 아니다"});
        return r;
    }
    const auto text = io::readFile(f);
    if (!text) {
        AssetGroupsFile r;
        r.present = true;
        r.issues.push_back({Severity::Error, "A7", std::string(kGroupsFile), "",
                            std::format("읽을 수 없다 — {}", text.error().describe())});
        return r;
    }
    return parseAssetGroups(*text);
}

bool matchAssetGlob(std::string_view pattern, std::string_view path) noexcept {
    const std::string prefix = std::string(kAssetDir) + "/";
    if (!path.starts_with(prefix)) {
        return false;
    }
    return globMatch(pattern, path.substr(prefix.size()));
}

std::vector<std::string> materialAssetIds(const content::ContentDatabase& db, const AssetScan& scan) {
    std::set<std::string> ids;
    const std::string prefix = std::string(kAssetDir) + "/";
    for (const content::PackInfo& p : db.packs()) {
        if (p.materials.empty()) {
            continue;
        }
        const json doc = json::parse(p.materials, nullptr, false);
        if (!doc.is_object() || !doc.contains("materials") || !doc["materials"].is_object()) {
            continue; // 문제는 A6 이 알린다
        }
        for (const auto& [name, m] : doc["materials"].items()) {
            if (!m.is_object() || !m.contains("sprite") || !m["sprite"].is_string()) {
                continue;
            }
            const std::string sprite = m["sprite"].get<std::string>();
            if (sprite.starts_with("asset:")) {
                if (const AssetEntry* e = scan.find(std::string_view(sprite).substr(6))) {
                    ids.insert(e->id);
                }
            } else if (sprite.starts_with(prefix)) {
                for (const AssetEntry& e : scan.assets) {
                    if (e.path == sprite || std::find(e.copies.begin(), e.copies.end(), sprite) != e.copies.end()) {
                        ids.insert(e.id);
                    }
                }
            }
        }
    }
    return {ids.begin(), ids.end()};
}

AssetUsage summarizeAssetUsage(const AssetScan& scan, std::span<const AssetGroup> groups,
                               std::span<const std::string> referencedIds) {
    AssetUsage r;
    std::set<std::string> covered(referencedIds.begin(), referencedIds.end());
    for (usize gi = 0; gi < groups.size(); ++gi) {
        const AssetGroup& g = groups[gi];
        std::set<std::string> ids;
        for (usize k = 0; k < g.include.size(); ++k) {
            usize matched = 0;
            for (const AssetEntry& e : scan.assets) {
                if (entryMatches(e, g.include[k])) {
                    ids.insert(e.id);
                    ++matched;
                }
            }
            if (matched == 0) {
                r.issues.push_back({Severity::Warning, "A7", std::string(kGroupsFile),
                                    std::format("/groups/{}/include/{}", gi, k),
                                    std::format("그룹 '{}' 의 '{}' 가 고르는 에셋이 없다", g.name, g.include[k])});
            }
        }
        GroupSummary s{g.name, {ids.begin(), ids.end()}, 0};
        for (const std::string& id : s.ids) {
            s.bytes += scan.find(id)->size;
        }
        covered.insert(ids.begin(), ids.end());
        r.groups.push_back(std::move(s));
    }
    for (const AssetEntry& e : scan.assets) {
        if (!covered.contains(e.id)) {
            r.unreferenced.push_back(e.path);
        }
    }
    std::sort(r.unreferenced.begin(), r.unreferenced.end());
    if (!r.unreferenced.empty()) {
        r.issues.push_back(
            {Severity::Warning, "A8", std::string(kAssetDir), "",
             std::format("어디서도 부르지 않는 에셋 {} 개 (첫째 '{}') — 정적 참조 (materials.json) 도 그룹 "
                         "(assets/groups.json) 도 아니다. 패키지에는 싣는다 (지우지 않는다)",
                         r.unreferenced.size(), r.unreferenced.front())});
    }
    return r;
}

} // namespace sbx::asset
