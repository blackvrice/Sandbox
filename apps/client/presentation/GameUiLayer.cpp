#include "apps/client/presentation/GameUiLayer.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "ui/input/UiTextEdit.hpp"

namespace sbx::client {

void GameUiLayer::setSources(const std::optional<GameUiInfo>& info) {
    std::string signature;
    if (info) {
        for (const auto* list : {&info->styles, &info->documents, &info->strings}) {
            for (const GameUiSource& s : *list) {
                signature += s.pack + '\0' + s.stem + '\0' + s.text + '\0';
            }
        }
    }
    if (signature == m_signature) {
        return;
    }
    m_signature = std::move(signature);
    m_baseStyles.clear();
    m_themes.clear();
    m_theme.clear();
    m_styles.clear();
    m_strings.clear();
    m_docs.clear();
    m_visible.clear();
    m_hidden.clear();
    m_layouts.clear();
    m_pressed = nullptr;
    m_focus = nullptr; // 문서를 다시 읽었다 — 노드 포인터가 바뀐다
    m_drag = nullptr;
    m_open = nullptr;
    m_scroll.clear();
    if (!info) {
        return;
    }
    for (const GameUiSource& s : info->styles) {
        ui::UiStylesResult st = ui::parseUiStyles(s.text, s.pack);
        for (auto& [name, style] : st.styles) {
            m_baseStyles.insert_or_assign(name, style); // 뒤 팩이 이긴다 (materials.json 과 같다)
        }
        for (auto& [name, theme] : st.themes) {
            m_themes.insert_or_assign(name, std::move(theme));
        }
    }
    m_styles = m_baseStyles;
    // 문자열 표 — 팩마다 모아서 (팩의 기본 표는 그 팩의 표들로 정한다). 원문은 팩 차례 · 로케일 이름 순
    for (usize i = 0; i < info->strings.size();) {
        const std::string& pack = info->strings[i].pack;
        ui::LocaleTables tables;
        for (; i < info->strings.size() && info->strings[i].pack == pack; ++i) {
            const GameUiSource& s = info->strings[i];
            ui::StringTableResult st = ui::parseStringTable(s.text, s.pack);
            if (!st.ok()) {
                const std::string why = st.issues.empty() ? std::string() : st.issues.front().message;
                m_warnings.push_back(std::format(
                    "문자열 표 {}/presentation/strings/{}.json 에 문제가 있다 — {} (sbx check)", s.pack, s.stem, why));
            }
            const std::string locale = ui::normalizeLocale(s.stem);
            if (!locale.empty()) {
                tables.emplace_back(locale, std::move(st.entries));
            }
        }
        m_strings.addPack(tables);
    }
    for (const GameUiSource& s : info->documents) {
        ui::UiParseResult parsed = ui::parseUiDocument(s.stem, s.text, ui::UiCheckContext{s.pack});
        if (!parsed.document) {
            const std::string why = parsed.issues.empty() ? std::string() : parsed.issues.front().message;
            m_warnings.push_back(
                std::format("게임 UI {}/presentation/ui/{}.json 을 읽지 못했다 — {} (sbx check)", s.pack, s.stem, why));
            continue;
        }
        m_visible.push_back(parsed.document->visible);
        m_docs.push_back(std::move(*parsed.document));
    }
}

void GameUiLayer::layout(i32 width, i32 height, f32 scale, const ui::ITextMeasure& measure) {
    m_height = height;
    m_layouts.clear();
    m_layouts.reserve(m_docs.size());
    for (usize i = 0; i < m_docs.size(); ++i) {
        const ui::UiDocument& d = m_docs[i];
        m_scale = scale;
        ui::UiLayoutOptions o{.width = width, .height = height, .scale = scale, .styles = &m_styles};
        o.textOf = [this](const ui::UiNode& n, i32) { return ui::formatText(n, m_data, &m_strings); };
        o.listCount = [this](const ui::UiNode& n) { return ui::listCountOf(n, m_data); };
        o.hidden = [this, &d](const ui::UiNode& n) { return !n.id.empty() && nodeHidden(d.id, n.id); };
        o.selectedOf = [this](const ui::UiNode& n, i32) { return selectedIndex(n); };
        o.optionOf = [this](const ui::UiNode& n, usize option) { return optionText(n, option); };
        o.scrollOf = [this](const ui::UiNode& n, i32 listIndex) { return scrollOf(n, listIndex); };
        // 숨긴 문서는 배치하지 않는다 (라우팅 · 그리기 모두 건너뛴다)
        m_layouts.push_back(ui::UiDocumentLayout{
            &d, m_visible[i] ? ui::layoutDocument(d, o, measure) : std::vector<ui::UiLayoutNode>{}, m_visible[i]});
    }
}

ui::UiPointerRoute GameUiLayer::route(i32 x, i32 y, bool toolsWantMouse, bool pressed, bool down,
                                      std::vector<ui::UiAction>& out) {
    ui::UiPointerRoute r = ui::routePointer(m_layouts, x, y, toolsWantMouse);
    // slider 끌기 — 포인터가 밖으로 나가도 놓을 때까지 (놓으면 신호 하나)
    if (m_drag != nullptr) {
        const ui::UiLayoutNode* l = layoutOf(m_drag, m_dragIndex);
        if (l != nullptr) {
            sliderTo(*m_drag, l->rect, x);
        }
        if (!down) {
            emitChange(*m_drag, out);
            m_drag = nullptr;
        }
    }
    if (!down) {
        m_pressed = nullptr;
    }
    // 열린 dropdown 목록 (v0.7 NG-06 (3) — ADR-0105): 문서들 위 — 목록 위의 포인터는 목록의 것 (가리킨 줄, 누르면
    // 고르고 닫는다). 밖을 누르면 닫고 그 누름은 아래로 간다 (열린 dropdown 자신을 누른 것이면 닫기만)
    if (m_open != nullptr) {
        const ui::UiLayoutNode* l = layoutOf(m_open, m_openIndex);
        if (l == nullptr || disabled(*m_open)) {
            closeList();
        } else {
            const usize count = m_open->options.size();
            const i32 at = ui::optionAt(ui::optionListRect(l->rect, count, m_height), count, x, y);
            if (at >= 0) {
                m_highlight = at;
                static constexpr ui::UiPointerRoute::Owner kOwners[] = {
                    ui::UiPointerRoute::Owner::Hud, ui::UiPointerRoute::Owner::Menu, ui::UiPointerRoute::Owner::Modal};
                r = {kOwners[static_cast<usize>(m_layouts[m_openDocument].document->layer)], true, m_openDocument,
                     nullptr};
                if (pressed) {
                    ++m_presses;
                    const ui::UiNode& n = *m_open;
                    closeList();
                    select(n, at, out);
                }
                return r;
            }
            if (pressed) {
                const bool self =
                    r.control != nullptr && r.control->node == m_open && r.control->listIndex == m_openIndex;
                closeList();
                if (self) {
                    return r;
                }
            }
        }
    }
    // 글자 칸을 편집하는 중에 다른 곳을 누르면 놓는다 (바뀌었으면 보내기 — ADR-0104)
    if (pressed && editingText() &&
        (r.control == nullptr || r.control->node != m_focus || r.control->listIndex != m_focusIndex)) {
        setFocus(nullptr, -1, &out);
    }
    if (r.control != nullptr && pressed && !disabled(*r.control->node)) {
        m_pressed = r.control->node;
        m_pressedIndex = r.control->listIndex;
        ++m_presses;
        // menu · modal 의 컨트롤은 키보드 포커스도 (hud 는 아니다 — 게임 키를 빼앗지 않는다, ADR-0103 4). 글자 칸은
        // 어느 층이든 (누르는 것이 곧 글을 쓰겠다는 것 — ADR-0104)
        const ui::UiLayer layer = m_layouts[r.document].document->layer;
        if (layer == ui::UiLayer::Menu || layer == ui::UiLayer::Modal ||
            r.control->node->kind == ui::UiKind::TextInput) {
            if (r.control->node != m_focus || r.control->listIndex != m_focusIndex) {
                setFocus(r.control->node, r.control->listIndex, &out);
            }
        }
        if (r.control->node->kind == ui::UiKind::Tabs) {
            select(*r.control->node, ui::partAt(*r.control, x, y), out); // 누른 머리 칸
        } else if (r.control->node->kind == ui::UiKind::Slider) {
            m_drag = r.control->node;
            m_dragIndex = r.control->listIndex;
            sliderTo(*m_drag, r.control->rect, x);
            if (!down) { // 같은 프레임에 눌렀다 뗐다
                emitChange(*m_drag, out);
                m_drag = nullptr;
            }
        } else {
            press(*r.control, r.document, out);
        }
    }
    return r;
}

f32 GameUiLayer::scrollOf(const ui::UiNode& n, i32 listIndex) const {
    const auto it = m_scroll.find({&n, listIndex});
    return it == m_scroll.end() ? 0.0f : it->second;
}

bool GameUiLayer::wheel(i32 x, i32 y, f32 notches) {
    if (notches == 0.0f) {
        return false;
    }
    const bool modal = std::any_of(m_layouts.begin(), m_layouts.end(), [](const ui::UiDocumentLayout& d) {
        return d.visible && !d.nodes.empty() && d.document->layer == ui::UiLayer::Modal;
    });
    // 층 차례 (modal → menu → hud), 같은 층은 뒤 문서가 위, 노드는 나중 (안쪽) 이 위
    for (const ui::UiLayer layer : {ui::UiLayer::Modal, ui::UiLayer::Menu, ui::UiLayer::Hud}) {
        if (modal && layer != ui::UiLayer::Modal) {
            return false;
        }
        for (usize i = m_layouts.size(); i-- > 0;) {
            const ui::UiDocumentLayout& d = m_layouts[i];
            if (!d.visible || d.nodes.empty() || d.document->layer != layer) {
                continue;
            }
            for (auto it = d.nodes.rbegin(); it != d.nodes.rend(); ++it) {
                if (it->node->kind == ui::UiKind::Scroll && it->scrollRange > 0 && ui::contains(it->rect, x, y) &&
                    ui::contains(it->clip, x, y)) {
                    m_scroll[{it->node, it->listIndex}] =
                        std::clamp(it->scroll - notches * kWheelStep * m_scale, 0.0f, it->scrollRange);
                    return true;
                }
            }
        }
    }
    return false;
}

void GameUiLayer::revealFocus() {
    for (const ui::UiDocumentLayout& d : m_layouts) {
        for (usize k = 0; k < d.nodes.size(); ++k) {
            const ui::UiLayoutNode& f = d.nodes[k];
            if (f.node != m_focus || f.listIndex != m_focusIndex) {
                continue;
            }
            // 조상을 거슬러 (앞 순서 — 깊이가 줄어드는 앞 노드) 가장 안쪽 scroll 을 찾는다
            u32 depth = f.depth;
            for (usize j = k; j-- > 0 && depth > 0;) {
                const ui::UiLayoutNode& a = d.nodes[j];
                if (a.depth >= depth) {
                    continue;
                }
                depth = a.depth;
                if (a.node->kind != ui::UiKind::Scroll || a.scrollRange <= 0) {
                    continue;
                }
                const i32 pad = static_cast<i32>(a.padding + 0.5f);
                const i32 top = a.rect.y + pad;
                const i32 bottom = a.rect.y + a.rect.h - pad;
                f32 delta = 0;
                if (f.rect.y < top || f.rect.h > bottom - top) {
                    delta = static_cast<f32>(f.rect.y - top); // 위로 가려졌거나 보이는 높이보다 크면 위를 맞춘다
                } else if (f.rect.y + f.rect.h > bottom) {
                    delta = static_cast<f32>(f.rect.y + f.rect.h - bottom);
                }
                if (delta != 0.0f) {
                    m_scroll[{a.node, a.listIndex}] = std::clamp(a.scroll + delta, 0.0f, a.scrollRange);
                }
                return;
            }
            return;
        }
    }
}

i32 GameUiLayer::selectedIndex(const ui::UiNode& n) const {
    const f32 v = controlValue(n);
    const f32 count = static_cast<f32>(n.options.size());
    if (!(v > -0.5f) || !(v < count - 0.5f)) {
        return -1;
    }
    return static_cast<i32>(std::floor(v + 0.5f));
}

void GameUiLayer::select(const ui::UiNode& n, i32 index, std::vector<ui::UiAction>& out) {
    if (index < 0 || static_cast<usize>(index) >= n.options.size() || index == selectedIndex(n)) {
        return;
    }
    setControlValue(n, static_cast<f32>(index));
    emitChange(n, out);
}

void GameUiLayer::openList(const ui::UiLayoutNode& l, usize document) {
    m_open = l.node;
    m_openIndex = l.listIndex;
    m_openDocument = document;
    m_highlight = std::max(0, selectedIndex(*l.node));
}

bool GameUiLayer::choiceKey(const ui::UiLayoutNode& l, usize document, ui::UiKey key, std::vector<ui::UiAction>& out) {
    using K = ui::UiKey;
    const ui::UiNode& n = *l.node;
    const i32 count = static_cast<i32>(n.options.size());
    if (m_open == &n && m_openIndex == l.listIndex) {
        // 열린 목록: 위 · 아래 = 가리킨 줄, Enter · Space = 고르고 닫기, Esc = 닫기 (포커스는 남는다)
        switch (key) {
        case K::Up:
        case K::Down:
            m_highlight = std::clamp(m_highlight + (key == K::Down ? 1 : -1), 0, count - 1);
            return true;
        case K::Activate:
            closeList();
            select(n, m_highlight, out);
            return true;
        case K::Release:
            closeList();
            return true;
        case K::Left:
        case K::Right:
            return true;
        default:
            return false;
        }
    }
    switch (key) {
    case K::Left:
    case K::Right: {
        // 한 칸 (모르는 값이면 오른 = 첫째) — tabs 는 머리 칸을 옮긴다
        const i32 cur = selectedIndex(n);
        const i32 next = cur < 0 ? 0 : std::clamp(cur + (key == K::Right ? 1 : -1), 0, count - 1);
        select(n, next, out);
        return true;
    }
    case K::Activate:
        if (n.kind == ui::UiKind::Dropdown) {
            ++m_presses;
            openList(l, document);
        }
        return true;
    default:
        return false; // 위 · 아래 = 탐색, Esc = 포커스 놓기, Tab = 차례
    }
}

bool GameUiLayer::disabled(const ui::UiNode& n) const {
    const std::optional<f64> v = ui::evaluate(n.disabled, m_data).asNumber();
    return v.has_value() && *v != 0.0;
}

f32 GameUiLayer::controlValue(const ui::UiNode& n) const {
    // 세션의 var 표가 먼저 (client 스크립트도 같은 키를 쓴다), 모르면 이 층이 쓴 값 (세션 없는 시험), 그것도 없으면 0 ·
    // min
    if (const std::optional<f64> v = ui::evaluate(n.value, m_data).asNumber()) {
        return static_cast<f32>(*v);
    }
    if (n.value.bind.starts_with("var:")) {
        if (const auto it = m_written.find(std::string_view(n.value.bind).substr(4)); it != m_written.end()) {
            return static_cast<f32>(it->second.asNumber().value_or(0));
        }
    }
    return n.kind == ui::UiKind::Slider ? n.min : 0.0f;
}

std::string GameUiLayer::textValue(const ui::UiNode& n) const {
    // 세션의 var 표가 먼저 (수 · 참거짓은 보이는 글자로), 모르면 이 층이 쓴 값, 그것도 없으면 빈 칸
    if (const ui::UiScalar v = ui::evaluate(n.value, m_data); v.known()) {
        return v.kind == ui::UiScalar::Kind::Text ? v.text : v.display();
    }
    if (n.value.bind.starts_with("var:")) {
        if (const auto it = m_written.find(std::string_view(n.value.bind).substr(4)); it != m_written.end()) {
            return it->second.kind == ui::UiScalar::Kind::Text ? it->second.text : it->second.display();
        }
    }
    return {};
}

void GameUiLayer::setTextValue(const ui::UiNode& n, std::string value) {
    if (!n.value.bind.starts_with("var:")) {
        return;
    }
    const std::string key = n.value.bind.substr(4);
    const ui::UiScalar v = ui::UiScalar::of(std::move(value));
    m_written.insert_or_assign(key, v);
    if (m_writeVar) {
        m_writeVar(key, v);
    }
}

bool GameUiLayer::editingText() const {
    return m_focus != nullptr && m_focus->kind == ui::UiKind::TextInput && !disabled(*m_focus);
}

void GameUiLayer::setFocus(const ui::UiNode* n, i32 listIndex, std::vector<ui::UiAction>* out) {
    if (m_focus != nullptr && m_focus->kind == ui::UiKind::TextInput && out != nullptr &&
        textValue(*m_focus) != m_editStart) {
        emitChange(*m_focus, *out);
    }
    if (m_open != nullptr && (m_open != n || m_openIndex != listIndex)) {
        closeList();
    }
    m_focus = n;
    m_focusIndex = listIndex;
    if (n != nullptr && n->kind == ui::UiKind::TextInput) {
        m_editStart = textValue(*n);
        m_caret = ui::codepointCount(m_editStart); // 캐럿은 끝에 (클릭 자리로 옮기기는 [계획])
    }
}

bool GameUiLayer::text(std::string_view utf8) {
    if (!editingText()) {
        return false;
    }
    std::string value = textValue(*m_focus);
    if (ui::insertText(value, m_caret, utf8, m_focus->maxLength, m_focus->multiline)) {
        setTextValue(*m_focus, std::move(value));
    }
    return true;
}

void GameUiLayer::setControlValue(const ui::UiNode& n, f32 value) {
    if (!n.value.bind.starts_with("var:")) {
        return;
    }
    const std::string key = n.value.bind.substr(4);
    // checkbox · toggle 은 참거짓, slider · dropdown · tabs 는 수 (번호)
    const bool flag = n.kind == ui::UiKind::Checkbox || n.kind == ui::UiKind::Toggle;
    const ui::UiScalar v = flag ? ui::UiScalar::ofBool(value != 0.0f) : ui::UiScalar::of(static_cast<f64>(value));
    m_written.insert_or_assign(key, v);
    if (m_writeVar) {
        m_writeVar(key, v);
    }
}

void GameUiLayer::press(const ui::UiLayoutNode& l, usize document, std::vector<ui::UiAction>& out) {
    const ui::UiNode& n = *l.node;
    switch (n.kind) {
    case ui::UiKind::Button:
        if (n.click.kind == ui::UiAction::Kind::Ui) {
            apply(n.click);
        } else if (n.click.kind != ui::UiAction::Kind::None) {
            out.push_back(n.click);
        }
        return;
    case ui::UiKind::Checkbox:
    case ui::UiKind::Toggle:
        setControlValue(n, controlValue(n) != 0.0f ? 0.0f : 1.0f);
        emitChange(n, out);
        return;
    case ui::UiKind::Dropdown:
        openList(l, document); // 고르기는 목록에서 (route 의 열린 목록 · 키)
        return;
    default:
        return;
    }
}

void GameUiLayer::sliderTo(const ui::UiNode& n, const ui::UiRect& rect, i32 x) {
    // 그리기와 같은 안쪽 (여백을 정수로 — UiDraw 의 inset)
    const ui::UiLayoutNode* l = layoutOf(&n, m_dragIndex);
    const f32 inner = l != nullptr ? static_cast<f32>(static_cast<i32>(l->padding + 0.5f)) : 0.0f;
    const f32 x0 = static_cast<f32>(rect.x) + inner;
    const f32 w = std::max(1.0f, static_cast<f32>(rect.w) - 2 * inner);
    const f32 t = std::clamp((static_cast<f32>(x) - x0) / w, 0.0f, 1.0f);
    f32 v = n.min + t * (n.max.number - n.min);
    if (n.step > 0) {
        v = n.min + std::round((v - n.min) / n.step) * n.step;
        v = std::min(v, n.max.number);
    }
    if (v != controlValue(n)) {
        setControlValue(n, v);
    }
}

void GameUiLayer::emitChange(const ui::UiNode& n, std::vector<ui::UiAction>& out) const {
    if (n.change.kind != ui::UiAction::Kind::Signal) {
        return;
    }
    ui::UiAction a = n.change;
    if (n.kind == ui::UiKind::TextInput) { // 글자는 클라이언트의 var 에 — 신호의 값은 코드 포인트 수 (ADR-0104)
        a.value = static_cast<i32>(ui::codepointCount(textValue(n)));
        out.push_back(std::move(a));
        return;
    }
    // 값은 컨트롤이 (ADR-0103 2): checkbox · toggle 0 · 1, slider 는 반올림한 i32 (범위 밖은 자른다)
    const f64 v = std::round(static_cast<f64>(controlValue(n)));
    a.value = static_cast<i32>(std::clamp(v, -2147483648.0, 2147483647.0));
    out.push_back(std::move(a));
}

const ui::UiLayoutNode* GameUiLayer::layoutOf(const ui::UiNode* n, i32 listIndex) const {
    for (const ui::UiDocumentLayout& d : m_layouts) {
        for (const ui::UiLayoutNode& l : d.nodes) {
            if (l.node == n && l.listIndex == listIndex) {
                return &l;
            }
        }
    }
    return nullptr;
}

bool GameUiLayer::editText(const ui::UiNode& n, ui::UiKey key, std::vector<ui::UiAction>& out) {
    using K = ui::UiKey;
    std::string value = textValue(n);
    const auto move = [&](ui::UiCaretMove m) { return ui::moveCaret(value, m_caret, m); };
    switch (key) {
    case K::Left:
        (void)move(ui::UiCaretMove::Left);
        return true;
    case K::Right:
        (void)move(ui::UiCaretMove::Right);
        return true;
    case K::Up:
    case K::Down:
        // multiline 의 줄 — 첫 · 끝 줄 (또는 한 줄 칸) 이면 다른 컨트롤로 (탐색)
        return n.multiline && move(key == K::Up ? ui::UiCaretMove::Up : ui::UiCaretMove::Down);
    case K::Home:
        (void)move(ui::UiCaretMove::LineStart);
        return true;
    case K::End:
        (void)move(ui::UiCaretMove::LineEnd);
        return true;
    case K::Backspace:
    case K::Delete:
        if (!disabled(n) && ui::eraseText(value, m_caret, key == K::Delete)) {
            setTextValue(n, std::move(value));
        }
        return true;
    case K::Activate:
        if (disabled(n)) {
            return true;
        }
        if (n.multiline) {
            if (ui::insertText(value, m_caret, "\n", n.maxLength, true)) {
                setTextValue(n, std::move(value));
            }
            return true;
        }
        ++m_presses;
        emitChange(n, out); // 보내기 — 같은 글이라도 (채팅의 같은 말 두 번)
        m_editStart = value;
        return true;
    default:
        return false; // Next · Previous · Release — 포커스를 옮기는 길 (떠날 때 보내기)
    }
}

bool GameUiLayer::key(ui::UiKey key, std::vector<ui::UiAction>& out) {
    using K = ui::UiKey;
    const std::vector<ui::UiFocusable> order =
        ui::focusOrder(m_layouts, [this](const ui::UiNode& n, i32) { return disabled(n); });
    // 지금 포커스의 차례 (보이지 않게 되었거나 꺼졌으면 놓는다)
    i32 at = -1;
    for (usize i = 0; i < order.size(); ++i) {
        if (order[i].node->node == m_focus && order[i].node->listIndex == m_focusIndex) {
            at = static_cast<i32>(i);
        }
    }
    if (at < 0) {
        setFocus(nullptr, -1, nullptr); // 사라진 칸 — 보낼 것이 없다 (노드가 보이지 않는다)
    }
    const auto focusAt = [&](usize i) {
        setFocus(order[i].node->node, order[i].node->listIndex, &out);
        revealFocus(); // scroll 안이면 보이게 (다음 배치부터 — v0.7 NG-06 (3b))
    };
    if (key == K::Next || key == K::Previous) {
        if (order.empty()) {
            return false;
        }
        const usize n = order.size();
        if (at < 0) {
            focusAt(key == K::Next ? 0 : n - 1);
        } else {
            focusAt(key == K::Next ? (static_cast<usize>(at) + 1) % n : (static_cast<usize>(at) + n - 1) % n);
        }
        return true;
    }
    if (at < 0) {
        return false; // 포커스가 없으면 방향 · 누르기 · 놓기는 게임의 것
    }
    const ui::UiLayoutNode& l = *order[static_cast<usize>(at)].node;
    if (l.node->kind == ui::UiKind::TextInput && editText(*l.node, key, out)) {
        return true;
    }
    if ((l.node->kind == ui::UiKind::Dropdown || l.node->kind == ui::UiKind::Tabs) &&
        choiceKey(l, order[static_cast<usize>(at)].document, key, out)) {
        return true;
    }
    switch (key) {
    case K::Release:
        setFocus(nullptr, -1, &out);
        return true;
    case K::Activate:
        ++m_presses;
        if (l.node->kind == ui::UiKind::Slider) {
            return true;
        }
        press(l, order[static_cast<usize>(at)].document, out);
        return true;
    case K::Left:
    case K::Right:
        if (l.node->kind == ui::UiKind::Slider) {
            const ui::UiNode& n = *l.node;
            const f32 step = n.step > 0 ? n.step : (n.max.number - n.min) / 20.0f;
            const f32 v = std::clamp(controlValue(n) + (key == K::Right ? step : -step), n.min, n.max.number);
            setControlValue(n, v);
            emitChange(n, out);
            return true;
        }
        [[fallthrough]];
    case K::Up:
    case K::Down:
        if (const i32 to = ui::nearestFocus(order, static_cast<usize>(at), key); to >= 0) {
            focusAt(static_cast<usize>(to));
        }
        return true;
    default:
        return true;
    }
}

void GameUiLayer::draw(ui::IUiRenderer& renderer, const ui::ITextMeasure* measure) const {
    ui::UiDrawOptions o{.styles = &m_styles, .pressed = m_pressed, .pressedIndex = m_pressedIndex, .scale = m_scale};
    o.textOf = [this](const ui::UiNode& n, i32) { return ui::formatText(n, m_data, &m_strings); };
    o.progressOf = [this](const ui::UiNode& n, i32) { return ui::progressOf(n, m_data); };
    o.valueOf = [this](const ui::UiNode& n, i32) { return controlValue(n); };
    o.disabledOf = [this](const ui::UiNode& n, i32) { return disabled(n); };
    o.focused = m_focus;
    o.focusedIndex = m_focusIndex;
    o.textValueOf = [this](const ui::UiNode& n, i32) { return textValue(n); };
    o.caret = m_caret;
    o.measure = measure;
    o.optionOf = [this](const ui::UiNode& n, usize i) { return optionText(n, i); };
    for (const ui::UiDocumentLayout& l : m_layouts) {
        if (l.visible) {
            ui::drawLayout(l.nodes, o, renderer);
        }
    }
    // 열린 dropdown 목록 — 모든 문서 위 (v0.7 NG-06 (3))
    if (const ui::UiLayoutNode* l = m_open != nullptr ? layoutOf(m_open, m_openIndex) : nullptr) {
        std::vector<std::string> texts;
        for (usize i = 0; i < m_open->options.size(); ++i) {
            texts.push_back(optionText(*m_open, i));
        }
        ui::drawOptionList(ui::optionListRect(l->rect, texts.size(), m_height), texts, selectedIndex(*m_open),
                           m_highlight, l->fontSize, m_scale, renderer);
    }
}

bool GameUiLayer::setTheme(std::string_view theme) {
    if (theme.empty() || theme == ui::kNoTheme) {
        m_theme.clear();
        m_styles = m_baseStyles;
        return true;
    }
    const auto it = m_themes.find(theme);
    if (it == m_themes.end()) {
        return false;
    }
    m_theme = std::string(theme);
    m_styles = ui::applyTheme(m_baseStyles, &it->second); // 다음 배치부터 — 색만 바꾼 테마면 사각형은 그대로
    return true;
}

void GameUiLayer::apply(const ui::UiAction& action) {
    if (action.kind != ui::UiAction::Kind::Ui) {
        return;
    }
    if (action.op == "theme") {
        (void)setTheme(action.target);
        return;
    }
    // 대상이 문서 id 와 같으면 문서, 아니면 "<문서>.<노드>" (문서 id 에도 점이 있다 — 문서 id 로 앞을 맞춘다)
    for (usize i = 0; i < m_docs.size(); ++i) {
        const std::string& id = m_docs[i].id;
        if (action.target == id) {
            m_visible[i] = action.op == "show" || (action.op == "toggle" && !m_visible[i]);
            return;
        }
        if (action.target.size() > id.size() + 1 && action.target.starts_with(id) && action.target[id.size()] == '.') {
            const bool hidden = m_hidden.contains(action.target);
            const bool hide = action.op == "hide" || (action.op == "toggle" && !hidden);
            if (hide) {
                m_hidden.insert(action.target);
            } else {
                m_hidden.erase(action.target);
            }
            return;
        }
    }
}

bool GameUiLayer::documentVisible(std::string_view id) const {
    for (usize i = 0; i < m_docs.size(); ++i) {
        if (m_docs[i].id == id) {
            return m_visible[i];
        }
    }
    return false;
}

bool GameUiLayer::nodeHidden(std::string_view documentId, std::string_view nodeId) const {
    return m_hidden.contains(std::format("{}.{}", documentId, nodeId));
}

std::vector<std::string> GameUiLayer::takeWarnings() {
    std::vector<std::string> out;
    out.swap(m_warnings);
    for (const std::string& key : m_strings.takeMissing()) {
        out.push_back(std::format("문자열 키 '{}' 가 어느 표에도 없다 — 키를 그대로 보인다 (sbx check)", key));
    }
    return out;
}

} // namespace sbx::client
