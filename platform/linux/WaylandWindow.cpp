// Wayland 창. docs/07-PLATFORM.md 6.2, ADR-0034.
//
// - 창 틀: libdecor (서버 장식 xdg-decoration 이 있으면 그것, 없으면 플러그인이 그린 틀 — GNOME). 닫기 = close 콜백.
// - 키: wl_keyboard (evdev 코드 → X11KeyMap 의 표 + 8), 키맵 · 수정자 · 글자 · 합성 키 · 자동 반복은 WaylandInput
//   (xkbcommon — 반복은 클라이언트가 repeat_info 로 만든다).
// - 글자 입력: 키에서 xkb 로 만든 글자 + text-input-v3 의 commit_string (입력기가 가져간 키는 오지 않는다).
// - 마우스: wl_pointer (논리 좌표 그대로), 휠 = axis_discrete(한 칸) 또는 axis 값 ÷ 10, 버튼 = BTN_*.
//   캡처 = pointer-constraints lock + relative-pointer (가속 없는 값).
// - 배율: 창이 걸친 출력의 wl_output.scale 중 가장 큰 정수 → wl_surface.set_buffer_scale. 분수 배율은 [계획].
// - 클립보드: wl_data_device (text/plain;charset=utf-8). 최소화는 Wayland 가 알려 주지 않는다 — libdecor 의 SUSPENDED 만.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "foundation/log/Log.hpp"
#include "platform/common/Window.hpp"
#include "platform/linux/LinuxWindows.hpp"
#include "platform/linux/WaylandInput.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <libdecor.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"

