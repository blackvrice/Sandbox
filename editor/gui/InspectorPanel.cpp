#include "editor/gui/InspectorPanel.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kRowH = 20.0;
constexpr double kBtnW = 56.0;
constexpr std::string_view kDegree = "\xC2\xB0"; // °

[[nodiscard]] std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] std::optional<f64> parseNumber(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.front() == '+') {
        s.remove_prefix(1);
    }
    f64 v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || ec != std::errc{} || end != s.data() + s.size() || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

[[nodiscard]] std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}


} // namespace

// ---- 값 글자 ----

std::string InspectorPanel::kindText(const FormField& f) {
    std::string k;
    switch (f.kind) {
    case FormFieldKind::Bool:
        k = "켜기 · 끄기 (true · false — Space 로 바꾸기)";
        break;
    case FormFieldKind::Int:
        k = "정수";
        break;
    case FormFieldKind::Number:
        k = f.hint == ecs::Hint::Angle ? "각도 (도)" : f.hint == ecs::Hint::Percent ? "비율 (0 ~ 1)" : "수";
        break;
    case FormFieldKind::Vec2:
        k = "좌표 (x, y)";
        break;
    case FormFieldKind::Vec2i:
        k = "정수 좌표 (x, y)";
        break;
    case FormFieldKind::Text:
        k = "글자";
        break;
    case FormFieldKind::PrefabRef:
        k = "Prefab id";
        break;
    case FormFieldKind::BehaviorRef:
        k = "행동 id";
        break;
    case FormFieldKind::EntityRef:
        return "개체 참조 — 읽기만";
    case FormFieldKind::Other:
        return "배열 · 객체 — 읽기만";
    }
    // 범위 (FieldMeta) — 각도는 라디안 범위라 보이지 않는다
    if (f.meta.hasRange && std::isfinite(f.meta.min) && std::isfinite(f.meta.max) && f.hint != ecs::Hint::Angle) {
        k += std::format(" · 범위 {:g} ~ {:g}", f.meta.min, f.meta.max);
    }
    return k;
}


std::string InspectorPanel::fieldText(const FormField& f) {
    const ecs::Json& v = f.value;
    switch (f.kind) {
    case FormFieldKind::Bool:
        return v.is_boolean() && v.get<bool>() ? "true" : "false";
    case FormFieldKind::Int:
        return v.is_number_unsigned() ? std::to_string(v.get<u64>()) : std::to_string(v.get<i64>());
    case FormFieldKind::Number: {
        const f64 x = v.get<f64>();
        if (f.hint == ecs::Hint::Angle) { // ImGui 창처럼 도로 (값은 라디안)
            return std::format("{:.1f}{}", x * 180.0 / std::numbers::pi, kDegree);
        }
        return std::format("{:.3f}", x);
    }
    case FormFieldKind::Vec2:
        return std::format("{:.2f}, {:.2f}", v[0].get<f64>(), v[1].get<f64>());
    case FormFieldKind::Vec2i:
        return std::format("{}, {}", v[0].get<i64>(), v[1].get<i64>());
    case FormFieldKind::Text:
    case FormFieldKind::PrefabRef:
    case FormFieldKind::BehaviorRef:
        return v.is_string() ? v.get<std::string>() : v.dump();
    case FormFieldKind::EntityRef:
        return v.is_number_unsigned() || v.is_number_integer() ? std::format("#{}", v.dump()) : v.dump();
    case FormFieldKind::Other: {
        std::string d = v.dump();
        if (d.size() > 80) {
            d.resize(77);
            d += "...";
        }
        return d;
    }
    }
    return v.dump();
}

