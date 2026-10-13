# SandboxPlatform — 창 · 입력 · 오디오. 의존: Foundation (+ nlohmann/json 은 ActionMap 파싱에만, PRIVATE).
# Core · Render · Network 를 링크하면 루트의 경계 검사가 구성을 중단한다. docs/07-PLATFORM.md 1장.
add_library(SandboxPlatform STATIC
    common/Key.hpp
    common/Key.cpp
    common/PlatformEvent.hpp
    common/Window.hpp
    common/HeadlessWindow.hpp
    common/HeadlessWindow.cpp
    common/Gamepad.hpp
    common/InputSystem.hpp
    common/InputSystem.cpp
    common/ActionMap.hpp
    common/ActionMap.cpp
    common/Audio.hpp
    common/Platform.hpp
    common/FramePacer.cpp
    audio/NullAudioBackend.hpp
    audio/NullAudioBackend.cpp
    # 순수 표 — 모든 OS 에서 컴파일해 단위 테스트가 확인한다 (windows.h · Xlib.h 를 쓰지 않는다)
    windows/Win32KeyMap.hpp
    windows/Win32KeyMap.cpp
    linux/X11KeyMap.hpp
    linux/X11KeyMap.cpp
)
add_library(Sandbox::Platform ALIAS SandboxPlatform)

if(WIN32)
    target_sources(SandboxPlatform PRIVATE
        windows/Win32Common.hpp
        windows/Win32Window.cpp
        windows/Win32Platform.cpp)
    # 유니코드 API, Windows 10 API (GetDpiForWindow · AdjustWindowRectExForDpi · Per-Monitor v2)
    target_compile_definitions(SandboxPlatform PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                                                       _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(SandboxPlatform PRIVATE user32 imm32)
elseif(SBX_ENABLE_X11)
    # Linux X11 창 (Phase 13B, ADR-0033). libX11 은 링크, XInput2(libXi)는 있으면 캡처 중 Raw 이동에 쓴다.
    find_package(X11)
    if(NOT X11_FOUND)
        message(FATAL_ERROR "X11 개발 파일이 없습니다 — Debian/Ubuntu: sudo apt install libx11-dev libxi-dev "
                            "(창 없이 빌드하려면 -D SBX_ENABLE_X11=OFF — SandboxClient 는 --headless 만)")
    endif()
    target_sources(SandboxPlatform PRIVATE
        linux/LinuxPlatform.cpp
        linux/X11Window.cpp)
    target_link_libraries(SandboxPlatform PRIVATE X11::X11)
    if(TARGET X11::Xi)
        target_link_libraries(SandboxPlatform PRIVATE X11::Xi)
        target_compile_definitions(SandboxPlatform PRIVATE SBX_HAS_XINPUT2=1)
    else()
        message(STATUS "Sandbox: XInput2(libxi-dev)가 없다 — 커서 캡처는 가운데로 되돌리는 방식 (가속이 섞인다)")
    endif()
    # Vulkan 의 Xlib 표면 (render/vulkan) 과 시험이 안다
    target_compile_definitions(SandboxPlatform PUBLIC SBX_HAS_X11=1)
else()
    # 창 구현이 없는 빌드: macOS(Phase 14), SBX_ENABLE_X11=OFF 인 Linux. createWindow 는 Unsupported.
    target_sources(SandboxPlatform PRIVATE stub/StubPlatform.cpp)
endif()

target_link_libraries(SandboxPlatform
    PUBLIC SandboxFoundation
    PRIVATE sbx_nlohmann_json sbx_warnings)
