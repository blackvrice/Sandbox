# SandboxClient — 앱 상태기계 + 창 루프. Phase 6: Platform. Phase 7A: + Render (Clear). Network·Editor·Core 는 Phase 8~12.
# 의존 계획: Editor + Network + Render + Platform + Core (docs/01-ARCHITECTURE.md 2장).
add_executable(SandboxClient
    main.cpp
    Application.hpp
    Application.cpp
    ClientOptions.hpp
    ClientOptions.cpp
    DefaultInput.hpp
    DefaultInput.cpp
    FrameRenderer.hpp
    ClearRenderer.cpp
)
target_link_libraries(SandboxClient PRIVATE SandboxRender SandboxPlatform sbx_warnings)

if(WIN32)
    # GUI 서브시스템 (콘솔 창 없음) + main 진입점 (docs/07-PLATFORM.md 6.1). 로그는 --console.
    set_target_properties(SandboxClient PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(SandboxClient PRIVATE /ENTRY:mainCRTStartup)
        # Per-Monitor DPI v2. MSVC 링커가 .manifest 소스를 실행 파일에 넣는다. MinGW 는 런타임에 같은 설정을 한다.
        target_sources(SandboxClient PRIVATE SandboxClient.manifest)
    endif()
endif()
