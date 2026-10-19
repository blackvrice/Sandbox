#pragma once
// macOS 창 생성 (MacPlatform.mm 의 createWindow 가 부른다). AppKit 타입 없이 선언만 — 정의는 CocoaWindow.mm.
#include <memory>

#include "platform/common/Window.hpp"

namespace sbx::platform::macwin {

[[nodiscard]] Expected<std::unique_ptr<IWindow>> createCocoaWindow(const WindowDesc& desc);

} // namespace sbx::platform::macwin
