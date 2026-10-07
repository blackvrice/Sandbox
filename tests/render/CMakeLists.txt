# sbx_render_tests — GPU 기준 이미지·수명 테스트 (docs/06-RENDERING.md 14장). 의존: Render (+ Platform). Core 를 모른다.
add_executable(sbx_render_tests
    main.cpp
    RenderTestEnv.hpp
    test_rhi_basic.cpp
)
# 7B 그리기 테스트는 셰이더가 내장됐을 때만 (SBX_BUILD_SHADERS — cmake/SbxShaders.cmake)
if(SBX_BUILD_SHADERS)
    target_sources(sbx_render_tests PRIVATE test_rhi_draw.cpp)
endif()
target_link_libraries(sbx_render_tests PRIVATE SandboxRender sbx_doctest sbx_warnings)
target_compile_definitions(sbx_render_tests PRIVATE SBX_RENDER_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/references")

# WARP(소프트웨어)로 — GPU 가 없는 CI 에서도 같은 결과. Debug Layer 는 "그래픽 도구" 가 설치돼 있으면 켜진다 (없으면 경고 후 계속).
add_test(NAME render_tests_warp
    COMMAND sbx_render_tests --warp --debug --out "${CMAKE_CURRENT_BINARY_DIR}/out_warp")
set_tests_properties(render_tests_warp PROPERTIES LABELS "render" PASS_REGULAR_EXPRESSION "render tests OK")
