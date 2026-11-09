#include "apps/client/presentation/GameUiLayer.hpp"

#include <format>

namespace sbx::client {

void GameUiLayer::setSources(const std::optional<GameUiInfo>& info) {
    std::string signature;
    if (info) {
        for (const auto* list : {&info->styles, &info->documents}) {
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
    m_docs.clear();
    m_visible.clear();
    m_hidden.clear();
    m_layouts.clear();
    m_pressed = nullptr;
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
    m_layouts.clear();
    m_layouts.reserve(m_docs.size());
    for (usize i = 0; i < m_docs.size(); ++i) {
        const ui::UiDocument& d = m_docs[i];
        m_scale = scale;
        ui::UiLayoutOptions o{.width = width, .height = height, .scale = scale, .styles = &m_styles};
        o.textOf = [this](const ui::UiNode& n, i32) { return ui::formatText(n, m_data); };
        o.listCount = [this](const ui::UiNode& n) { return ui::listCountOf(n, m_data); };
        o.hidden = [this, &d](const ui::UiNode& n) { return !n.id.empty() && nodeHidden(d.id, n.id); };
        // 숨긴 문서는 배치하지 않는다 (라우팅 · 그리기 모두 건너뛴다)
        m_layouts.push_back(ui::UiDocumentLayout{
            &d, m_visible[i] ? ui::layoutDocument(d, o, measure) : std::vector<ui::UiLayoutNode>{}, m_visible[i]});
    }
}

ui::UiPointerRoute GameUiLayer::route(i32 x, i32 y, bool toolsWantMouse, bool pressed, bool down,
                                      std::vector<ui::UiAction>& out) {
    const ui::UiPointerRoute r = ui::routePointer(m_layouts, x, y, toolsWantMouse);
    if (!down) {
        m_pressed = nullptr;
    }
    if (r.button != nullptr && pressed) {
        m_pressed = r.button->node;
        m_pressedIndex = r.button->listIndex;
        ++m_presses;
        const ui::UiAction& a = r.button->node->click;
        if (a.kind == ui::UiAction::Kind::Ui) {
            apply(a);
        } else if (a.kind != ui::UiAction::Kind::None) {
            out.push_back(a);
        }
    }
    return r;
}

void GameUiLayer::draw(ui::IUiRenderer& renderer) const {
    ui::UiDrawOptions o{.styles = &m_styles, .pressed = m_pressed, .pressedIndex = m_pressedIndex, .scale = m_scale};
    o.textOf = [this](const ui::UiNode& n, i32) { return ui::formatText(n, m_data); };
    o.progressOf = [this](const ui::UiNode& n, i32) { return ui::progressOf(n, m_data); };
    for (const ui::UiDocumentLayout& l : m_layouts) {
        if (l.visible) {
            ui::drawLayout(l.nodes, o, renderer);
        }
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
    return out;
}

} // namespace sbx::client
