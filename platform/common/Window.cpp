#include "platform/common/Window.hpp"

namespace sbx::platform {

std::string_view windowSystemName(WindowSystem s) noexcept {
    switch (s) {
    case WindowSystem::Auto:
        return "auto";
    case WindowSystem::X11:
        return "x11";
    case WindowSystem::Wayland:
        return "wayland";
    }
    return "auto";
}

std::optional<WindowSystem> parseWindowSystem(std::string_view name) noexcept {
    for (const WindowSystem s : {WindowSystem::Auto, WindowSystem::X11, WindowSystem::Wayland}) {
        if (windowSystemName(s) == name) {
            return s;
        }
    }
    return std::nullopt;
}

} // namespace sbx::platform