namespace sbx::platform {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kTextMime = "text/plain;charset=utf-8";

void decodeUtf8(std::string_view s, std::vector<char32_t>& out) {
    usize i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        const usize n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
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

struct Output {
    wl_output* output = nullptr;
    u32 name = 0;
    i32 scale = 1;
};

class WaylandWindow final : public IWindow {
public:
    explicit WaylandWindow(const WindowDesc& desc) : m_highDpi(desc.highDpi) {}
    ~WaylandWindow() override;
    WaylandWindow(const WaylandWindow&) = delete;
    WaylandWindow& operator=(const WaylandWindow&) = delete;

    static Expected<std::unique_ptr<IWindow>> create(const WindowDesc& desc);

    void pollEvents(PlatformEventQueue& out) override;
    void waitEvents(std::chrono::milliseconds timeout) override;

    [[nodiscard]] Extent2D framebufferSize() const override {
        return {m_logical.width * static_cast<u32>(m_scale), m_logical.height * static_cast<u32>(m_scale)};
    }
    [[nodiscard]] Extent2D windowSize() const override { return m_logical; }
    [[nodiscard]] f32 contentScale() const override { return static_cast<f32>(m_scale); }
    [[nodiscard]] bool shouldClose() const override { return m_shouldClose; }
    [[nodiscard]] bool minimized() const override { return m_suspended; }
    [[nodiscard]] bool focused() const override { return m_focused; }

    void setTitle(std::string_view utf8) override;
    void setCursor(CursorShape shape) override;
    void setCursorCaptured(bool captured) override;
    [[nodiscard]] bool cursorCaptured() const override { return m_captured; }
    void setTextInputActive(bool active) override;
    [[nodiscard]] bool textInputActive() const override { return m_textInput; }

    [[nodiscard]] std::string clipboardText() const override;
    void setClipboardText(std::string_view utf8) override;

    [[nodiscard]] NativeWindowHandle nativeHandle() const override {
        NativeWindowHandle h;
        h.kind = NativeWindowHandle::Kind::Wayland;
        h.window = m_surface;
        h.display = m_display;
        return h;
    }
    [[nodiscard]] std::string_view backendName() const override { return "Wayland"; }

private:
    // ---- 리스너 (C 콜백 → 멤버) ----
    static void onGlobal(void* data, wl_registry* reg, u32 name, const char* iface, u32 version);
    static void onGlobalRemove(void* data, wl_registry* reg, u32 name);
    static void onSeatCaps(void* data, wl_seat* seat, u32 caps);
    static void onConfigure(libdecor_frame* frame, libdecor_configuration* cfg, void* data);
    static void onClose(libdecor_frame* frame, void* data);
    static void onCommit(libdecor_frame* frame, void* data);

    void push(PlatformEvent e) { m_pending.push(std::move(e)); }
    void updateScale();
    void applyCursor();
    void releaseCapture();
    void enableTextInput(bool on);
    void flush() const { wl_display_flush(m_display); }
    void attachPlaceholder();
    [[nodiscard]] bool emitKeyText() const noexcept { return m_textInput; }

    friend struct Listeners;

    bool m_highDpi = true;
    wl_display* m_display = nullptr;
    wl_registry* m_registry = nullptr;
    wl_compositor* m_compositor = nullptr;
    wl_shm* m_shm = nullptr;
    wl_seat* m_seat = nullptr;
    wl_keyboard* m_keyboard = nullptr;
    wl_pointer* m_pointer = nullptr;
    wl_data_device_manager* m_dataManager = nullptr;
    wl_data_device* m_dataDevice = nullptr;
    wl_data_offer* m_selectionOffer = nullptr;
    std::vector<std::pair<wl_data_offer*, bool>> m_offers; // 이 offer 가 글자를 주나
    wl_data_source* m_source = nullptr;
    std::string m_clipboardText;
    zwp_text_input_manager_v3* m_textManager = nullptr;
    zwp_text_input_v3* m_text = nullptr;
    bool m_textFocused = false;
    bool m_textEnabled = false;
    std::string m_textCommit;
    zwp_relative_pointer_manager_v1* m_relManager = nullptr;
    zwp_pointer_constraints_v1* m_constraints = nullptr;
    zwp_relative_pointer_v1* m_relPointer = nullptr;
    zwp_locked_pointer_v1* m_locked = nullptr;
    std::vector<Output> m_outputs;
    std::vector<wl_output*> m_entered;

    wl_surface* m_surface = nullptr;
    libdecor* m_decor = nullptr;
    libdecor_frame* m_frame = nullptr;
    wl_cursor_theme* m_cursorTheme = nullptr;
    i32 m_cursorThemeScale = 0;
    wl_surface* m_cursorSurface = nullptr;
    wl_buffer* m_placeholder = nullptr; // 렌더러가 버퍼를 붙이기 전까지 보이는 어두운 바탕 (없으면 창이 안 보인다)

    PlatformEventQueue m_pending;
    wl::KeyboardState m_keys;
    wl::ClickCounter m_clicks;
    Extent2D m_logical{1600, 900};
    Extent2D m_floating{1600, 900}; // 최대화 · 타일이 아닐 때의 크기 — 되돌릴 때 서버가 크기를 주지 않는다
    i32 m_scale = 1;
    bool m_configured = false;
    bool m_shouldClose = false;
    bool m_suspended = false;
    bool m_focused = false;
    bool m_captured = false;
    bool m_textInput = false;
    bool m_resizable = true;
    CursorShape m_cursor = CursorShape::Arrow;
    u32 m_pointerSerial = 0; // set_cursor 에 쓴다 (enter 의 serial)
    u32 m_inputSerial = 0;   // set_selection 에 쓴다 (마지막 키 · 버튼)
    Vec2 m_lastMouse;
    bool m_haveLastMouse = false;
    // 포인터가 우리 표면 위인가 — libdecor 장식(제목 줄 · 테두리)도 같은 클라이언트의 표면이라 wl_pointer 가 받는다
    bool m_pointerInside = false;
    // 한 pointer.frame 안의 휠
    f32 m_wheelX = 0.f, m_wheelY = 0.f;
    i32 m_discreteX = 0, m_discreteY = 0;
};

// 프로토콜 리스너 표 — 순서는 wayland 헤더의 구조체 그대로
struct Listeners {
    static WaylandWindow* self(void* d) { return static_cast<WaylandWindow*>(d); }

    static constexpr wl_registry_listener registry{&WaylandWindow::onGlobal, &WaylandWindow::onGlobalRemove};
    static constexpr wl_seat_listener seat{&WaylandWindow::onSeatCaps, [](void*, wl_seat*, const char*) {}};

    static constexpr wl_output_listener output{
        [](void*, wl_output*, i32, i32, i32, i32, i32, const char*, const char*, i32) {}, // geometry
        [](void*, wl_output*, u32, i32, i32, i32) {},                                     // mode
        [](void* d, wl_output*) { self(d)->updateScale(); },                              // done
        [](void* d, wl_output* o, i32 factor) {                                           // scale
            for (Output& out : self(d)->m_outputs) {
                if (out.output == o) {
                    out.scale = factor;
                }
            }
        },
        nullptr,
        nullptr};

    static constexpr wl_surface_listener surface{[](void* d, wl_surface*, wl_output* o) {
                                                     self(d)->m_entered.push_back(o);
                                                     self(d)->updateScale();
                                                 },
                                                 [](void* d, wl_surface*, wl_output* o) {
                                                     std::erase(self(d)->m_entered, o);
                                                     self(d)->updateScale();
                                                 },
                                                 nullptr, nullptr};

    static constexpr wl_keyboard_listener keyboard{
        [](void* d, wl_keyboard*, u32 format, i32 fd, u32 size) { // keymap
            if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
                void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
                if (map != MAP_FAILED) {
                    if (!self(d)->m_keys.setKeymapText({static_cast<const char*>(map), size > 0 ? size - 1 : 0})) {
                        log::warn("platform", "Wayland 키맵을 읽지 못했습니다 — 키는 나오고 글자는 안 나온다");
                    }
                    munmap(map, size);
                }
            }
            close(fd);
        },
        [](void* d, wl_keyboard*, u32 serial, wl_surface*, wl_array*) { // enter
            self(d)->m_focused = true;
            self(d)->m_inputSerial = serial;
            self(d)->push(FocusGained{});
        },
        [](void* d, wl_keyboard*, u32, wl_surface*) { // leave
            self(d)->m_focused = false;
            self(d)->m_keys.focusLost();
            self(d)->push(FocusLost{}); // I3
        },
        [](void* d, wl_keyboard*, u32 serial, u32, u32 key, u32 state) { // key
            WaylandWindow* w = self(d);
            w->m_inputSerial = serial;
            w->m_keys.key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED, Clock::now(), w->emitKeyText(), w->m_pending);
        },
        [](void* d, wl_keyboard*, u32, u32 depressed, u32 latched, u32 locked, u32 group) { // modifiers
            self(d)->m_keys.setModifiers(depressed, latched, locked, group);
        },
        [](void* d, wl_keyboard*, i32 rate, i32 delay) { self(d)->m_keys.setRepeatInfo(rate, delay); }}; // repeat_info

