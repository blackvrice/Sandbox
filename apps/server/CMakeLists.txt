# SandboxServer — 헤드리스 전용 서버. 의존: Network + Core 만 (Phase 9 전까지는 Core 만).
# Platform / Render / Editor 를 링크하면 루트의 경계 검사가 구성을 중단한다.
add_executable(SandboxServer
    main.cpp
    ServerOptions.hpp
    ServerOptions.cpp
)
target_link_libraries(SandboxServer PRIVATE SandboxCore sbx_warnings)
target_compile_definitions(SandboxServer PRIVATE SBX_DEFAULT_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
