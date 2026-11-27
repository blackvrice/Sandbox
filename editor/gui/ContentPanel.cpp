#include "editor/gui/ContentPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

#include "editor/gui/InspectorPanel.hpp"
#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kRowH = 20.0;
constexpr double kLine = 26.0;
constexpr double kIssuesH = 60.0;

// ImGui "콘텐츠" 창의 거르기와 같은 규칙 (ASCII 대소문자 무시 · 부분 글자)
[[nodiscard]] bool containsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::search(text, needle, [&](char a, char b) { return lower(a) == lower(b); }).begin() != text.end();
}

[[nodiscard]] std::string_view kindName(content::ContentKind k) {
    switch (k) {
    case content::ContentKind::Rule:
        return "규칙";
    case content::ContentKind::Behavior:
        return "행동";
    case content::ContentKind::Prefab:
        return "Prefab";
    case content::ContentKind::Trigger:
        return "트리거";
    case content::ContentKind::Script:
        return "스크립트";
    }
    return "?";
}

void setKeys(gui::ListView& list, std::vector<gui::ListItemKey>& keys, usize count) {
    std::vector<gui::ListItemKey> next(count);
    for (usize i = 0; i < count; ++i) {
        next[i] = i; // 줄 차례 — 줄 수가 같으면 그대로 (스크롤 유지)
    }
    if (next != keys) {
        keys = std::move(next);
        (void)list.setItems(keys);
    }
}

[[nodiscard]] std::optional<gui::LayoutRect> boundsOf(const gui::ListView& list, const std::vector<gui::ListItemKey>& keys,
                                                      usize index) {
    if (index >= keys.size()) {
        return std::nullopt;
    }
    const gui::ListRange range = list.visibleRange();
    for (usize i = range.first; i < range.past; ++i) {
        if (const auto r = list.visibleRow(i); r && r->key == keys[index]) {
            return r->bounds;
        }
    }
    return std::nullopt;
}

} // namespace

// ---- 보기 ----

void ContentPanel::setView(ContentView view) {
    m_view = std::move(view);
}

void ContentPanel::setResult(std::vector<ContentIssueLine> issues, bool sent) {
    m_issues = std::move(issues);
    if (sent) {
        m_pendingFrom = m_sendFrom.value_or(std::string());
    }
    m_sendFrom.reset();
}

void ContentPanel::openItem(std::string id, std::string text, bool isNew) {
    m_selected = std::move(id);
    m_isNew = isNew;
    m_text = std::move(text);
    m_loaded = isNew ? std::string() : m_text;
    m_needLoad = false;
    m_pendingFrom.reset();
    m_issues.clear();
    m_formError.clear();
    m_selRow.reset();
    m_newId.setValue({});
}

bool ContentPanel::formMode() const {
    return m_kind == content::ContentKind::Prefab && m_view.available && m_view.catalog != nullptr && !m_selected.empty() &&
           !m_needLoad;
}

std::string ContentPanel::title() const {
    if (!m_view.available) {
        return "콘텐츠 — 프로젝트 안에서 쓴다";
    }
    if (m_selected.empty()) {
        return std::format("{} — 왼쪽에서 항목을 고르십시오 (● = 이 월드가 바꾼 것){}", kindName(m_kind),
                           m_view.editable ? "" : " · 보기만");
    }
    const bool inOverlay = m_view.kind == m_kind && m_view.selected == m_selected && m_view.inOverlay;
    return std::format("{}  — {}{}{}", m_selected,
                       m_isNew      ? "새 항목 (적용하면 오버레이에 더한다)"
                       : inOverlay ? "월드 오버레이"
                                   : "팩 원본 (적용하면 오버레이로)",
                       dirty() ? " · 고침" : "", m_view.editable ? "" : " · 보기만");
}

const FormSection* ContentPanel::selectedSection() const {
    if (!m_selRow || !formMode()) {
        return nullptr;
    }
    const Row& r = m_rows[*m_selRow];
    if (r.kind != Row::Kind::Header && r.kind != Row::Kind::Field) {
        return nullptr;
    }
    return r.section < m_sections.size() ? &m_sections[r.section] : nullptr;
}

const FormField* ContentPanel::selectedField() const {
    const FormSection* s = selectedSection();
    if (s == nullptr || m_rows[*m_selRow].kind != Row::Kind::Field) {
        return nullptr;
    }
    const usize f = m_rows[*m_selRow].field;
    return f < s->fields.size() ? &s->fields[f] : nullptr;
}

std::string ContentPanel::message() const {
    if (!m_formError.empty()) {
        return m_formError;
    }
    if (!formMode()) {
        if (!m_selected.empty() && m_view.available) {
            return "읽기만 — 여러 줄 고치기는 기존 UI 의 콘텐츠 창 (GraphicsUI 글 칸은 한 줄)";
        }
        return {};
    }
    if (const FormField* f = selectedField()) {
        return std::format("{}.{} — {}{}", selectedSection()->component, f->name, InspectorPanel::kindText(*f),
                           f->set ? "" : " · 지금은 기본값");
    }
    if (const FormSection* s = selectedSection()) {
        return std::format("{} — 컴포넌트 빼기, 또는 이름 칸으로 컴포넌트 더하기", s->component);
    }
    const std::vector<std::string> m = addMatches();
    if (!m_addName.value().empty()) {
        return m.empty() ? std::string("더할 수 있는 컴포넌트가 없다")
                         : std::format("더하기 후보 {} 개 — {}", m.size(), m.front());
    }
    return "줄을 고르면 아래 칸에서 값을 고친다 (Prefab 폼 — 검사 · 적용은 아래 단추)";
}