    static constexpr wl_pointer_listener pointer{
        [](void* d, wl_pointer*, u32 serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) { // enter
            WaylandWindow* w = self(d);
            w->m_pointerSerial = serial;
            w->m_pointerInside = surface == w->m_surface;
            if (!w->m_pointerInside) {
                return; // 장식 위 — 커서 · 버튼은 libdecor 가 맡는다
            }
            w->applyCursor();
            const Vec2 p{static_cast<f32>(wl_fixed_to_double(x)), static_cast<f32>(wl_fixed_to_double(y))};
            if (!w->m_captured) {
                w->push(MouseMove{p, w->m_haveLastMouse ? p - w->m_lastMouse : Vec2{}});
            }
            w->m_lastMouse = p;
            w->m_haveLastMouse = true;
        },
        [](void* d, wl_pointer*, u32, wl_surface* surface) { // leave
            if (surface == self(d)->m_surface) {
                self(d)->m_pointerInside = false;
            }
        },
        [](void* d, wl_pointer*, u32, wl_fixed_t x, wl_fixed_t y) { // motion
            WaylandWindow* w = self(d);
            if (!w->m_pointerInside) {
                return;
            }
            const Vec2 p{static_cast<f32>(wl_fixed_to_double(x)), static_cast<f32>(wl_fixed_to_double(y))};
            // 캡처 중에는 위치를 묶어 둔다 — 잠금이 아직 켜지지 않은 서버(또는 잠금 전 몇 프레임)도 절대 이동이 새지 않게
            if (!w->m_captured) {
                w->push(MouseMove{p, w->m_haveLastMouse ? p - w->m_lastMouse : Vec2{}});
                w->m_lastMouse = p;
                w->m_haveLastMouse = true;
            }
        },
        [](void* d, wl_pointer*, u32 serial, u32 time, u32 button, u32 state) { // button
            WaylandWindow* w = self(d);
            w->m_inputSerial = serial;
            const auto b = wl::buttonFromLinux(button);
            if (!b || !w->m_pointerInside) {
                return;
            }
            if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
                w->push(MouseButtonDown{*b, w->m_lastMouse, w->m_clicks.press(button, time, w->m_lastMouse)});
            } else {
                w->push(MouseButtonUp{*b, w->m_lastMouse});
            }
        },
        [](void* d, wl_pointer*, u32, u32 axis, wl_fixed_t value) { // axis (한 칸 ≈ 10)
            if (!self(d)->m_pointerInside) {
                return;
            }
            const f32 v = static_cast<f32>(wl_fixed_to_double(value)) / 10.f;
            (axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? self(d)->m_wheelY : self(d)->m_wheelX) += v;
        },
        [](void* d, wl_pointer*) { // frame — 한 묶음의 휠을 한 이벤트로
            WaylandWindow* w = self(d);
            // 칸 수가 있으면 그것 (마우스 휠), 없으면 값 (터치패드). Wayland 는 아래 = +, 엔진은 위 = +
            const f32 dy = w->m_discreteY != 0 ? static_cast<f32>(w->m_discreteY) : w->m_wheelY;
            const f32 dx = w->m_discreteX != 0 ? static_cast<f32>(w->m_discreteX) : w->m_wheelX;
            if (dx != 0.f || dy != 0.f) {
                w->push(MouseWheel{{dx, -dy}});
            }
            w->m_wheelX = w->m_wheelY = 0.f;
            w->m_discreteX = w->m_discreteY = 0;
        },
        [](void*, wl_pointer*, u32) {},                    // axis_source
        [](void*, wl_pointer*, u32, u32) {},               // axis_stop
        [](void* d, wl_pointer*, u32 axis, i32 discrete) { // axis_discrete
            if (!self(d)->m_pointerInside) {
                return;
            }
            (axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? self(d)->m_discreteY : self(d)->m_discreteX) += discrete;
        },
        nullptr,
        nullptr};

    static constexpr zwp_relative_pointer_v1_listener relative{[](void* d, zwp_relative_pointer_v1*, u32, u32,
                                                                  wl_fixed_t, wl_fixed_t, wl_fixed_t dxU,
                                                                  wl_fixed_t dyU) {
        WaylandWindow* w = self(d);
        if (w->m_captured) {
            w->push(MouseMove{w->m_lastMouse,
                              {static_cast<f32>(wl_fixed_to_double(dxU)), static_cast<f32>(wl_fixed_to_double(dyU))}});
        }
    }};

    static constexpr zwp_text_input_v3_listener text{
        [](void* d, zwp_text_input_v3*, wl_surface*) { // enter
            self(d)->m_textFocused = true;
            if (self(d)->m_textInput) {
                self(d)->enableTextInput(true);
            }
        },
        [](void* d, zwp_text_input_v3*, wl_surface*) { // leave — 서버가 끈 것으로 본다
            self(d)->m_textFocused = false;
            self(d)->m_textEnabled = false;
        },
        [](void*, zwp_text_input_v3*, const char*, i32, i32) {}, // preedit_string — 조합 창은 입력기의 것
        [](void* d, zwp_text_input_v3*, const char* text) {      // commit_string
            self(d)->m_textCommit = text != nullptr ? text : "";
        },
        [](void*, zwp_text_input_v3*, u32, u32) {}, // delete_surrounding_text
        [](void* d, zwp_text_input_v3*, u32) {      // done — 확정 글자를 낸다
            WaylandWindow* w = self(d);
            if (w->m_textInput && !w->m_textCommit.empty()) {
                std::vector<char32_t> cps;
                decodeUtf8(w->m_textCommit, cps);
                for (char32_t cp : cps) {
                    if (!(cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0))) {
                        w->push(TextInput{cp});
                    }
                }
            }
            w->m_textCommit.clear();
        }};

    static constexpr wl_data_offer_listener offer{[](void* d, wl_data_offer* o, const char* mime) {
                                                      if (std::strcmp(mime, kTextMime) == 0 ||
                                                          std::strcmp(mime, "UTF8_STRING") == 0 ||
                                                          std::strcmp(mime, "text/plain") == 0) {
                                                          for (auto& [known, isText] : self(d)->m_offers) {
                                                              if (known == o) {
                                                                  isText = true;
                                                              }
                                                          }
                                                      }
                                                  },
                                                  [](void*, wl_data_offer*, u32) {}, [](void*, wl_data_offer*, u32) {}};

    static constexpr wl_data_device_listener device{
        [](void* d, wl_data_device*, wl_data_offer* o) { // data_offer
            self(d)->m_offers.emplace_back(o, false);
            wl_data_offer_add_listener(o, &offer, d);
        },
        [](void*, wl_data_device*, u32, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer*) {
        },                                                          // enter (끌어 놓기 — 안 씀)
        [](void*, wl_data_device*) {},                              // leave
        [](void*, wl_data_device*, u32, wl_fixed_t, wl_fixed_t) {}, // motion
        [](void*, wl_data_device*) {},                              // drop
        [](void* d, wl_data_device*, wl_data_offer* o) {            // selection — 옛 offer 는 버린다
            WaylandWindow* w = self(d);
            for (auto it = w->m_offers.begin(); it != w->m_offers.end();) {
                if (it->first != o) {
                    wl_data_offer_destroy(it->first);
                    it = w->m_offers.erase(it);
                } else {
                    ++it;
                }
            }
            w->m_selectionOffer = o;
        }};

    static constexpr wl_data_source_listener source{
        [](void*, wl_data_source*, const char*) {},         // target
        [](void* d, wl_data_source*, const char*, i32 fd) { // send — 우리 글을 쓴다
            const std::string& t = self(d)->m_clipboardText;
            usize off = 0;
            while (off < t.size()) {
                const ssize_t n = write(fd, t.data() + off, t.size() - off);
                if (n <= 0) {
                    break;
                }
                off += static_cast<usize>(n);
            }
            close(fd);
        },
        [](void* d, wl_data_source* s) { // cancelled — 다른 프로그램이 가져갔다
            wl_data_source_destroy(s);
            if (self(d)->m_source == s) {
                self(d)->m_source = nullptr;
            }
        },
        nullptr,
        nullptr,
        nullptr};

    static constexpr libdecor_interface decor{
        [](libdecor*, libdecor_error, const char* msg) { log::error("platform", "libdecor: {}", msg); }};

    static libdecor_frame_interface frame;
};