Expected<ecs::Json> InspectorPanel::parseFieldText(const FormField& f, std::string_view text) {
    const std::string_view t = trim(text);
    switch (f.kind) {
    case FormFieldKind::Bool: {
        const std::string l = lowerAscii(t);
        if (l == "true" || l == "1" || l == "on" || l == "켜기" || l == "켬") {
            return ecs::Json(true);
        }
        if (l == "false" || l == "0" || l == "off" || l == "끄기" || l == "끔") {
            return ecs::Json(false);
        }
        return makeError(ErrorCode::ParseError, "true 또는 false");
    }
    case FormFieldKind::Int:
    case FormFieldKind::Number: {
        std::string_view n = t;
        if (f.hint == ecs::Hint::Angle && n.ends_with(kDegree)) {
            n.remove_suffix(kDegree.size());
        }
        const auto x = parseNumber(n);
        if (!x) {
            return makeError(ErrorCode::ParseError, f.kind == FormFieldKind::Int ? "정수를 쓰십시오" : "수를 쓰십시오");
        }
        if (f.hint == ecs::Hint::Angle) {
            return ecs::Json(*x * std::numbers::pi / 180.0);
        }
        return ecs::Json(*x); // 정수 반올림 · 범위 자르기는 setFormField
    }
    case FormFieldKind::Vec2:
    case FormFieldKind::Vec2i: {
        std::string_view s = t;
        if (s.starts_with('[') && s.ends_with(']')) {
            s = s.substr(1, s.size() - 2);
        }
        // "x, y" (쉼표가 없으면 빈칸)
        usize cut = s.find(',');
        usize skip = 1;
        if (cut == std::string_view::npos) {
            cut = trim(s).find(' ');
            s = trim(s);
        }
        if (cut == std::string_view::npos) {
            return makeError(ErrorCode::ParseError, "x, y 두 수를 쓰십시오");
        }
        const auto x = parseNumber(s.substr(0, cut));
        const auto y = parseNumber(s.substr(cut + skip));
        if (!x || !y) {
            return makeError(ErrorCode::ParseError, "x, y 두 수를 쓰십시오");
        }
        return ecs::Json::array({*x, *y});
    }
    case FormFieldKind::Text:
    case FormFieldKind::PrefabRef:
    case FormFieldKind::BehaviorRef:
        return ecs::Json(std::string(text)); // 글자는 그대로 (빈칸도 값)
    case FormFieldKind::EntityRef:
    case FormFieldKind::Other:
        break;
    }
    return makeError(ErrorCode::PermissionDenied, "이 필드는 읽기만");
}

// ---- 보기 · 줄 ----

void InspectorPanel::setView(InspectorView view) {
    m_view = std::move(view);
}

std::string InspectorPanel::status() const {
    if (!m_view.available) {
        return "프로젝트가 열려 있지 않다 (또는 편집할 수 없는 접속) — 월드를 열면 고른 개체의 컴포넌트";
    }
    if (m_view.entity == kInvalidNetEntityId) {
        return "선택 없음 — 목록이나 장면에서 개체를 고르십시오";
    }
    std::string s = m_view.title;
    if (m_view.selected > 1) {
        s += std::format("  외 {} — 바꾸면 그 컴포넌트가 있는 개체 전부", m_view.selected - 1);
    }
    if (!m_view.editable) {
        s += "  · 보기만 (지금 역할로는 바꿀 수 없다)";
    }
    return s;
}

const InspectorSection* InspectorPanel::selectedSection() const {
    if (!m_selRow || m_rows[*m_selRow].kind == Row::Kind::Removed) {
        return nullptr;
    }
    return &m_view.sections[m_rows[*m_selRow].section];
}

const FormField* InspectorPanel::selectedField() const {
    if (!m_selRow || m_rows[*m_selRow].kind != Row::Kind::Field) {
        return nullptr;
    }
    const Row& r = m_rows[*m_selRow];
    const auto& fields = m_view.sections[r.section].form.fields;
    return r.field < fields.size() ? &fields[r.field] : nullptr;
}

