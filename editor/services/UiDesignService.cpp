#include "editor/services/UiDesignService.hpp"

#include <algorithm>
#include <format>
#include <map>

namespace sbx::editor::services {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

bool uiKindHasChildren(std::string_view kind) noexcept {
    return kind == "panel" || kind == "scroll" || kind == "list" || kind == "tabs";
}

namespace {

// 노드의 자식 — children (panel · scroll · tabs), list 는 항목 틀 item 하나가 자식 0 (UiNode 의 children 과 같은 차례)
usize childCount(const Json& n) {
    if (const auto c = n.find("children"); c != n.end() && c->is_array()) {
        return c->size();
    }
    const auto item = n.find("item");
    return item != n.end() && item->is_object() ? 1 : 0;
}
template <class J>
J* childAt(J& n, usize i) {
    if (const auto c = n.find("children"); c != n.end() && c->is_array()) {
        return i < c->size() ? &(*c)[i] : nullptr;
    }
    const auto item = n.find("item");
    return i == 0 && item != n.end() && item->is_object() ? &*item : nullptr;
}

// path 의 노드 (없으면 nullptr). root = j["root"]
Json* nodeAt(Json& doc, const UiNodePath& path) {
    if (!doc.is_object() || !doc.contains("root")) {
        return nullptr;
    }
    Json* n = &doc["root"];
    for (const u32 i : path) {
        if (!n->is_object()) {
            return nullptr;
        }
        n = childAt(*n, i);
        if (n == nullptr) {
            return nullptr;
        }
    }
    return n->is_object() ? n : nullptr;
}

std::string describe(const UiNodePath& path) {
    std::string s = "root";
    for (const u32 i : path) {
        s += std::format("/{}", i);
    }
    return s;
}

void collectIds(const Json& n, std::vector<std::string>& out) {
    if (!n.is_object()) {
        return;
    }
    if (const auto id = n.find("id"); id != n.end() && id->is_string()) {
        out.push_back(id->get<std::string>());
    }
    for (usize i = 0; i < childCount(n); ++i) {
        collectIds(*childAt(n, i), out);
    }
}

// 요소 라이브러리의 기본 노드 — 검사 (U1 ~ U8) 를 통과하는 가장 작은 모양
Json defaultNode(std::string_view kind, const std::string& id) {
    Json n = Json::object();
    n["kind"] = std::string(kind);
    n["id"] = id;
    if (kind == "panel" || kind == "scroll") {
        n["size"] = {160, 80};
        n["stack"] = "column";
        n["children"] = Json::array();
    } else if (kind == "text") {
        n["text"] = "글자";
    } else if (kind == "image") {
        n["material"] = "ui/image"; // 팩 presentation/materials.json 의 이름으로 바꾼다 (없으면 색 사각형)
        n["size"] = {32, 32};
    } else if (kind == "button") {
        n["text"] = "버튼";
    } else if (kind == "progress") {
        n["value"] = 0.5;
        n["size"] = {120, 12};
    } else if (kind == "list") {
        n["items"] = Json{{"bind", std::format("var:{}", id)}};
        n["item"] = Json{{"kind", "text"}, {"text", "항목"}};
    } else if (kind == "checkbox" || kind == "toggle" || kind == "slider" || kind == "textinput" ||
               kind == "dropdown" || kind == "tabs") {
        n["value"] = Json{{"bind", std::format("var:{}", id)}};
        if (kind == "checkbox" || kind == "toggle") {
            n["text"] = "켜기";
        } else if (kind == "slider") {
            n["max"] = 1;
        } else if (kind == "dropdown") {
            n["options"] = {"하나", "둘"};
        } else if (kind == "tabs") {
            n["labels"] = {"첫째", "둘째"};
            n["children"] = Json::array({Json{{"kind", "panel"}, {"children", Json::array()}},
                                         Json{{"kind", "panel"}, {"children", Json::array()}}});
        }
    }
    return n;
}

} // namespace

UiDesignService::UiDesignService(DocumentService& documents) : m_docs(documents) {}

Expected<void> UiDesignService::open(const fs::path& file) {
    auto id = m_docs.open(file);
    if (!id) {
        return std::unexpected(id.error());
    }
    m_doc = *id;
    m_fresh = false;
    return {};
}

std::string UiDesignService::stem() const {
    return m_doc ? m_docs.path(*m_doc).stem().string() : std::string();
}

Expected<Json> UiDesignService::current() const {
    if (!m_doc || !m_docs.isOpen(*m_doc)) {
        return makeError(ErrorCode::InvalidArgument, "열린 UI 문서가 없다");
    }
    Json j = Json::parse(m_docs.text(*m_doc), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return makeError(ErrorCode::ParseError, "UI 문서가 JSON 객체가 아니다 — 글로 고친 뒤 다시",
                         m_docs.path(*m_doc).string());
    }
    return j;
}

void UiDesignService::refresh() {
    if (!m_doc || !m_docs.isOpen(*m_doc)) {
        m_nodes.clear();
        m_issues.clear();
        m_parseError = "열린 UI 문서가 없다";
        m_fresh = false;
        return;
    }
    const std::string& text = m_docs.text(*m_doc);
    if (m_fresh && text == m_seen) {
        return;
    }
    m_seen = text;
    m_fresh = true;
    m_nodes.clear();
    m_issues.clear();
    m_parseError.clear();
    const Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("root")) {
        m_parseError = "JSON 객체가 아니거나 root 가 없다";
        return;
    }
    // 앞 순서 — 부모 먼저, 자식은 문서 차례
    const auto walk = [&](auto&& self, const Json& n, UiNodePath path) -> void {
        if (!n.is_object()) {
            return;
        }
        UiDesignNode d;
        d.path = path;
        d.depth = static_cast<u32>(path.size());
        d.kind = n.value("kind", std::string("?"));
        d.id = n.contains("id") && n["id"].is_string() ? n["id"].get<std::string>() : std::string();
        d.label = d.kind;
        if (!d.id.empty()) {
            d.label += " #" + d.id;
        }
        if (n.contains("text") && n["text"].is_string()) {
            d.label += " '" + n["text"].get<std::string>() + "'";
        }
        m_nodes.push_back(std::move(d));
        for (usize i = 0; i < childCount(n); ++i) {
            UiNodePath child = path;
            child.push_back(static_cast<u32>(i));
            self(self, *childAt(n, i), std::move(child));
        }
    };
    walk(walk, j["root"], {});
    m_issues = ui::parseUiDocument(stem(), text, ui::UiCheckContext{}).issues;
}