libdecor_frame_interface Listeners::frame{&WaylandWindow::onConfigure, &WaylandWindow::onClose,
                                          &WaylandWindow::onCommit,
                                          [](libdecor_frame*, const char*, void*) {}}; // dismiss_popup (팝업 없음)

} // namespace

void WaylandWindow::onGlobal(void* data, wl_registry* reg, u32 name, const char* iface, u32 version) {
    auto* w = static_cast<WaylandWindow*>(data);
    const std::string_view i(iface);
    const auto bind = [&](const wl_interface* wanted, u32 maxVersion) {
        return wl_registry_bind(reg, name, wanted, std::min(version, maxVersion));
    };
    if (i == wl_compositor_interface.name) {
        w->m_compositor = static_cast<wl_compositor*>(bind(&wl_compositor_interface, 4));
    } else if (i == wl_shm_interface.name) {
        w->m_shm = static_cast<wl_shm*>(bind(&wl_shm_interface, 1));
    } else if (i == wl_seat_interface.name && w->m_seat == nullptr) {
        w->m_seat = static_cast<wl_seat*>(bind(&wl_seat_interface, 5));
        wl_seat_add_listener(w->m_seat, &Listeners::seat, w);
    } else if (i == wl_output_interface.name) {
        auto* o = static_cast<wl_output*>(bind(&wl_output_interface, 2));
        w->m_outputs.push_back({o, name, 1});
        wl_output_add_listener(o, &Listeners::output, w);
    } else if (i == wl_data_device_manager_interface.name) {
        w->m_dataManager = static_cast<wl_data_device_manager*>(bind(&wl_data_device_manager_interface, 3));
    } else if (i == zwp_text_input_manager_v3_interface.name) {
        w->m_textManager = static_cast<zwp_text_input_manager_v3*>(bind(&zwp_text_input_manager_v3_interface, 1));
    } else if (i == zwp_relative_pointer_manager_v1_interface.name) {
        w->m_relManager =
            static_cast<zwp_relative_pointer_manager_v1*>(bind(&zwp_relative_pointer_manager_v1_interface, 1));
    } else if (i == zwp_pointer_constraints_v1_interface.name) {
        w->m_constraints = static_cast<zwp_pointer_constraints_v1*>(bind(&zwp_pointer_constraints_v1_interface, 1));
    }
}

