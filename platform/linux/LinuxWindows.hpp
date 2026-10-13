#pragma once
// Linux 창 구현들 — createWindow(LinuxPlatform.cpp)가 WindowDesc.system · WAYLAND_DISPLAY 로 고른다. ADR-0033 · 0034.

#include <memory>

#include "foundation/types/Error.hpp"
#include "platform/common/Window.hpp"

namespace sbx::platform::linuxwin {

#if defined(SBX_HAS_X11)
[[nodiscard]] Expected<std::unique_ptr<IWindow>> createX11Window(const WindowDesc& desc);
#endif
#if defined(SBX_HAS_WAYLAND)
[[nodiscard]] Expected<std::unique_ptr<IWindow>> createWaylandWindow(const WindowDesc& desc);
#endif

} // namespace sbx::platform::linuxwin