const std::vector<UiDesignNode>& UiDesignService::nodes() {
    refresh();
    return m_nodes;
}

const std::vector<ui::UiIssue>& UiDesignService::issues() {
    refresh();
    return m_issues;
}

const std::string& UiDesignService::parseError() {
    refresh();
    return m_parseError;
}

std::optional<Json> UiDesignService::node(const UiNodePath& path) {
    auto j = current();
    if (!j) {
        return std::nullopt;
    }
    const Json* n = nodeAt(*j, path);
    return n != nullptr ? std::optional<Json>(*n) : std::nullopt;
}

Expected<void> UiDesignService::commit(const Json& root, std::string label) {
    return m_docs.setText(*m_doc, root.dump(2) + "\n", std::move(label));
}

Expected<void> UiDesignService::setProperty(const UiNodePath& path, std::string_view key, Json value) {
    auto j = current();
    if (!j) {
        return std::unexpected(j.error());
    }
    Json* n = nodeAt(*j, path);
    if (n == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("노드 {} 가 없다", describe(path)));
    }
    if (key == "children") {
        return makeError(ErrorCode::InvalidArgument, "children 은 addChild · remove · move 로 바꾼다");
    }
    (*n)[std::string(key)] = std::move(value);
    return commit(*j, std::format("UI {} · {}", describe(path), key));
}