std::vector<std::string> ContentPanel::addMatches() const {
    std::vector<std::string> out;
    const std::string& q = m_addName.value();
    if (q.empty() || !formMode()) {
        return out;
    }
    for (const std::string& name : m_view.addable) {
        const bool present = std::ranges::any_of(m_sections, [&](const FormSection& s) { return s.component == name; });
        if (present) {
            continue;
        }
        if (name == q) {
            return {name}; // 정확한 이름이면 그것 하나
        }
        if (containsIgnoreCase(name, q)) {
            out.push_back(name);
        }
    }
    return out;
}

void ContentPanel::syncFromView() {
    // ImGui 창과 같은 다시 불러오기 — 보기가 지금 고른 항목의 것일 때만
    if (!m_view.available || m_selected.empty() || m_view.kind != m_kind || m_view.selected != m_selected) {
        return;
    }
    const std::string current = m_view.current.value_or(std::string());
    if (m_needLoad) {
        m_needLoad = false;
        if (!m_view.current) {
            m_selected.clear();
            return;
        }
        m_text = m_loaded = current;
        return;
    }
    if (m_pendingFrom) {
        if (current != *m_pendingFrom) { // 보낸 것이 받아들여졌다 (또는 팩 값으로 돌아갔다)
            m_pendingFrom.reset();
            m_isNew = false;
            m_text = m_loaded = current;
            if (!m_view.current) {
                m_selected.clear();
            }
        }
    } else if (!m_isNew) {
        if (!m_view.current) {
            m_selected.clear(); // 오버레이로 더한 것을 누가 뺐다
        } else if (m_text == m_loaded && current != m_loaded) {
            m_text = m_loaded = current; // 고치지 않은 채 남이 바꿨다
            m_issues.clear();
        }
    }
}

void ContentPanel::rebuildItems() {
    m_items.clear();
    if (m_view.available && m_view.kind == m_kind) {
        const std::string& filter = m_filter.value();
        std::vector<std::pair<const ContentListGroup*, std::vector<const ContentListItem*>>> shown;
        for (const ContentListGroup& g : m_view.groups) {
            std::vector<const ContentListItem*> its;
            for (const ContentListItem& it : g.items) {
                if (containsIgnoreCase(it.id, filter)) {
                    its.push_back(&it);
                }
            }
            if (!its.empty()) {
                shown.emplace_back(&g, std::move(its));
            }
        }
        for (const auto& [g, its] : shown) {
            if (shown.size() > 1) { // 팩별 묶음 — 하나면 머리 없이 (ImGui 창과 같다)
                m_items.push_back({true, g->label, false});
            }
            for (const ContentListItem* it : its) {
                m_items.push_back({false, it->id, it->overlay});
            }
        }
    }
    setKeys(m_itemList, m_itemKeys, m_items.size());
}

