#include "platform/common/ActionMap.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"

namespace sbx::platform {
namespace {

using json = nlohmann::json;

struct ModName {
    std::string_view name;
    u8 bit;
};
constexpr std::array<ModName, 4> kModNames{{
    {"Ctrl", Modifiers::kCtrl},
    {"Shift", Modifiers::kShift},
    {"Alt", Modifiers::kAlt},
    {"Super", Modifiers::kSuper},
}};

bool validActionName(std::string_view s) {
    if (s.empty() || s.front() == '.' || s.back() == '.') {
        return false;
    }
    char prev = 0;
    for (const char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok || (c == '.' && prev == '.')) {
            return false;
        }
        prev = c;
    }
    return true;
}

} // namespace

Expected<Binding> parseBinding(std::string_view text) {
    if (text.empty()) {
        return makeError(ErrorCode::ParseError, "빈 바인딩");
    }
    Binding b;
    std::string_view rest = text;
    for (;;) {
        const auto plus = rest.find('+');
        if (plus == std::string_view::npos) {
            break;
        }
        const std::string_view part = rest.substr(0, plus);
        const auto it = std::ranges::find(kModNames, part, &ModName::name);
        if (it == kModNames.end()) {
            return makeError(ErrorCode::ParseError,
                             std::format("'{}' : 수정자는 Ctrl · Shift · Alt · Super 입니다 ('{}')", text, part));
        }
        if ((b.mods.bits & it->bit) != 0) {
            return makeError(ErrorCode::ParseError, std::format("'{}' : 수정자 '{}' 가 두 번", text, part));
        }
        b.mods.bits = static_cast<u8>(b.mods.bits | it->bit);
        rest = rest.substr(plus + 1);
    }
    if (const auto key = keyFromName(rest)) {
        b.kind = Binding::Kind::Key;
        b.key = *key;
        return b;
    }
    if (const auto button = mouseButtonFromName(rest)) {
        b.kind = Binding::Kind::Mouse;
        b.button = *button;
        return b;
    }
    return makeError(ErrorCode::ParseError,
                     std::format("'{}' : 알 수 없는 키 '{}' (이름은 docs/07-PLATFORM.md 4장, 예: W · Digit1 · "
                                 "LeftCtrl · MouseMiddle)",
                                 text, rest));
}

std::string bindingName(const Binding& b) {
    std::string out;
    for (const ModName& m : kModNames) {
        if ((b.mods.bits & m.bit) != 0) {
            out += m.name;
            out += '+';
        }
    }
    out += b.kind == Binding::Kind::Key ? keyName(b.key) : mouseButtonName(b.button);
    return out;
}

bool bindingPressed(const Binding& b, const InputState& s) noexcept {
    // 수정자는 키가 눌린 순간의 것으로 본다 — 같은 프레임 안에 수정자를 먼저 떼어도 조합을 놓치지 않는다
    if (b.kind == Binding::Kind::Key) {
        if (!s.pressed(b.key)) {
            return false;
        }
        const u8 mods = static_cast<u8>(s.keyPressMods[static_cast<usize>(b.key)] & ~modifierBitOf(b.key));
        return mods == b.mods.bits;
    }
    return s.pressed(b.button) && s.buttonPressMods[static_cast<usize>(b.button)] == b.mods.bits;
}

bool bindingActive(const Binding& b, const InputState& s) noexcept {
    if (bindingPressed(b, s)) {
        return true;
    }
    u8 mods = s.modifiers().bits;
    if (b.kind == Binding::Kind::Key) {
        if (!s.down(b.key)) {
            return false;
        }
        // 바인딩된 키가 수정자 자신이면 그 비트는 비교에서 뺀다 ("LeftShift" 단독 바인딩)
        mods = static_cast<u8>(mods & ~modifierBitOf(b.key));
    } else if (!s.down(b.button)) {
        return false;
    }
    return mods == b.mods.bits;
}

