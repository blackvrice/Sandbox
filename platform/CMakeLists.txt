# SandboxPlatform — 창 · 입력 · 오디오. 의존: Foundation (+ nlohmann/json 은 ActionMap 파싱에만, PRIVATE).
# Core · Render · Network 를 링크하면 루트의 경계 검사가 구성을 중단한다. docs/07-PLATFORM.md 1장.
add_library(SandboxPlatform STATIC
    common/Key.hpp
    common/Key.cpp
    common/PlatformEvent.hpp
    common/Window.hpp
    common/Window.cpp
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
elseif(SBX_ENABLE_X11 OR SBX_ENABLE_WAYLAND)
    # Linux 창: X11 (Phase 13B, ADR-0033) · Wayland (Phase 13C, ADR-0034). 둘 다 켜면 실행 중에 고른다
    # (WAYLAND_DISPLAY 가 있으면 Wayland, 안 되면 X11 — --platform 으로 강제).
    target_sources(SandboxPlatform PRIVATE
        linux/LinuxWindows.hpp
        linux/LinuxPlatform.cpp)
    if(SBX_ENABLE_X11)
        # libX11 은 링크, XInput2(libXi)는 있으면 캡처 중 Raw 이동에 쓴다.
        find_package(X11)
        if(NOT X11_FOUND)
            message(FATAL_ERROR "X11 개발 파일이 없습니다 — Debian/Ubuntu: sudo apt install libx11-dev libxi-dev "
                                "(X11 창 없이 빌드하려면 -D SBX_ENABLE_X11=OFF)")
        endif()
        target_sources(SandboxPlatform PRIVATE linux/X11Window.cpp)
        target_link_libraries(SandboxPlatform PRIVATE X11::X11)
        if(TARGET X11::Xi)
            target_link_libraries(SandboxPlatform PRIVATE X11::Xi)
            target_compile_definitions(SandboxPlatform PRIVATE SBX_HAS_XINPUT2=1)
        else()
            message(STATUS "Sandbox: XInput2(libxi-dev)가 없다 — 커서 캡처는 가운데로 되돌리는 방식 (가속이 섞인다)")
        endif()
        # Vulkan 의 Xlib 표면 (render/vulkan) 과 시험이 안다
        target_compile_definitions(SandboxPlatform PUBLIC SBX_HAS_X11=1)
    endif()
    if(SBX_ENABLE_WAYLAND)
        include(SbxWayland)
        sbx_wayland_protocols(SandboxPlatform
            unstable/text-input/text-input-unstable-v3.xml
            unstable/relative-pointer/relative-pointer-unstable-v1.xml
            unstable/pointer-constraints/pointer-constraints-unstable-v1.xml)
        target_sources(SandboxPlatform PRIVATE
            linux/WaylandInput.hpp
            linux/WaylandInput.cpp
            linux/WaylandWindow.cpp)
        target_link_libraries(SandboxPlatform PRIVATE PkgConfig::SBX_WAYLAND)
        # C 리스너 표(libdecor 의 reserved 칸 · 버전 밖 이벤트)는 일부만 채운다 — 나머지는 nullptr 이 맞다
        if(NOT MSVC)
            set_source_files_properties(linux/WaylandWindow.cpp PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
        endif()
        # Vulkan 의 Wayland 표면 · 시험이 안다
        target_compile_definitions(SandboxPlatform PUBLIC SBX_HAS_WAYLAND=1)
    endif()
else()
    # 창 구현이 없는 빌드: macOS(Phase 14), X11 · Wayland 를 모두 끈 Linux. createWindow 는 Unsupported.
    target_sources(SandboxPlatform PRIVATE stub/StubPlatform.cpp)
endif()

target_link_libraries(SandboxPlatform
    PUBLIC SandboxFoundation
    PRIVATE sbx_nlohmann_json sbx_warnings)
