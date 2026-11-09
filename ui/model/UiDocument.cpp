#include "ui/model/UiDocument.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>

#include <nlohmann/json.hpp>

namespace sbx::ui {
namespace {

using json = nlohmann::json;
using Severity = UiIssue::Severity;

constexpr usize kMaxIdBytes = 32;
constexpr usize kMaxKeyBytes = 64;

bool idChars(std::string_view s, usize max) {
    return !s.empty() && s.size() <= max && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

// "<팩>.<이름>" — 팩 · 이름 모두 id 글자, 점 하나 이상 (팩 id 에 점이 있을 수 있다 — 앞이 packId 와 같은지만 본다)
bool dottedId(std::string_view s) {
    if (s.empty() || s.size() > kMaxKeyBytes || s.front() == '.' || s.back() == '.' ||
        s.find("..") != std::string_view::npos || s.find('.') == std::string_view::npos) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

class Parser {
public:
    Parser(const UiCheckContext& context, std::vector<UiIssue>& issues) : m_ctx(context), m_issues(issues) {}

    void error(std::string rule, std::string pointer, std::string message) {
        if (rule == "U1" || rule == "U3" || rule == "U7") {
            m_structural = true;
        }
        m_issues.push_back({Severity::Error, std::move(rule), std::move(pointer), std::move(message)});
    }
    void warning(std::string rule, std::string pointer, std::string message) {
        m_issues.push_back({Severity::Warning, std::move(rule), std::move(pointer), std::move(message)});
    }
    [[nodiscard]] bool structural() const noexcept { return m_structural; }
    [[nodiscard]] usize nodes() const noexcept { return m_nodes; }

    // 문자열 하나 (U1 모양 · U7 길이). 아니면 nullopt
    std::optional<std::string> string(const json& v, const std::string& pointer) {
        if (!v.is_string()) {
            error("U1", pointer, "문자열이어야 한다");
            return std::nullopt;
        }
        std::string s = v.get<std::string>();
        if (s.size() > kMaxStringBytes) {
            error("U7", pointer, std::format("문자열이 {} 바이트를 넘는다", kMaxStringBytes));
            return std::nullopt;
        }
        return s;
    }

    std::optional<f32> number(const json& v, const std::string& pointer, f32 lo, f32 hi) {
        if (!v.is_number()) {
            error("U1", pointer, "수여야 한다");
            return std::nullopt;
        }
        const double d = v.get<double>();
        if (!(d >= lo && d <= hi)) { // NaN 도 여기서 걸린다
            error("U1", pointer, std::format("{} ~ {} 밖", lo, hi));
            return std::nullopt;
        }
        return static_cast<f32>(d);
    }

    template <usize N>
    bool numbers(const json& v, const std::string& pointer, f32 lo, f32 hi, f32 (&out)[N]) {
        if (!v.is_array() || v.size() != N) {
            error("U1", pointer, std::format("수 {} 개의 배열", N));
            return false;
        }
        for (usize i = 0; i < N; ++i) {
            const auto n = number(v[i], std::format("{}/{}", pointer, i), lo, hi);
            if (!n) {
                return false;
            }
            out[i] = *n;
        }
        return true;
    }

    // 수 또는 {"bind": 식}
    UiValue value(const json& v, const std::string& pointer) {
        UiValue out;
        if (v.is_number()) {
            if (const auto n = number(v, pointer, -1e9f, 1e9f)) {
                out.number = *n;
            }
            return out;
        }
        if (!v.is_object() || v.size() != 1 || !v.contains("bind")) {
            error("U1", pointer, R"(수 또는 {"bind": "<식>"})");
            return out;
        }
        if (const auto s = string(v["bind"], pointer + "/bind")) {
            if (const auto why = checkBinding(*s, m_ctx.componentFields)) {
                error("U4", pointer + "/bind", *why);
            }
            out.bind = *s;
        }
        return out;
    }

    // "@<키>" = 문자열 표의 키 (v0.7 M4-T1), "@@…" = 글자 "@…" 그대로
    void textValue(const std::string& text, const std::string& pointer) {
        if (text.starts_with('@') && !text.starts_with("@@")) {
            const std::string_view key = std::string_view(text).substr(1);
            if (!dottedId(key)) {
                error("U1", pointer, std::format("문자열 키 '{}' — \"@<팩>.<이름>\"", text));
            } else if (m_ctx.strings != nullptr && !m_ctx.strings->contains(key)) {
                warning("U5", pointer, std::format("문자열 키 '{}' 가 문자열 표에 없다 — 키를 그대로 보인다", key));
            }
        }
    }

    UiAction action(const json& v, const std::string& pointer) {
        UiAction a;
        if (!v.is_object()) {
            error("U1", pointer, R"({"signal": id, "value"?} · {"action": id} · {"ui": "show|hide|toggle", "target"})");
            return a;
        }
        if (v.contains("signal")) {
            a.kind = UiAction::Kind::Signal;
            for (const auto& [k, x] : v.items()) {
                if (k != "signal" && k != "value") {
                    error("U3", pointer + "/" + k, std::format("모르는 키 '{}' (signal · value)", k));
                }
            }
            if (const auto s = string(v["signal"], pointer + "/signal")) {
                a.target = *s;
                if (m_ctx.signals != nullptr && !m_ctx.signals->contains(*s)) {
                    error("U6", pointer + "/signal", std::format("신호 '{}' 가 콘텐츠에 없다 (팩의 signals.json)", *s));
                }
            }
            if (v.contains("value")) {
                if (!v["value"].is_number_integer() || v["value"].get<i64>() < std::numeric_limits<i32>::min() ||
                    v["value"].get<i64>() > std::numeric_limits<i32>::max()) {
                    error("U1", pointer + "/value", "i32 정수");
                } else {
                    a.value = static_cast<i32>(v["value"].get<i64>());
                }
            }
        } else if (v.contains("action")) {
            a.kind = UiAction::Kind::Action;
            if (v.size() != 1) {
                error("U3", pointer, "action 이벤트는 {\"action\": id} 하나");
            }
            if (const auto s = string(v["action"], pointer + "/action")) {
                a.target = *s;
                if (m_ctx.actions != nullptr && !m_ctx.actions->contains(*s)) {
                    error("U6", pointer + "/action", std::format("행동 '{}' 가 콘텐츠에 없다 (팩의 actions.json)", *s));
                }
            }
        } else if (v.contains("ui")) {
            a.kind = UiAction::Kind::Ui;
            for (const auto& [k, x] : v.items()) {
                if (k != "ui" && k != "target") {
                    error("U3", pointer + "/" + k, std::format("모르는 키 '{}' (ui · target)", k));
                }
            }
            if (const auto op = string(v["ui"], pointer + "/ui")) {
                if (*op != "show" && *op != "hide" && *op != "toggle" && *op != "theme") {
                    error("U1", pointer + "/ui", std::format("'{}' — show · hide · toggle · theme", *op));
                }
                a.op = *op;
            }
            if (!v.contains("target")) {
                error("U1", pointer, "ui 이벤트에 target \"<문서>.<노드>\" 이 필요하다");
            } else if (const auto t = string(v["target"], pointer + "/target")) {
                if (!dottedId(*t) && !(a.op == "theme" && *t == kNoTheme)) {
                    error("U2", pointer + "/target",
                          std::format("'{}' — \"<문서 id>.<노드 id>\" (theme 이면 \"<팩>.<테마>\" · \"none\")", *t));
                }
                a.target = *t;
            }
        } else {
            error("U3", pointer, "이벤트는 signal · action · ui 중 하나");
        }
        return a;
    }

    UiNode node(const json& j, const std::string& pointer, usize depth) {
        UiNode n;
        if (++m_nodes > kMaxNodes) {
            if (m_nodes == kMaxNodes + 1) {
                error("U7", pointer, std::format("노드가 {} 개를 넘는다", kMaxNodes));
            }
            return n;
        }
        if (depth > kMaxDepth) {
            error("U7", pointer, std::format("깊이가 {} 을 넘는다", kMaxDepth));
            return n;
        }
        if (!j.is_object() || !j.contains("kind") || !j["kind"].is_string()) {
            error("U1", pointer, "노드는 {\"kind\": …} 객체");
            return n;
        }
        const std::string kind = j["kind"].get<std::string>();
        static constexpr std::array<std::pair<std::string_view, UiKind>, 13> kKinds{{{"panel", UiKind::Panel},
                                                                                     {"text", UiKind::Text},
                                                                                     {"image", UiKind::Image},
                                                                                     {"button", UiKind::Button},
                                                                                     {"progress", UiKind::Progress},
                                                                                     {"list", UiKind::List},
                                                                                     {"checkbox", UiKind::Checkbox},
                                                                                     {"toggle", UiKind::Toggle},
                                                                                     {"slider", UiKind::Slider},
                                                                                     {"textinput", UiKind::TextInput},
                                                                                     {"dropdown", UiKind::Dropdown},
                                                                                     {"tabs", UiKind::Tabs},
                                                                                     {"scroll", UiKind::Scroll}}};
        const auto k = std::find_if(kKinds.begin(), kKinds.end(), [&](const auto& p) { return p.first == kind; });
        if (k == kKinds.end()) {
            error("U3", pointer + "/kind",
                  std::format("모르는 요소 '{}' (panel · text · image · button · progress · list · checkbox · "
                              "toggle · slider · textinput · dropdown · tabs · scroll)",
                              kind));
            return n;
        }
        n.kind = k->second;
        const auto allowed = [&](std::string_view key) {
            static constexpr std::array kCommon{"kind", "id", "anchor", "offset", "size", "style"};
            if (std::find(kCommon.begin(), kCommon.end(), key) != kCommon.end()) {
                return true;
            }
            switch (n.kind) {
            case UiKind::Panel:
            case UiKind::Scroll:
                return key == "stack" || key == "gap" || key == "padding" || key == "align" || key == "children";
            case UiKind::Text:
                return key == "text" || key == "args" || key == "overflow";
            case UiKind::Image:
                return key == "material";
            case UiKind::Button:
                return key == "text" || key == "args" || key == "on" || key == "overflow" || key == "disabled" ||
                       key == "tabIndex";
            case UiKind::Checkbox:
            case UiKind::Toggle:
                return key == "text" || key == "args" || key == "value" || key == "on" || key == "disabled" ||
                       key == "tabIndex";
            case UiKind::Slider:
                return key == "value" || key == "min" || key == "max" || key == "step" || key == "on" ||
                       key == "disabled" || key == "tabIndex";
            case UiKind::TextInput:
                return key == "value" || key == "placeholder" || key == "maxLength" || key == "multiline" ||
                       key == "on" || key == "disabled" || key == "tabIndex";
            case UiKind::Dropdown:
                return key == "value" || key == "options" || key == "on" || key == "disabled" || key == "tabIndex";
            case UiKind::Tabs:
                return key == "value" || key == "labels" || key == "children" || key == "on" || key == "disabled" ||
                       key == "tabIndex";
            case UiKind::Progress:
                return key == "value" || key == "max";
            case UiKind::List:
                return key == "items" || key == "item" || key == "stack" || key == "gap";
            }
            return false;
        };
        for (const auto& [key, v] : j.items()) {
            if (!allowed(key)) {
                error("U3", pointer + "/" + key, std::format("{} 에 모르는 키 '{}'", kind, key));
            }
        }
        if (j.contains("id")) {
            if (const auto s = string(j["id"], pointer + "/id")) {
                if (!idChars(*s, kMaxIdBytes)) {
                    error("U2", pointer + "/id", std::format("노드 id '{}' — [a-z0-9_-] 1 ~ {}", *s, kMaxIdBytes));
                } else if (std::find(m_ids.begin(), m_ids.end(), *s) != m_ids.end()) {
                    error("U2", pointer + "/id", std::format("노드 id '{}' 가 문서에 두 번 있다", *s));
                } else {
                    m_ids.push_back(*s);
                }
                n.id = *s;
            }
        }
        if (j.contains("anchor")) {
            const json& a = j["anchor"];
            if (!a.is_object() || a.size() != 2 || !a.contains("min") || !a.contains("max")) {
                error("U1", pointer + "/anchor", R"({"min": [x, y], "max": [x, y]} (0 ~ 1))");
            } else if (numbers(a["min"], pointer + "/anchor/min", 0, 1, n.anchorMin) &&
                       numbers(a["max"], pointer + "/anchor/max", 0, 1, n.anchorMax) &&
                       (n.anchorMin[0] > n.anchorMax[0] || n.anchorMin[1] > n.anchorMax[1])) {
                error("U1", pointer + "/anchor", "min 이 max 보다 크다");
            }
        }
        if (j.contains("offset")) {
            (void)numbers(j["offset"], pointer + "/offset", -16384, 16384, n.offset);
        }
        if (j.contains("size")) {
            f32 size[2] = {0, 0};
            if (numbers(j["size"], pointer + "/size", 0, 16384, size)) {
                n.size = std::array<f32, 2>{size[0], size[1]};
            }
        }
        if (j.contains("style")) {
            if (const auto s = string(j["style"], pointer + "/style")) {
                n.style = *s;
                if (m_ctx.styles != nullptr && !m_ctx.styles->contains(*s)) {
                    warning("U5", pointer + "/style",
                            std::format("스타일 '{}' 가 없다 (presentation/ui/styles.json) — 기본 모양", *s));
                }
            }
        }
        if (j.contains("stack")) {
            if (const auto s = string(j["stack"], pointer + "/stack")) {
                if (*s == "row") {
                    n.stack = UiStack::Row;
                } else if (*s == "column") {
                    n.stack = UiStack::Column;
                } else {
                    error("U1", pointer + "/stack", std::format("'{}' — row · column", *s));
                }
            }
        }
        if (j.contains("gap")) {
            n.gap = number(j["gap"], pointer + "/gap", 0, 4096).value_or(0);
        }
        if (j.contains("padding")) {
            n.padding = number(j["padding"], pointer + "/padding", 0, 4096).value_or(0);
        }
        if (j.contains("overflow")) { // v0.7 M4-T2
            if (const auto s = string(j["overflow"], pointer + "/overflow")) {
                if (*s == "clip") {
                    n.overflow = UiOverflow::Clip;
                } else if (*s == "wrap") {
                    n.overflow = UiOverflow::Wrap;
                } else if (*s == "fit") {
                    n.overflow = UiOverflow::Fit;
                } else {
                    error("U1", pointer + "/overflow", std::format("'{}' — clip · wrap · fit", *s));
                }
            }
        }
        if (j.contains("align")) {
            if (const auto s = string(j["align"], pointer + "/align")) {
                static constexpr std::array<std::pair<std::string_view, UiAlign>, 4> kAligns{
                    {{"start", UiAlign::Start},
                     {"center", UiAlign::Center},
                     {"end", UiAlign::End},
                     {"stretch", UiAlign::Stretch}}};
                const auto a =
                    std::find_if(kAligns.begin(), kAligns.end(), [&](const auto& p) { return p.first == *s; });
                if (a == kAligns.end()) {
                    error("U1", pointer + "/align", std::format("'{}' — start · center · end · stretch", *s));
                } else {
                    n.align = a->second;
                }
            }
        }
        if (j.contains("text")) {
            if (const auto s = string(j["text"], pointer + "/text")) {
                n.text = *s;
                textValue(*s, pointer + "/text");
            }
        } else if (n.kind == UiKind::Text || n.kind == UiKind::Button) {
            error("U1", pointer, std::format("{} 에 text 가 필요하다", kind));
        }
        if (j.contains("args")) {
            const json& a = j["args"];
            if (!a.is_array() || a.size() > 8) {
                error("U1", pointer + "/args", "인자는 8 개까지의 배열");
            } else {
                for (usize i = 0; i < a.size(); ++i) {
                    n.args.push_back(value(a[i], std::format("{}/args/{}", pointer, i)));
                }
            }
        }
        if (n.kind == UiKind::Progress) {
            if (!j.contains("value")) {
                error("U1", pointer, "progress 에 value 가 필요하다");
            } else {
                n.value = value(j["value"], pointer + "/value");
            }
            if (j.contains("max")) {
                n.max = value(j["max"], pointer + "/max");
            }
        }
        if (n.kind == UiKind::Image) {
            if (!j.contains("material")) {
                error("U1", pointer, "image 에 material 이 필요하다");
            } else if (const auto s = string(j["material"], pointer + "/material")) {
                n.material = *s;
                if (m_ctx.materials != nullptr && !m_ctx.materials->contains(*s)) {
                    warning("U8", pointer + "/material", std::format("material '{}' 이 없다 — 대체 색으로 그린다", *s));
                }
            }
        }
        if (j.contains("on")) {
            const json& on = j["on"];
            const bool control = n.kind != UiKind::Button && isControl(n.kind);
            const char* event = control ? "change" : "click";
            if (!on.is_object() || on.size() != 1 || !on.contains(event)) {
                error("U3", pointer + "/on", std::format(R"(이벤트는 {{"{}": …}} 하나)", event));
            } else if (control) {
                // 바뀜 = 신호만, 값은 컨트롤이 (ADR-0103 2)
                const json& c = on["change"];
                if (!c.is_object() || !c.contains("signal") || c.contains("value")) {
                    error("U1", pointer + "/on/change", R"(바뀜은 {"signal": "<팩>.<이름>"} — 값은 컨트롤이 정한다)");
                } else {
                    n.change = action(c, pointer + "/on/change");
                }
            } else {
                n.click = action(on["click"], pointer + "/on/click");
            }
        }
        if (n.kind != UiKind::Button && isControl(n.kind)) {
            // 값 = "var:<키>" 하나 (ADR-0103 1 — 컨트롤이 쓴다)
            if (!j.contains("value")) {
                error("U1", pointer, std::format(R"({} 에 value {{"bind": "var:<키>"}} 가 필요하다)", kind));
            } else {
                n.value = value(j["value"], pointer + "/value");
                if (!n.value.bind.starts_with("var:")) {
                    error("U4", pointer + "/value", "컨트롤의 value 는 \"var:<키>\" 바인딩이어야 한다 (컨트롤이 쓴다)");
                }
            }
        }
        if (n.kind == UiKind::Slider) {
            n.max = UiValue{1, {}};
            if (j.contains("min")) {
                n.min = number(j["min"], pointer + "/min", -1e9f, 1e9f).value_or(0);
            }
            if (j.contains("max")) {
                n.max.number = number(j["max"], pointer + "/max", -1e9f, 1e9f).value_or(1);
            }
            if (j.contains("step")) {
                n.step = number(j["step"], pointer + "/step", 0, 1e9f).value_or(0);
            }
            if (!(n.max.number > n.min)) {
                error("U1", pointer + "/max", "slider 의 max 는 min 보다 커야 한다");
            }
        }
        if (n.kind == UiKind::TextInput) { // v0.7 NG-06 (2) — ADR-0104
            if (j.contains("placeholder")) {
                if (const auto s = string(j["placeholder"], pointer + "/placeholder")) {
                    n.text = *s; // 빈 칸일 때 흐리게 — "@<키>" 도 (formatText 가 푼다)
                    textValue(*s, pointer + "/placeholder");
                }
            }
            if (j.contains("maxLength")) {
                const json& m = j["maxLength"];
                if (!m.is_number_integer() || m.get<i64>() < 1 || m.get<i64>() > static_cast<i64>(kMaxTextLength)) {
                    error("U1", pointer + "/maxLength", std::format("1 ~ {} 정수 (코드 포인트)", kMaxTextLength));
                } else {
                    n.maxLength = static_cast<u32>(m.get<i64>());
                }
            }
            if (j.contains("multiline")) {
                if (!j["multiline"].is_boolean()) {
                    error("U1", pointer + "/multiline", "참거짓");
                } else {
                    n.multiline = j["multiline"].get<bool>();
                }
            }
        }
        if (n.kind == UiKind::Dropdown || n.kind == UiKind::Tabs) { // v0.7 NG-06 (3) — ADR-0105
            const char* key = n.kind == UiKind::Dropdown ? "options" : "labels";
            const json* list = j.contains(key) ? &j[key] : nullptr;
            if (list == nullptr || !list->is_array() || list->empty() || list->size() > kMaxOptions) {
                error("U1", list == nullptr ? pointer : pointer + "/" + key,
                      std::format("{} 에 {} (글자 1 ~ {} 개) 가 필요하다", kind, key, kMaxOptions));
            } else {
                for (usize i = 0; i < list->size(); ++i) {
                    const std::string at = std::format("{}/{}/{}", pointer, key, i);
                    if (const auto s = string((*list)[i], at)) {
                        textValue(*s, at);
                        n.options.push_back(*s);
                    }
                }
            }
        }
        if (j.contains("disabled")) {
            const json& d = j["disabled"];
            if (d.is_boolean()) {
                n.disabled.number = d.get<bool>() ? 1.0f : 0.0f;
            } else {
                n.disabled = value(d, pointer + "/disabled");
            }
        }
        if (j.contains("tabIndex")) {
            const json& t = j["tabIndex"];
            if (!t.is_number_integer() || t.get<i64>() < 0 || t.get<i64>() > 1000) {
                error("U1", pointer + "/tabIndex", "0 ~ 1000 정수");
            } else {
                n.tabIndex = static_cast<i32>(t.get<i64>());
            }
        }
        if (n.kind == UiKind::List) {
            if (!j.contains("items")) {
                error("U1", pointer, R"(list 에 items {"bind": "<식>"} 가 필요하다)");
            } else {
                const UiValue items = value(j["items"], pointer + "/items");
                if (items.bind.empty()) {
                    error("U4", pointer + "/items", "list 의 items 는 바인딩이어야 한다");
                }
                n.items = items.bind;
            }
            if (!j.contains("item")) {
                error("U1", pointer, "list 에 item (반복할 노드) 이 필요하다");
            } else {
                n.children.push_back(node(j["item"], pointer + "/item", depth + 1));
            }
        }
        if (j.contains("children")) {
            const json& c = j["children"];
            if (!c.is_array()) {
                error("U1", pointer + "/children", "노드 배열");
            } else {
                for (usize i = 0; i < c.size() && m_nodes <= kMaxNodes; ++i) {
                    n.children.push_back(node(c[i], std::format("{}/children/{}", pointer, i), depth + 1));
                }
            }
        }
        if (n.kind == UiKind::Tabs && !n.options.empty() && n.children.size() != n.options.size()) {
            error("U1", pointer + "/children",
                  std::format("tabs 의 children ({}) 은 labels ({}) 와 하나씩이어야 한다", n.children.size(),
                              n.options.size()));
        }
        return n;
    }

private:
    const UiCheckContext& m_ctx;
    std::vector<UiIssue>& m_issues;
    std::vector<std::string> m_ids;
    usize m_nodes = 0;
    bool m_structural = false;
};

bool noErrors(const std::vector<UiIssue>& issues) {
    return std::none_of(issues.begin(), issues.end(), [](const UiIssue& i) { return i.severity == Severity::Error; });
}

void collectTargets(const UiNode& n, const std::string& documentId, const std::string& pointer,
                    std::vector<UiTargetRef>& out) {
    if (n.click.kind == UiAction::Kind::Ui) {
        out.push_back({documentId, pointer + "/on/click/target", n.click.target, n.click.op == "theme"});
    }
    for (usize i = 0; i < n.children.size(); ++i) {
        const std::string child =
            n.kind == UiKind::List ? pointer + "/item" : std::format("{}/children/{}", pointer, i);
        collectTargets(n.children[i], documentId, child, out);
    }
}

void collectIds(const UiNode& n, const std::string& documentId, std::vector<std::string>& out) {
    if (!n.id.empty()) {
        out.push_back(documentId + "." + n.id);
    }
    for (const UiNode& c : n.children) {
        collectIds(c, documentId, out);
    }
}

} // namespace

bool isUiKey(std::string_view s) {
    return dottedId(s);
}

bool UiParseResult::ok() const noexcept {
    return noErrors(issues);
}

bool UiStylesResult::ok() const noexcept {
    return noErrors(issues);
}

std::optional<std::string> checkBinding(std::string_view bind,
                                        const std::set<std::string, std::less<>>* componentFields) {
    if (bind.starts_with("var:")) {
        const std::string_view key = bind.substr(4);
        if (!dottedId(key) && !idChars(key, kMaxKeyBytes)) {
            return std::format("'{}' — var:<키> ([a-z0-9_.-])", bind);
        }
        return std::nullopt;
    }
    if (bind.starts_with("session:")) {
        const std::string_view f = bind.substr(8);
        if (f == "state" || f == "tick" || f == "time" || f == "team" || f == "message") {
            return std::nullopt;
        }
        if (f.starts_with("score[") && f.ends_with(']') && f.size() > 7 && f.size() <= 9) {
            const std::string_view digits = f.substr(6, f.size() - 7);
            if (std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
                std::stoi(std::string(digits)) < 16) {
                return std::nullopt;
            }
        }
        return std::format("'{}' — session:state · tick · time · team · message · score[0~15]", bind);
    }
    if (bind.starts_with("component:")) {
        const std::string_view rest = bind.substr(10);
        const usize at = rest.find('@');
        if (at == std::string_view::npos) {
            return std::format("'{}' — component:<컴포넌트>.<칸>@owned · selected", bind);
        }
        const std::string_view field = rest.substr(0, at);
        const std::string_view target = rest.substr(at + 1);
        if (target != "owned" && target != "selected") {
            return std::format("'{}' — 대상은 owned · selected (entity:<netId> 는 [계획])", bind);
        }
        if (!dottedId(field)) {
            return std::format("'{}' — <컴포넌트 id>.<칸>", bind);
        }
        if (componentFields != nullptr && !componentFields->contains(field)) {
            return std::format("'{}' — 컴포넌트 칸 '{}' 이 없다", bind, field);
        }
        return std::nullopt;
    }
    return std::format("'{}' — var: · session: · component: 중 하나", bind);
}

UiParseResult parseUiDocument(std::string_view fileStem, std::string_view jsonText, const UiCheckContext& context) {
    UiParseResult r;
    Parser p(context, r.issues);
    if (jsonText.size() > kMaxDocumentBytes) {
        p.error("U7", "", std::format("문서가 {} 바이트를 넘는다", kMaxDocumentBytes));
        return r;
    }
    const json j = json::parse(jsonText, nullptr, false);
    if (!j.is_object()) {
        p.error("U1", "", "JSON 객체가 아니다");
        return r;
    }
    for (const auto& [key, v] : j.items()) {
        if (key != "id" && key != "layer" && key != "visible" && key != "root") {
            p.error("U3", "/" + key, std::format("모르는 키 '{}' (id · layer · visible · root)", key));
        }
    }
    UiDocument doc;
    if (!j.contains("id")) {
        p.error("U1", "", "id \"<팩>.<이름>\" 이 필요하다");
    } else if (const auto id = p.string(j["id"], "/id")) {
        const std::string expected = std::format("{}.{}", context.packId, fileStem);
        if (!dottedId(*id) || (!context.packId.empty() && *id != expected)) {
            p.error("U2", "/id", std::format("문서 id '{}' — '{}' (팩 id . 파일 이름)", *id, expected));
        }
        doc.id = *id;
    }
    if (j.contains("layer")) {
        if (const auto layer = p.string(j["layer"], "/layer")) {
            if (*layer == "hud") {
                doc.layer = UiLayer::Hud;
            } else if (*layer == "menu") {
                doc.layer = UiLayer::Menu;
            } else if (*layer == "modal") {
                doc.layer = UiLayer::Modal;
            } else {
                p.error("U1", "/layer", std::format("'{}' — hud · menu · modal", *layer));
            }
        }
    }
    if (j.contains("visible")) {
        if (!j["visible"].is_boolean()) {
            p.error("U1", "/visible", "참 · 거짓");
        } else {
            doc.visible = j["visible"].get<bool>();
        }
    }
    if (!j.contains("root")) {
        p.error("U1", "", "root 노드가 필요하다");
    } else {
        doc.root = p.node(j["root"], "/root", 1);
    }
    doc.nodeCount = std::min(p.nodes(), kMaxNodes);
    if (!p.structural()) {
        r.document = std::move(doc);
    }
    return r;
}

void UiStylePatch::applyTo(UiStyle& style) const {
    if (color) {
        style.color = color;
    }
    if (borderColor) {
        style.borderColor = borderColor;
    }
    if (borderWidth) {
        style.borderWidth = *borderWidth;
    }
    if (fontSize) {
        style.fontSize = *fontSize;
    }
    if (padding) {
        style.padding = *padding;
    }
}

UiStyleTable applyTheme(const UiStyleTable& styles, const UiTheme* theme) {
    UiStyleTable out = styles;
    if (theme != nullptr) {
        for (const auto& [name, patch] : *theme) {
            if (const auto it = out.find(name); it != out.end()) {
                patch.applyTo(it->second);
            }
        }
    }
    return out;
}

UiStylesResult parseUiStyles(std::string_view jsonText, std::string_view packId) {
    UiStylesResult r;
    const auto error = [&](std::string rule, std::string pointer, std::string message) {
        r.issues.push_back({Severity::Error, std::move(rule), std::move(pointer), std::move(message)});
    };
    if (jsonText.size() > kMaxDocumentBytes) {
        error("U7", "", std::format("문서가 {} 바이트를 넘는다", kMaxDocumentBytes));
        return r;
    }
    const json j = json::parse(jsonText, nullptr, false);
    const bool shape = j.is_object() && j.contains("styles") && j["styles"].is_object() &&
                       (!j.contains("themes") || j["themes"].is_object()) &&
                       j.size() == (j.contains("themes") ? 2u : 1u);
    if (!shape) {
        error(
            "U1", "",
            R"({"styles": {"<팩>.<이름>": {color?, border?, fontSize?, padding?}}, "themes"?: {"<팩>.<테마>": {"<스타일>": {…}}}})");
        return r;
    }
    const auto rgba = [&](const json& v, const std::string& pointer) -> std::optional<std::array<u8, 4>> {
        if (!v.is_array() || v.size() != 4 || !std::all_of(v.begin(), v.end(), [](const json& c) {
                return c.is_number_integer() && c.get<i64>() >= 0 && c.get<i64>() <= 255;
            })) {
            error("U1", pointer, "색은 [r, g, b, a] (0 ~ 255 정수)");
            return std::nullopt;
        }
        return std::array<u8, 4>{static_cast<u8>(v[0].get<i64>()), static_cast<u8>(v[1].get<i64>()),
                                 static_cast<u8>(v[2].get<i64>()), static_cast<u8>(v[3].get<i64>())};
    };
    const auto num = [&](const json& v, const std::string& pointer, f32 lo, f32 hi) -> std::optional<f32> {
        if (!v.is_number() || !(v.get<double>() >= lo && v.get<double>() <= hi)) {
            error("U1", pointer, std::format("{} ~ {} 의 수", lo, hi));
            return std::nullopt;
        }
        return static_cast<f32>(v.get<double>());
    };
    // 스타일 객체 하나 → 덮기 (적은 칸만). 스타일 · 테마가 같은 모양을 쓴다
    const auto patchOf = [&](const json& s, const std::string& where) -> std::optional<UiStylePatch> {
        if (!s.is_object()) {
            error("U1", where, "스타일은 객체");
            return std::nullopt;
        }
        UiStylePatch p;
        for (const auto& [key, v] : s.items()) {
            const std::string at = where + "/" + key;
            if (key == "color") {
                p.color = rgba(v, at);
            } else if (key == "border") {
                if (!v.is_object()) {
                    error("U1", at, R"({"color": [r, g, b, a], "width": n})");
                    continue;
                }
                for (const auto& [bk, bv] : v.items()) {
                    if (bk == "color") {
                        p.borderColor = rgba(bv, at + "/color");
                    } else if (bk == "width") {
                        p.borderWidth = num(bv, at + "/width", 0, 64);
                    } else {
                        error("U3", at + "/" + bk, std::format("모르는 키 '{}' (color · width)", bk));
                    }
                }
            } else if (key == "fontSize") {
                p.fontSize = num(v, at, 1, 256);
            } else if (key == "padding") {
                p.padding = num(v, at, 0, 4096);
            } else {
                error("U3", at, std::format("모르는 키 '{}' (color · border · fontSize · padding)", key));
            }
        }
        return p;
    };
    const auto ownName = [&](const std::string& name) {
        return dottedId(name) && (packId.empty() || name.starts_with(std::string(packId) + "."));
    };
    for (const auto& [name, s] : j["styles"].items()) {
        const std::string where = "/styles/" + name;
        if (!ownName(name)) {
            error("U2", where, std::format("스타일 이름 '{}' — \"{}.<이름>\"", name, packId));
            continue;
        }
        if (const auto p = patchOf(s, where)) {
            UiStyle style;
            p->applyTo(style);
            r.styles.emplace(name, style);
        }
    }
    if (j.contains("themes")) {
        for (const auto& [name, t] : j["themes"].items()) {
            const std::string where = "/themes/" + name;
            if (!ownName(name)) {
                error("U2", where, std::format("테마 이름 '{}' — \"{}.<이름>\"", name, packId));
                continue;
            }
            if (!t.is_object()) {
                error("U1", where, R"(테마는 {"<스타일>": {덮을 칸}})");
                continue;
            }
            UiTheme theme;
            for (const auto& [style, s] : t.items()) {
                if (!dottedId(style)) {
                    error("U2", where + "/" + style, std::format("스타일 이름 '{}' — \"<팩>.<이름>\"", style));
                } else if (const auto p = patchOf(s, where + "/" + style)) {
                    theme.emplace(style, *p);
                }
            }
            r.themes.emplace(name, std::move(theme));
        }
    }
    return r;
}

std::vector<UiTargetRef> uiTargets(const UiDocument& document) {
    std::vector<UiTargetRef> out;
    collectTargets(document.root, document.id, "/root", out);
    return out;
}

std::vector<std::string> nodeIds(const UiDocument& document) {
    std::vector<std::string> out;
    collectIds(document.root, document.id, out);
    return out;
}

} // namespace sbx::ui