Expected<ActionMap> ActionMap::parse(std::string_view jsonText, std::string_view source) {
    const json j = json::parse(jsonText, nullptr, false);
    const auto fail = [&](std::string msg, std::string_view where) {
        return makeError(ErrorCode::ParseError, std::move(msg), std::format("{}:{}", source, where));
    };
    if (j.is_discarded()) {
        return fail("JSON 구문 오류", "");
    }
    if (!j.is_object()) {
        return fail("최상위는 객체여야 합니다", "");
    }
    for (const auto& [k, v] : j.items()) {
        if (k != "format" && k != "version" && k != "actions") {
            return fail(std::format("모르는 키 '{}' (format · version · actions)", k), "/" + k);
        }
    }
    if (!j.contains("format") || !j["format"].is_string() || j["format"].get<std::string>() != "sandbox.input") {
        return fail("\"format\": \"sandbox.input\" 이 필요합니다", "/format");
    }
    if (!j.contains("version") || !j["version"].is_number_unsigned()) {
        return fail("\"version\" 은 0 이상의 정수", "/version");
    }
    if (const auto ver = j["version"].get<u64>(); ver != kFormatVersion) {
        return makeError(ErrorCode::VersionMismatch,
                         std::format("입력 설정 version {} 은 지원하지 않습니다 (지원: {})", ver, kFormatVersion),
                         std::format("{}:/version", source));
    }
    if (!j.contains("actions") || !j["actions"].is_object()) {
        return fail("\"actions\" 는 객체", "/actions");
    }

    ActionMap map;
    for (const auto& [name, list] : j["actions"].items()) { // nlohmann 객체는 키 순서 — 결과도 이름 순
        const std::string where = "/actions/" + name;
        if (!validActionName(name)) {
            return fail(std::format("액션 이름 '{}' : 소문자·숫자·_ 를 . 으로 잇습니다 (예: camera.pan.up)", name),
                        where);
        }
        if (!list.is_array()) {
            return fail("바인딩 목록은 문자열 배열", where);
        }
        Action a{name, {}};
        for (usize i = 0; i < list.size(); ++i) {
            if (!list[i].is_string()) {
                return fail("바인딩은 문자열", std::format("{}/{}", where, i));
            }
            auto b = parseBinding(list[i].get<std::string>());
            if (!b) {
                return fail(b.error().message, std::format("{}/{}", where, i));
            }
            if (std::ranges::find(a.bindings, *b) != a.bindings.end()) {
                return fail(std::format("같은 바인딩 '{}' 가 두 번", bindingName(*b)), std::format("{}/{}", where, i));
            }
            a.bindings.push_back(*b);
        }
        map.m_actions.push_back(std::move(a));
    }
    if (map.m_actions.size() > 0xFFFF) {
        return fail("액션이 너무 많습니다 (최대 65535)", "/actions");
    }
    return map;
}

Expected<ActionMap> ActionMap::loadFile(const std::filesystem::path& path) {
    auto text = io::readFile(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return parse(*text, io::displayPath(path));
}

void ActionMap::merge(const ActionMap& overrides) {
    for (const Action& o : overrides.m_actions) {
        const auto it = std::ranges::lower_bound(m_actions, o.name, {}, &Action::name);
        if (it != m_actions.end() && it->name == o.name) {
            it->bindings = o.bindings;
        } else {
            m_actions.insert(it, o);
        }
    }
}

std::optional<ActionId> ActionMap::find(std::string_view name) const noexcept {
    const auto it =
        std::ranges::lower_bound(m_actions, name, {}, [](const Action& a) -> std::string_view { return a.name; });
    if (it == m_actions.end() || it->name != name) {
        return std::nullopt;
    }
    return static_cast<ActionId>(it - m_actions.begin());
}

std::vector<ActionConflict> ActionMap::conflicts() const {
    std::vector<ActionConflict> out;
    for (usize a = 0; a < m_actions.size(); ++a) {
        for (const Binding& b : m_actions[a].bindings) {
            for (usize c = a + 1; c < m_actions.size(); ++c) {
                if (std::ranges::find(m_actions[c].bindings, b) != m_actions[c].bindings.end()) {
                    out.push_back({b, m_actions[a].name, m_actions[c].name});
                }
            }
        }
    }
    return out;
}

std::string ActionMap::toJson() const {
    json actions = json::object();
    for (const Action& a : m_actions) {
        json list = json::array();
        for (const Binding& b : a.bindings) {
            list.push_back(bindingName(b));
        }
        actions[a.name] = std::move(list);
    }
    const json j = {{"format", "sandbox.input"}, {"version", kFormatVersion}, {"actions", std::move(actions)}};
    return j.dump(2) + "\n";
}

bool ActionMap::active(ActionId id, const InputState& s) const {
    for (const Binding& b : m_actions.at(id).bindings) {
        if (bindingActive(b, s)) {
            return true;
        }
    }
    return false;
}

bool ActionMap::triggered(ActionId id, const InputState& s) const {
    for (const Binding& b : m_actions.at(id).bindings) {
        if (bindingPressed(b, s)) {
            return true;
        }
    }
    return false;
}

void ActionState::update(const ActionMap& map, const InputState& s) {
    if (m_cur.size() != map.size()) {
        m_cur.assign(map.size(), 0);
        m_edge.assign(map.size(), 0);
    }
    m_prev = m_cur;
    for (usize i = 0; i < map.size(); ++i) {
        const auto id = static_cast<ActionId>(i);
        m_cur[i] = map.active(id, s) ? 1 : 0;
        m_edge[i] = m_cur[i] != 0 && map.triggered(id, s) ? 1 : 0;
    }
}

void ActionState::reset() noexcept {
    m_cur.clear();
    m_prev.clear();
    m_edge.clear();
}

} // namespace sbx::platform
