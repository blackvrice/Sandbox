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
    # 순수 표 — 모든 OS 에서 컴파일해 단위 테스트가 확인한다 (windows.h 를 쓰지 않는다)
    windows/Win32KeyMap.hpp
    windows/Win32KeyMap.cpp
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
else()
    # 창 구현이 아직 없는 OS: Linux Phase 13, macOS Phase 14. createWindow 는 Unsupported.
    target_sources(SandboxPlatform PRIVATE stub/StubPlatform.cpp)
endif()

target_link_libraries(SandboxPlatform
    PUBLIC SandboxFoundation
    PRIVATE sbx_nlohmann_json sbx_warnings)