std::vector<std::string> InspectorPanel::addMatches() const {
    const std::string q = lowerAscii(trim(m_addName.value()));
    std::vector<std::string> out;
    if (q.empty()) {
        return out;
    }
    for (const std::string& n : m_view.addable) {
        if (lowerAscii(n) == q) {
            return {n}; // 정확히 맞으면 그것만
        }
        if (lowerAscii(n).find(q) != std::string::npos) {
            out.push_back(n);
        }
    }
    return out;
}

std::string InspectorPanel::message() const {
    if (!m_error.empty()) {
        return m_error;
    }
    if (!trim(m_addName.value()).empty()) {
        const std::vector<std::string> m = addMatches();
        if (m.empty()) {
            return "더할 수 있는 컴포넌트 중 맞는 이름이 없다";
        }
        if (m.size() == 1) {
            return std::format("더하기: {} (Enter)", m.front());
        }
        std::string s = std::format("후보 {} —", m.size());
        for (usize i = 0; i < m.size() && i < 4; ++i) {
            s += (i == 0 ? " " : ", ") + m[i];
        }
        return m.size() > 4 ? s + " …" : s;
    }
    if (const FormField* f = selectedField()) {
        std::string s = std::format("{}.{} — {}", m_view.sections[m_rows[*m_selRow].section].form.component, f->name,
                                    InspectorPanel::kindText(*f));
        if (f->kind == FormFieldKind::PrefabRef && !m_view.prefabs.empty()) {
            s += std::format(" · Prefab {} 개", m_view.prefabs.size());
        }
        return s;
    }
    if (m_selRow && m_rows[*m_selRow].kind == Row::Kind::Removed) {
        return "Prefab 에 있던 컴포넌트를 이 개체에서 뺐다 — 되살리기";
    }
    if (const InspectorSection* s = selectedSection()) {
        return std::format("{} — 필드 {} 개", s->form.component, s->form.fields.size());
    }
    return {};
}

std::string InspectorPanel::revertLabel() const {
    if (m_selRow && m_rows[*m_selRow].kind == Row::Kind::Removed) {
        return "되살리기";
    }
    if (const InspectorSection* s = selectedSection(); s != nullptr && s->mark == InspectorSection::Mark::Added) {
        return "Prefab 처럼 (빼기)";
    }
    return "Prefab 값으로";
}

