#include "editor/PackGroups.hpp"

#include <algorithm>
#include <format>

#include "core/content/ContentDatabase.hpp"

namespace sbx::editor {

std::vector<PackGroup> groupByPack(const content::ContentDatabase& db, std::span<const std::string_view> ids) {
    std::vector<PackGroup> groups;
    PackGroup unknown;
    unknown.label = "기타";
    for (usize i = 0; i < ids.size(); ++i) {
        const std::string_view pack = ids[i].substr(0, ids[i].find('.'));
        const content::ContentSourcePack* source = nullptr;
        if (db.sources() != nullptr && pack.size() < ids[i].size()) {
            for (const content::ContentSourcePack& sp : db.sources()->packs) {
                if (sp.info.id == pack) {
                    source = &sp;
                }
            }
        }
        if (source == nullptr) {
            unknown.items.push_back(i);
            continue;
        }
        auto g = std::find_if(groups.begin(), groups.end(), [&](const PackGroup& x) { return x.pack == pack; });
        if (g == groups.end()) {
            PackGroup made;
            made.pack = std::string(pack);
            const content::PackInfo& info = source->info;
            if (source->folder.starts_with(".sandbox/packages/")) {
                made.origin = PackOrigin::Installed;
                made.label =
                    std::format("{} {} · {} · 설치한 패키지", made.pack, info.version.empty() ? "?" : info.version,
                                info.license.empty() ? "라이선스 없음" : info.license);
            } else if (source->folder.starts_with("content/")) {
                made.origin = PackOrigin::Project;
                made.label = std::format("{} · 프로젝트", made.pack);
            } else {
                made.origin = PackOrigin::Pack;
                made.label = made.pack;
            }
            groups.push_back(std::move(made));
            g = groups.end() - 1;
        }
        g->items.push_back(i);
    }
    std::stable_sort(groups.begin(), groups.end(), [](const PackGroup& a, const PackGroup& b) {
        return a.origin != b.origin ? a.origin < b.origin : a.pack < b.pack;
    });
    if (!unknown.items.empty()) {
        groups.push_back(std::move(unknown));
    }
    return groups;
}

} // namespace sbx::editor