void WaylandWindow::onGlobalRemove(void* data, wl_registry*, u32 name) {
    auto* w = static_cast<WaylandWindow*>(data);
    for (auto it = w->m_outputs.begin(); it != w->m_outputs.end(); ++it) {
        if (it->name == name) {
            std::erase(w->m_entered, it->output);
            wl_output_destroy(it->output);
            w->m_outputs.erase(it);
            w->updateScale();
            return;
        }
    }
}

void WaylandWindow::onSeatCaps(void* data, wl_seat* seat, u32 caps) {
    auto* w = static_cast<WaylandWindow*>(data);
    const bool kb = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    const bool ptr = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (kb && w->m_keyboard == nullptr) {
        w->m_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(w->m_keyboard, &Listeners::keyboard, w);
    } else if (!kb && w->m_keyboard != nullptr) {
        wl_keyboard_destroy(w->m_keyboard);
        w->m_keyboard = nullptr;
    }
    if (ptr && w->m_pointer == nullptr) {
        w->m_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(w->m_pointer, &Listeners::pointer, w);
    } else if (!ptr && w->m_pointer != nullptr) {
        w->releaseCapture();
        wl_pointer_destroy(w->m_pointer);
        w->m_pointer = nullptr;
    }
}

void WaylandWindow::onConfigure(libdecor_frame* frame, libdecor_configuration* cfg, void* data) {
    auto* w = static_cast<WaylandWindow*>(data);
    libdecor_window_state ws = LIBDECOR_WINDOW_STATE_NONE;
    if (libdecor_configuration_get_window_state(cfg, &ws)) {
        w->m_suspended = (ws & LIBDECOR_WINDOW_STATE_SUSPENDED) != 0;
    }
    constexpr u32 kServerSized = LIBDECOR_WINDOW_STATE_MAXIMIZED | LIBDECOR_WINDOW_STATE_FULLSCREEN |
                                 LIBDECOR_WINDOW_STATE_TILED_LEFT | LIBDECOR_WINDOW_STATE_TILED_RIGHT |
                                 LIBDECOR_WINDOW_STATE_TILED_TOP | LIBDECOR_WINDOW_STATE_TILED_BOTTOM;
    const bool floating = (static_cast<u32>(ws) & kServerSized) == 0;
    int width = 0;
    int height = 0;
    if (!libdecor_configuration_get_content_size(cfg, frame, &width, &height) || width <= 0 || height <= 0) {
        // 서버가 정하지 않았다 (처음 · 최대화를 풀 때) — 떠 있을 때의 크기로
        width = static_cast<int>(w->m_floating.width);
        height = static_cast<int>(w->m_floating.height);
    }
    libdecor_state* st = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, st, cfg);
    libdecor_state_free(st);
    const Extent2D logical{static_cast<u32>(width), static_cast<u32>(height)};
    const bool changed = logical != w->m_logical;
    w->m_logical = logical;
    if (floating) {
        w->m_floating = logical;
    }
    if (!w->m_configured) {
        w->m_configured = true;
        wl_surface_commit(w->m_surface); // 첫 configure 를 받았다는 커밋 (버퍼는 렌더러가 붙인다)
    } else if (changed) {
        w->push(Resized{w->framebufferSize(), logical});
    }
}

void WaylandWindow::onClose(libdecor_frame*, void* data) {
    auto* w = static_cast<WaylandWindow*>(data);
    w->m_shouldClose = true; // 창은 소유자가 파괴할 때 닫힌다
    w->push(CloseRequested{});
}

void WaylandWindow::onCommit(libdecor_frame*, void* data) {
    wl_surface_commit(static_cast<WaylandWindow*>(data)->m_surface);
}

