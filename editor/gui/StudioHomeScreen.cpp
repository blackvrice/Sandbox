#include "editor/gui/StudioHomeScreen.hpp"

#include <algorithm>
#include <format>

#include "editor/services/ProjectHome.hpp"

namespace sbx::editor::gui {

using services::displayTime;
using services::filterRecent;
using services::suggestGameId;
using services::suggestProjectDir;

namespace {

// 색 — Studio 어두운 테마 (editor/ui/StudioTheme 의 Dark 와 같은 계열 · 토큰 공유는 UI-R2-03 의 GCSS 테마)
constexpr gui::RenderColor kBackground{24, 26, 30, 255};
constexpr gui::RenderColor kPanel{30, 33, 39, 255};
constexpr gui::RenderColor kText{232, 234, 238, 255};
constexpr gui::RenderColor kMuted{150, 156, 168, 255};
constexpr gui::RenderColor kRow{36, 39, 46, 255};
constexpr gui::RenderColor kRowHover{48, 53, 63, 255};
constexpr gui::RenderColor kRowSelected{44, 62, 98, 255};
constexpr gui::RenderColor kFocus{86, 140, 255, 255};
constexpr gui::RenderColor kAccent{58, 110, 220, 255};
constexpr gui::RenderColor kAccentHover{78, 130, 240, 255};
constexpr gui::RenderColor kNeutral{60, 64, 74, 255};
constexpr gui::RenderColor kNeutralHover{76, 81, 93, 255};
constexpr gui::RenderColor kFieldBg{18, 20, 24, 255};
constexpr gui::RenderColor kSelection{58, 92, 160, 255};
constexpr gui::RenderColor kError{240, 112, 104, 255};
constexpr gui::RenderColor kWarning{232, 180, 80, 255};
constexpr gui::RenderColor kOk{120, 200, 140, 255};

constexpr double kPad = 12.0;
constexpr double kFieldPad = 8.0;
constexpr double kFieldEm = 15.0;

// GraphicsUI 그리기 명령은 0 이상 크기만 받는다 — 좁은 화면에서 안쪽 여백을 빼면 음수가 된다
double fit(double v) {
    return v > 0.0 ? v : 0.0;
}

gui::LayoutNode::Intrinsic textIntrinsic(const uihost::ITextMeasure& text, std::string s, double em) {
    return [&text, s = std::move(s), em](const gui::LayoutConstraints&) -> gui::Result<gui::LayoutSize> {
        return text.measure(s, em);
    };
}

bool isKeyDown(const gui::RoutedEvent& ev, gui::KeyCode key) {
    const auto* k = std::get_if<gui::KeyEvent>(&ev.input);
    return k != nullptr && k->action == gui::KeyAction::down && k->chord.key == key &&
           k->chord.modifiers == gui::KeyModifiers::none;
}

std::string pathText(const std::filesystem::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

std::filesystem::path pathOf(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace

void StudioHomeScreen::setModel(StudioHomeModel model) {
    m_model = std::move(model);
    m_model.recovery.resize(m_model.recent.size()); // 짝이 맞지 않으면 복구 없음으로
}

void StudioHomeScreen::setStatus(std::string message, bool failed) {
    m_status = std::move(message);
    m_failed = failed;
}

std::optional<StudioHomeIntent> StudioHomeScreen::takeIntent() {
    auto out = std::move(m_intent);
    m_intent.reset();
    return out;
}

std::optional<usize> StudioHomeScreen::selectedRecent() const noexcept {
    const auto key = m_list.selection();
    return key ? std::optional<usize>(static_cast<usize>(*key)) : std::nullopt;
}

StudioHomeScreen::Field& StudioHomeScreen::field(Part part) {
    for (Field& f : m_fields) {
        if (f.part == part) {
            return f;
        }
    }
    return m_fields.front(); // 모든 칸을 mount 가 만든다 — 여기 오지 않는다
}

const std::string& StudioHomeScreen::fieldText(Part part) const {
    static const std::string kEmpty;
    for (const Field& f : m_fields) {
        if (f.part == part) {
            return f.value;
        }
    }
    return kEmpty;
}

const StudioHomeScreen::Node* StudioHomeScreen::nodeOf(gui::ComponentHandle h) const {
    const auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const Node& n) { return n.handle == h; });
    return it == m_nodes.end() ? nullptr : &*it;
}

std::optional<gui::ComponentHandle> StudioHomeScreen::handleOf(Part part) const {
    for (const Node& n : m_nodes) {
        bool match = false;
        switch (n.role) {
        case Role::Field:
            match = m_fields[n.index].part == part;
            break;
        case Role::List:
            match = part == Part::List;
            break;
        case Role::Forget:
            match = part == Part::Forget;
            break;
        case Role::Create:
            match = part == Part::Create;
            break;
        case Role::Open:
            match = part == Part::Open;
            break;
        case Role::CopyExample:
            match = part == Part::CopyExample;
            break;
        case Role::PickPrev:
            match = part == (n.index == kTemplatePicker ? Part::TemplatePrev : Part::ExamplePrev);
            break;
        case Role::PickNext:
            match = part == (n.index == kTemplatePicker ? Part::TemplateNext : Part::ExampleNext);
            break;
        case Role::Switch:
            match = part == Part::Switch;
            break;
        case Role::Density:
            match = part == Part::Density;
            break;
        case Role::Quit:
            match = part == Part::Quit;
            break;
        default:
            break;
        }
        if (match) {
            return n.handle;
        }
    }
    return std::nullopt;
}

std::string StudioHomeScreen::textOf(const Node& n) const {
    switch (n.role) {
    case Role::Title:
        return "Sandbox Studio";
    case Role::Version:
        return m_model.version.empty() ? std::string("GraphicsUI 미리보기")
                                       : std::format("엔진 {} · GraphicsUI 미리보기", m_model.version);
    case Role::Heading:
        return std::format("최근 프로젝트 ({} / {})", m_list.itemCount(), m_model.recent.size());
    case Role::NewHeading:
        return "새 프로젝트";
    case Role::OpenHeading:
        return "폴더 열기";
    case Role::ExampleHeading:
        return "예제 복사";
    case Role::PickPrev:
        return "‹";
    case Role::PickNext:
        return "›";
    case Role::PickLabel:
        if (n.index == kTemplatePicker) {
            if (m_model.templates.empty()) {
                return "템플릿 없음";
            }
            const auto& t = m_model.templates[m_template];
            const std::string name = t.id == "empty" ? std::string("빈 월드") : t.title.empty() ? t.id : t.title;
            return std::format("{} ({} / {})", name, m_template + 1, m_model.templates.size());
        }
        if (m_model.examples.empty()) {
            return "예제 없음";
        }
        return std::format("{} ({} / {})", m_model.examples[m_example].name, m_example + 1, m_model.examples.size());
    case Role::Forget:
        return "목록에서 빼기";
    case Role::Create:
        return "만들기";
    case Role::Open:
        return "열기";
    case Role::CopyExample:
        return "복사해서 열기";
    case Role::Message:
        return m_status.empty() ? std::string(" ") : m_status;
    case Role::Switch:
        return "기존 UI 로";
    case Role::Density:
        return m_compact ? "간격: 촘촘" : "간격: 보통";
    case Role::Quit:
        return "종료";
    case Role::Note:
        return "UI-R2 — GraphicsUI Core 의 배치 · 입력 · 그리기 명령. 그리기는 과도기 경로 (ImGui 렌더러)";
    default:
        return {};
    }
}

double StudioHomeScreen::emOf(Role role) const noexcept {
    const double s = m_compact ? 0.93 : 1.0; // 촘촘 — 글자 · 여백을 조금 줄인다 (ImGui 홈의 Density 와 같은 뜻)
    switch (role) {
    case Role::Title:
        return 28.0 * s;
    case Role::Version:
    case Role::Note:
    case Role::Message:
        return 13.0 * s;
    default:
        return 15.0 * s;
    }
}

void StudioHomeScreen::refilter() {
    const std::string& query = fieldText(Part::Search);
    const std::vector<usize> order = filterRecent(m_model.recent, query);
    std::vector<gui::ListItemKey> keys;
    keys.reserve(order.size());
    for (const usize i : order) {
        keys.push_back(static_cast<gui::ListItemKey>(i));
    }
    const auto prev = m_list.selection();
    (void)m_list.setItems(keys); // 키는 고유 (최근 목록의 차례) — 실패하지 않는다
    // 고른 것이 걸러져 나갔으면 (ListView 가 선택을 푼다) 첫 항목
    if (!m_list.selection() || m_list.selection() != prev) {
        (void)m_list.select(keys.empty() ? std::nullopt : std::optional<gui::ListItemKey>(keys.front()));
    }
    (void)m_list.scrollTo(0.0);
}

void StudioHomeScreen::suggestExampleDir() {
    if (m_example >= m_model.examples.size()) {
        return;
    }
    Field& dest = field(Part::ExampleDir);
    if (!dest.edited) {
        dest.value = pathText(suggestProjectDir(m_model.recent, m_model.examples[m_example].name));
        if (dest.input) {
            (void)dest.input->setText(dest.value);
        }
    }
}

void StudioHomeScreen::fieldChanged(Field& f) {
    switch (f.part) {
    case Part::Search:
        refilter();
        break;
    case Part::Name: {
        // ImGui 홈과 같은 제안 — 사용자가 고치지 않은 칸만 따라간다
        Field& id = field(Part::GameId);
        if (!id.edited) {
            id.value = suggestGameId(f.value);
            (void)id.input->setText(id.value);
        }
        Field& dir = field(Part::Dir);
        if (!dir.edited) {
            dir.value = pathText(suggestProjectDir(m_model.recent, f.value));
            (void)dir.input->setText(dir.value);
        }
        break;
    }
    default:
        break;
    }
}

void StudioHomeScreen::submit(Part from) {
    StudioHomeIntent intent;
    switch (from) {
    case Part::OpenDir:
    case Part::Open:
        intent.kind = StudioHomeIntent::Kind::OpenFolder;
        intent.dir = pathOf(field(Part::OpenDir).value);
        break;
    case Part::ExampleDir:
    case Part::CopyExample:
        if (m_example >= m_model.examples.size()) {
            return;
        }
        intent.kind = StudioHomeIntent::Kind::CopyExample;
        intent.dir = m_model.examples[m_example].dir;
        intent.dest = pathOf(field(Part::ExampleDir).value);
        break;
    default:
        intent.kind = StudioHomeIntent::Kind::NewProject;
        intent.dir = pathOf(field(Part::Dir).value);
        intent.gameId = field(Part::GameId).value;
        intent.name = field(Part::Name).value;
        if (m_template < m_model.templates.size()) {
            intent.templateId = m_model.templates[m_template].id;
        }
        break;
    }
    m_intent = std::move(intent);
}

void StudioHomeScreen::forgetSelected() {
    if (const auto sel = selectedRecent()) {
        StudioHomeIntent intent;
        intent.kind = StudioHomeIntent::Kind::ForgetRecent;
        intent.dir = m_model.recent[*sel].dir;
        m_intent = std::move(intent);
    }
}

void StudioHomeScreen::activate(const Node& n) {
    const auto step = [](usize at, usize count, bool forward) -> usize {
        if (count == 0) {
            return 0;
        }
        return forward ? (at + 1) % count : (at + count - 1) % count; // 끝에서 처음으로 돈다
    };
    switch (n.role) {
    case Role::PickPrev:
    case Role::PickNext: {
        const bool forward = n.role == Role::PickNext;
        if (n.index == kTemplatePicker) {
            m_template = step(m_template, m_model.templates.size(), forward);
        } else {
            m_example = step(m_example, m_model.examples.size(), forward);
            suggestExampleDir();
        }
        break;
    }
    case Role::Forget:
        forgetSelected();
        break;
    case Role::Create:
        submit(Part::Create);
        break;
    case Role::Open:
        submit(Part::Open);
        break;
    case Role::CopyExample:
        submit(Part::CopyExample);
        break;
    case Role::Switch:
        m_intent = StudioHomeIntent{StudioHomeIntent::Kind::SwitchToClassic, {}, {}, {}, {}, {}, false};
        break;
    case Role::Density:
        m_intent = StudioHomeIntent{StudioHomeIntent::Kind::Density, {}, {}, {}, {}, {}, !m_compact};
        break;
    default:
        m_intent = StudioHomeIntent{StudioHomeIntent::Kind::Quit, {}, {}, {}, {}, {}, false};
        break;
    }
}

void StudioHomeScreen::moveSelection(std::ptrdiff_t delta, bool toEnd, bool toStart) {
    const usize n = m_list.itemCount();
    if (n == 0) {
        return;
    }
    std::ptrdiff_t at = 0;
    if (const auto key = m_list.selection()) {
        at = static_cast<std::ptrdiff_t>(m_list.indexOf(*key).value_or(0));
    }
    at = toStart ? 0 : toEnd ? static_cast<std::ptrdiff_t>(n) - 1 : at + delta;
    at = std::clamp<std::ptrdiff_t>(at, 0, static_cast<std::ptrdiff_t>(n) - 1);
    const auto key = m_list.itemKey(static_cast<usize>(at));
    (void)m_list.select(key);
    if (key) {
        (void)m_list.reveal(*key);
    }
}

gui::EventReply StudioHomeScreen::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    const Node* list = nodeOf(ev.receiver);
    if (list == nullptr) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        const gui::LayoutPoint local{p->x - list->bounds.x, p->y - list->bounds.y};
        const auto row = m_list.hitTest(local);
        switch (p->action) {
        case gui::PointerAction::down:
            if (p->button != gui::PointerButton::primary) {
                return {};
            }
            (void)m_host->input().setFocus(list->handle);
            (void)m_host->input().capturePointer(p->pointer, list->handle);
            m_pressKey = row ? std::optional<gui::ListItemKey>(row->key) : std::nullopt;
            if (row) {
                (void)m_list.select(row->key);
            }
            return {true, true};
        case gui::PointerAction::up: {
            (void)m_host->input().releasePointer(p->pointer);
            const bool same = row && m_pressKey && row->key == *m_pressKey;
            m_pressKey.reset();
            if (same && p->button == gui::PointerButton::primary) {
                StudioHomeIntent intent;
                intent.kind = StudioHomeIntent::Kind::OpenRecent;
                intent.dir = m_model.recent[static_cast<usize>(row->key)].dir;
                m_intent = std::move(intent);
            }
            return {true, true};
        }
        case gui::PointerAction::cancel:
            m_pressKey.reset();
            return {true, false};
        default:
            return {};
        }
    }
    const auto& k = std::get<gui::KeyEvent>(ev.input);
    if (k.action != gui::KeyAction::down || k.chord.modifiers != gui::KeyModifiers::none) {
        return {};
    }
    switch (k.chord.key) {
    case gui::up_key:
        moveSelection(-1);
        return {true, true};
    case gui::down_key:
        moveSelection(+1);
        return {true, true};
    case gui::home_key:
        moveSelection(0, false, true);
        return {true, true};
    case gui::end_key:
        moveSelection(0, true, false);
        return {true, true};
    case gui::delete_key: // ImGui 홈의 카드 메뉴 "목록에서 빼기" 와 같다
        if (!k.repeat) {
            forgetSelected();
        }
        return {true, true};
    case gui::enter_key:
        if (const auto sel = selectedRecent(); sel && !k.repeat) {
            StudioHomeIntent intent;
            intent.kind = StudioHomeIntent::Kind::OpenRecent;
            intent.dir = m_model.recent[*sel].dir;
            m_intent = std::move(intent);
        }
        return {true, true};
    default:
        return {};
    }
}