Expected<void> UiDesignService::removeProperty(const UiNodePath& path, std::string_view key) {
    auto j = current();
    if (!j) {
        return std::unexpected(j.error());
    }
    Json* n = nodeAt(*j, path);
    if (n == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("노드 {} 가 없다", describe(path)));
    }
    if (key == "kind" || key == "children") {
        return makeError(ErrorCode::InvalidArgument, std::format("'{}' 는 뺄 수 없다", key));
    }
    if (n->erase(std::string(key)) == 0) {
        return {}; // 없던 키 — 바꿀 것이 없다 (되돌리기 칸도 없다)
    }
    return commit(*j, std::format("UI {} · {} 빼기", describe(path), key));
}

Expected<void> UiDesignService::setNode(const UiNodePath& path, Json value) {
    if (!value.is_object() || !value.contains("kind") || !value["kind"].is_string()) {
        return makeError(ErrorCode::InvalidArgument, "노드는 {\"kind\": …} 객체");
    }
    auto j = current();
    if (!j) {
        return std::unexpected(j.error());
    }
    Json* n = nodeAt(*j, path);
    if (n == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("노드 {} 가 없다", describe(path)));
    }
    for (const char* key : {"children", "item"}) {
        if (!value.contains(key) && n->contains(key)) {
            value[key] = (*n)[key];
        }
    }
    *n = std::move(value);
    return commit(*j, std::format("UI {} 고치기", describe(path)));
}

Expected<UiNodePath> UiDesignService::addChild(const UiNodePath& parent, std::string_view kind) {
    if (std::ranges::find(kUiLibrary, kind) == kUiLibrary.end()) {
        return makeError(ErrorCode::InvalidArgument, std::format("요소 라이브러리에 '{}' 가 없다", kind));
    }
    auto j = current();
    if (!j) {
        return std::unexpected(j.error());
    }
    Json* p = nodeAt(*j, parent);
    if (p == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("노드 {} 가 없다", describe(parent)));
    }
    const std::string parentKind = p->value("kind", std::string());
    if (!uiKindHasChildren(parentKind) || parentKind == "list" || parentKind == "tabs") {
        // list 는 항목 틀 하나 · tabs 는 labels 와 하나씩 — 자식 수를 디자이너가 맞춘다 (options 와 함께)
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' 에는 자식을 더하지 않는다 (panel · scroll 만)", parentKind));
    }
    std::vector<std::string> ids;
    collectIds((*j)["root"], ids);
    std::string id;
    for (u32 i = 1;; ++i) {
        id = std::format("{}{}", kind, i);
        if (std::ranges::find(ids, id) == ids.end()) {
            break;
        }
    }
    if (!p->contains("children") || !(*p)["children"].is_array()) {
        (*p)["children"] = Json::array();
    }
    (*p)["children"].push_back(defaultNode(kind, id));
    UiNodePath made = parent;
    made.push_back(static_cast<u32>((*p)["children"].size() - 1));
    if (auto r = commit(*j, std::format("UI {} 더하기 ({})", kind, id)); !r) {
        return std::unexpected(r.error());
    }
    return made;
}

Expected<void> UiDesignService::remove(const UiNodePath& path) {
    if (path.empty()) {
        return makeError(ErrorCode::InvalidArgument, "root 는 지울 수 없다");
    }
    auto j = current();
    if (!j) {
        return std::unexpected(j.error());
    }
    const UiNodePath parentPath(path.begin(), path.end() - 1);
    Json* p = nodeAt(*j, parentPath);
    if (p == nullptr || nodeAt(*j, path) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("노드 {} 가 없다", describe(path)));
    }
    if (!p->contains("children") || !(*p)["children"].is_array()) {
        return makeError(ErrorCode::InvalidArgument, "list 의 항목 틀은 지우거나 옮기지 않는다 — 속성을 고친다");
    }
    (*p)["children"].erase((*p)["children"].begin() + path.back());
    return commit(*j, std::format("UI {} 지우기", describe(path)));
}