Expected<std::unique_ptr<IWindow>> WaylandWindow::create(const WindowDesc& desc) {
    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        const char* env = std::getenv("WAYLAND_DISPLAY");
        return makeError(ErrorCode::Unsupported, std::format("Wayland 서버에 연결할 수 없습니다 (WAYLAND_DISPLAY={})",
                                                             env != nullptr ? env : "(없음)"));
    }
    auto w = std::make_unique<WaylandWindow>(desc);
    w->m_display = display;
    w->m_floating = {desc.width, desc.height};
    w->m_logical = w->m_floating;
    w->m_resizable = desc.resizable;
    w->m_registry = wl_display_get_registry(display);
    wl_registry_add_listener(w->m_registry, &Listeners::registry, w.get());
    wl_display_roundtrip(display); // 전역 목록
    wl_display_roundtrip(display); // 좌석 능력 · 출력 배율
    if (w->m_compositor == nullptr || w->m_shm == nullptr) {
        return makeError(ErrorCode::Unsupported, "Wayland 서버에 wl_compositor · wl_shm 이 없습니다");
    }
    if (w->m_seat != nullptr && w->m_dataManager != nullptr) {
        w->m_dataDevice = wl_data_device_manager_get_data_device(w->m_dataManager, w->m_seat);
        wl_data_device_add_listener(w->m_dataDevice, &Listeners::device, w.get());
    }
    if (w->m_seat != nullptr && w->m_textManager != nullptr) {
        w->m_text = zwp_text_input_manager_v3_get_text_input(w->m_textManager, w->m_seat);
        zwp_text_input_v3_add_listener(w->m_text, &Listeners::text, w.get());
    }

    w->m_surface = wl_compositor_create_surface(w->m_compositor);
    wl_surface_add_listener(w->m_surface, &Listeners::surface, w.get());
    w->m_cursorSurface = wl_compositor_create_surface(w->m_compositor);

    w->m_decor = libdecor_new(display, const_cast<libdecor_interface*>(&Listeners::decor));
    if (w->m_decor == nullptr) {
        return makeError(ErrorCode::Unsupported, "libdecor 를 시작할 수 없습니다");
    }
    w->m_frame = libdecor_decorate(w->m_decor, w->m_surface, &Listeners::frame, w.get());
    if (w->m_frame == nullptr) {
        return makeError(ErrorCode::Unsupported, "libdecor 가 창 틀을 만들지 못했습니다");
    }
    libdecor_frame_set_app_id(w->m_frame, "sandbox");
    w->setTitle(desc.title);
    libdecor_frame_set_min_content_size(w->m_frame, 320, 200);
    if (!desc.resizable) {
        libdecor_frame_set_min_content_size(w->m_frame, static_cast<int>(desc.width), static_cast<int>(desc.height));
        libdecor_frame_set_max_content_size(w->m_frame, static_cast<int>(desc.width), static_cast<int>(desc.height));
        libdecor_frame_unset_capabilities(w->m_frame, LIBDECOR_ACTION_RESIZE);
    }
    libdecor_frame_map(w->m_frame);
    // 첫 configure 까지 (2 초)
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!w->m_configured && Clock::now() < deadline) {
        if (libdecor_dispatch(w->m_decor, 50) < 0) {
            return makeError(ErrorCode::Unsupported, "Wayland 연결이 끊겼습니다 (첫 configure 전)");
        }
    }
    if (!w->m_configured) {
        return makeError(ErrorCode::Unsupported, "Wayland 서버가 창을 configure 하지 않습니다 (2 초)");
    }
    w->updateScale();
    w->attachPlaceholder();
    w->flush();
    log::info("platform", "Wayland 창 {}×{} (논리) · 배율 {} · libdecor · 입력기 {} · 상대 이동 {}", w->m_logical.width,
              w->m_logical.height, w->m_scale, w->m_textManager != nullptr ? "text-input-v3" : "없음",
              w->m_relManager != nullptr && w->m_constraints != nullptr ? "있음" : "없음");
    return std::unique_ptr<IWindow>(std::move(w));
}

WaylandWindow::~WaylandWindow() {
    if (m_display == nullptr) {
        return;
    }
    releaseCapture();
    if (m_text != nullptr) {
        zwp_text_input_v3_destroy(m_text);
    }
    for (auto& [offer, text] : m_offers) {
        wl_data_offer_destroy(offer);
    }
    if (m_source != nullptr) {
        wl_data_source_destroy(m_source);
    }
    if (m_dataDevice != nullptr) {
        wl_data_device_destroy(m_dataDevice);
    }
    if (m_keyboard != nullptr) {
        wl_keyboard_destroy(m_keyboard);
    }
    if (m_pointer != nullptr) {
        wl_pointer_destroy(m_pointer);
    }
    if (m_cursorTheme != nullptr) {
        wl_cursor_theme_destroy(m_cursorTheme);
    }
    if (m_frame != nullptr) {
        libdecor_frame_unref(m_frame);
    }
    if (m_decor != nullptr) {
        libdecor_unref(m_decor);
    }
    if (m_cursorSurface != nullptr) {
        wl_surface_destroy(m_cursorSurface);
    }
    if (m_placeholder != nullptr) {
        wl_buffer_destroy(m_placeholder);
    }
    if (m_surface != nullptr) {
        wl_surface_destroy(m_surface);
    }
    for (const Output& o : m_outputs) {
        wl_output_destroy(o.output);
    }
    if (m_textManager != nullptr) {
        zwp_text_input_manager_v3_destroy(m_textManager);
    }
    if (m_relManager != nullptr) {
        zwp_relative_pointer_manager_v1_destroy(m_relManager);
    }
    if (m_constraints != nullptr) {
        zwp_pointer_constraints_v1_destroy(m_constraints);
    }
    if (m_dataManager != nullptr) {
        wl_data_device_manager_destroy(m_dataManager);
    }
    if (m_seat != nullptr) {
        wl_seat_destroy(m_seat);
    }
    if (m_shm != nullptr) {
        wl_shm_destroy(m_shm);
    }
    if (m_compositor != nullptr) {
        wl_compositor_destroy(m_compositor);
    }
    wl_registry_destroy(m_registry);
    wl_display_flush(m_display);
    wl_display_disconnect(m_display);
}