void ContentPanel::rebuildRows() {
    m_rows.clear();
    const bool viewMatches = m_view.available && m_view.kind == m_kind && m_view.selected == m_selected;
    if (m_view.available && !m_selected.empty() && !m_needLoad) {
        if (viewMatches && !m_isNew) {
            for (const std::string& s : m_view.summary) {
                Row r;
                r.kind = Row::Kind::Summary;
                r.name = s;
                m_rows.push_back(std::move(r));
            }
        }
        if (formMode()) {
            if (m_formText != m_text || m_sections.empty()) {
                m_formText = m_text;
                m_sections.clear();
                m_formWarnings.clear();
                const ecs::Json j = ecs::Json::parse(m_text, nullptr, false);
                if (j.is_discarded() || !j.is_object()) {
                    m_formWarnings.emplace_back("JSON 이 깨져 있다 — 기존 UI 의 콘텐츠 창 (JSON 탭) 에서 고치십시오");
                } else {
                    m_sections = prefabForm(j, *m_view.catalog, &m_formWarnings);
                }
            }
            for (const std::string& w : m_formWarnings) {
                Row r;
                r.kind = Row::Kind::Summary;
                r.name = w;
                m_rows.push_back(std::move(r));
            }
            for (usize i = 0; i < m_sections.size(); ++i) {
                const FormSection& s = m_sections[i];
                const bool collapsed = std::ranges::find(m_collapsed, s.component) != m_collapsed.end();
                const bool known = s.stableId != 0 && m_view.catalog->find(s.stableId) != nullptr;
                Row head;
                head.kind = Row::Kind::Header;
                head.section = i;
                head.component = s.component;
                head.name = std::format("{} {}{}", collapsed ? "▸" : "▾", s.component, known ? "" : "  (JSON 으로만)");
                m_rows.push_back(std::move(head));
                if (collapsed) {
                    continue;
                }
                for (usize j = 0; j < s.fields.size() && known; ++j) {
                    const FormField& f = s.fields[j];
                    Row r;
                    r.kind = Row::Kind::Field;
                    r.section = i;
                    r.field = j;
                    r.component = s.component;
                    r.name = f.name;
                    r.value = InspectorPanel::fieldText(f);
                    r.set = f.set;
                    r.editable = m_view.editable && f.editable();
                    m_rows.push_back(std::move(r));
                }
                if (!known || (s.fields.empty() && !s.raw.is_null())) {
                    Row r;
                    r.kind = Row::Kind::Text;
                    r.name = "(값)";
                    r.value = s.raw.dump();
                    if (r.value.size() > 80) {
                        r.value.resize(77);
                        r.value += "...";
                    }
                    m_rows.push_back(std::move(r));
                }
            }
        } else {
            // 읽기만 — 내용 줄 (JSON dump(2) · 스크립트 소스)
            std::string_view t = m_text;
            while (!t.empty()) {
                const usize nl = t.find('\n');
                Row r;
                r.kind = Row::Kind::Text;
                r.name = std::string(t.substr(0, nl));
                if (!r.name.empty() && r.name.back() == '\r') {
                    r.name.pop_back();
                }
                m_rows.push_back(std::move(r));
                t = nl == std::string_view::npos ? std::string_view() : t.substr(nl + 1);
            }
        }
    }
    setKeys(m_rowList, m_rowKeys, m_rows.size());
    // 고른 줄을 이름으로 다시 찾는다 (폼의 머리 · 필드), 그 밖의 줄은 차례로
    const std::optional<usize> before = m_selRow;
    m_selRow.reset();
    for (usize i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        if ((m_selKind == Row::Kind::Header || m_selKind == Row::Kind::Field) && r.kind == m_selKind &&
            r.component == m_selComponent && (r.kind != Row::Kind::Field || r.name == m_selField)) {
            m_selRow = i;
            break;
        }
    }
    if (!m_selRow && before && *before < m_rows.size() && m_rows[*before].kind == m_selKind &&
        m_selKind != Row::Kind::Header && m_selKind != Row::Kind::Field) {
        m_selRow = before;
    }
    if (before != m_selRow) {
        loadValue();
    } else if (const auto focus = m_host->input().focus(); !(focus && *focus == m_value.handle())) {
        const FormField* f = selectedField(); // 값 칸을 쓰는 중이 아니면 지금 값을 따라간다
        const std::string now = f != nullptr ? InspectorPanel::fieldText(*f) : std::string();
        if (now != m_value.value()) {
            m_value.setValue(now);
        }
    }
}

void ContentPanel::applyEnabled() {
    if (!m_check) {
        return;
    }
    const bool ok = m_view.available;
    for (usize i = 0; i < kKinds; ++i) {
        (void)m_kindButtons[i]->setEnabled(ok);
    }
    m_filter.setEnabled(ok);
    const bool canNew = ok && m_view.editable && m_kind != content::ContentKind::Script; // 새 스크립트는 파일 — 기존 UI 로
    m_newId.setEnabled(canNew);
    (void)m_new->setEnabled(canNew && !m_newId.value().empty());
    const bool form = formMode() && m_view.editable;
    const FormField* f = selectedField();
    const bool fieldOk = form && f != nullptr && f->editable();
    m_value.setEnabled(fieldOk);
    (void)m_set->setEnabled(fieldOk);
    (void)m_reset->setEnabled(fieldOk && f->set);
    (void)m_remove->setEnabled(form && selectedSection() != nullptr);
    m_addName.setEnabled(form);
    (void)m_add->setEnabled(form && addMatches().size() == 1);
    const bool has = ok && !m_selected.empty() && !m_needLoad;
    (void)m_check->setEnabled(has);
    (void)m_apply->setEnabled(has && m_view.editable && dirty());
    (void)m_discard->setEnabled(has && m_view.editable && dirty());
    const bool matches = m_view.kind == m_kind && m_view.selected == m_selected;
    (void)m_revert->setEnabled(has && m_view.editable && !m_isNew && matches && m_view.inOverlay);
}

void ContentPanel::selectRow(std::optional<usize> row) {
    m_selRow = row;
    if (row) {
        const Row& r = m_rows[*row];
        m_selKind = r.kind;
        m_selComponent = r.component;
        m_selField = r.kind == Row::Kind::Field ? r.name : std::string();
        (void)m_rowList.reveal(m_rowKeys[*row]);
    }
    m_formError.clear();
    loadValue();
    applyEnabled();
}

void ContentPanel::loadValue() {
    const FormField* f = selectedField();
    m_value.setValue(f != nullptr ? InspectorPanel::fieldText(*f) : std::string());
}

// ---- 일 ----

void ContentPanel::selectKind(content::ContentKind k) {
    if (k == m_kind) {
        return;
    }
    m_kind = k;
    m_selected.clear();
    m_isNew = false;
    m_needLoad = false;
    m_pendingFrom.reset();
    m_text.clear();
    m_loaded.clear();
    m_issues.clear();
    m_formError.clear();
    m_selRow.reset();
}

