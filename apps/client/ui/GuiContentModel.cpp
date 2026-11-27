#include "apps/client/ui/GuiContentModel.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "core/content/ContentDatabase.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "editor/ContentItems.hpp"
#include "editor/PackGroups.hpp"
#include "editor/ui/ContentPanel.hpp"
#include "editor/ui/TriggerForm.hpp"

namespace sbx::client {

std::vector<std::string> contentSummaryLines(const content::ContentDatabase& db, content::ContentKind kind,
                                             std::string_view id) {
    std::vector<std::string> out;
    switch (kind) {
    case content::ContentKind::Rule:
        if (const content::Rule* r = db.findRule(id)) {
            out.push_back(std::format("action {} · 우선순위 {} · 범위 {:.1f} · 조건 {} · 효과 {}{}", r->action, r->priority,
                                      static_cast<double>(r->range), r->conditions.size(), r->effects.size(),
                                      r->exclusive ? " · 배타" : ""));
        }
        break;
    case content::ContentKind::Behavior:
        if (const content::BehaviorGraph* g = db.findBehavior(id)) {
            out.push_back(std::format("상태 {} · 전이 {}", g->states.size(), g->transitions.size()));
            for (usize i = 0; i < g->states.size(); ++i) {
                const auto& st = g->states[i];
                out.push_back(std::format("  • {}{}  (들어갈 때 {} · 틱마다 {} · 나갈 때 {})", st.id, i == g->initial ? " ◀ 처음" : "",
                                          st.onEnter.size(), st.onTick.size(), st.onExit.size()));
            }
            for (const content::Transition& t : g->transitions) {
                const std::string from = t.from == content::kAnyState ? std::string("*") : g->states.at(t.from).id;
                out.push_back(std::format("  • {} → {}  (우선순위 {})", from, g->states.at(t.to).id, t.priority));
            }
        }
        break;
    case content::ContentKind::Trigger:
        if (const content::TriggerDef* t = db.findTrigger(id)) {
            static constexpr std::array<const char*, 6> kEvents{"게임 시작", "매 틱", "타이머", "영역 진입", "점수", "죽음"};
            out.push_back(std::format("{} · 조건 {} · 효과 {} · 틱마다 {}{} — 실행 표현 {} 줄",
                                      kEvents.at(static_cast<usize>(t->event.kind)), t->conditions.size(), t->effects.size(),
                                      t->perTick, t->hasSubject ? " · 대상 있음" : "", t->program.size()));
            for (const std::string& line : editor::describeProgram(*t)) {
                out.push_back("  " + line);
            }
        }
        break;
    case content::ContentKind::Script: {
        // 이 파일이 정의한 시스템 (콘텐츠의 바이트코드 — 마지막으로 받아들여진 소스)
        const auto path = content::scriptOverlayPath(id);
        const auto& programs = db.scriptPrograms();
        if (const auto f = editor::scriptFileOf(db, id)) {
            out.push_back(f->display);
        }
        if (!path || !programs) {
            break;
        }
        for (const content::ScriptPackProgram& pp : programs->packs) {
            if (pp.pack != path->pack) {
                continue;
            }
            for (const script::bc::SystemDesc& s : pp.module.systems) {
                if (s.module != path->rel) {
                    continue;
                }
                std::string line = std::format("시스템 {}", s.id);
                if (s.onTick != script::api::kNoCallback) {
                    line += s.intervalTicks == 1 ? " · onTick" : std::format(" · onTick ({} 틱마다)", s.intervalTicks);
                }
                line += s.onStart != script::api::kNoCallback ? " · onStart" : "";
                line += s.onEvent != script::api::kNoCallback ? " · onEvent" : "";
                line += s.onStop != script::api::kNoCallback ? " · onStop" : "";
                out.push_back(std::move(line));
            }
        }
        break;
    }
    case content::ContentKind::Prefab:
        if (const content::Prefab* p = db.findPrefab(id)) {
            std::string names;
            for (const content::PrefabComponent& c : p->components) {
                names += (names.empty() ? "" : " · ") + c.name;
            }
            out.push_back(std::format("{} · 태그 {}", p->name.empty() ? "(이름 없음)" : p->name, db.describeTags(p->tags)));
            out.push_back(names);
        }
        break;
    }
    return out;
}

editor::gui::ContentView makeGuiContentView(const editor::gui::ContentPanel& panel, const editor::Editor& editor,
                                            editor::IEditorHost& host) {
    editor::gui::ContentView v;
    const content::ContentDatabase* db = host.content();
    if (db == nullptr) {
        return v; // 접속 전
    }
    v.available = true;
    v.editable = host.permissions().content;
    v.kind = panel.kind();
    v.overlayCount = db->overlay().size();
    // 팩별 묶음은 전부로 한 번 — 패널이 묶음 안에서 거른다 (ImGui 창은 거른 뒤 묶는다 — 묶음 차례는 팩 · 출처로 정해져 같다)
    const std::vector<editor::ContentItem> items = editor::contentItems(*db, v.kind);
    std::vector<std::string_view> ids;
    for (const editor::ContentItem& it : items) {
        ids.push_back(it.id);
    }
    for (const editor::PackGroup& g : editor::groupByPack(*db, ids)) {
        editor::gui::ContentListGroup group;
        group.label = g.label;
        for (const usize i : g.items) {
            group.items.push_back({items[i].id, items[i].overlay});
        }
        v.groups.push_back(std::move(group));
    }
    v.selected = panel.selected();
    if (!v.selected.empty()) {
        v.current = editor::currentContentText(*db, v.kind, v.selected);
        v.inOverlay = db->overlay().find(v.kind, v.selected) != nullptr;
        v.summary = contentSummaryLines(*db, v.kind, v.selected);
    }
    v.catalog = host.catalog();
    if (v.kind == content::ContentKind::Prefab) {
        for (const content::Prefab& p : db->prefabs()) {
            v.prefabs.push_back(p.id);
        }
        if (v.catalog != nullptr) {
            for (const ecs::ComponentInfo& info : v.catalog->all()) {
                if (ecs::hasFlag(info.flags, ecs::ComponentFlags::EditorVisible) &&
                    !content::isEngineManagedComponent(info.name)) {
                    v.addable.emplace_back(info.name);
                }
            }
            std::ranges::sort(v.addable);
        }
    }
    // ImGui 창과 같이 — Editor 의 마지막 사유 중 콘텐츠 거절만
    if (editor.lastMessage().find("콘텐츠") != std::string::npos) {
        v.message = editor.lastMessage();
    }
    return v;
}

void runGuiContentIntent(const editor::gui::WorkspaceIntent& intent, editor::gui::ContentPanel& panel, editor::Editor& editor,
                         editor::IEditorHost& host) {
    using K = editor::gui::WorkspaceIntent::Kind;
    const auto kind = static_cast<content::ContentKind>(intent.value);
    const content::ContentDatabase* db = host.content();
    std::vector<editor::ContentPanelState::Issue> found;
    const auto result = [&](bool sent) {
        std::vector<editor::gui::ContentIssueLine> lines;
        for (editor::ContentPanelState::Issue& i : found) {
            lines.push_back({i.error, std::move(i.text)});
        }
        panel.setResult(std::move(lines), sent);
    };
    switch (intent.kind) {
    case K::ContentCheck:
        (void)editor::checkContentEdit(host, kind, intent.command, intent.text, found);
        if (found.empty()) {
            found.push_back({false, "문제 없음"});
        }
        result(false);
        return;
    case K::ContentApply: {
        if (!host.permissions().content) {
            return;
        }
        bool sent = false;
        if (editor::checkContentEdit(host, kind, intent.command, intent.text, found) &&
            editor.editContent(host, kind, intent.command, editor::contentEditValue(kind, intent.command, intent.text))) {
            found.push_back({false, "보냈습니다 — 서버가 받아들이면 모두에게 적용됩니다"});
            sent = true;
        }
        result(sent);
        return;
    }
    case K::ContentRevert: // 팩 값으로 = 오버레이에서 뺀다
        if (host.permissions().content) {
            result(editor.editContent(host, kind, intent.command, nullptr));
        }
        return;
    case K::ContentNew: {
        if (db == nullptr || !host.permissions().content || intent.command.empty() || kind == content::ContentKind::Script) {
            return;
        }
        const std::optional<std::string> existing = editor::currentContentText(*db, kind, intent.command);
        panel.openItem(intent.command, existing ? *existing : editor::contentTemplate(kind, intent.command).dump(2),
                       !existing.has_value());
        return;
    }
    default:
        return;
    }
}

} // namespace sbx::client