void WaylandWindow::pollEvents(PlatformEventQueue& out) {
    if (libdecor_dispatch(m_decor, 0) < 0 && !m_shouldClose) {
        log::error("platform", "Wayland 연결이 끊겼습니다 — 창을 닫습니다");
        m_shouldClose = true;
        push(CloseRequested{});
    }
    if (m_focused) {
        m_keys.tick(Clock::now(), emitKeyText(), m_pending);
    }
    flush();
    out.takeFrom(m_pending);
}

void WaylandWindow::waitEvents(std::chrono::milliseconds timeout) {
    auto ms = std::max<std::chrono::milliseconds::rep>(0, timeout.count());
    if (const auto next = m_keys.nextRepeat(); next && m_focused) { // 자동 반복 시각에 깬다
        const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(*next - Clock::now()).count();
        ms = std::clamp<std::chrono::milliseconds::rep>(until, 0, ms);
    }
    flush();
    (void)libdecor_dispatch(m_decor, static_cast<int>(ms));
}

// Wayland 의 창은 버퍼를 붙여야 보인다 (입력도 그때부터). 렌더러가 없거나(--no-render) 첫 프레임 전에도 창이 보이게
// 어두운 회색 shm 버퍼 하나를 붙인다 — Win32 창 클래스의 배경 브러시와 같은 몫. 렌더러가 붙이면 그것으로 바뀐다
void WaylandWindow::attachPlaceholder() {
    const Extent2D fb = framebufferSize();
    const usize stride = static_cast<usize>(fb.width) * 4;
    const usize bytes = stride * fb.height;
    if (bytes == 0) {
        return;
    }
    const int fd = memfd_create("sandbox-placeholder", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        return;
    }
    void* px = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px != MAP_FAILED) {
        std::fill_n(static_cast<u32*>(px), bytes / 4, 0xFF202226u); // ARGB — Win32 의 RGB(32, 34, 38)
        munmap(px, bytes);
        wl_shm_pool* pool = wl_shm_create_pool(m_shm, fd, static_cast<i32>(bytes));
        m_placeholder = wl_shm_pool_create_buffer(pool, 0, static_cast<i32>(fb.width), static_cast<i32>(fb.height),
                                                  static_cast<i32>(stride), WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        wl_surface_attach(m_surface, m_placeholder, 0, 0);
        wl_surface_damage(m_surface, 0, 0, static_cast<i32>(m_logical.width), static_cast<i32>(m_logical.height));
        wl_surface_commit(m_surface);
    }
    close(fd);
}

void WaylandWindow::updateScale() {
    i32 s = 1;
    if (m_highDpi) {
        for (wl_output* o : m_entered) {
            for (const Output& out : m_outputs) {
                if (out.output == o) {
                    s = std::max(s, out.scale);
                }
            }
        }
    }
    if (s == m_scale) {
        return;
    }
    m_scale = s;
    if (m_surface != nullptr) {
        wl_surface_set_buffer_scale(m_surface, s); // 렌더러가 다음 프레임에 그만큼 큰 버퍼를 붙인다
    }
    if (m_configured) {
        push(ContentScaleChanged{static_cast<f32>(s)});
        push(Resized{framebufferSize(), m_logical});
    }
    applyCursor(); // 커서도 배율에 맞춘 테마로
}

void WaylandWindow::setTitle(std::string_view utf8) {
    const std::string t(utf8);
    libdecor_frame_set_title(m_frame, t.c_str());
    flush();
}

void WaylandWindow::applyCursor() {
    if (m_pointer == nullptr || m_pointerSerial == 0) {
        return;
    }
    if (m_captured || m_cursor == CursorShape::Hidden) {
        wl_pointer_set_cursor(m_pointer, m_pointerSerial, nullptr, 0, 0);
        flush();
        return;
    }
    if (m_cursorTheme == nullptr || m_cursorThemeScale != m_scale) {
        if (m_cursorTheme != nullptr) {
            wl_cursor_theme_destroy(m_cursorTheme);
        }
        int size = 24;
        if (const char* env = std::getenv("XCURSOR_SIZE"); env != nullptr && std::atoi(env) > 0) {
            size = std::atoi(env);
        }
        m_cursorTheme = wl_cursor_theme_load(std::getenv("XCURSOR_THEME"), size * m_scale, m_shm);
        m_cursorThemeScale = m_scale;
    }
    if (m_cursorTheme == nullptr) {
        return;
    }
    // CSS 이름(새 테마) → X 이름(옛 테마) 순
    static constexpr std::array<std::array<const char*, 2>, static_cast<usize>(CursorShape::Count)> kNames{{
        {"default", "left_ptr"},
        {"text", "xterm"},
        {"pointer", "hand2"},
        {"move", "fleur"},
        {"ew-resize", "sb_h_double_arrow"},
        {"ns-resize", "sb_v_double_arrow"},
        {"nesw-resize", "bottom_left_corner"},
        {"nwse-resize", "bottom_right_corner"},
        {"not-allowed", "crossed_circle"},
        {"default", "left_ptr"},
    }};
    wl_cursor* cursor = nullptr;
    for (const char* name : kNames[static_cast<usize>(m_cursor)]) {
        if ((cursor = wl_cursor_theme_get_cursor(m_cursorTheme, name)) != nullptr) {
            break;
        }
    }
    if (cursor == nullptr || cursor->image_count == 0) {
        return;
    }
    wl_cursor_image* img = cursor->images[0];
    wl_buffer* buf = wl_cursor_image_get_buffer(img);
    if (buf == nullptr) {
        return;
    }
    wl_surface_set_buffer_scale(m_cursorSurface, m_scale);
    wl_surface_attach(m_cursorSurface, buf, 0, 0);
    wl_surface_damage(m_cursorSurface, 0, 0, static_cast<i32>(img->width), static_cast<i32>(img->height));
    wl_surface_commit(m_cursorSurface);
    wl_pointer_set_cursor(m_pointer, m_pointerSerial, m_cursorSurface, static_cast<i32>(img->hotspot_x) / m_scale,
                          static_cast<i32>(img->hotspot_y) / m_scale);
    flush();
}

