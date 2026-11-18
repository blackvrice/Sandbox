#include "editor/ui/TriggerCanvas.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include <imgui.h>

#include "editor/ui/TriggerForm.hpp"

namespace sbx::editor {

namespace {

ecs::Json* findNode(ecs::Json& t, std::string_view id) {
    if (!t.contains("nodes") || !t["nodes"].is_array()) {
        return nullptr;
    }
    for (ecs::Json& n : t["nodes"]) {
        if (n.is_object() && n.value("id", "") == id) {
            return &n;
        }
    }
    return nullptr;
}

bool sameLink(const ecs::Json& l, std::string_view from, std::string_view to, bool otherwise) {
    return l.is_object() && l.value("from", "") == from && l.value("to", "") == to &&
           (l.value("port", "then") == "else") == otherwise;
}

} // namespace

std::vector<TriggerNodeLayout> layoutTrigger(const ecs::Json& t) {
    std::vector<TriggerNodeLayout> out;
    if (!t.is_object() || !t.contains("nodes") || !t["nodes"].is_array()) {
        return out;
    }
    std::map<std::string, usize, std::less<>> index;
    for (const ecs::Json& n : t["nodes"]) {
        if (!n.is_object()) {
            continue;
        }
        TriggerNodeLayout l;
        l.id = n.value("id", "");
        l.type = n.value("type", "");
        if (const trigger::NodeTypeSpec* spec = trigger::findNodeType(l.type)) {
            l.category = spec->category;
        }
        if (n.contains("pos") && n["pos"].is_array() && n["pos"].size() == 2 && n["pos"][0].is_number() &&
            n["pos"][1].is_number()) {
            l.pos = {static_cast<f32>(n["pos"][0].get<f64>()), static_cast<f32>(n["pos"][1].get<f64>())};
            l.placed = true;
        }
        index.emplace(l.id, out.size());
        out.push_back(std::move(l));
    }
    // 깊이: 들어오는 연결이 없는 노드 = 0, 그 밖은 앞 노드 + 1 (여러 길이면 가장 긴 것 — 순환은 노드 수에서 멈춘다)
    std::vector<std::pair<usize, usize>> edges;
    if (t.contains("links") && t["links"].is_array()) {
        for (const ecs::Json& l : t["links"]) {
            if (!l.is_object()) {
                continue;
            }
            const auto a = index.find(l.value("from", ""));
            const auto b = index.find(l.value("to", ""));
            if (a != index.end() && b != index.end() && a->second != b->second) {
                edges.emplace_back(a->second, b->second);
            }
        }
    }
    std::vector<usize> depth(out.size(), 0);
    for (usize round = 0; round < out.size(); ++round) {
        bool moved = false;
        for (const auto& [a, b] : edges) {
            if (depth[b] < depth[a] + 1 && depth[a] + 1 < out.size()) {
                depth[b] = depth[a] + 1;
                moved = true;
            }
        }
        if (!moved) {
            break;
        }
    }
    std::map<usize, usize> rowOf; // 깊이 → 다음 행
    for (usize i = 0; i < out.size(); ++i) {
        const usize row = rowOf[depth[i]]++;
        if (!out[i].placed) {
            out[i].pos = {static_cast<f32>(depth[i]) * kTriggerColumn, static_cast<f32>(row) * kTriggerRow};
        }
    }
    return out;
}

bool setTriggerNodePos(ecs::Json& t, std::string_view nodeId, Vec2 pos) {
    ecs::Json* n = findNode(t, nodeId);
    if (n == nullptr || !std::isfinite(pos.x) || !std::isfinite(pos.y)) {
        return false;
    }
    (*n)["pos"] = ecs::Json::array({std::lround(pos.x), std::lround(pos.y)});
    return true;
}

std::optional<std::string> linkTriggerNodes(ecs::Json& t, std::string_view from, std::string_view to, bool otherwise) {
    if (from == to) {
        return std::string("자기 자신에 잇지 않는다");
    }
    const ecs::Json* a = findNode(t, from);
    const ecs::Json* b = findNode(t, to);
    if (a == nullptr || b == nullptr) {
        return std::format("없는 노드 ('{}' → '{}')", from, to);
    }
    const trigger::NodeTypeSpec* to_ = trigger::findNodeType(b->value("type", ""));
    if (to_ != nullptr && to_->category == trigger::NodeCategory::Event) {
        return std::string("이벤트 노드로는 들어가지 않는다 (나무의 뿌리)");
    }
    const trigger::NodeTypeSpec* from_ = trigger::findNodeType(a->value("type", ""));
    if (otherwise && (from_ == nullptr || from_->category != trigger::NodeCategory::Condition)) {
        return std::string("else 는 조건 노드에서만");
    }
    if (!t.contains("links") || !t["links"].is_array()) {
        t["links"] = ecs::Json::array();
    }
    for (const ecs::Json& l : t["links"]) {
        if (sameLink(l, from, to, otherwise)) {
            return std::string("이미 있는 연결");
        }
    }
    ecs::Json l{{"from", std::string(from)}, {"to", std::string(to)}};
    if (otherwise) {
        l["port"] = "else";
    }
    t["links"].push_back(std::move(l));
    return std::nullopt;
}

bool unlinkTriggerNodes(ecs::Json& t, std::string_view from, std::string_view to, bool otherwise) {
    if (!t.contains("links") || !t["links"].is_array()) {
        return false;
    }
    ecs::Json& links = t["links"];
    for (usize i = 0; i < links.size(); ++i) {
        if (sameLink(links[i], from, to, otherwise)) {
            links.erase(i);
            return true;
        }
    }
    return false;
}

bool drawTriggerCanvas(ecs::Json& t, TriggerCanvasState& st, bool readOnly) {
    const f32 dpi = ImGui::GetStyle().FontScaleDpi;
    const std::vector<TriggerNodeLayout> nodes = layoutTrigger(t);
    bool changed = false;
    if (!st.message.empty()) {
        ImGui::TextColored({1.f, 0.6f, 0.4f, 1.f}, "%s", st.message.c_str());
    } else {
        ImGui::TextDisabled(
            "상자를 끌어 옮기기 · 오른쪽 점에서 끌어 잇기 (Shift = else) · 연결 점 오른쪽 클릭 = 지우기");
    }
    if (!ImGui::BeginChild("##trigger_canvas", {0, 0}, ImGuiChildFlags_Borders,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        ImGui::EndChild();
        return false;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##canvas_bg", {std::max(avail.x, 1.f), std::max(avail.y, 1.f)},
                           ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        st.pan.x += d.x;
        st.pan.y += d.y;
    }
    const auto toScreen = [&](Vec2 p) {
        return ImVec2{origin.x + st.pan.x + p.x * dpi, origin.y + st.pan.y + p.y * dpi};
    };
    const f32 w = kTriggerNodeWidth * dpi;
    const f32 h = kTriggerNodeHeight * dpi;
    std::map<std::string, Vec2, std::less<>> posOf;
    for (const TriggerNodeLayout& n : nodes) {
        posOf[n.id] = n.pos;
    }
    // 연결
    std::optional<std::tuple<std::string, std::string, bool>> unlink;
    if (t.contains("links") && t["links"].is_array()) {
        for (usize i = 0; i < t["links"].size(); ++i) {
            const ecs::Json& l = t["links"][i];
            if (!l.is_object()) {
                continue;
            }
            const auto a = posOf.find(l.value("from", ""));
            const auto b = posOf.find(l.value("to", ""));
            if (a == posOf.end() || b == posOf.end()) {
                continue;
            }
            const bool otherwise = l.value("port", "then") == "else";
            const ImVec2 p0 = toScreen(a->second);
            const ImVec2 p1 = toScreen(b->second);
            const ImVec2 s{p0.x + w, p0.y + h * 0.5f};
            const ImVec2 e{p1.x, p1.y + h * 0.5f};
            const ImU32 col = otherwise ? IM_COL32(230, 110, 90, 255) : IM_COL32(170, 190, 220, 255);
            dl->AddBezierCubic(s, {s.x + 60 * dpi, s.y}, {e.x - 60 * dpi, e.y}, e, col, 2.f * dpi);
            const ImVec2 mid{(s.x + e.x) * 0.5f, (s.y + e.y) * 0.5f};
            dl->AddCircleFilled(mid, 4.f * dpi, col);
            ImGui::SetCursorScreenPos({mid.x - 6 * dpi, mid.y - 6 * dpi});
            ImGui::PushID(static_cast<int>(i));
            ImGui::InvisibleButton("##link", {12 * dpi, 12 * dpi}, ImGuiButtonFlags_MouseButtonRight);
            if (!readOnly && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                unlink = std::make_tuple(l.value("from", ""), l.value("to", ""), otherwise);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s → %s%s (오른쪽 클릭 = 지우기)", l.value("from", "").c_str(),
                                  l.value("to", "").c_str(), otherwise ? " · else" : "");
            }
            ImGui::PopID();
        }
    }
    if (unlink) {
        changed |= unlinkTriggerNodes(t, std::get<0>(*unlink), std::get<1>(*unlink), std::get<2>(*unlink));
    }
    // 노드
    std::optional<std::string> hoveredNode;
    for (usize i = 0; i < nodes.size(); ++i) {
        const TriggerNodeLayout& n = nodes[i];
        const ImVec2 p = toScreen(n.pos);
        const ImU32 fill = !n.category                                       ? IM_COL32(90, 90, 90, 255)
                           : *n.category == trigger::NodeCategory::Event     ? IM_COL32(60, 110, 70, 255)
                           : *n.category == trigger::NodeCategory::Condition ? IM_COL32(120, 100, 50, 255)
                                                                             : IM_COL32(60, 80, 130, 255);
        dl->AddRectFilled(p, {p.x + w, p.y + h}, fill, 6.f * dpi);
        dl->AddRect(p, {p.x + w, p.y + h}, IM_COL32(220, 220, 220, 120), 6.f * dpi);
        const ecs::Json* src = findNode(t, n.id);
        const std::string label = src != nullptr ? describeTriggerNode(*src) : n.id;
        dl->PushClipRect(p, {p.x + w, p.y + h}, true);
        dl->AddText({p.x + 6 * dpi, p.y + 6 * dpi}, IM_COL32_WHITE, label.c_str());
        dl->PopClipRect();
        ImGui::PushID(static_cast<int>(10000 + i));
        // 몸통 — 끌어 옮기기
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##node", {w - 14 * dpi, h});
        if (ImGui::IsItemHovered()) {
            hoveredNode = n.id;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            if (!readOnly && (d.x != 0.f || d.y != 0.f)) {
                changed |= setTriggerNodePos(t, n.id, {n.pos.x + d.x / dpi, n.pos.y + d.y / dpi});
            }
        }
        // 오른쪽 점 — 잇기 시작
        const ImVec2 port{p.x + w, p.y + h * 0.5f};
        dl->AddCircleFilled(port, 5.f * dpi, IM_COL32(240, 240, 240, 255));
        ImGui::SetCursorScreenPos({port.x - 7 * dpi, port.y - 7 * dpi});
        ImGui::InvisibleButton("##port", {14 * dpi, 14 * dpi});
        if (!readOnly && ImGui::IsItemActivated()) {
            st.linking = n.id;
        }
        ImGui::PopID();
    }
    // 잇는 중 — 선을 그리고, 놓은 곳의 노드로
    if (st.linking) {
        const auto a = posOf.find(*st.linking);
        if (a != posOf.end()) {
            const ImVec2 p0 = toScreen(a->second);
            dl->AddLine({p0.x + w, p0.y + h * 0.5f}, ImGui::GetIO().MousePos, IM_COL32(255, 255, 255, 200), 2.f * dpi);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hoveredNode && *hoveredNode != *st.linking) {
                const auto why = linkTriggerNodes(t, *st.linking, *hoveredNode, ImGui::GetIO().KeyShift);
                st.message = why.value_or("");
                changed |= !why.has_value();
            }
            st.linking.reset();
        }
    }
    ImGui::EndChild();
    return changed && !readOnly;
}

} // namespace sbx::editor