void InspectorPanel::rebuild() {
    m_rows.clear();
    if (m_view.available && m_view.entity != kInvalidNetEntityId) {
        for (const InspectorRemoved& r : m_view.removed) {
            Row row;
            row.kind = Row::Kind::Removed;
            row.component = r.stableId;
            row.name = "Prefab 에서 뺌: " + r.component;
            m_rows.push_back(std::move(row));
        }
        for (usize i = 0; i < m_view.sections.size(); ++i) {
            const InspectorSection& s = m_view.sections[i];
            const bool collapsed = std::ranges::find(m_collapsed, s.form.stableId) != m_collapsed.end();
            Row head;
            head.kind = Row::Kind::Header;
            head.section = i;
            head.component = s.form.stableId;
            head.name = std::format("{} {}{}{}", collapsed ? "▸" : "▾", s.form.component, s.replicated ? "" : "  (서버)",
                                    s.mark == InspectorSection::Mark::Added       ? "  ● 더함"
                                    : s.mark == InspectorSection::Mark::Different ? "  ● Prefab 과 다름"
                                                                                  : "");
            m_rows.push_back(std::move(head));
            if (collapsed) {
                continue;
            }
            for (usize j = 0; j < s.form.fields.size(); ++j) {
                const FormField& f = s.form.fields[j];
                Row row;
                row.section = i;
                row.field = j;
                row.component = s.form.stableId;
                row.name = f.name;
                row.value = fieldText(f);
                row.editable = m_view.editable && f.editable();
                row.overridden = std::ranges::find(s.overridden, f.name) != s.overridden.end();
                m_rows.push_back(std::move(row));
            }
            if (s.form.fields.empty() && !s.form.raw.is_null()) { // opaque (render.* · client.*) — 통째로 읽기만
                Row row;
                row.section = i;
                row.field = static_cast<usize>(-1);
                row.component = s.form.stableId;
                row.name = "(값)";
                row.value = s.form.raw.dump();
                if (row.value.size() > 80) {
                    row.value.resize(77);
                    row.value += "...";
                }
                m_rows.push_back(std::move(row));
            }
        }
    }
    std::vector<gui::ListItemKey> keys(m_rows.size());
    for (usize i = 0; i < keys.size(); ++i) {
        keys[i] = i; // 줄 차례 — 줄 수가 같으면 그대로 (스크롤 유지)
    }
    if (keys != m_keys) {
        m_keys = std::move(keys);
        (void)m_list.setItems(m_keys);
    }
    // 고른 줄을 이름으로 다시 찾는다 (컴포넌트가 빠졌으면 없음)
    const std::optional<usize> before = m_selRow;
    m_selRow.reset();
    for (usize i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        if (r.kind == m_selKind && r.component == m_selComponent && (r.kind != Row::Kind::Field || r.name == m_selField)) {
            m_selRow = i;
            break;
        }
    }
    const bool changedEntity = m_selEntity != m_view.entity;
    m_selEntity = m_view.entity;
    if (before.has_value() != m_selRow.has_value() || changedEntity) {
        m_error.clear();
        loadValue();
    } else {
        // 값 칸을 쓰는 중이 아니면 지금 값을 따라간다 (움직이는 개체의 위치 등)
        const auto focus = m_host->input().focus();
        if (!(focus && *focus == m_value.handle())) {
            const FormField* f = selectedField();
            const std::string now = f != nullptr ? fieldText(*f) : std::string();
            if (now != m_value.value()) {
                m_value.setValue(now);
            }
        }
    }
    applyEnabled();
}

void InspectorPanel::loadValue() {
    const FormField* f = selectedField();
    m_value.setValue(f != nullptr ? fieldText(*f) : std::string());
}

void InspectorPanel::applyEnabled() {
    if (!m_apply) {
        return;
    }
    const FormField* f = selectedField();
    const bool fieldOk = m_view.editable && f != nullptr && f->editable();
    m_value.setEnabled(fieldOk);
    (void)m_apply->setEnabled(fieldOk);
    (void)m_remove->setEnabled(m_view.editable && selectedSection() != nullptr);
    const InspectorSection* s = selectedSection();
    const bool removedRow = m_selRow && m_rows[*m_selRow].kind == Row::Kind::Removed;
    (void)m_revert->setEnabled(m_view.editable && (removedRow || (s != nullptr && s->mark != InspectorSection::Mark::None)));
    (void)m_add->setEnabled(m_view.editable && m_view.catalog != nullptr && m_view.entity != kInvalidNetEntityId &&
                            addMatches().size() == 1);
    m_addName.setEnabled(m_view.editable && m_view.entity != kInvalidNetEntityId);
}

void InspectorPanel::selectRow(std::optional<usize> row) {
    m_selRow = row;
    if (row) {
        const Row& r = m_rows[*row];
        m_selKind = r.kind;
        m_selComponent = r.component;
        m_selField = r.kind == Row::Kind::Field ? r.name : std::string();
        (void)m_list.reveal(m_keys[*row]);
    }
    m_error.clear();
    loadValue();
    applyEnabled();
}

// ---- 일 ----