void WaylandWindow::setCursor(CursorShape shape) {
    if (shape >= CursorShape::Count || shape == m_cursor) {
        return;
    }
    m_cursor = shape;
    applyCursor();
}

void WaylandWindow::releaseCapture() {
    if (m_relPointer != nullptr) {
        zwp_relative_pointer_v1_destroy(m_relPointer);
        m_relPointer = nullptr;
    }
    if (m_locked != nullptr) {
        zwp_locked_pointer_v1_destroy(m_locked);
        m_locked = nullptr;
    }
}

void WaylandWindow::setCursorCaptured(bool captured) {
    if (captured == m_captured) {
        return;
    }
    m_captured = captured;
    if (captured) {
        if (m_pointer != nullptr && m_relManager != nullptr && m_constraints != nullptr) {
            // 커서를 그 자리에 묶고(잠금) 가속 없는 상대 이동만 받는다. 포커스를 잃었다 돌아와도 유지 (PERSISTENT)
            m_locked = zwp_pointer_constraints_v1_lock_pointer(m_constraints, m_surface, m_pointer, nullptr,
                                                               ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
            m_relPointer = zwp_relative_pointer_manager_v1_get_relative_pointer(m_relManager, m_pointer);
            zwp_relative_pointer_v1_add_listener(m_relPointer, &Listeners::relative, this);
        } else {
            log::warn("platform", "Wayland 서버에 pointer-constraints · relative-pointer 가 없어 캡처는 커서 숨김만");
        }
    } else {
        releaseCapture();
    }
    applyCursor();
    flush();
}

void WaylandWindow::enableTextInput(bool on) {
    if (m_text == nullptr) {
        return;
    }
    if (on && m_textFocused) {
        zwp_text_input_v3_enable(m_text);
        zwp_text_input_v3_set_content_type(m_text, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                           ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
        zwp_text_input_v3_commit(m_text);
        m_textEnabled = true;
    } else if (!on && m_textEnabled) {
        zwp_text_input_v3_disable(m_text);
        zwp_text_input_v3_commit(m_text);
        m_textEnabled = false;
    }
    flush();
}

void WaylandWindow::setTextInputActive(bool active) {
    if (active == m_textInput) {
        return;
    }
    m_textInput = active;
    m_textCommit.clear();
    enableTextInput(active);
}

std::string WaylandWindow::clipboardText() const {
    if (m_source != nullptr) {
        return m_clipboardText; // 우리가 가진 선택
    }
    if (m_selectionOffer == nullptr) {
        return {};
    }
    bool text = false;
    for (const auto& [offer, isText] : m_offers) {
        text = text || (offer == m_selectionOffer && isText);
    }
    if (!text) {
        return {};
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        return {};
    }
    wl_data_offer_receive(m_selectionOffer, kTextMime, fds[1]);
    close(fds[1]);
    wl_display_flush(m_display);
    // 주인이 쓸 때까지 (250 ms 까지). 다른 Wayland 이벤트는 건드리지 않는다
    std::string out;
    std::array<char, 4096> buf{};
    const auto deadline = Clock::now() + std::chrono::milliseconds(250);
    while (Clock::now() < deadline) {
        pollfd p{fds[0], POLLIN, 0};
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (::poll(&p, 1, static_cast<int>(std::max<long long>(left, 0))) <= 0) {
            break;
        }
        const ssize_t n = read(fds[0], buf.data(), buf.size());
        if (n <= 0) {
            break; // 끝
        }
        out.append(buf.data(), static_cast<usize>(n));
    }
    close(fds[0]);
    return out;
}

void WaylandWindow::setClipboardText(std::string_view utf8) {
    m_clipboardText.assign(utf8);
    if (m_dataManager == nullptr || m_dataDevice == nullptr) {
        return;
    }
    if (m_source != nullptr) {
        wl_data_source_destroy(m_source);
    }
    m_source = wl_data_device_manager_create_data_source(m_dataManager);
    wl_data_source_add_listener(m_source, &Listeners::source, this);
    wl_data_source_offer(m_source, kTextMime);
    wl_data_source_offer(m_source, "UTF8_STRING");
    wl_data_source_offer(m_source, "text/plain");
    wl_data_device_set_selection(m_dataDevice, m_source, m_inputSerial);
    flush();
}

Expected<std::unique_ptr<IWindow>> linuxwin::createWaylandWindow(const WindowDesc& desc) {
    return WaylandWindow::create(desc);
}

} // namespace sbx::platform
