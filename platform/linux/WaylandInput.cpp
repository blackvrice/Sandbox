#include "platform/linux/WaylandInput.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/linux/X11KeyMap.hpp"

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

namespace sbx::platform::wl {
namespace {

bool isTextCodepoint(char32_t cp) noexcept {
    return !(cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)); // 제어 문자는 키 이벤트로 (I4)
}

void decodeUtf8(std::string_view s, std::vector<char32_t>& out) {
    usize i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        usize n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (n == 0 || i + n > s.size()) {
            ++i;
            continue;
        }
        u32 cp = n == 1 ? c : (c & (0x7Fu >> n));
        bool ok = true;
        for (usize k = 1; k < n; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            ok = ok && (cc & 0xC0) == 0x80;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (ok) {
            out.push_back(static_cast<char32_t>(cp));
        }
        i += ok ? n : 1;
    }
}

// 합성 표(데드 키 · Compose)는 사용자 로캘을 따른다
const char* composeLocale() {
    for (const char* var : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        if (const char* v = std::getenv(var); v != nullptr && *v != '\0') {
            return v;
        }
    }
    return "C";
}

} // namespace

KeyboardState::KeyboardState() {
    m_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (m_ctx != nullptr) {
        m_composeTable = xkb_compose_table_new_from_locale(m_ctx, composeLocale(), XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (m_composeTable != nullptr) {
            m_compose = xkb_compose_state_new(m_composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
        }
    }
}

KeyboardState::~KeyboardState() {
    xkb_compose_state_unref(m_compose);
    xkb_compose_table_unref(m_composeTable);
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);
    xkb_context_unref(m_ctx);
}

bool KeyboardState::setKeymapText(std::string_view text) {
    if (m_ctx == nullptr) {
        return false;
    }
    // 서버가 준 문자열은 끝에 NUL 이 있다 — 없을 수도 있어 복사한다
    const std::string s(text.data(), text.size());
    xkb_keymap* km =
        xkb_keymap_new_from_string(m_ctx, s.c_str(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (km == nullptr) {
        return false;
    }
    xkb_state* st = xkb_state_new(km);
    if (st == nullptr) {
        xkb_keymap_unref(km);
        return false;
    }
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);
    m_keymap = km;
    m_state = st;
    return true;
}

bool KeyboardState::setKeymapNames(const char* layout, const char* variant) {
    if (m_ctx == nullptr) {
        return false;
    }
    xkb_rule_names names{};
    names.layout = layout;
    names.variant = variant;
    xkb_keymap* km = xkb_keymap_new_from_names(m_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (km == nullptr) {
        return false;
    }
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);
    m_keymap = km;
    m_state = xkb_state_new(km);
    return m_state != nullptr;
}

u32 KeyboardState::modifierMask(std::string_view name) const noexcept {
    if (m_keymap == nullptr) {
        return 0;
    }
    const std::string n(name);
    const xkb_mod_index_t i = xkb_keymap_mod_get_index(m_keymap, n.c_str());
    return i == XKB_MOD_INVALID ? 0u : (1u << i);
}

void KeyboardState::setModifiers(u32 depressed, u32 latched, u32 locked, u32 group) {
    if (m_state != nullptr) {
        xkb_state_update_mask(m_state, depressed, latched, locked, 0, 0, group);
    }
}

void KeyboardState::setRepeatInfo(i32 ratePerSecond, i32 delayMs) {
    m_rate = ratePerSecond < 0 ? 0 : ratePerSecond;
    m_delay = delayMs < 0 ? 0 : delayMs;
    if (m_rate == 0) {
        m_repeatKey = 0;
    }
}

Modifiers KeyboardState::modifiers() const noexcept {
    Modifiers m;
    if (m_state == nullptr) {
        return m;
    }
    const auto on = [&](const char* name) {
        return xkb_state_mod_name_is_active(m_state, name, XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    if (on(XKB_MOD_NAME_SHIFT)) {
        m.bits |= Modifiers::kShift;
    }
    if (on(XKB_MOD_NAME_CTRL)) {
        m.bits |= Modifiers::kCtrl;
    }
    if (on(XKB_MOD_NAME_ALT)) {
        m.bits |= Modifiers::kAlt;
    }
    if (on(XKB_MOD_NAME_LOGO)) {
        m.bits |= Modifiers::kSuper;
    }
    return m;
}

bool KeyboardState::keyRepeats(u32 evdevCode) const noexcept {
    if (m_keymap == nullptr) {
        return modifierBitOf(x11::keyFromKeycode(evdevCode + x11::kEvdevOffset)) == 0;
    }
    return xkb_keymap_key_repeats(m_keymap, evdevCode + x11::kEvdevOffset) != 0;
}

void KeyboardState::emitTextFor(u32 evdevCode, PlatformEventQueue& out) {
    if (m_state == nullptr) {
        return;
    }
    const xkb_keycode_t kc = evdevCode + x11::kEvdevOffset;
    if (m_compose != nullptr) {
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(m_state, kc);
        if (sym != XKB_KEY_NoSymbol && xkb_compose_state_feed(m_compose, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(m_compose)) {
            case XKB_COMPOSE_COMPOSING:
                return; // 데드 키 · Compose 중 — 글자는 다음 키에서
            case XKB_COMPOSE_COMPOSED: {
                std::array<char, 64> buf{};
                const int n = xkb_compose_state_get_utf8(m_compose, buf.data(), buf.size());
                xkb_compose_state_reset(m_compose);
                std::vector<char32_t> cps;
                decodeUtf8({buf.data(), static_cast<usize>(n > 0 ? n : 0)}, cps);
                for (char32_t cp : cps) {
                    if (isTextCodepoint(cp)) {
                        out.push(TextInput{cp});
                    }
                }
                return;
            }
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(m_compose);
                return;
            case XKB_COMPOSE_NOTHING:
                break;
            }
        }
    }
    const auto cp = static_cast<char32_t>(xkb_state_key_get_utf32(m_state, kc));
    if (cp != 0 && isTextCodepoint(cp)) {
        out.push(TextInput{cp});
    }
}

void KeyboardState::key(u32 evdevCode, bool down, Clock::time_point now, bool emitText, PlatformEventQueue& out) {
    const u32 xcode = evdevCode + x11::kEvdevOffset;
    const Key key = x11::keyFromKeycode(xcode);
    const u8 bit = modifierBitOf(key);
    Modifiers mods = modifiers(); // 서버의 modifiers 이벤트는 키 뒤에 온다 — 수정자 키 자신을 반영한다
    if (down) {
        mods.bits |= bit;
        out.push(KeyDown{key, xcode, mods, false});
        if (emitText) {
            emitTextFor(evdevCode, out);
        }
        if (m_rate > 0 && keyRepeats(evdevCode)) {
            m_repeatKey = evdevCode + 1;
            m_nextRepeat = now + std::chrono::milliseconds(m_delay);
        }
        return;
    }
    mods.bits = static_cast<u8>(mods.bits & ~bit);
    out.push(KeyUp{key, xcode, mods});
    if (m_repeatKey == evdevCode + 1) {
        m_repeatKey = 0;
    }
}

void KeyboardState::tick(Clock::time_point now, bool emitText, PlatformEventQueue& out) {
    if (m_repeatKey == 0 || m_rate <= 0) {
        return;
    }
    const auto interval =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(1.0 / static_cast<f64>(m_rate)));
    // 프레임이 오래 멈췄으면 따라잡지 않는다 (몇 개만)
    if (now - m_nextRepeat > interval * 5) {
        m_nextRepeat = now - interval * 5;
    }
    const u32 code = m_repeatKey - 1;
    const u32 xcode = code + x11::kEvdevOffset;
    while (now >= m_nextRepeat) {
        Modifiers mods = modifiers();
        const Key key = x11::keyFromKeycode(xcode);
        mods.bits |= modifierBitOf(key);
        out.push(KeyDown{key, xcode, mods, true});
        if (emitText) {
            emitTextFor(code, out);
        }
        m_nextRepeat += interval;
    }
}

std::optional<KeyboardState::Clock::time_point> KeyboardState::nextRepeat() const noexcept {
    if (m_repeatKey == 0 || m_rate <= 0) {
        return std::nullopt;
    }
    return m_nextRepeat;
}

void KeyboardState::focusLost() noexcept {
    m_repeatKey = 0;
    if (m_compose != nullptr) {
        xkb_compose_state_reset(m_compose);
    }
}

std::optional<MouseButton> buttonFromLinux(u32 code) noexcept {
    switch (code) {
    case 0x110: // BTN_LEFT
        return MouseButton::Left;
    case 0x111: // BTN_RIGHT
        return MouseButton::Right;
    case 0x112: // BTN_MIDDLE
        return MouseButton::Middle;
    case 0x113: // BTN_SIDE (뒤로)
    case 0x116: // BTN_BACK
        return MouseButton::X1;
    case 0x114: // BTN_EXTRA (앞으로)
    case 0x115: // BTN_FORWARD
        return MouseButton::X2;
    default:
        return std::nullopt;
    }
}

u8 ClickCounter::press(u32 button, u32 timeMs, Vec2 position) noexcept {
    const Vec2 d = position - m_pos;
    const bool dbl =
        !m_lastDouble && button == m_button && timeMs - m_time <= 500u && std::abs(d.x) <= 4.f && std::abs(d.y) <= 4.f;
    m_lastDouble = dbl;
    m_button = button;
    m_time = timeMs;
    m_pos = position;
    return dbl ? 2 : 1;
}

} // namespace sbx::platform::wl
