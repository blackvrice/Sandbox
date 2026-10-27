# sbx — 게임 프로젝트 도구 (v0.2 M1-T4, ADR-0043 — docs/18-PLATFORM.md 5장). Core 만 링크한다.
add_executable(sbx
    main.cpp
    SbxOptions.hpp
    SbxOptions.cpp
    Process.hpp
    Process.cpp
)
target_link_libraries(sbx PRIVATE SandboxCore sbx_warnings)
# new --from 의 팩을 찾을 기본 루트 = 저장소의 content/ (--content-root 로 바꾼다)
target_compile_definitions(sbx PRIVATE SBX_DEFAULT_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