gui::EventReply StudioHomeScreen::fieldEvent(usize fieldIndex, const gui::RoutedEvent& ev) {
    Field& f = m_fields[fieldIndex];
    const auto publish = [this, fieldIndex](std::string_view v) -> gui::Result<void> {
        Field& target = m_fields[fieldIndex];
        target.value = std::string(v);
        target.edited = true;
        fieldChanged(target);
        return {};
    };
    const auto r = f.input->handle(ev, m_host->editCallbacks(publish));
    // 눌러 캐럿 놓기 — Core 의 TextInput 은 포커스만 잡는다, 자리는 호스트의 글자 배치가 안다 (그래핌 경계)
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input);
        p != nullptr && p->action == gui::PointerAction::down && ev.phase == gui::EventPhase::target) {
        if (const Node* n = nodeOf(ev.receiver)) {
            const usize at = uihost::caretFromX(m_host->text(), f.value, kFieldEm, p->x - n->bounds.x - kFieldPad);
            if (auto session = f.input->beginInput(); session && *session) {
                (void)f.input->setSelection(**session, {at, at});
            }
        }
    }
    if (r && r->handled) {
        return *r;
    }
    // Enter = 그 폼의 단추 (검색 칸은 목록의 고른 것을 연다)
    if (ev.phase == gui::EventPhase::target && isKeyDown(ev, gui::enter_key)) {
        if (f.part == Part::Search) {
            if (const auto sel = selectedRecent()) {
                StudioHomeIntent intent;
                intent.kind = StudioHomeIntent::Kind::OpenRecent;
                intent.dir = m_model.recent[*sel].dir;
                m_intent = std::move(intent);
            }
        } else {
            submit(f.part);
        }
        return {true, true};
    }
    // 검색 칸의 ↑↓ 는 목록을 움직인다 (글자 칸을 떠나지 않고 고른다)
    if (f.part == Part::Search && ev.phase == gui::EventPhase::target) {
        if (isKeyDown(ev, gui::down_key)) {
            moveSelection(+1);
            return {true, true};
        }
        if (isKeyDown(ev, gui::up_key)) {
            moveSelection(-1);
            return {true, true};
        }
    }
    return r ? *r : gui::EventReply{};
}

