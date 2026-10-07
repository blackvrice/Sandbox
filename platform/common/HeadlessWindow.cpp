#include "platform/common/HeadlessWindow.hpp"

#include <cmath>

namespace sbx::platform {
namespace {

u32 scaled(u32 v, f32 scale) {
    return static_cast<u32>(std::lround(static_cast<f64>(v) * static_cast<f64>(scale)));
}

} // namespace

HeadlessWindow::HeadlessWindow(const WindowDesc& desc, f32 contentScale)
    : m_window{desc.width, desc.height}, m_scale(desc.highDpi ? contentScale : 1.f), m_title(desc.title) {
    m_framebuffer = {scaled(desc.width, m_scale), scaled(desc.height, m_scale)};
}

void HeadlessWindow::inject(PlatformEvent e) {
    m_pending.push_back(std::move(e));
}

void HeadlessWindow::injectResize(u32 width, u32 height) {
    inject(Resized{{scaled(width, m_scale), scaled(height, m_scale)}, {width, height}});
}

void HeadlessWindow::pollEvents(PlatformEventQueue& out) {
    while (!m_pending.empty()) {
        PlatformEvent e = std::move(m_pending.front());
        m_pending.pop_front();
        if (const auto* r = std::get_if<Resized>(&e)) {
            m_framebuffer = r->framebuffer;
            m_window = r->window;
        } else if (const auto* s = std::get_if<ContentScaleChanged>(&e)) {
            m_scale = s->scale;
        } else if (std::holds_alternative<FocusGained>(e)) {
            m_focused = true;
        } else if (std::holds_alternative<FocusLost>(e)) {
            m_focused = false;
        } else if (std::holds_alternative<CloseRequested>(e)) {
            m_shouldClose = true;
        }
        out.push(std::move(e));
    }
}

void HeadlessWindow::waitEvents(std::chrono::milliseconds /*timeout*/) {
    ++m_waits; // 기다리지 않는다 — 테스트·CI 를 느리게 만들 이유가 없다
}

} // namespace sbx::platform
