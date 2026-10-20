#include "apps/client/PlayKeys.hpp"

#include <format>

namespace sbx::client {

PlayKeys buildPlayKeys(std::span<const PackInput> packs, const platform::ActionMap& user) {
    PlayKeys out;
    for (const PackInput& p : packs) {
        if (p.text.empty()) {
            continue;
        }
        const std::string source = std::format("{}/presentation/input.json", p.pack);
        auto parsed = platform::ActionMap::parse(p.text, source);
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
    }
    out.map.merge(user.withPrefix(kPlayPrefix)); // 사용자 파일이 마지막에 이긴다
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