void ContentPanel::selectItem(usize item) {
    if (item >= m_items.size() || m_items[item].header) {
        return;
    }
    (void)m_itemList.reveal(m_itemKeys[item]);
    if (m_items[item].text == m_selected && !m_isNew) {
        return; // 같은 항목 — 고친 것을 버리지 않는다
    }
    m_selected = m_items[item].text;
    m_isNew = false;
    m_needLoad = true; // 지금 값은 다음 보기에서 (앱이 이 id 로 만든다)
    m_pendingFrom.reset();
    m_text.clear();
    m_loaded.clear();
    m_issues.clear();
    m_formError.clear();
    m_selRow.reset();
}

bool ContentPanel::editPrefab(const std::function<Expected<void>(ecs::Json& prefab)>& f) {
    ecs::Json j = ecs::Json::parse(m_text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        m_formError = "JSON 이 깨져 있다 — 기존 UI 의 콘텐츠 창 (JSON 탭) 에서 고치십시오";
        return false;
    }
    if (auto r = f(j); !r) {
        m_formError = r.error().message;
        return false;
    }
    m_text = j.dump(2); // ImGui 폼과 같은 모양 (검사 · 적용은 아래 단추)
    m_formError.clear();
    return true;
}

void ContentPanel::setField() {
    const FormField* f = selectedField();
    const FormSection* s = selectedSection();
    if (f == nullptr || s == nullptr || !m_view.editable || !f->editable()) {
        return;
    }
    if (m_value.value() == InspectorPanel::fieldText(*f)) {
        m_formError.clear(); // 바꾼 것이 없다 (보이는 자릿수로 원래 값을 덮지 않는다)
        return;
    }
    auto v = InspectorPanel::parseFieldText(*f, m_value.value());
    if (!v) {
        m_formError = std::format("{} — {}", f->name, v.error().message);
        return;
    }
    if (f->kind == FormFieldKind::PrefabRef && !m_view.prefabs.empty() &&
        std::ranges::find(m_view.prefabs, v->get<std::string>()) == m_view.prefabs.end()) {
        m_formError = std::format("없는 Prefab '{}'", v->get<std::string>());
        return;
    }
    const ecs::ComponentInfo* info = m_view.catalog->find(s->stableId);
    if (info == nullptr) {
        m_formError = "카탈로그에 없는 컴포넌트";
        return;
    }
    const std::string component = s->component;
    const std::string name = f->name;
    (void)editPrefab([&](ecs::Json& prefab) -> Expected<void> {
        ecs::Json& partial = prefab["components"][component];
        if (partial.is_null()) {
            partial = ecs::Json::object();
        }
        if (auto r = setFormField(*info, partial, name, *v); !r) {
            return makeError(r.error().code, std::format("{} — {}", name, r.error().message));
        }
        return {};
    });
    loadValue();
}

void ContentPanel::resetField() {
    const FormField* f = selectedField();
    const FormSection* s = selectedSection();
    if (f == nullptr || s == nullptr || !m_view.editable || !f->set) {
        return;
    }
    const std::string component = s->component;
    const std::string name = f->name;
    (void)editPrefab([&](ecs::Json& prefab) -> Expected<void> {
        if (!resetFormField(prefab["components"][component], name)) {
            return makeError(ErrorCode::NotFound, "이미 기본값");
        }
        return {};
    });
    loadValue();
}

void ContentPanel::toggleBool() {
    const FormField* f = selectedField();
    if (f == nullptr || f->kind != FormFieldKind::Bool || !m_view.editable) {
        return;
    }
    m_value.setValue(f->value.is_boolean() && f->value.get<bool>() ? "false" : "true");
    setField();
}

void ContentPanel::removeComponent() {
    const FormSection* s = selectedSection();
    if (s == nullptr || !m_view.editable) {
        return;
    }
    const std::string component = s->component;
    if (editPrefab([&](ecs::Json& prefab) -> Expected<void> {
            if (!removePrefabComponent(prefab, component)) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 은 뺄 수 없다", component));
            }
            return {};
        })) {
        selectRow(std::nullopt);
    }
}

void ContentPanel::addComponent() {
    const std::vector<std::string> m = addMatches();
    if (!formMode() || !m_view.editable || m.size() != 1) {
        return;
    }
    const std::string name = m.front();
    if (editPrefab([&](ecs::Json& prefab) -> Expected<void> {
            if (!addPrefabComponent(prefab, name, *m_view.catalog)) {
                return makeError(ErrorCode::InvalidArgument, std::format("{} 을 더할 수 없다", name));
            }
            return {};
        })) {
        m_addName.setValue({});
    }
}

void ContentPanel::emitContent(WorkspaceIntent::Kind kind) {
    WorkspaceIntent i = WorkspaceIntent::of(kind, kind == WorkspaceIntent::Kind::ContentNew ? m_newId.value() : m_selected,
                                            static_cast<int>(m_kind));
    i.text = m_text;
    const bool matches = m_view.kind == m_kind && m_view.selected == m_selected;
    m_sendFrom = matches ? m_view.current.value_or(std::string()) : std::string();
    m_ctx.intent = std::move(i);
}