Expected<void> StudioHomeScreen::mount(uihost::UiHost& host) {
    m_host = &host;
    m_nodes.clear();
    m_intent.reset();
    m_pressKey.reset();
    if (m_template >= m_model.templates.size()) {
        m_template = 0;
    }
    if (m_example >= m_model.examples.size()) {
        m_example = 0;
    }
    if (m_fields.empty()) { // 처음 — 다시 마운트할 때는 쓰던 글자를 남긴다
        const auto make = [&](Part part, std::string placeholder, std::string value = {}) {
            Field f;
            f.part = part;
            f.placeholder = std::move(placeholder);
            f.value = std::move(value);
            m_fields.push_back(std::move(f));
        };
        make(Part::Search, "검색 — 이름 · gameId · 경로");
        make(Part::Name, "이름 (예: 토끼 지키기)");
        make(Part::GameId, "gameId — 작성자.게임 (예: me.rabbit)");
        make(Part::Dir, "폴더", pathText(suggestProjectDir(m_model.recent, "")));
        make(Part::OpenDir, "프로젝트 폴더 (sandbox.project.json 이 있는 곳)");
        make(Part::ExampleDir, "복사할 곳 (없거나 빈 폴더)");
    }

    // 부모 우선 게시 목록 — 타입 = 역할, 키 = 같은 부모 안의 차례
    std::vector<gui::ComponentSpec> specs;
    const auto push = [&](std::optional<usize> parent, Role role, gui::ComponentKey key, usize index = 0) {
        specs.push_back({parent, static_cast<gui::ComponentType>(role) + 1, key, true});
        Node n;
        n.role = role;
        n.index = index;
        m_nodes.push_back(std::move(n));
        return specs.size() - 1;
    };
    const auto fieldIndex = [&](Part p) {
        return static_cast<usize>(std::find_if(m_fields.begin(), m_fields.end(),
                                               [&](const Field& f) { return f.part == p; }) -
                                  m_fields.begin());
    };
    const auto picker = [&](usize parent, gui::ComponentKey key, usize which) {
        const usize row = push(parent, Role::Picker, key, which);
        push(row, Role::PickPrev, 1, which);
        push(row, Role::PickLabel, 2, which);
        push(row, Role::PickNext, 3, which);
    };
    const usize root = push(std::nullopt, Role::Root, 0);
    push(root, Role::Title, 1);
    push(root, Role::Version, 2);
    const usize body = push(root, Role::Body, 3);
    const usize left = push(body, Role::Left, 1);
    push(left, Role::Heading, 1);
    push(left, Role::Field, 2, fieldIndex(Part::Search));
    push(left, Role::List, 3);
    const usize listBar = push(left, Role::ListBar, 4);
    push(listBar, Role::Forget, 1);
    const usize right = push(body, Role::Right, 2);
    push(right, Role::NewHeading, 1);
    picker(right, 2, kTemplatePicker);
    push(right, Role::Field, 3, fieldIndex(Part::Name));
    push(right, Role::Field, 4, fieldIndex(Part::GameId));
    push(right, Role::Field, 5, fieldIndex(Part::Dir));
    push(right, Role::Create, 6);
    push(right, Role::OpenHeading, 7);
    push(right, Role::Field, 8, fieldIndex(Part::OpenDir));
    push(right, Role::Open, 9);
    if (!m_model.examples.empty()) { // 예제가 없는 실행 (samples 폴더 없음) 이면 칸이 없다
        push(right, Role::ExampleHeading, 10);
        picker(right, 11, kExamplePicker);
        push(right, Role::Field, 12, fieldIndex(Part::ExampleDir));
        push(right, Role::CopyExample, 13);
    }
    push(root, Role::Message, 4);
    const usize bar = push(root, Role::Bar, 5);
    push(bar, Role::Switch, 1);
    push(bar, Role::Density, 2);
    push(bar, Role::Quit, 3);
    push(root, Role::Note, 6);

    if (auto r = host.tree().commit(specs); !r) {
        return std::unexpected(uihost::toError(r.error(), "홈 트리 게시"));
    }
    auto snapshot = host.tree().snapshot();
    if (!snapshot || snapshot->components.size() != m_nodes.size()) {
        return makeError(ErrorCode::Unknown, "GraphicsUI 홈: 게시한 트리를 읽지 못했다");
    }
    const auto check = [](gui::Result<void> r, std::string_view what) -> Expected<void> {
        if (!r) {
            return std::unexpected(uihost::toError(r.error(), what));
        }
        return {};
    };
    std::optional<gui::ComponentHandle> searchHandle;
    for (usize i = 0; i < m_nodes.size(); ++i) {
        Node& n = m_nodes[i];
        n.handle = snapshot->components[i].handle; // snapshot 은 게시 순서 그대로
        switch (n.role) {
        case Role::Field: {
            Field& f = m_fields[n.index];
            f.input = std::make_unique<gui::TextInput>(host.input(), n.handle, 512);
            if (auto r = check(f.input->setEnabled(true), "TextInput::setEnabled"); !r) {
                return r;
            }
            if (auto r = check(f.input->setText(f.value), "TextInput::setText"); !r) {
                return r;
            }
            const usize fi = n.index;
            if (auto r = check(host.input().setHandler(n.handle,
                                                       [this, fi](const gui::RoutedEvent& ev) {
                                                           return fieldEvent(fi, ev);
                                                       }),
                               "setHandler");
                !r) {
                return r;
            }
            uihost::TextTarget target;
            target.insert = [this, fi](std::string_view utf8) {
                Field& tf = m_fields[fi];
                if (auto session = tf.input->beginInput(); session && *session) {
                    (void)tf.input->edit(**session, {gui::TextEditAction::replace_selection, utf8},
                                         m_host->editCallbacks([this, fi](std::string_view v) -> gui::Result<void> {
                                             Field& pf = m_fields[fi];
                                             pf.value = std::string(v);
                                             pf.edited = true;
                                             fieldChanged(pf);
                                             return {};
                                         }));
                }
            };
            target.caret = [this, fi]() -> std::optional<gui::LayoutRect> {
                const Field& cf = m_fields[fi];
                const usize caret = std::min(cf.input->selection().caret, cf.value.size());
                const double x =
                    kFieldPad + m_host->text().measure(std::string_view(cf.value).substr(0, caret), kFieldEm).width;
                return gui::LayoutRect{x, kFieldPad, 1.0, kFieldEm * 1.25};
            };
            if (auto r = host.setTextTarget(n.handle, std::move(target)); !r) {
                return r;
            }
            if (f.part == Part::Search) {
                searchHandle = n.handle;
            }
            break;
        }
        case Role::List:
            if (auto r = check(host.input().setFocusable(n.handle, true), "setFocusable"); !r) {
                return r;
            }
            if (auto r = check(host.input().setHandler(n.handle,
                                                       [this](const gui::RoutedEvent& ev) { return listEvent(ev); }),
                               "setHandler");
                !r) {
                return r;
            }
            if (auto r = host.setWheelHandler(n.handle,
                                              [this](f32, f32 dy) {
                                                  // 한 칸 = 세 줄 (y 양수 = 위로 → 내용은 아래로 = 오프셋 감소)
                                                  (void)m_list.scrollBy(-static_cast<double>(dy) * rowHeight() * 3.0);
                                                  return true;
                                              });
                !r) {
                return r;
            }
            break;
        case Role::Forget:
        case Role::PickPrev:
        case Role::PickNext:
        case Role::Create:
        case Role::Open:
        case Role::CopyExample:
        case Role::Switch:
        case Role::Density:
        case Role::Quit: {
            n.button = std::make_unique<gui::Button>(host.input(), n.handle);
            if (auto r = check(n.button->setEnabled(true), "Button::setEnabled"); !r) {
                return r;
            }
            const usize ni = i;
            if (auto r = check(host.input().setHandler(n.handle,
                                                       [this, ni](const gui::RoutedEvent& ev) -> gui::EventReply {
                                                           Node& self = m_nodes[ni];
                                                           const auto b = self.button->handle(ev);
                                                           if (!b) {
                                                               return {};
                                                           }
                                                           if (b->activated) {
                                                               activate(self);
                                                           }
                                                           return b->reply;
                                                       }),
                               "setHandler");
                !r) {
                return r;
            }
            break;
        }
        default:
            break;
        }
    }
    refilter();
    suggestExampleDir();
    // 첫 포커스 = 검색 칸 (바로 쓰고 ↑↓ · Enter 로 연다)
    if (searchHandle) {
        if (auto r = check(host.input().setFocus(searchHandle), "setFocus"); !r) {
            return r;
        }
    }
    return {};
}

