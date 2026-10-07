# sbx_sim_check — 결정론 하네스 (docs/04-DETERMINISM.md, docs/13-TESTING.md 5장). Core 만 링크한다.
add_executable(sbx_sim_check
    main.cpp
    SimCheckOptions.hpp
    SimCheckOptions.cpp
)
target_link_libraries(sbx_sim_check PRIVATE SandboxCore sbx_warnings)
# 기본 콘텐츠 루트 = 저장소의 content/ (개발용. --content-root 로 바꾼다)
target_compile_definitions(sbx_sim_check PRIVATE SBX_DEFAULT_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
