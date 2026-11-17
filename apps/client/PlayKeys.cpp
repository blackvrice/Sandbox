#include "apps/client/PlayKeys.hpp"

#include <format>

#include <nlohmann/json.hpp>

namespace sbx::client {

PlayKeys buildPlayKeys(std::span<const PackInput> packs, const platform::ActionMap& user) {
    PlayKeys out;
    std::vector<std::pair<std::string, PlayContext>> pending; // 팩 차례 (뒤 것이 이긴다)
    for (const PackInput& p : packs) {
        if (p.text.empty()) {
            continue;
        }
        const std::string source = std::format("{}/presentation/input.json", p.pack);
        // "contexts" (v0.10 RT-06-8 조각 2) 는 팩만의 키 — 떼어 내고 나머지를 ActionMap 이 읽는다
        std::string text = p.text;
        std::vector<std::pair<std::string, PlayContext>> contexts;
        if (nlohmann::json j = nlohmann::json::parse(p.text, nullptr, false); j.is_object() && j.contains("contexts")) {
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

void PlayGate::update(const PlayKeys& keys, const platform::ActionState& raw, bool menuOpen) {
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
