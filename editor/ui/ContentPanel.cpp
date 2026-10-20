#include "editor/ui/ContentPanel.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "core/content/ContentDatabase.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/ecs/ComponentCatalog.hpp"

namespace sbx::editor {

namespace {

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoSavedSettings;

[[nodiscard]] f32 dpi() {
    return ImGui::GetStyle().FontScaleDpi;
}

struct Item {
    std::string_view id;
    bool overlay = false;
    const ecs::Json* raw = nullptr;
};

[[nodiscard]] std::vector<Item> itemsOf(const content::ContentDatabase& db, content::ContentKind kind) {
    std::vector<Item> out;
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
    }
    return out;
}

[[nodiscard]] const ecs::Json* rawOf(const content::ContentDatabase& db, content::ContentKind kind,
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
    }
    return nullptr;
}

[[nodiscard]] bool containsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::search(text, needle, [&](char a, char b) { return lower(a) == lower(b); }).begin() !=
           text.end();
}

// std::string 을 InputTextMultiline 에 (imgui_stdlib 와 같은 크기 바꾸기 콜백)
int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<usize>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

bool inputJson(const char* label, std::string& text, ImVec2 size, bool readOnly) {
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput;
    if (readOnly) {
        flags |= ImGuiInputTextFlags_ReadOnly;
    }
    return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, size, flags, resizeCallback, &text);
}

void summary(const content::ContentDatabase& db, content::ContentKind kind, std::string_view id) {
    switch (kind) {
    case content::ContentKind::Rule:
        if (const content::Rule* r = db.findRule(id)) {
            ImGui::TextDisabled("action %s · 우선순위 %d · 범위 %.1f · 조건 %zu · 효과 %zu%s", r->action.c_str(),
                                r->priority, static_cast<double>(r->range), r->conditions.size(), r->effects.size(),
                                r->exclusive ? " · 배타" : "");
        }
        break;
    case content::ContentKind::Behavior:
        if (const content::BehaviorGraph* g = db.findBehavior(id)) {
            if (ImGui::TreeNode("##graph", "상태 %zu · 전이 %zu", g->states.size(), g->transitions.size())) {
                for (usize i = 0; i < g->states.size(); ++i) {
                    const auto& st = g->states[i];
                    ImGui::BulletText("%s%s  (들어갈 때 %zu · 틱마다 %zu · 나갈 때 %zu)", st.id.c_str(),
                                      i == g->initial ? " ◀ 처음" : "", st.onEnter.size(), st.onTick.size(),
                                      st.onExit.size());
                }
                for (const content::Transition& t : g->transitions) {
                    const std::string from = t.from == content::kAnyState ? std::string("*") : g->states.at(t.from).id;
                    ImGui::BulletText("%s → %s  (우선순위 %d)", from.c_str(), g->states.at(t.to).id.c_str(),
                                      t.priority);
                }
                ImGui::TreePop();
            }
        }
        break;
    case content::ContentKind::Trigger:
        if (const content::TriggerDef* t = db.findTrigger(id)) {
            static constexpr std::array<const char*, 6> kEvents{"게임 시작", "매 틱", "타이머",
                                                                "영역 진입", "점수",  "죽음"};
            if (ImGui::TreeNode("##program", "%s · 조건 %zu · 효과 %zu · 틱마다 %u%s — 실행 표현 %zu 줄",
                                kEvents.at(static_cast<usize>(t->event.kind)), t->conditions.size(), t->effects.size(),
                                t->perTick, t->hasSubject ? " · 대상 있음" : "", t->program.size())) {
                for (const std::string& line : describeProgram(*t)) {
                    ImGui::TextDisabled("%s", line.c_str());
                }
                ImGui::TreePop();
            }
        }
        break;
    case content::ContentKind::Prefab:
        if (const content::Prefab* p = db.findPrefab(id)) {
            std::string names;
            for (const content::PrefabComponent& c : p->components) {
                names += (names.empty() ? "" : " · ") + c.name;
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 340.f * dpi());
            ImGui::TextDisabled("%s · 태그 %s · %s", p->name.empty() ? "(이름 없음)" : p->name.c_str(),
                                db.describeTags(p->tags).c_str(), names.c_str());
            ImGui::PopTextWrapPos();
        }
        break;
    }
}

} // namespace

bool checkContentEdit(const IEditorHost& host, content::ContentKind kind, const std::string& id,
                      const std::string& text, std::vector<ContentPanelState::Issue>& out) {
    out.clear();
    const content::ContentDatabase* db = host.content();
    const ecs::ComponentCatalog* catalog = host.catalog();
    if (db == nullptr || catalog == nullptr) {
        out.push_back({true, "접속 전입니다"});
        return false;
    }
    ecs::Json value = ecs::Json::parse(text, nullptr, false);
    if (value.is_discarded() || !value.is_object()) {
        out.push_back({true, "JSON 객체가 아닙니다 (괄호 · 따옴표 · 쉼표를 확인하십시오)"});
        return false;
    }
    content::ContentOverlay overlay = db->overlay();
    overlay.set(kind, id, std::move(value));
    const content::ContentLoadResult r = content::ContentLoader::applyOverlay(*db, overlay, *catalog);
    const std::string file = std::format("overlay/{}/{}.json", content::contentKindFolder(kind), id);
    for (const content::ContentIssue& i : r.issues) {
        const bool error = i.severity == content::ContentIssue::Severity::Error;
        if (error || i.file == file) { // 경고는 이 항목 것만 (팩의 다른 경고는 늘 있던 것)
            out.push_back({error, i.describe()});
        }
    }
    return r.ok();
}

