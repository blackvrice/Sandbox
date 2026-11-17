# sbx — 게임 프로젝트 도구 (v0.2 M1-T4, ADR-0043 — docs/18-PLATFORM.md 5장). Core 만 링크한다.
add_executable(sbx
    main.cpp
    SbxOptions.hpp
    SbxOptions.cpp
    Process.hpp
    Process.cpp
)
# SandboxEditorServices — sbx ui 가 UI 디자이너의 모델 (UiDesignService) 을 쓴다 (v0.10 NG-18, ADR-0147 — 화면 · ImGui 없음)
target_link_libraries(sbx PRIVATE SandboxCore SandboxEditorServices SandboxProcess sbx_warnings)
# new --from 의 팩을 찾을 기본 루트 = 저장소의 content/ (--content-root 로 바꾼다)
target_compile_definitions(sbx PRIVATE SBX_DEFAULT_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content"
                                       SBX_DEFAULT_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets"
                                       SBX_DEFAULT_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