Expected<UiNodePath> UiDesignService::move(const UiNodePath& path, i32 delta) {
    if (path.empty()) {
        return makeError(ErrorCode::InvalidArgument, "root 는 옮길 수 없다");
    }
    auto j = current();
    if (!j) {
        return std::unexpected(j.error());
    }
    const UiNodePath parentPath(path.begin(), path.end() - 1);
    Json* p = nodeAt(*j, parentPath);
    if (p == nullptr || nodeAt(*j, path) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("노드 {} 가 없다", describe(path)));
    }
    if (!p->contains("children") || !(*p)["children"].is_array()) {
        return makeError(ErrorCode::InvalidArgument, "list 의 항목 틀은 지우거나 옮기지 않는다 — 속성을 고친다");
    }
    Json& kids = (*p)["children"];
    const i64 from = path.back();
    const i64 to = std::clamp<i64>(from + delta, 0, static_cast<i64>(kids.size()) - 1);
    UiNodePath moved = path;
    moved.back() = static_cast<u32>(to);
    if (to == from) {
        return moved;
    }
    Json item = kids[static_cast<usize>(from)];
    kids.erase(kids.begin() + from);
    kids.insert(kids.begin() + to, std::move(item));
    if (auto r = commit(*j, std::format("UI {} 옮기기", describe(path))); !r) {
        return std::unexpected(r.error());
    }
    return moved;
}

Expected<void> UiDesignService::setPlacement(const UiNodePath& path, std::array<f32, 2> anchorMin,
                                             std::array<f32, 2> anchorMax, std::array<f32, 4> offset) {
    auto j = current();
    if (!j) {
        return std::unexpected(j.error());
    }
    Json* n = nodeAt(*j, path);
    if (n == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("노드 {} 가 없다", describe(path)));
    }
    (*n)["anchor"] = Json{{"min", {anchorMin[0], anchorMin[1]}}, {"max", {anchorMax[0], anchorMax[1]}}};
    (*n)["offset"] = {offset[0], offset[1], offset[2], offset[3]};
    return commit(*j, std::format("UI {} 자리", describe(path)));
}

std::vector<UiDesignRect> UiDesignService::layout(i32 width, i32 height, f32 scale) {
    std::vector<UiDesignRect> out;
    if (!m_doc || !m_docs.isOpen(*m_doc)) {
        return out;
    }
    const ui::UiParseResult parsed = ui::parseUiDocument(stem(), m_docs.text(*m_doc), ui::UiCheckContext{});
    if (!parsed.document) {
        return out;
    }
    // 노드 주소 → 경로 (배치 결과는 문서의 노드를 가리킨다)
    std::map<const ui::UiNode*, UiNodePath> paths;
    const auto walk = [&](auto&& self, const ui::UiNode& n, UiNodePath path) -> void {
        paths[&n] = path;
        for (u32 i = 0; i < n.children.size(); ++i) {
            UiNodePath child = path;
            child.push_back(i);
            self(self, n.children[i], std::move(child));
        }
    };
    walk(walk, parsed.document->root, {});
    ui::UiLayoutOptions options;
    options.width = width;
    options.height = height;
    options.scale = scale;
    const ui::FixedTextMeasure measure;
    for (const ui::UiLayoutNode& l : ui::layoutDocument(*parsed.document, options, measure)) {
        if (const auto it = paths.find(l.node); it != paths.end()) {
            out.push_back({it->second, l.rect, l.depth});
        }
    }
    return out;
}

Expected<void> UiDesignService::save(bool force) {
    if (!m_doc) {
        return makeError(ErrorCode::InvalidArgument, "열린 UI 문서가 없다");
    }
    const ui::UiParseResult parsed = ui::parseUiDocument(stem(), m_docs.text(*m_doc), ui::UiCheckContext{});
    for (const ui::UiIssue& i : parsed.issues) {
        if (i.severity == ui::UiIssue::Severity::Error) {
            return makeError(
                ErrorCode::ValidationFailed,
                std::format("UI 문서에 오류가 있어 저장하지 않았다 — {} {}: {}", i.rule, i.pointer, i.message),
                m_docs.path(*m_doc).string());
        }
    }
    return m_docs.save(*m_doc, force);
}

} // namespace sbx::editor::services
