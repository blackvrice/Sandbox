# sbx_net_probe — 서버 접속 확인 도구 (Phase 9, docs/15-BUILD.md 7장). Network + Core 만 링크한다 (창 · GPU 없음).
add_executable(sbx_net_probe
    main.cpp
    NetProbeOptions.hpp
    NetProbeOptions.cpp
)
target_link_libraries(sbx_net_probe PRIVATE SandboxNetwork SandboxCore sbx_warnings)
# 서버가 알려 준 콘텐츠 팩을 읽을 기본 루트 = 저장소의 content/ (개발용. --content-root 로 바꾼다)
target_compile_definitions(sbx_net_probe PRIVATE SBX_DEFAULT_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
