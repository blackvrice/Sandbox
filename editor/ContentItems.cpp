#include "editor/ContentItems.hpp"

#include <algorithm>

#include "core/content/ContentDatabase.hpp"

namespace sbx::editor {

std::optional<ScriptFile> scriptFileOf(const content::ContentDatabase& db, std::string_view id) {
    const auto path = content::scriptOverlayPath(id);
    if (!path || db.sources() == nullptr) {
        return std::nullopt;
    }
    for (const content::ContentSourcePack& sp : db.sources()->packs) {
        if (sp.info.id != path->pack) {
            continue;
        }
        ScriptFile out{sp.folder + "/" + path->rel, nullptr,
                       sp.dir.empty() ? std::filesystem::path() : sp.dir / path->rel,
                       sp.folder.starts_with(".sandbox/packages/")};
        for (const content::ContentSourceFile& f : sp.files) {
            if (f.rel == path->rel) {
                out.text = &f.text;
            }
        }
        return out;
    }
    return std::nullopt;
}

std::optional<std::string> scriptSourceOf(const content::ContentDatabase& db, std::string_view id) {
    if (const ecs::Json* v = db.overlay().find(content::ContentKind::Script, id);
        v != nullptr && v->contains("source") && (*v)["source"].is_string()) {
        return (*v)["source"].get<std::string>();
    }
    const auto file = scriptFileOf(db, id);
    if (file && file->text != nullptr) {
        return *file->text;
    }
    return std::nullopt;
}

std::vector<ContentItem> contentItems(const content::ContentDatabase& db, content::ContentKind kind) {
    std::vector<ContentItem> out;
    switch (kind) {
    case content::ContentKind::Prefab:
        for (const content::Prefab& p : db.prefabs()) {
            out.push_back({p.id, p.overlay, &p.raw});
        }
        break;
    case content::ContentKind::Rule:
        for (const content::Rule& r : db.rules()) {
            out.push_back({r.id, r.overlay, &r.raw});
        }
        break;
    case content::ContentKind::Behavior:
        for (const content::BehaviorGraph& g : db.behaviors()) {
            out.push_back({g.id, g.overlay, &g.raw});
        }
        break;
    case content::ContentKind::Trigger:
        for (const content::TriggerDef& t : db.triggers()) { // 실행 순서
            out.push_back({t.id, t.overlay, &t.raw});
        }
        break;
    case content::ContentKind::Script: {
        // 팩의 scripts/*.ts (오버레이 id 로 쓸 수 있는 경로만) + 오버레이로 더한 파일 — id 정렬
        std::vector<std::string> ids;
        if (db.sources() != nullptr) {
            for (const content::ContentSourcePack& sp : db.sources()->packs) {
                for (const content::ContentSourceFile& f : sp.files) {
                    if (const auto id = content::scriptOverlayId(sp.info.id, f.rel)) {
                        ids.push_back(*id);
                    }
                }
            }
        }
        for (const auto& [id, value] : db.overlay().items(content::ContentKind::Script)) {
            (void)value;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (std::string& id : ids) {
            const bool overlaid = db.overlay().find(content::ContentKind::Script, id) != nullptr;
            out.push_back({std::move(id), overlaid, nullptr});
        }
        break;
    }
    }
    return out;
}

// 고르는 항목의 지금 값 — JSON 종류는 dump(2), 스크립트는 소스. 없으면 nullopt
[[nodiscard]] std::optional<std::string> currentContentText(const content::ContentDatabase& db, content::ContentKind kind,
                                                       std::string_view id);

const ecs::Json* contentRawOf(const content::ContentDatabase& db, content::ContentKind kind,
                                     std::string_view id) {
    switch (kind) {
    case content::ContentKind::Prefab: {
        const content::Prefab* p = db.findPrefab(id);
        return p != nullptr ? &p->raw : nullptr;
    }
    case content::ContentKind::Rule: {
        const content::Rule* r = db.findRule(id);
        return r != nullptr ? &r->raw : nullptr;
    }
    case content::ContentKind::Behavior: {
        const content::BehaviorGraph* g = db.findBehavior(id);
        return g != nullptr ? &g->raw : nullptr;
    }
    case content::ContentKind::Trigger: {
        const content::TriggerDef* t = db.findTrigger(id);
        return t != nullptr ? &t->raw : nullptr;
    }
    case content::ContentKind::Script:
        return nullptr; // 소스 텍스트 — currentTextOf
    }
    return nullptr;
}

std::optional<std::string> currentContentText(const content::ContentDatabase& db, content::ContentKind kind,
                                         std::string_view id) {
    if (kind == content::ContentKind::Script) {
        return scriptSourceOf(db, id);
    }
    const ecs::Json* raw = contentRawOf(db, kind, id);
    return raw != nullptr ? std::optional<std::string>(raw->dump(2)) : std::nullopt;
}

} // namespace sbx::editor