void InspectorPanel::apply() {
    const FormField* f = selectedField();
    const InspectorSection* s = selectedSection();
    if (f == nullptr || s == nullptr || !m_view.editable || !f->editable()) {
        return;
    }
    if (m_value.value() == fieldText(*f)) {
        m_error.clear(); // 바꾼 것이 없다 (보이는 값의 자릿수로 원래 값을 덮지 않는다)
        return;
    }
    auto v = parseFieldText(*f, m_value.value());
    if (!v) {
        m_error = std::format("{} — {}", f->name, v.error().message);
        return;
    }
    if (f->kind == FormFieldKind::PrefabRef && !m_view.prefabs.empty() &&
        std::ranges::find(m_view.prefabs, v->get<std::string>()) == m_view.prefabs.end()) {
        m_error = std::format("없는 Prefab '{}'", v->get<std::string>());
        return;
    }
    // DataForm 으로 검사 · 범위로 자르기 · 정수 반올림 (콘텐츠 창의 Prefab 폼과 같은 규칙) — 통과한 값만 보낸다
    const ecs::ComponentInfo* info = m_view.catalog != nullptr ? m_view.catalog->find(s->form.stableId) : nullptr;
    if (info == nullptr) {
        m_error = "카탈로그에 없는 컴포넌트";
        return;
    }
    ecs::Json partial = partialFromForm(s->form);
    if (auto r = setFormField(*info, partial, f->name, *v); !r) {
        m_error = std::format("{} — {}", f->name, r.error().message);
        return;
    }
    m_error.clear();
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::SetField, f->name);
    i.component = s->form.stableId;
    i.json = partial[f->name];
    m_ctx.intent = std::move(i);
}

void InspectorPanel::toggleBool() {
    const FormField* f = selectedField();
    const InspectorSection* s = selectedSection();
    if (f == nullptr || s == nullptr || f->kind != FormFieldKind::Bool || !m_view.editable) {
        return;
    }
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::SetField, f->name);
    i.component = s->form.stableId;
    i.json = ecs::Json(!(f->value.is_boolean() && f->value.get<bool>()));
    m_ctx.intent = std::move(i);
}

void InspectorPanel::addComponent() {
    const std::vector<std::string> m = addMatches();
    if (!m_view.editable || m_view.catalog == nullptr || m.size() != 1) {
        return;
    }
    const ecs::ComponentInfo* info = m_view.catalog->find(std::string_view(m.front()));
    if (info == nullptr) {
        return;
    }
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::AddComponent, m.front());
    i.component = info->stableId;
    m_ctx.intent = std::move(i);
    m_addName.setValue({});
}

void InspectorPanel::clickRow(usize row) {
    if (row >= m_rows.size()) {
        return;
    }
    const u64 frame = m_host->stats().frames;
    const bool twice = m_lastClick == row && frame - m_lastClickFrame <= kDoubleClickFrames;
    m_lastClick = twice ? std::nullopt : std::optional<usize>(row);
    m_lastClickFrame = frame;
    selectRow(row);
    const Row& r = m_rows[row];
    if (r.kind == Row::Kind::Header) { // 머리 = 접기 · 펴기 (ImGui 의 CollapsingHeader 처럼)
        const auto at = std::ranges::find(m_collapsed, r.component);
        if (at != m_collapsed.end()) {
            m_collapsed.erase(at);
        } else {
            m_collapsed.push_back(r.component);
        }
        return;
    }
    if (twice && r.kind == Row::Kind::Field) {
        const FormField* f = selectedField();
        if (f != nullptr && f->kind == FormFieldKind::Bool) {
            toggleBool();
        } else if (r.editable) {
            m_value.focusAll();
        }
    }
}

std::optional<gui::LayoutRect> InspectorPanel::rowBounds(usize row) const {
    if (row >= m_keys.size()) {
        return std::nullopt;
    }
    const gui::ListRange range = m_list.visibleRange();
    for (usize i = range.first; i < range.past; ++i) {
        if (const auto r = m_list.visibleRow(i); r && r->key == m_keys[row]) {
            return r->bounds;
        }
    }
    return std::nullopt;
}

