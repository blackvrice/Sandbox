#include "editor/ui/TriggerForm.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <optional>

#include <imgui.h>

namespace sbx::editor {
namespace {

using trigger::NodeCategory;
using trigger::ParamSpec;
using trigger::ParamType;

constexpr std::array<const char*, 6> kOps{"<", "<=", ">", ">=", "==", "!="};
constexpr std::array<const char*, 3> kResults{"win", "lose", "draw"};

[[nodiscard]] f32 dpi() {
    return ImGui::GetStyle().FontScaleDpi;
}

[[nodiscard]] std::string_view categoryLabel(NodeCategory c) {
    switch (c) {
    case NodeCategory::Event:
        return "이벤트";
    case NodeCategory::Condition:
        return "조건";
    case NodeCategory::Effect:
        return "효과";
    }
    return "?";
}

[[nodiscard]] ecs::Json defaultValue(const ParamSpec& p, const content::ContentDatabase* db, std::string_view pack) {
    switch (p.type) {
    case ParamType::Number:
        return std::clamp(1.0, p.min, p.max); // 초 · 확률 · 값 — 1 (타이머는 1 틱 이상이어야 한다)
    case ParamType::Integer:
        return static_cast<i64>(std::clamp(1.0, p.min, p.max));
    case ParamType::Bool:
        return false;
    case ParamType::Tags:
        return ecs::Json{{"all", ecs::Json::array()}};
    case ParamType::Op:
        return ">=";
    case ParamType::Vec2:
        return p.key == "max" ? ecs::Json::array({4, 4}) : ecs::Json::array({0, 0});
    case ParamType::Field:
        return "life.energy.value";
    case ParamType::Prefab:
        return db != nullptr && !db->prefabs().empty() ? db->prefabs().front().id : std::string();
    case ParamType::Tag:
        return db != nullptr && !db->tags().empty() ? db->tags().front() : std::string();
    case ParamType::Team:
        return "";
    case ParamType::Result:
        return "win";
    case ParamType::EventName:
        return std::format("{}.event", pack.empty() ? "pack" : pack);
    }
    return nullptr;
}

// std::string 을 ImGui 글자 칸에 (ContentPanel 의 것과 같은 크기 바꾸기 콜백)
int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<usize>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

bool inputString(const char* label, std::string& text, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputText(label, text.data(), text.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                            resizeCallback, &text);
}

// JSON 문자열 값 하나를 글자 칸으로
bool editString(const char* label, ecs::Json& v) {
    std::string s = v.is_string() ? v.get<std::string>() : std::string();
    if (inputString(label, s)) {
        v = s;
        return true;
    }
    return false;
}

bool comboOf(const char* label, ecs::Json& v, const std::vector<std::string>& items) {
    const std::string cur = v.is_string() ? v.get<std::string>() : std::string();
    bool changed = false;
    if (ImGui::BeginCombo(label, cur.c_str())) {
        for (const std::string& item : items) {
            if (ImGui::Selectable(item.c_str(), item == cur)) {
                v = item;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// 태그 식 {"all": [...], "any": [...], "none": [...]} — 칸마다 쉼표로 나눈 이름
bool editTags(ecs::Json& v) {
    if (!v.is_object()) {
        v = ecs::Json{{"all", ecs::Json::array()}};
    }
    bool changed = false;
    for (const char* key : {"all", "any", "none"}) {
        std::string joined;
        if (v.contains(key) && v[key].is_array()) {
            for (const ecs::Json& t : v[key]) {
                joined += (joined.empty() ? "" : ", ") + (t.is_string() ? t.get<std::string>() : std::string());
            }
        }
        ImGui::SetNextItemWidth(180.f * dpi());
        if (inputString(std::format("{}##tags_{}", key, key).c_str(), joined)) {
            ecs::Json list = ecs::Json::array();
            usize start = 0;
            while (start <= joined.size()) {
                const usize comma = joined.find(',', start);
                std::string name = joined.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                name.erase(0, name.find_first_not_of(' '));
                name.erase(name.find_last_not_of(' ') + 1);
                if (!name.empty()) {
                    list.push_back(name);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            if (list.empty() && std::string_view(key) != "all") {
                v.erase(key);
            } else {
                v[key] = std::move(list);
            }
            changed = true;
        }
    }
    return changed;
}

bool editParam(const ParamSpec& p, ecs::Json& v, const content::ContentDatabase& db) {
    const std::string label = std::format("{}##{}", p.label.empty() ? p.key : p.label, p.key);
    ImGui::SetNextItemWidth(180.f * dpi());
    switch (p.type) {
    case ParamType::Number: {
        double d = v.is_number() ? v.get<double>() : 0.0;
        if (ImGui::InputDouble(label.c_str(), &d, 0.0, 0.0, "%.3f")) {
            v = std::clamp(d, p.min, p.max);
            return true;
        }
        return false;
    }
    case ParamType::Integer: {
        // 범위가 int 를 넘을 수 있다 (점수 ±10억 — int 안) — 자르고 넣는다
        constexpr i64 kLo = std::numeric_limits<int>::min();
        constexpr i64 kHi = std::numeric_limits<int>::max();
        int n = v.is_number_integer() ? static_cast<int>(std::clamp<i64>(v.get<i64>(), kLo, kHi)) : 0;
        if (ImGui::InputInt(label.c_str(), &n)) {
            v = static_cast<i64>(std::clamp(static_cast<f64>(n), p.min, p.max));
            return true;
        }
        return false;
    }
    case ParamType::Bool: {
        bool b = v.is_boolean() && v.get<bool>();
        if (ImGui::Checkbox(label.c_str(), &b)) {
            v = b;
            return true;
        }
        return false;
    }
    case ParamType::Tags:
        ImGui::TextUnformatted(label.substr(0, label.find("##")).c_str());
        ImGui::Indent();
        {
            const bool c = editTags(v);
            ImGui::Unindent();
            return c;
        }
    case ParamType::Op:
        return comboOf(label.c_str(), v, {kOps.begin(), kOps.end()});
    case ParamType::Vec2: {
        std::array<float, 2> xy{0.f, 0.f};
        if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
            xy = {v[0].get<float>(), v[1].get<float>()};
        }
        if (ImGui::InputFloat2(label.c_str(), xy.data(), "%.2f")) {
            v = ecs::Json::array({xy[0], xy[1]});
            return true;
        }
        return false;
    }
    case ParamType::Prefab: {
        std::vector<std::string> ids;
        for (const content::Prefab& pf : db.prefabs()) {
            ids.push_back(pf.id);
        }
        return comboOf(label.c_str(), v, ids);
    }
    case ParamType::Tag:
        return comboOf(label.c_str(), v, {db.tags().begin(), db.tags().end()});
    case ParamType::Result:
        return comboOf(label.c_str(), v, {kResults.begin(), kResults.end()});
    case ParamType::Field:
    case ParamType::Team:
    case ParamType::EventName:
        return editString(label.c_str(), v);
    }
    return false;
}

} // namespace

ecs::Json triggerTemplate(const std::string& id) {
    const std::string pack = id.substr(0, id.find('.'));
    return ecs::Json{
        {"id", id},
        {"nodes", ecs::Json::array({ecs::Json{{"id", "start"}, {"type", "event.game_start"}},
                                    ecs::Json{{"id", "say"}, {"type", "effect.event"}, {"name", pack + ".started"}}})},
        {"links", ecs::Json::array({ecs::Json{{"from", "start"}, {"to", "say"}}})}};
}

ecs::Json defaultTriggerNode(const trigger::NodeTypeSpec& spec, std::string id, const content::ContentDatabase* db,
                             std::string_view pack) {
    ecs::Json n{{"id", std::move(id)}, {"type", std::string(spec.type)}};
    for (const ParamSpec& p : spec.params) {
        if (p.required) {
            n[std::string(p.key)] = defaultValue(p, db, pack);
        }
    }
    return n;
}

std::string freeNodeId(const ecs::Json& trigger, std::string_view base) {
    const auto taken = [&](const std::string& id) {
        if (!trigger.contains("nodes") || !trigger["nodes"].is_array()) {
            return false;
        }
        return std::any_of(trigger["nodes"].begin(), trigger["nodes"].end(),
                           [&](const ecs::Json& n) { return n.is_object() && n.value("id", "") == id; });
    };
    std::string id(base);
    for (int i = 2; taken(id); ++i) {
        id = std::format("{}_{}", base, i);
    }
    return id;
}

bool removeTriggerNode(ecs::Json& trigger, std::string_view nodeId) {
    if (!trigger.contains("nodes") || !trigger["nodes"].is_array()) {
        return false;
    }
    ecs::Json& nodes = trigger["nodes"];
    const auto it = std::find_if(nodes.begin(), nodes.end(),
                                 [&](const ecs::Json& n) { return n.is_object() && n.value("id", "") == nodeId; });
    if (it == nodes.end()) {
        return false;
    }
    nodes.erase(it);
    if (trigger.contains("links") && trigger["links"].is_array()) {
        ecs::Json kept = ecs::Json::array();
        for (const ecs::Json& l : trigger["links"]) {
            if (!l.is_object() || (l.value("from", "") != nodeId && l.value("to", "") != nodeId)) {
                kept.push_back(l);
            }
        }
        trigger["links"] = std::move(kept);
    }
    return true;
}

std::string describeTriggerNode(const ecs::Json& node) {
    const std::string type = node.is_object() ? node.value("type", "") : std::string();
    const std::string id = node.is_object() ? node.value("id", "") : std::string();
    const trigger::NodeTypeSpec* spec = trigger::findNodeType(type);
    if (spec == nullptr) {
        return std::format("[?] {} · {}", type, id);
    }
    return std::format("[{}] {} · {}", categoryLabel(spec->category), spec->label, id);
}

std::vector<std::string> describeProgram(const content::TriggerDef& t) {
    static constexpr std::array<const char*, 6> kCond{"개체 수", "점수", "경과 시간", "대상 필드", "대상 태그", "확률"};
    static constexpr std::array<const char*, 8> kEffect{"field.add", "field.set",  "destroy", "spawn",
                                                        "tag.add",   "tag.remove", "event",   "score.add"};
    std::vector<std::string> out;
    for (usize i = 0; i < t.program.size(); ++i) {
        const content::TriggerStep& s = t.program[i];
        switch (s.op) {
        case content::TriggerStep::Op::Cond:
            out.push_back(std::format("{:>2}  조건 #{} {} — 거짓이면 {}", i, s.index,
                                      kCond.at(static_cast<usize>(t.conditions.at(s.index).kind)), s.jump));
            break;
        case content::TriggerStep::Op::Effect: {
            const content::TriggerEffect& x = t.effects.at(s.index);
            out.push_back(std::format(
                "{:>2}  효과 #{} {}", i, s.index,
                x.kind == content::TriggerEffectKind::GameEnd ? "game.end" : kEffect.at(static_cast<usize>(x.fx.op))));
            break;
        }
        case content::TriggerStep::Op::Jump:
            out.push_back(std::format("{:>2}  건너뛰기 → {}", i, s.jump));
            break;
        }
    }
    return out;
}

bool drawTriggerForm(ecs::Json& t, TriggerFormState& st, const content::ContentDatabase& db, bool readOnly) {
    bool changed = false;
    const std::string id = t.value("id", "");
    const std::string pack = id.substr(0, id.find('.'));
    ImGui::BeginDisabled(readOnly);

    // 이름 · 예산
    {
        ecs::Json name = t.contains("name") ? t["name"] : ecs::Json(id);
        ImGui::SetNextItemWidth(220.f * dpi());
        if (editString("이름", name)) {
            t["name"] = name;
            changed = true;
        }
        ImGui::SameLine();
        int perTick = t.contains("limits") && t["limits"].is_object() ? t["limits"].value("perTick", 1) : 1;
        ImGui::SetNextItemWidth(90.f * dpi());
        if (ImGui::InputInt("틱마다", &perTick)) {
            t["limits"] =
                ecs::Json{{"perTick", std::clamp(perTick, 1, static_cast<int>(content::kMaxTriggerFiresPerTick))}};
            changed = true;
        }
    }
    if (!t.contains("nodes") || !t["nodes"].is_array()) {
        t["nodes"] = ecs::Json::array();
    }
    if (!t.contains("links") || !t["links"].is_array()) {
        t["links"] = ecs::Json::array();
    }

    // 노드
    ImGui::SeparatorText(std::format("노드 {}", t["nodes"].size()).c_str());
    std::string removeNode;
    for (usize i = 0; i < t["nodes"].size(); ++i) {
        ecs::Json& n = t["nodes"][i];
        if (!n.is_object()) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));
        const trigger::NodeTypeSpec* spec = trigger::findNodeType(n.value("type", ""));
        if (ImGui::CollapsingHeader(describeTriggerNode(n).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ecs::Json nid = n["id"];
            ImGui::SetNextItemWidth(140.f * dpi());
            if (editString("id", nid)) {
                // 연결도 따라간다
                const std::string from = n.value("id", "");
                for (ecs::Json& l : t["links"]) {
                    for (const char* k : {"from", "to"}) {
                        if (l.is_object() && l.value(k, "") == from) {
                            l[k] = nid;
                        }
                    }
                }
                n["id"] = nid;
                changed = true;
            }
            if (spec == nullptr) {
                ImGui::TextColored({1.f, 0.5f, 0.4f, 1.f}, "모르는 종류 — JSON 탭에서 고치십시오");
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", std::string(spec->type).c_str());
                for (const ParamSpec& p : spec->params) {
                    ImGui::PushID(std::string(p.key).c_str());
                    const std::string key(p.key);
                    if (!p.required) {
                        bool used = n.contains(key);
                        if (ImGui::Checkbox("##use", &used)) {
                            if (used) {
                                n[key] = defaultValue(p, &db, pack);
                            } else {
                                n.erase(key);
                            }
                            changed = true;
                        }
                        ImGui::SameLine();
                    }
                    if (n.contains(key)) {
                        changed = editParam(p, n[key], db) || changed;
                    } else {
                        ImGui::TextDisabled("%s (쓰지 않음)", std::string(p.label).c_str());
                    }
                    ImGui::PopID();
                }
            }
            if (ImGui::SmallButton("노드 지우기")) {
                removeNode = n.value("id", "");
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    if (!removeNode.empty()) {
        changed = removeTriggerNode(t, removeNode) || changed;
    }
    // 노드 더하기
    const auto types = trigger::nodeTypes();
    st.addType = std::clamp(st.addType, 0, static_cast<int>(types.size()) - 1);
    ImGui::SetNextItemWidth(240.f * dpi());
    const std::string current = std::format("{} · {}", categoryLabel(types[static_cast<usize>(st.addType)].category),
                                            types[static_cast<usize>(st.addType)].label);
    if (ImGui::BeginCombo("##addtype", current.c_str())) {
        for (usize k = 0; k < types.size(); ++k) {
            const std::string item =
                std::format("{} · {}  ({})", categoryLabel(types[k].category), types[k].label, types[k].type);
            if (ImGui::Selectable(item.c_str(), static_cast<int>(k) == st.addType)) {
                st.addType = static_cast<int>(k);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("노드 더하기") && t["nodes"].size() < content::kMaxTriggerNodes) {
        const trigger::NodeTypeSpec& spec = types[static_cast<usize>(st.addType)];
        const std::string_view base = spec.type.substr(spec.type.rfind('.') + 1);
        t["nodes"].push_back(defaultTriggerNode(spec, freeNodeId(t, base), &db, pack));
        changed = true;
    }

    // 연결
    ImGui::SeparatorText(std::format("연결 {}", t["links"].size()).c_str());
    std::vector<std::string> nodeIds;
    for (const ecs::Json& n : t["nodes"]) {
        if (n.is_object()) {
            nodeIds.push_back(n.value("id", ""));
        }
    }
    std::optional<usize> removeLink;
    for (usize i = 0; i < t["links"].size(); ++i) {
        ecs::Json& l = t["links"][i];
        if (!l.is_object()) {
            continue;
        }
        ImGui::PushID(static_cast<int>(1000 + i));
        ecs::Json from = l.contains("from") ? l["from"] : ecs::Json("");
        ecs::Json to = l.contains("to") ? l["to"] : ecs::Json("");
        ImGui::SetNextItemWidth(120.f * dpi());
        if (comboOf("##from", from, nodeIds)) {
            l["from"] = from;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("→");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.f * dpi());
        if (comboOf("##to", to, nodeIds)) {
            l["to"] = to;
            changed = true;
        }
        // else 는 조건 노드에서만
        const auto fromNode = std::find_if(t["nodes"].begin(), t["nodes"].end(), [&](const ecs::Json& n) {
            return n.is_object() && n.value("id", "") == l.value("from", "");
        });
        const trigger::NodeTypeSpec* fromSpec =
            fromNode != t["nodes"].end() ? trigger::findNodeType(fromNode->value("type", "")) : nullptr;
        if (fromSpec != nullptr && fromSpec->category == NodeCategory::Condition) {
            ImGui::SameLine();
            bool otherwise = l.value("port", "then") == "else";
            if (ImGui::Checkbox("else", &otherwise)) {
                if (otherwise) {
                    l["port"] = "else";
                } else {
                    l.erase("port");
                }
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("지우기")) {
            removeLink = i;
        }
        ImGui::PopID();
    }
    if (removeLink) {
        t["links"].erase(*removeLink);
        changed = true;
    }
    if (ImGui::Button("연결 더하기") && nodeIds.size() >= 2) {
        t["links"].push_back(ecs::Json{{"from", nodeIds.front()}, {"to", nodeIds.back()}});
        changed = true;
    }
    ImGui::EndDisabled();
    return changed && !readOnly;
}

} // namespace sbx::editor
