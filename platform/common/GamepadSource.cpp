// 게임패드 구현이 없는 OS 의 소스 (docs/07-PLATFORM.md I5 — Linux evdev · macOS GameController 는 [계획]).
// Windows 는 platform/windows/Win32Gamepad.cpp 가 정의한다.
#include "platform/common/Gamepad.hpp"

#if !defined(_WIN32)
namespace sbx::platform {

std::unique_ptr<IGamepadSource> createGamepadSource() {
    return std::make_unique<NullGamepadSource>();
}

} // namespace sbx::platform
#endif
