#include "apps/client/PlayKeys.hpp"

#include <format>
#include <map>
#include <tuple>

#include <nlohmann/json.hpp>

namespace sbx::client {

PlayKeys buildPlayKeys(std::span<const PackInput> packs, const platform::ActionMap& user) {
    PlayKeys out;
    std::vector<std::pair<std::string, PlayContext>> pending;                       // 팩 차례 (뒤 것이 이긴다)
    std::vector<std::tuple<std::string, nlohmann::json, std::string>> triggerSpecs; // 이름 · 정의 · 파일 (팩 차례)
    for (const PackInput& p : packs) {
        if (p.text.empty()) {
            continue;
        }
        const std::string source = std::format("{}/presentation/input.json", p.pack);
        // "contexts" (v0.10 RT-06-8 조각 2) 는 팩만의 키 — 떼어 내고 나머지를 ActionMap 이 읽는다
        std::string text = p.text;
        std::vector<std::pair<std::string, PlayContext>> contexts;
        // "triggers" (v0.12 RT-06-13) 도 팩만의 키 — 떼어 두고 모든 팩을 합친 뒤 푼다
        if (nlohmann::json j = nlohmann::json::parse(text, nullptr, false); j.is_object() && j.contains("triggers")) {
            if (!j["triggers"].is_object()) {
                out.warnings.push_back(std::format("{}: \"triggers\" 는 객체 — 무시한다", source));
            } else {
                for (const auto& [name, spec] : j["triggers"].items()) {
                    triggerSpecs.emplace_back(name, spec, source);
                }
            }
            j.erase("triggers");
            text = j.dump();
        }
        if (nlohmann::json j = nlohmann::json::parse(text, nullptr, false); j.is_object() && j.contains("contexts")) {
            const nlohmann::json& c = j["contexts"];
            if (!c.is_object()) {
                out.warnings.push_back(std::format("{}: \"contexts\" 는 객체 — 무시한다", source));
            } else {
                for (const auto& [name, value] : c.items()) {
                    const std::string v = value.is_string() ? value.get<std::string>() : std::string();
                    if (v != "game" && v != "menu" && v != "always") {
                        out.warnings.push_back(
                            std::format("{}: contexts.{} = {} — \"game\" · \"menu\" · \"always\" 중 하나 (무시한다)",
                                        source, name, value.dump()));
                        continue;
                    }
                    contexts.emplace_back(name, v == "menu"     ? PlayContext::Menu
                                                : v == "always" ? PlayContext::Always
                                                                : PlayContext::Game);
                }
            }
            j.erase("contexts");
            text = j.dump();
        }
        auto parsed = platform::ActionMap::parse(text, source);
        if (!parsed) {
            out.warnings.push_back(parsed.error().describe());
            continue;
        }
        for (usize i = 0; i < parsed->size(); ++i) {
            const std::string_view name = parsed->name(static_cast<platform::ActionId>(i));
            if (!name.starts_with(kPlayPrefix)) {
                out.warnings.push_back(std::format("{}: '{}' 는 무시한다 (팩은 play.* 만 바꿀 수 있다)", source, name));
            }
        }
        out.map.merge(parsed->withPrefix(kPlayPrefix));
        for (auto& c : contexts) {
            if (!c.first.starts_with(kPlayPrefix) || !parsed->find(c.first)) {
                out.warnings.push_back(
                    std::format("{}: contexts.{} — 이 파일의 play.* 바인딩이 아니다 (무시한다)", source, c.first));
                continue;
            }
            pending.push_back(std::move(c)); // 뒤 팩이 이긴다 — 이름으로 마지막 것
        }
    }
    out.map.merge(user.withPrefix(kPlayPrefix)); // 사용자 파일이 마지막에 이긴다
    // 트리거 (v0.12 RT-06-13) — 같은 이름은 뒤 팩이 이긴다. 가상 키를 바인딩 없이 더한다 (ActionId 는 이름 순이라 다
    // 더한 뒤 푼다)
    std::map<std::string, std::pair<nlohmann::json, std::string>, std::less<>> specs;
    for (auto& [name, spec, source] : triggerSpecs) {
        if (!name.starts_with(kPlayPrefix)) {
            out.warnings.push_back(std::format("{}: triggers.{} — play.* 만 (무시한다)", source, name));
            continue;
        }
        if (out.map.find(name) && !specs.contains(name)) {
            out.warnings.push_back(
                std::format("{}: triggers.{} — 바인딩이 있는 키와 같은 이름 (무시한다)", source, name));
            continue;
        }
        specs[name] = {std::move(spec), std::move(source)};
    }
    struct Parsed {
        std::string name;
        TriggerKind kind;
        std::vector<std::string> sources;
        f64 seconds;
    };
    std::vector<Parsed> valid; // 이름 차례
    for (const auto& [name, v] : specs) {
        const auto& [spec, source] = v;
        const auto bad = [&](std::string_view why) {
            out.warnings.push_back(std::format("{}: triggers.{} — {} (무시한다)", source, name, why));
        };
        if (!spec.is_object() || !spec.contains("ms") || !spec["ms"].is_number() ||
            !(spec["ms"].get<f64>() >= 1.0 && spec["ms"].get<f64>() <= 5000.0)) {
            bad("{ \"hold\" | \"doubleTap\" | \"combo\": …, \"ms\": 1 ~ 5000 }");
            continue;
        }
        Parsed t{name, TriggerKind::Hold, {}, spec["ms"].get<f64>() / 1000.0};
        if (spec.contains("hold") && spec["hold"].is_string()) {
            t.sources.push_back(spec["hold"].get<std::string>());
        } else if (spec.contains("doubleTap") && spec["doubleTap"].is_string()) {
            t.kind = TriggerKind::DoubleTap;
            t.sources.push_back(spec["doubleTap"].get<std::string>());
        } else if (spec.contains("combo") && spec["combo"].is_array() && spec["combo"].size() >= 2 &&
                   spec["combo"].size() <= kMaxComboSteps) {
            t.kind = TriggerKind::Combo;
            for (const auto& c : spec["combo"]) {
                t.sources.push_back(c.is_string() ? c.get<std::string>() : std::string());
            }
        } else {
            bad(std::format("hold · doubleTap (키 이름) 또는 combo (키 이름 2 ~ {} 개) 가 필요하다", kMaxComboSteps));
            continue;
        }
        // 원본은 바인딩이 있는 play.* 키 (트리거 위의 트리거는 [계획])
        const auto missing = std::ranges::find_if(
            t.sources, [&](const std::string& src) { return !out.map.find(src) || specs.contains(src); });
        if (missing != t.sources.end()) {
            bad(std::format("원본 '{}' 는 바인딩이 있는 play.* 키여야 한다", *missing));
            continue;
        }
        valid.push_back(std::move(t));
    }
    for (const Parsed& t : valid) { // 가상 키 — 바인딩 없이 (ActionId 는 이름 순이라 다 더한 뒤 푼다)
        out.map.set(t.name, {});
    }
    for (const Parsed& t : valid) {
        PlayTrigger pt;
        pt.id = *out.map.find(t.name);
        pt.kind = t.kind;
        pt.seconds = t.seconds;
        for (const std::string& src : t.sources) {
            pt.sources.push_back(*out.map.find(src));
        }
        out.triggers.push_back(std::move(pt));
    }
    // Context 는 사용자 파일이 바꾸지 않는다 (키만 다시 묶는다) — ActionId 는 합친 맵의 이름 순
    out.contexts.assign(out.map.size(), PlayContext::Game);
    for (const auto& [name, context] : pending) {
        if (const auto id = out.map.find(name)) {
            out.contexts[*id] = context;
        }
    }
    // 엔진 키와 겹침 — 막지 않는다 (예: 행동 키를 Space 로 두면 일시정지와 함께 켜진다)
    for (usize i = 0; i < out.map.size(); ++i) {
        const auto id = static_cast<platform::ActionId>(i);
        for (const platform::Binding& b : out.map.bindings(id)) {
            for (usize k = 0; k < user.size(); ++k) {
                const auto uk = static_cast<platform::ActionId>(k);
                if (user.name(uk).starts_with(kPlayPrefix)) {
                    continue;
                }
                for (const platform::Binding& ub : user.bindings(uk)) {
                    if (ub == b) {
                        out.warnings.push_back(std::format("'{}' 의 {} 가 엔진 키 '{}' 와 겹친다", out.map.name(id),
                                                           platform::bindingName(b), user.name(uk)));
                    }
                }
            }
        }
    }
    return out;
}

void PlayGate::update(const PlayKeys& keys, const platform::ActionState& raw, bool menuOpen, f64 nowSeconds) {
    const usize n = keys.map.size();
    m_down.resize(n, 0);
    m_pressed.assign(n, 0);
    m_released.assign(n, 0);
    for (usize i = 0; i < n; ++i) {
        const auto id = static_cast<platform::ActionId>(i);
        const bool allowed = PlayGate::allowed(keys.contextOf(id), menuOpen);
        const bool was = m_down[i] != 0;
        const bool now = allowed && raw.down(id);
        m_pressed[i] = allowed && raw.pressed(id) ? 1 : 0;
        // 뗌 = 보였던 키가 이제 안 보인다 — 손을 뗐거나 위 Context 가 먹었다 (눌린 채 메뉴가 열리면 뗌 한 번)
        m_released[i] = was && !now ? 1 : 0;
        m_down[i] = now ? 1 : 0;
    }
    updateTriggers(keys, nowSeconds);
}

// 트리거 (v0.12 RT-06-13) — 원본은 Context 로 거른 값 (위 루프). 가상 키는 바인딩이 없어 위에서 늘 꺼짐 — 여기서 켠다
void PlayGate::updateTriggers(const PlayKeys& keys, f64 now) {
    m_triggers.resize(keys.triggers.size());
    for (usize k = 0; k < keys.triggers.size(); ++k) {
        const PlayTrigger& t = keys.triggers[k];
        TriggerState& st = m_triggers[k];
        const usize i = t.id;
        if (i >= m_down.size() || t.sources.empty()) {
            continue;
        }
        const bool was = m_released[i] != 0; // 위 루프: 지난 프레임에 켜져 있었으면 뗌 (가상 키는 바인딩이 없다)
        bool on = false;
        bool fire = false;
        switch (t.kind) {
        case TriggerKind::Hold: {
            const platform::ActionId src = t.sources.front();
            if (pressed(src)) {
                st.since = now;
                st.fired = false;
            }
            if (!down(src)) {
                st.since.reset();
                st.fired = false;
            } else if (st.since && now - *st.since >= t.seconds) {
                on = true;
                fire = !st.fired;
                st.fired = true;
            }
            break;
        }
        case TriggerKind::DoubleTap: {
            const platform::ActionId src = t.sources.front();
            if (pressed(src)) {
                if (st.since && now - *st.since <= t.seconds) {
                    fire = true;
                    on = true;
                    st.since.reset();
                } else {
                    st.since = now;
                }
            }
            break;
        }
        case TriggerKind::Combo: {
            if (st.step > 0 && st.since && now - *st.since > t.seconds) {
                st.step = 0; // 늦었다
            }
            for (usize s = 0; s < t.sources.size(); ++s) {
                if (!pressed(t.sources[s])) {
                    continue;
                }
                if (s == st.step) {
                    ++st.step;
                    st.since = now;
                } else if (s == 0) {
                    st.step = 1; // 처음부터 다시
                    st.since = now;
                } else {
                    st.step = 0; // 차례가 틀렸다
                }
            }
            if (st.step == t.sources.size()) {
                fire = true;
                on = true;
                st.step = 0;
                st.since.reset();
            }
            break;
        }
        }
        m_pressed[i] = fire ? 1 : 0;
        m_released[i] = was && !on ? 1 : 0;
        m_down[i] = on ? 1 : 0;
    }
}

std::string playKeyLabel(const platform::ActionMap& map, std::string_view actionId) {
    const auto id = map.find(std::format("{}{}", kPlayActionPrefix, actionId));
    std::string label;
    if (!id) {
        return label;
    }
    for (const platform::Binding& b : map.bindings(*id)) {
        label += (label.empty() ? "" : " / ") + platform::bindingName(b);
    }
    return label;
}

} // namespace sbx::client