ecs::Json contentTemplate(content::ContentKind kind, const std::string& id) {
    const std::string pack = id.substr(0, id.find('.'));
    switch (kind) {
    case content::ContentKind::Prefab:
        return ecs::Json{{"id", id},
                         {"name", ""},
                         {"tags", ecs::Json::array()},
                         {"components", ecs::Json{{"core.transform", ecs::Json::object()}}}};
    case content::ContentKind::Rule:
        return ecs::Json{
            {"id", id},
            {"action", "act"},
            {"range", 1.0},
            {"effects", ecs::Json::array({ecs::Json{{"op", "event"}, {"name", pack + ".event"}, {"who", "target"}}})}};
    case content::ContentKind::Trigger:
        return triggerTemplate(id);
    case content::ContentKind::Behavior:
        return ecs::Json{
            {"id", id},
            {"initial", "idle"},
            {"states", ecs::Json::array(
                           {ecs::Json{{"id", "idle"}, {"onTick", ecs::Json::array({ecs::Json{{"action", "idle"}}})}}})},
            {"transitions", ecs::Json::array()}};
    }
    return ecs::Json::object();
}

void drawContentPanel(Editor& editor, ContentPanelState& st, IEditorHost& host, bool& open) {
    if (!open) {
        st.wasOpen = false;
        return; // 큰 창 — 필요할 때 연다
    }
    if (!std::exchange(st.wasOpen, true)) {
        ImGui::SetNextWindowFocus(); // 다른 패널에 가리지 않게
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f},
                            ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({600.f * dpi(), 470.f * dpi()}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("콘텐츠", &open, kPanelFlags)) {
        ImGui::End();
        return;
    }
    const content::ContentDatabase* db = host.content();
    if (db == nullptr) {
        ImGui::TextDisabled("접속 전");
        ImGui::End();
        return;
    }
    const bool editable = host.permissions().content;

    // 종류
    const auto kindButton = [&](const char* label, content::ContentKind k) {
        if (ImGui::RadioButton(label, st.kind == k) && st.kind != k) {
            st.kind = k;
            st.selected.clear();
            st.isNew = false;
            st.issues.clear();
        }
    };
    kindButton("규칙 (Rule)", content::ContentKind::Rule);
    ImGui::SameLine();
    kindButton("행동 (Behavior)", content::ContentKind::Behavior);
    ImGui::SameLine();
    kindButton("Prefab", content::ContentKind::Prefab);
    ImGui::SameLine();
    kindButton("트리거", content::ContentKind::Trigger);
    ImGui::SameLine();
    ImGui::TextDisabled("· 오버레이 %zu", db->overlay().size());

    // 서버 값이 바뀌면 다시 불러온다: 보낸 것이 받아들여졌거나(pendingFrom 과 달라짐), 고치지 않은 채 남이 바꿨다.
    // 없어진 항목(오버레이로 더한 것을 뺐다)은 놓는다
    const ecs::Json* raw = st.selected.empty() ? nullptr : rawOf(*db, st.kind, st.selected);
    const std::string current = raw != nullptr ? raw->dump(2) : std::string();
    if (!st.selected.empty()) {
        if (st.pendingFrom) {
            if (current != *st.pendingFrom) {
                st.pendingFrom.reset();
                st.isNew = false;
                st.text = st.loaded = current;
                if (raw == nullptr) {
                    st.selected.clear();
                }
            }
        } else if (!st.isNew) {
            if (raw == nullptr) {
                st.selected.clear();
            } else if (st.text == st.loaded && current != st.loaded) {
                st.text = st.loaded = current;
                st.issues.clear();
            }
        }
    }

    // 왼쪽: 목록
    ImGui::BeginChild("##list", {200.f * dpi(), 0}, ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "거르기", st.filter.data(), st.filter.size());
    const std::string_view filter(st.filter.data());
    for (const Item& item : itemsOf(*db, st.kind)) {
        if (!containsIgnoreCase(item.id, filter)) {
            continue;
        }
        const std::string label = std::format("{}{}", item.overlay ? "● " : "   ", item.id);
        if (ImGui::Selectable(label.c_str(), st.selected == item.id && !st.isNew)) {
            st.selected = std::string(item.id);
            st.isNew = false;
            st.pendingFrom.reset();
            st.text = st.loaded = item.raw->dump(2);
            st.issues.clear();
            st.checked.clear();
        }
    }
    if (editable) {
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##newid", "새 id (팩.이름)", st.newId.data(), st.newId.size());
        if (ImGui::Button("새 항목") && st.newId[0] != '\0') {
            st.selected = st.newId.data();
            st.isNew = rawOf(*db, st.kind, st.selected) == nullptr;
            st.text =
                st.isNew ? contentTemplate(st.kind, st.selected).dump(2) : rawOf(*db, st.kind, st.selected)->dump(2);
            st.loaded = st.isNew ? std::string() : st.text;
            st.issues.clear();
            st.checked.clear();
            st.pendingFrom.reset();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // 오른쪽: 고르기 · JSON · 단추 · 검사 결과
    ImGui::BeginChild("##edit", {0, 0});
    if (st.selected.empty()) {
        ImGui::TextDisabled("왼쪽에서 항목을 고르십시오 (● = 이 월드가 바꾼 것)");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    const content::ContentOverlay& overlay = db->overlay();
    const bool inOverlay = overlay.find(st.kind, st.selected) != nullptr;
    ImGui::TextUnformatted(st.selected.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", st.isNew    ? "새 항목 (적용하면 오버레이에 더한다)"
                              : inOverlay ? "월드 오버레이"
                                          : "팩 원본 (적용하면 오버레이로)");
    if (!st.isNew) {
        summary(*db, st.kind, st.selected);
    }
    if (!editable) {
        ImGui::TextDisabled("보기만 — 지금 역할로는 콘텐츠를 바꿀 수 없습니다");
    }
    const f32 issuesHeight = 90.f * dpi();
    const ImVec2 editSize{-1, std::max(120.f * dpi(), ImGui::GetContentRegionAvail().y - issuesHeight)};
    if (st.kind == content::ContentKind::Trigger) {
        // 트리거: 폼 (원본 JSON 을 바로 고친다 — 검사 · 적용은 아래 그대로) · JSON
        if (ImGui::BeginTabBar("##trigger_tabs")) {
            if (ImGui::BeginTabItem("폼")) {
                ImGui::BeginChild("##form", {editSize.x, editSize.y - ImGui::GetFrameHeightWithSpacing()},
                                  ImGuiChildFlags_Borders);
                ecs::Json j = ecs::Json::parse(st.text, nullptr, false);
                if (j.is_discarded() || !j.is_object()) {
                    ImGui::TextColored({1.f, 0.5f, 0.4f, 1.f}, "JSON 이 깨져 있습니다 — JSON 탭에서 고치십시오");
                } else if (drawTriggerForm(j, st.trigger, *db, !editable)) {
                    st.text = j.dump(2);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("JSON")) {
                inputJson("##json", st.text, {editSize.x, editSize.y - ImGui::GetFrameHeightWithSpacing()}, !editable);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        inputJson("##json", st.text, editSize, !editable);
    }
    const bool dirty = st.isNew || st.text != st.loaded;
    if (ImGui::Button("검사")) {
        (void)checkContentEdit(host, st.kind, st.selected, st.text, st.issues);
        st.checked = st.text;
        if (st.issues.empty()) {
            st.issues.push_back({false, "문제 없음"});
        }
    }
    if (editable) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button("적용")) {
            st.checked = st.text;
            if (checkContentEdit(host, st.kind, st.selected, st.text, st.issues)) {
                if (editor.editContent(host, st.kind, st.selected, ecs::Json::parse(st.text, nullptr, false))) {
                    st.issues.push_back({false, "보냈습니다 — 서버가 받아들이면 모두에게 적용됩니다"});
                    st.pendingFrom = current;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("고친 것 버리기")) {
            st.text = st.loaded;
            st.issues.clear();
            if (st.isNew) {
                st.selected.clear();
                st.isNew = false;
            }
        }
        ImGui::EndDisabled();
        if (inOverlay) {
            ImGui::SameLine();
            if (ImGui::Button("팩 값으로")) {
                if (editor.editContent(host, st.kind, st.selected, nullptr)) {
                    st.pendingFrom = current;
                }
                st.issues.clear();
            }
        }
    }
    ImGui::BeginChild("##issues", {0, 0}, ImGuiChildFlags_Borders);
    for (const ContentPanelState::Issue& i : st.issues) {
        ImGui::PushTextWrapPos(0);
        if (i.error) {
            ImGui::TextColored({1.f, 0.5f, 0.4f, 1.f}, "%s", i.text.c_str());
        } else {
            ImGui::TextColored({0.6f, 0.9f, 0.6f, 1.f}, "%s", i.text.c_str());
        }
        ImGui::PopTextWrapPos();
    }
    if (!editor.lastMessage().empty() && editor.lastMessage().find("콘텐츠") != std::string::npos) {
        ImGui::TextColored({1.f, 0.6f, 0.4f, 1.f}, "%s", editor.lastMessage().c_str());
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();
}

} // namespace sbx::editor