gui::EventReply ContentPanel::itemEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_itemsNode);
            if (const auto row = m_itemList.hitTest({p->x - m_itemBounds.x, p->y - m_itemBounds.y})) {
                selectItem(static_cast<usize>(row->key));
            }
            return {true, true};
        }
        return {};
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if ((up || down) && !m_items.empty()) {
        std::optional<usize> at;
        for (usize i = 0; i < m_items.size(); ++i) {
            if (!m_items[i].header && m_items[i].text == m_selected) {
                at = i;
            }
        }
        if (down) {
            for (usize i = at ? *at + 1 : 0; i < m_items.size(); ++i) {
                if (!m_items[i].header) {
                    selectItem(i);
                    break;
                }
            }
        } else {
            for (usize i = at ? *at : m_items.size(); i-- > 0;) {
                if (!m_items[i].header) {
                    selectItem(i);
                    break;
                }
            }
        }
        return {true, true};
    }
    return {};
}

gui::EventReply ContentPanel::rowEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    const auto toggleHeader = [this](usize row) {
        const Row& r = m_rows[row];
        const auto at = std::ranges::find(m_collapsed, r.component);
        if (at != m_collapsed.end()) {
            m_collapsed.erase(at);
        } else {
            m_collapsed.push_back(r.component);
        }
    };
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_rowsNode);
            if (const auto row = m_rowList.hitTest({p->x - m_rowBounds.x, p->y - m_rowBounds.y})) {
                const usize at = static_cast<usize>(row->key);
                selectRow(at);
                if (m_rows[at].kind == Row::Kind::Header) { // 머리 = 접기 · 펴기
                    toggleHeader(at);
                }
            }
            return {true, true};
        }
        return {};
    }
    if (m_rows.empty()) {
        return {};
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if (up || down) {
        usize at = m_selRow.value_or(up ? m_rows.size() : static_cast<usize>(-1));
        at = up ? (at == 0 ? 0 : at - 1) : std::min(at + 1, m_rows.size() - 1);
        selectRow(at);
        return {true, true};
    }
    if (m_selRow && (kit::keyDown(ev, gui::enter_key) || kit::keyDown(ev, gui::space_key))) {
        const bool space = kit::keyDown(ev, gui::space_key);
        const Row& r = m_rows[*m_selRow];
        if (r.kind == Row::Kind::Header) {
            toggleHeader(*m_selRow);
        } else if (const FormField* f = selectedField(); f != nullptr && f->kind == FormFieldKind::Bool) {
            toggleBool();
        } else if (!space && r.kind == Row::Kind::Field && r.editable) {
            m_value.focusAll();
        }
        return {true, true};
    }
    return {};
}

std::optional<gui::LayoutRect> ContentPanel::itemBounds(usize item) const {
    return boundsOf(m_itemList, m_itemKeys, item);
}

std::optional<gui::LayoutRect> ContentPanel::rowBounds(usize row) const {
    return boundsOf(m_rowList, m_rowKeys, row);
}

// ---- 화면 ----