gui::EventReply InspectorPanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y})) {
                clickRow(static_cast<usize>(row->key));
            }
            return {true, true};
        }
        return {};
    }
    // 되돌리기 · 다시 — 툴바 · 단축키와 같은 명령 (Editor 의 기록)
    for (const gui::KeyModifiers mod : {gui::KeyModifiers::control, gui::KeyModifiers::meta}) {
        if (kit::keyDown(ev, 'Z', mod)) {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "edit.undo");
            return {true, true};
        }
        if (kit::keyDown(ev, 'Y', mod) || kit::keyDown(ev, 'Z', mod | gui::KeyModifiers::shift)) {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "edit.redo");
            return {true, true};
        }
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
    if (!m_selRow) {
        return {};
    }
    const Row& r = m_rows[*m_selRow];
    if (kit::keyDown(ev, gui::enter_key) || kit::keyDown(ev, gui::space_key)) {
        const bool space = kit::keyDown(ev, gui::space_key);
        if (r.kind == Row::Kind::Header) {
            clickRow(*m_selRow);
            m_lastClick.reset();
        } else if (const FormField* f = selectedField(); f != nullptr && f->kind == FormFieldKind::Bool) {
            toggleBool();
        } else if (!space && r.editable) {
            m_value.focusAll();
        }
        return {true, true};
    }
    return {};
}

// ---- 화면 ----

Expected<void> InspectorPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 제목 · 목록 · 안내 · 값 줄 (값 · 적용) · 컴포넌트 줄 (빼기 · 되돌리기) · 더하기 줄 (이름 · 더하기)
    const std::array<gui::ComponentSpec, 13> specs{{{std::nullopt, 1, 0, true},
                                                    {0, 2, 1, true},
                                                    {0, 3, 2, true},
                                                    {0, 4, 3, true},
                                                    {0, 5, 4, true},
                                                    {4, 6, 1, true},
                                                    {4, 7, 2, true},
                                                    {0, 5, 5, true},
                                                    {7, 7, 1, true},
                                                    {7, 7, 2, true},
                                                    {0, 5, 6, true},
                                                    {10, 6, 1, true},
                                                    {10, 7, 2, true}}};
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_title = (*h)[1];
    m_listNode = (*h)[2];
    m_label = (*h)[3];
    m_valueRow = (*h)[4];
    m_applyNode = (*h)[6];
    m_compRow = (*h)[7];
    m_removeNode = (*h)[8];
    m_revertNode = (*h)[9];
    m_addRow = (*h)[10];
    m_addNode = (*h)[12];
    const auto valueKeys = [this](const gui::KeyEvent& k) {
        if (k.action != gui::KeyAction::down || k.chord.modifiers != gui::KeyModifiers::none) {
            return false;
        }
        if (k.chord.key == gui::enter_key) {
            apply();
            return true;
        }
        if (k.chord.key == gui::escape_key) { // 쓰던 것을 버리고 지금 값으로
            m_error.clear();
            loadValue();
            return true;
        }
        return false;
    };
    if (auto r = m_value.mount(host, (*h)[5], [this](const std::string&) { m_error.clear(); }, valueKeys); !r) {
        return r;
    }
    const auto addKeys = [this](const gui::KeyEvent& k) {
        if (k.action == gui::KeyAction::down && k.chord.key == gui::enter_key && k.chord.modifiers == gui::KeyModifiers::none) {
            addComponent();
            return true;
        }
        return false;
    };
    if (auto r = m_addName.mount(host, (*h)[11], [this](const std::string&) { m_error.clear(); applyEnabled(); }, addKeys);
        !r) {
        return r;
    }
    m_apply = kit::bindButton(host, m_applyNode, [this] { apply(); });
    m_remove = kit::bindButton(host, m_removeNode, [this] {
        if (const InspectorSection* s = selectedSection(); s != nullptr && m_view.editable) {
            WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::RemoveComponent, s->form.component);
            i.component = s->form.stableId;
            m_ctx.intent = std::move(i);
        }
    });
    m_revert = kit::bindButton(host, m_revertNode, [this] {
        if (m_selRow && m_view.editable) {
            WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::RevertToPrefab);
            i.component = m_rows[*m_selRow].component;
            m_ctx.intent = std::move(i);
        }
    });
    m_add = kit::bindButton(host, m_addNode, [this] { addComponent(); });
    applyEnabled();
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> InspectorPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    rebuild(); // 줄 · 고른 줄 · 단추 (frame 마다 — ImGui 창도 frame 마다 값을 읽는다)
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    constexpr double kLine = 26;
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    const auto leaf = [&](gui::ComponentHandle c, double w, double h) {
        gui::LayoutNode n;
        n.component = c;
        n.style.preferred_width = w;
        n.style.preferred_height = h;
        out.push_back(n);
    };
    const auto row = [&](gui::ComponentHandle c) {
        gui::LayoutNode n;
        n.component = c;
        n.style.kind = gui::LayoutKind::row;
        n.style.gap = 6;
        out.push_back(n);
    };
    leaf(m_title, inner, 16);
    // 고정 높이: 여백 16 · 제목 16 · 안내 16 · 줄 3 개 · 사이 5 개
    leaf(m_listNode, inner, fit(vp.height - 16 - 16 - 16 - 3 * kLine - 5 * 6));
    leaf(m_label, inner, 16);
    const double field = std::max(60.0, inner - kBtnW - 6);
    row(m_valueRow);
    leaf(m_value.handle(), field, kLine);
    leaf(m_applyNode, kBtnW, kLine);
    row(m_compRow);
    const double half = fit((inner - 6) / 2);
    leaf(m_removeNode, half, kLine);
    leaf(m_revertNode, half, kLine);
    row(m_addRow);
    leaf(m_addName.handle(), field, kLine);
    leaf(m_addNode, kBtnW, kLine);
    return {};
}