Expected<void> StudioHomeScreen::layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree,
                                     std::vector<gui::LayoutNode>& out) {
    if (tree.components.size() != m_nodes.size()) {
        return makeError(ErrorCode::Unknown, "GraphicsUI 홈: 트리와 노드 수가 다르다");
    }
    const gui::LayoutSize vp = host.viewport();
    // 넓으면 두 칸 (목록 | 폼), 좁으면 한 칸으로 쌓는다
    const bool wide = vp.width >= 900.0;
    const double inner = fit(vp.width - 64.0);
    const double leftW = wide ? fit((inner - 24.0) * 0.55) : inner;
    const double rightW = wide ? fit(inner - 24.0 - leftW) : inner;
    const double fieldH = m_compact ? 30.0 : 34.0;
    const double gap = m_compact ? 6.0 : 8.0;
    // 목록 높이 = 남는 높이 (넓을 때 — 제목 · 검색 · 빼기 · 아래 줄 · 여백), 좁으면 행 다섯
    const double listH = wide ? std::max(rowHeight() * 2.0, vp.height - 340.0) : rowHeight() * 5.0;
    const double pickerLabelW = fit(rightW - 2.0 * 36.0 - 2.0 * 6.0);
    for (const Node& n : m_nodes) {
        gui::LayoutNode node;
        node.component = n.handle;
        gui::LayoutStyle& st = node.style;
        switch (n.role) {
        case Role::Root:
            st.kind = gui::LayoutKind::column;
            st.padding = {32, m_compact ? 20.0 : 28.0, 32, m_compact ? 14.0 : 20.0};
            st.gap = m_compact ? 7 : 10;
            break;
        case Role::Body:
            st.kind = wide ? gui::LayoutKind::row : gui::LayoutKind::column;
            st.gap = wide ? 24 : 16;
            break;
        case Role::Left:
        case Role::Right:
            st.kind = gui::LayoutKind::column;
            st.gap = gap;
            st.preferred_width = n.role == Role::Left ? leftW : rightW;
            break;
        case Role::Picker:
        case Role::ListBar:
        case Role::Bar:
            st.kind = gui::LayoutKind::row;
            st.gap = n.role == Role::Bar ? 12 : 6;
            break;
        case Role::PickLabel:
            st.preferred_width = pickerLabelW;
            st.preferred_height = fieldH;
            break;
        case Role::PickPrev:
        case Role::PickNext:
            st.preferred_width = 36;
            st.preferred_height = fieldH;
            break;
        case Role::Field:
            st.preferred_width = m_fields[n.index].part == Part::Search ? leftW : rightW;
            st.preferred_height = fieldH;
            break;
        case Role::List:
            st.preferred_width = leftW;
            st.preferred_height = listH;
            break;
        case Role::Forget:
        case Role::Create:
        case Role::Open:
        case Role::CopyExample:
        case Role::Switch:
        case Role::Density:
        case Role::Quit:
            st.padding = {14, m_compact ? 5.0 : 7.0, 14, m_compact ? 5.0 : 7.0};
            node.intrinsic = textIntrinsic(host.text(), textOf(n), emOf(n.role));
            break;
        default:
            node.intrinsic = textIntrinsic(host.text(), textOf(n), emOf(n.role));
            break;
        }
        out.push_back(std::move(node));
    }
    return {};
}