Expected<void> ContentPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // (종류: 1 뿌리 · 2 글 · 3 줄 · 4 단추 · 5 목록 · 6 글 칸 · 7 세로 칸) — 부모 우선
    std::vector<gui::ComponentSpec> specs;
    const auto add = [&specs](std::optional<usize> parent, gui::ComponentType kind, gui::ComponentKey order) {
        specs.push_back({parent, kind, order, true});
        return specs.size() - 1;
    };
    const usize root = add(std::nullopt, 1, 0);
    const usize bar = add(root, 3, 1);
    std::array<usize, kKinds> kinds{};
    for (usize i = 0; i < kKinds; ++i) {
        kinds[i] = add(bar, 4, static_cast<u32>(i + 1));
    }
    const usize overlay = add(bar, 2, 6);
    const usize body = add(root, 3, 2);
    const usize left = add(body, 7, 1);
    const usize filter = add(left, 6, 1);
    const usize items = add(left, 5, 2);
    const usize newRow = add(left, 3, 3);
    const usize newId = add(newRow, 6, 1);
    const usize newBtn = add(newRow, 4, 2);
    const usize right = add(body, 7, 2);
    const usize title = add(right, 2, 1);
    const usize rows = add(right, 5, 2);
    const usize label = add(right, 2, 3);
    const usize editRow = add(right, 3, 4);
    const usize value = add(editRow, 6, 1);
    const usize setBtn = add(editRow, 4, 2);
    const usize resetBtn = add(editRow, 4, 3);
    const usize compRow = add(right, 3, 5);
    const usize removeBtn = add(compRow, 4, 1);
    const usize addName = add(compRow, 6, 2);
    const usize addBtn = add(compRow, 4, 3);
    const usize actionRow = add(right, 3, 6);
    const usize check = add(actionRow, 4, 1);
    const usize apply = add(actionRow, 4, 2);
    const usize discard = add(actionRow, 4, 3);
    const usize revert = add(actionRow, 4, 4);
    const usize issues = add(right, 5, 7);
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    const auto& H = *h;
    m_root = H[root];
    m_kindBar = H[bar];
    for (usize i = 0; i < kKinds; ++i) {
        m_kindNodes[i] = H[kinds[i]];
        const content::ContentKind k = kKindOrder[i];
        m_kindButtons[i] = kit::bindButton(host, m_kindNodes[i], [this, k] {
            if (m_view.available) {
                selectKind(k);
            }
        });
    }
    m_overlayNode = H[overlay];
    m_body = H[body];
    m_left = H[left];
    m_itemsNode = H[items];
    m_newRow = H[newRow];
    m_newNode = H[newBtn];
    m_right = H[right];
    m_titleNode = H[title];
    m_rowsNode = H[rows];
    m_label = H[label];
    m_editRow = H[editRow];
    m_setNode = H[setBtn];
    m_resetNode = H[resetBtn];
    m_compRow = H[compRow];
    m_removeNode = H[removeBtn];
    m_addNode = H[addBtn];
    m_actionRow = H[actionRow];
    m_checkNode = H[check];
    m_applyNode = H[apply];
    m_discardNode = H[discard];
    m_revertNode = H[revert];
    m_issuesNode = H[issues];
    const auto enterKey = [](const gui::KeyEvent& k) {
        return k.action == gui::KeyAction::down && k.chord.key == gui::enter_key && k.chord.modifiers == gui::KeyModifiers::none;
    };
    if (auto r = m_filter.mount(host, H[filter]); !r) {
        return r;
    }
    const auto newKeys = [this, enterKey](const gui::KeyEvent& k) {
        if (enterKey(k)) {
            if (m_new->enabled()) {
                emitContent(WorkspaceIntent::Kind::ContentNew);
            }
            return true;
        }
        return false;
    };
    if (auto r = m_newId.mount(host, H[newId], [this](const std::string&) { applyEnabled(); }, newKeys); !r) {
        return r;
    }
    const auto valueKeys = [this, enterKey](const gui::KeyEvent& k) {
        if (enterKey(k)) {
            setField();
            return true;
        }
        if (k.action == gui::KeyAction::down && k.chord.key == gui::escape_key && k.chord.modifiers == gui::KeyModifiers::none) {
            m_formError.clear(); // 쓰던 것을 버리고 지금 값으로
            loadValue();
            return true;
        }
        return false;
    };
    if (auto r = m_value.mount(host, H[value], [this](const std::string&) { m_formError.clear(); }, valueKeys); !r) {
        return r;
    }
    const auto addKeys = [this, enterKey](const gui::KeyEvent& k) {
        if (enterKey(k)) {
            addComponent();
            return true;
        }
        return false;
    };
    if (auto r = m_addName.mount(host, H[addName], [this](const std::string&) { m_formError.clear(); applyEnabled(); }, addKeys);
        !r) {
        return r;
    }
    m_new = kit::bindButton(host, m_newNode, [this] {
        if (!m_newId.value().empty()) {
            emitContent(WorkspaceIntent::Kind::ContentNew);
        }
    });
    m_set = kit::bindButton(host, m_setNode, [this] { setField(); });
    m_reset = kit::bindButton(host, m_resetNode, [this] { resetField(); });
    m_remove = kit::bindButton(host, m_removeNode, [this] { removeComponent(); });
    m_add = kit::bindButton(host, m_addNode, [this] { addComponent(); });
    m_check = kit::bindButton(host, m_checkNode, [this] { emitContent(WorkspaceIntent::Kind::ContentCheck); });
    m_apply = kit::bindButton(host, m_applyNode, [this] {
        if (m_view.editable && dirty()) {
            emitContent(WorkspaceIntent::Kind::ContentApply);
        }
    });
    m_discard = kit::bindButton(host, m_discardNode, [this] {
        m_text = m_loaded; // 고친 것 버리기 (ImGui 창과 같다 — 새 항목이면 놓는다)
        m_issues.clear();
        m_formError.clear();
        if (m_isNew) {
            m_selected.clear();
            m_isNew = false;
        }
    });
    m_revert = kit::bindButton(host, m_revertNode, [this] {
        if (m_view.editable && m_view.inOverlay) {
            emitContent(WorkspaceIntent::Kind::ContentRevert);
        }
    });
    applyEnabled();
    for (const auto& [node, handler] :
         std::array<std::pair<gui::ComponentHandle, gui::EventReply (ContentPanel::*)(const gui::RoutedEvent&)>, 2>{
             {{m_itemsNode, &ContentPanel::itemEvent}, {m_rowsNode, &ContentPanel::rowEvent}}}) {
        (void)host.input().setFocusable(node, true);
        (void)host.input().setHandler(node, [this, h = handler](const gui::RoutedEvent& ev) { return (this->*h)(ev); });
    }
    (void)host.setWheelHandler(m_itemsNode, [this](f32, f32 dy) {
        (void)m_itemList.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    (void)host.setWheelHandler(m_rowsNode, [this](f32, f32 dy) {
        (void)m_rowList.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    (void)host.setWheelHandler(m_issuesNode, [this](f32, f32 dy) {
        (void)m_issueList.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> ContentPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    syncFromView();
    rebuildItems();
    rebuildRows();
    // 검사 결과 줄 — 여러 줄 문제 ("고치는 법", 스크립트 줄) 는 줄마다, 끝에 Editor 의 콘텐츠 거절
    m_issueLines.clear();
    const auto addLines = [this](const std::string& t, bool error) {
        std::string_view v = t;
        while (true) {
            const usize nl = v.find('\n');
            m_issueLines.push_back({error, std::string(v.substr(0, nl))});
            if (nl == std::string_view::npos) {
                break;
            }
            v = v.substr(nl + 1);
        }
    };
    for (const ContentIssueLine& i : m_issues) {
        addLines(i.text, i.error);
    }
    if (!m_view.message.empty()) {
        addLines(m_view.message, true);
    }
    setKeys(m_issueList, m_issueKeys, m_issueLines.size());
    applyEnabled();
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    constexpr double kGap = 6;
    const auto leaf = [&](gui::ComponentHandle c, double w, double h) {
        gui::LayoutNode n;
        n.component = c;
        n.style.preferred_width = w;
        n.style.preferred_height = h;
        out.push_back(n);
    };
    const auto box = [&](gui::ComponentHandle c, gui::LayoutKind kind, double gap, std::optional<double> w = std::nullopt) {
        gui::LayoutNode n;
        n.component = c;
        n.style.kind = kind;
        n.style.gap = gap;
        n.style.preferred_width = w;
        out.push_back(n);
    };
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = kGap;
    out.push_back(root);
    // 종류 단추 (ImGui 창의 라디오) · 오버레이 수
    box(m_kindBar, gui::LayoutKind::row, 4);
    const double kindW = 84;
    for (const gui::ComponentHandle k : m_kindNodes) {
        leaf(k, kindW, kLine);
    }
    leaf(m_overlayNode, fit(inner - kKinds * (kindW + 4)), kLine);
    // 몸통 — 왼쪽 목록 | 오른쪽 내용 (ImGui 창의 두 칸)
    const double body = fit(vp.height - 16 - kLine - kGap);
    const double leftW = std::clamp(std::floor(inner * 0.32), 150.0, 280.0);
    const double rightW = fit(inner - leftW - 8);
    box(m_body, gui::LayoutKind::row, 8);
    box(m_left, gui::LayoutKind::column, kGap, leftW);
    leaf(m_filter.handle(), leftW, kLine);
    leaf(m_itemsNode, leftW, fit(body - 2 * kLine - 2 * kGap));
    box(m_newRow, gui::LayoutKind::row, kGap);
    leaf(m_newId.handle(), fit(leftW - 64 - kGap), kLine);
    leaf(m_newNode, 64, kLine);
    box(m_right, gui::LayoutKind::column, kGap, rightW);
    leaf(m_titleNode, rightW, 16);
    leaf(m_rowsNode, rightW, fit(body - 16 - 16 - 3 * kLine - kIssuesH - 6 * kGap));
    leaf(m_label, rightW, 16);
    box(m_editRow, gui::LayoutKind::row, kGap);
    leaf(m_value.handle(), fit(rightW - 56 - 96 - 2 * kGap), kLine);
    leaf(m_setNode, 56, kLine);
    leaf(m_resetNode, 96, kLine);
    box(m_compRow, gui::LayoutKind::row, kGap);
    leaf(m_removeNode, 110, kLine);
    leaf(m_addName.handle(), fit(rightW - 110 - 56 - 2 * kGap), kLine);
    leaf(m_addNode, 56, kLine);
    box(m_actionRow, gui::LayoutKind::row, kGap);
    const double actW = fit((rightW - 3 * kGap) / 4);
    for (const gui::ComponentHandle a : {m_checkNode, m_applyNode, m_discardNode, m_revertNode}) {
        leaf(a, actW, kLine);
    }
    leaf(m_issuesNode, rightW, kIssuesH);
    return {};
}

Expected<void> ContentPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label, bool accent) {
        out.add(box.component, b.enabled() ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, accent, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
    };
    const auto text = [&](const gui::LayoutBox& box, const std::string& s, double em, services::Rgba8 color) {
        out.add(box.component,
                {gui::DrawText{{0, fit((box.bounds.height - 16) / 2), box.bounds.width, 16}, kFont, em, s, rc(color)}}, true);
    };
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
            continue;
        }
        if (const auto k = std::ranges::find(m_kindNodes, box.component); k != m_kindNodes.end()) {
            const usize i = static_cast<usize>(k - m_kindNodes.begin());
            btn(box, *m_kindButtons[i], std::string(kindName(kKindOrder[i])), m_view.available && kKindOrder[i] == m_kind);
            continue;
        }
        if (box.component == m_overlayNode) {
            text(box, m_view.available ? std::format("· 오버레이 {}", m_view.overlayCount) : std::string(), 12.5, c.muted);
        } else if (box.component == m_titleNode) {
            text(box, title(), 13.0, m_selected.empty() ? c.muted : c.text);
        } else if (box.component == m_label) {
            text(box, message(), 12.5, m_formError.empty() ? c.muted : c.error);
        } else if (box.component == m_filter.handle()) {
            m_filter.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_newId.handle()) {
            m_newId.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_value.handle()) {
            m_value.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_addName.handle()) {
            m_addName.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_newNode) {
            btn(box, *m_new, "새 항목", false);
        } else if (box.component == m_setNode) {
            btn(box, *m_set, "바꾸기", false);
        } else if (box.component == m_resetNode) {
            btn(box, *m_reset, "기본값으로", false);
        } else if (box.component == m_removeNode) {
            btn(box, *m_remove, "컴포넌트 빼기", false);
        } else if (box.component == m_addNode) {
            btn(box, *m_add, "더하기", false);
        } else if (box.component == m_checkNode) {
            btn(box, *m_check, "검사", false);
        } else if (box.component == m_applyNode) {
            btn(box, *m_apply, "적용", true);
        } else if (box.component == m_discardNode) {
            btn(box, *m_discard, "고친 것 버리기", false);
        } else if (box.component == m_revertNode) {
            btn(box, *m_revert, "팩 값으로", false);
        } else if (box.component == m_itemsNode) {
            m_itemBounds = box.bounds;
            (void)m_itemList.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const bool focused = focus && *focus == m_itemsNode;
            const gui::ListRange range = m_itemList.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_itemList.visibleRow(i);
                if (!vr || vr->key >= m_items.size()) {
                    continue;
                }
                const Item& it = m_items[static_cast<usize>(vr->key)];
                const gui::LayoutRect b = vr->bounds;
                if (it.header) {
                    d.push_back(gui::FillRect{{b.x, b.y + 1, b.width, fit(b.height - 2)}, rc(c.elevated)});
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont, 12.5, it.text, rc(c.accent)});
                    continue;
                }
                if (it.text == m_selected && !m_isNew) {
                    d.push_back(gui::FillRect{b, focused ? rc(c.header) : rc(c.headerHover)});
                    d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                }
                d.push_back(gui::DrawText{{b.x + 8, b.y + 2, fit(b.width - 14), 16}, kFont, 13.0,
                                          (it.overlay ? "● " : "   ") + it.text, rc(c.text)});
            }
            if (m_items.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          !m_view.available ? "프로젝트 밖"
                                          : m_filter.value().empty() ? "항목이 없다"
                                                                     : "거르기에 맞는 항목이 없다",
                                          rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_rowsNode) {
            m_rowBounds = box.bounds;
            (void)m_rowList.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const bool focused = focus && *focus == m_rowsNode;
            const gui::ListRange range = m_rowList.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_rowList.visibleRow(i);
                if (!vr || vr->key >= m_rows.size()) {
                    continue;
                }
                const usize index = static_cast<usize>(vr->key);
                const Row& r = m_rows[index];
                const gui::LayoutRect b = vr->bounds;
                if (r.kind == Row::Kind::Header) {
                    d.push_back(gui::FillRect{{b.x, b.y + 1, b.width, fit(b.height - 2)}, rc(c.elevated)});
                }
                if (m_selRow == index) {
                    d.push_back(gui::FillRect{b, focused ? rc(c.header) : rc(c.headerHover)});
                    if (focused) {
                        d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                    }
                }
                switch (r.kind) {
                case Row::Kind::Summary:
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont, 12.5, r.name, rc(c.muted)});
                    break;
                case Row::Kind::Header:
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont, 13.0, r.name, rc(c.accent)});
                    break;
                case Row::Kind::Field: {
                    // 이름 | 값 — 원본에 쓴 필드는 밝게, 기본값은 흐리게 (ImGui 폼과 같은 뜻)
                    const double split = std::floor(b.width * 0.42);
                    d.push_back(gui::DrawText{{b.x + 16, b.y + 2, fit(split - 20), 16}, kFont, 12.5, r.name, rc(c.muted)});
                    d.push_back(gui::DrawText{{b.x + split, b.y + 2, fit(b.width - split - 6), 16}, kFont, 12.5,
                                              r.set ? r.value : r.value + "  (기본)",
                                              r.set && r.editable ? rc(c.text) : rc(c.muted)});
                    break;
                }
                case Row::Kind::Text:
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont, 12.5,
                                              r.value.empty() ? r.name : r.name + "  " + r.value, rc(c.text)});
                    break;
                }
            }
            if (m_rows.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          m_needLoad ? "불러오는 중" : m_selected.empty() ? "고른 항목이 없다" : "내용이 없다",
                                          rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_issuesNode) {
            (void)m_issueList.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_issueList.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_issueList.visibleRow(i);
                if (!vr || vr->key >= m_issueLines.size()) {
                    continue;
                }
                const ContentIssueLine& line = m_issueLines[static_cast<usize>(vr->key)];
                const gui::LayoutRect b = vr->bounds;
                d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont, 12.5, line.text,
                                          line.error ? rc(c.error) : rc(c.success)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