Expected<void> InspectorPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label) {
        out.add(box.component, b.enabled() ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, false, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
    };
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_title) {
            const bool real = m_view.available && m_view.entity != kInvalidNetEntityId;
            out.add(box.component, {gui::DrawText{{0, 0, w, h}, kFont, 13.0, status(), real ? rc(c.text) : rc(c.muted)}},
                    true);
        } else if (box.component == m_label) {
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 12.5, message(), messageIsError() ? rc(c.error) : rc(c.muted)}}, true);
        } else if (box.component == m_value.handle()) {
            m_value.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_addName.handle()) {
            m_addName.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_applyNode) {
            btn(box, *m_apply, "적용");
        } else if (box.component == m_removeNode) {
            btn(box, *m_remove, "컴포넌트 빼기");
        } else if (box.component == m_revertNode) {
            btn(box, *m_revert, revertLabel());
        } else if (box.component == m_addNode) {
            btn(box, *m_add, "더하기");
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const bool listFocused = focus && *focus == m_listNode;
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_list.visibleRow(i);
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
                    d.push_back(gui::FillRect{b, listFocused ? rc(c.header) : rc(c.headerHover)});
                    if (listFocused) {
                        d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                    }
                }
                if (r.kind != Row::Kind::Field) {
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont, 13.0, r.name,
                                              r.kind == Row::Kind::Removed ? rc(c.warning) : rc(c.accent)});
                    continue;
                }
                // 필드 — 이름 | 값 (Prefab 과 다른 값은 경고색 — ImGui 창의 오버라이드 색, 읽기만은 흐리게)
                const double split = std::floor(b.width * 0.42);
                d.push_back(gui::DrawText{{b.x + 16, b.y + 2, fit(split - 20), 16}, kFont, 12.5, r.name, rc(c.muted)});
                d.push_back(gui::DrawText{{b.x + split, b.y + 2, fit(b.width - split - 6), 16}, kFont, 12.5, r.value,
                                          r.overridden ? rc(c.warning)
                                          : r.editable ? rc(c.text)
                                                       : rc(c.muted)});
            }
            if (m_rows.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          m_view.entity != kInvalidNetEntityId ? "컴포넌트가 없다" : "고른 개체가 없다",
                                          rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