Expected<void> StudioHomeScreen::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const auto hit = host.pointerHit();
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    const double rowH = rowHeight();
    for (const gui::LayoutBox& box : frame.boxes()) {
        const auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                                     [&](const Node& n) { return n.handle == box.component; });
        if (it == m_nodes.end()) {
            continue;
        }
        Node& n = *it;
        n.bounds = box.bounds;
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        const bool focused = focus && *focus == n.handle;
        std::vector<gui::DrawCommand> draws;
        gui::ControlVisualState vs;
        if (n.button) {
            if (auto v = n.button->visualState(hit)) {
                vs = *v;
            }
        }
        bool clip = false;
        switch (n.role) {
        case Role::Root:
            draws.push_back(gui::FillRect{{0, 0, w, h}, kBackground});
            break;
        case Role::Field: {
            const Field& f = m_fields[n.index];
            clip = true;
            draws.push_back(gui::FillRect{{0, 0, w, h}, kFieldBg});
            draws.push_back(gui::FillRect{{0, fit(h - 2), w, 2}, focused ? kFocus : kNeutral});
            const gui::TextSelection sel = f.input ? f.input->selection() : gui::TextSelection{};
            const usize a = std::min({sel.anchor, sel.caret, f.value.size()});
            const usize b = std::min(std::max(sel.anchor, sel.caret), f.value.size());
            const std::string_view v = f.value;
            if (focused && a != b) {
                const double x0 = host.text().measure(v.substr(0, a), kFieldEm).width;
                const double x1 = host.text().measure(v.substr(0, b), kFieldEm).width;
                draws.push_back(gui::FillRect{{kFieldPad + x0, 5, fit(x1 - x0), fit(h - 10)}, kSelection});
            }
            const bool empty = f.value.empty();
            draws.push_back(gui::DrawText{{kFieldPad, fit((h - kFieldEm * 1.25) / 2), fit(w - 2 * kFieldPad),
                                           kFieldEm * 1.25},
                                          kFont, kFieldEm, empty ? f.placeholder : f.value, empty ? kMuted : kText});
            if (focused) {
                const usize c = std::min(sel.caret, f.value.size());
                const double x = host.text().measure(v.substr(0, c), kFieldEm).width;
                draws.push_back(gui::FillRect{{kFieldPad + x, 6, 1.5, fit(h - 12)}, kText});
            }
            break;
        }
        case Role::List: {
            clip = true;
            (void)m_list.resize({w, h}, rowH);
            draws.push_back(gui::FillRect{{0, 0, w, h}, kPanel});
            const gui::ListRange range = m_list.visibleRange();
            const double nameEm = m_compact ? 15.0 : 16.0;
            for (usize i = range.first; i < range.past; ++i) {
                const auto row = m_list.visibleRow(i);
                if (!row) {
                    continue;
                }
                const usize at = static_cast<usize>(row->key);
                const services::RecentProject& p = m_model.recent[at];
                const gui::LayoutRect r = row->bounds;
                const bool pressing = hit && *hit == n.handle && m_pressKey && *m_pressKey == row->key;
                draws.push_back(gui::FillRect{{r.x, r.y + 1, r.width, fit(r.height - 2)},
                                              row->selected ? kRowSelected : pressing ? kRowHover : kRow});
                if (row->selected && focused) {
                    draws.push_back(gui::FillRect{{r.x, r.y + 1, 3, fit(r.height - 2)}, kFocus});
                }
                const std::string title = p.name.empty() ? p.gameId : p.name;
                std::string detail = std::format("{} · {}", p.gameId, pathText(p.dir));
                if (!p.openedAt.empty()) {
                    detail += " · " + displayTime(p.openedAt);
                }
                // 오른쪽 위 표시: 폴더 없음 (빨강) · 복구 가능 (주황 — ImGui 홈의 배지와 같은 뜻)
                std::string badge;
                gui::RenderColor badgeColor = kMuted;
                if (p.missing) {
                    badge = "폴더가 없다";
                    badgeColor = kError;
                } else if (!m_model.recovery[at].empty()) {
                    badge = std::format("복구 가능 ({})", displayTime(m_model.recovery[at]));
                    badgeColor = kWarning;
                }
                double badgeW = 0.0;
                if (!badge.empty()) {
                    badgeW = host.text().measure(badge, 12.0).width;
                    draws.push_back(gui::DrawText{{fit(r.x + r.width - kPad - badgeW), r.y + 5, badgeW, 18}, kFont, 12.0,
                                                  badge, badgeColor});
                }
                const double textW = fit(r.width - 2 * kPad - (badgeW > 0 ? badgeW + kPad : 0.0));
                const double detailY = r.y + (m_compact ? 21.0 : 27.0);
                draws.push_back(gui::DrawText{{r.x + kPad, r.y + 4, textW, nameEm * 1.3}, kFont, nameEm, title,
                                              p.missing ? kMuted : kText});
                draws.push_back(gui::DrawText{{r.x + kPad, detailY, fit(r.width - 2 * kPad), 16}, kFont, 12.0, detail,
                                              kMuted});
            }
            if (m_list.itemCount() == 0) {
                draws.push_back(gui::DrawText{{kPad, kPad, fit(w - 2 * kPad), 22}, kFont, 14.0,
                                              m_model.recent.empty() ? "최근 프로젝트가 없다 — 오른쪽에서 새로 만들거나 연다"
                                                                     : "검색에 맞는 프로젝트가 없다",
                                              kMuted});
            }
            break;
        }
        case Role::PickLabel:
            draws.push_back(gui::FillRect{{0, 0, w, h}, kFieldBg});
            draws.push_back(gui::DrawText{{kFieldPad, fit((h - 15.0 * 1.25) / 2), fit(w - 2 * kFieldPad), 15.0 * 1.25},
                                          kFont, 15.0, textOf(n), kText});
            clip = true;
            break;
        case Role::Forget:
        case Role::PickPrev:
        case Role::PickNext:
        case Role::Create:
        case Role::Open:
        case Role::CopyExample:
        case Role::Switch:
        case Role::Density:
        case Role::Quit: {
            const bool accent = n.role == Role::Create || n.role == Role::Open || n.role == Role::CopyExample;
            const bool hot = vs.hovered || vs.pressed;
            draws.push_back(gui::FillRect{{0, 0, w, h}, accent ? (hot ? kAccentHover : kAccent)
                                                                : (hot ? kNeutralHover : kNeutral)});
            if (vs.focused) {
                draws.push_back(gui::FillRect{{0, fit(h - 2), w, 2}, kFocus});
            }
            const double em = emOf(n.role);
            const double tw = host.text().measure(textOf(n), em).width;
            draws.push_back(gui::DrawText{{fit((w - tw) / 2), fit((h - em * 1.25) / 2), fit(std::min(tw, w)), em * 1.25},
                                          kFont, em, textOf(n), kText});
            break;
        }
        case Role::Message:
            draws.push_back(gui::DrawText{{0, 0, w, h}, kFont, emOf(n.role), textOf(n), m_failed ? kError : kOk});
            break;
        case Role::Body:
        case Role::Left:
        case Role::Right:
        case Role::Picker:
        case Role::ListBar:
        case Role::Bar:
            break;
        default:
            draws.push_back(gui::DrawText{{0, 0, w, h}, kFont, emOf(n.role), textOf(n),
                                          n.role == Role::Title || n.role == Role::Heading ||
                                                  n.role == Role::NewHeading || n.role == Role::OpenHeading ||
                                                  n.role == Role::ExampleHeading
                                              ? kText
                                              : kMuted});
            break;
        }
        if (!draws.empty()) {
            out.add(n.handle, std::move(draws), clip);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
