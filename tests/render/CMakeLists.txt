# sbx_render_tests — GPU 기준 이미지·수명 테스트 (docs/06-RENDERING.md 14장). 의존: Render (+ Platform). Core 를 모른다.
add_executable(sbx_render_tests
    main.cpp
    RenderTestEnv.hpp
    test_rhi_basic.cpp
)
# 7B · 8A · 8B · 8C 그리기 테스트는 셰이더가 내장됐을 때만 (SBX_BUILD_SHADERS — cmake/SbxShaders.cmake)
if(SBX_BUILD_SHADERS)
    target_sources(sbx_render_tests PRIVATE test_rhi_draw.cpp test_sprites.cpp test_world_passes.cpp test_imgui.cpp)
endif()
target_link_libraries(sbx_render_tests PRIVATE SandboxRender sbx_imgui sbx_doctest sbx_warnings)
target_compile_definitions(sbx_render_tests PRIVATE SBX_RENDER_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/references")

# D3D12 는 WARP(소프트웨어)로 — GPU 가 없는 CI 에서도 같은 결과. Debug Layer 는 "그래픽 도구" 가 설치돼 있으면 켜진다
# (없으면 경고 후 계속).
if(WIN32)
    add_test(NAME render_tests_warp
        COMMAND sbx_render_tests --rhi dx12 --warp --debug --out "${CMAKE_CURRENT_BINARY_DIR}/out_warp")
    set_tests_properties(render_tests_warp PROPERTIES LABELS "render" PASS_REGULAR_EXPRESSION "render tests OK")
endif()
# Vulkan 은 lavapipe(Mesa 의 CPU 드라이버)로 — WARP 와 같은 자리. 드라이버 목록을 lavapipe 하나로 좁혀 GPU 가 있는
# 기계에서도 같은 결과를 낸다. Validation 레이어(VK_LAYER_KHRONOS_validation)가 없으면 경고 후 계속.
# Windows 의 Vulkan 시험은 드라이버가 기계마다 달라 CTest 에 넣지 않는다 (docs/15-BUILD.md · MANUAL-QA).
if(SBX_ENABLE_VULKAN AND NOT WIN32)
    find_file(SBX_LAVAPIPE_ICD NAMES lvp_icd.json lvp_icd.x86_64.json lvp_icd.aarch64.json
              PATHS /usr/share/vulkan/icd.d /usr/local/share/vulkan/icd.d /etc/vulkan/icd.d NO_DEFAULT_PATH)
    add_test(NAME render_tests_lavapipe
        COMMAND sbx_render_tests --rhi vulkan --warp --debug --out "${CMAKE_CURRENT_BINARY_DIR}/out_lavapipe")
    set_tests_properties(render_tests_lavapipe PROPERTIES LABELS "render" PASS_REGULAR_EXPRESSION "render tests OK")
    if(SBX_LAVAPIPE_ICD)
        set_tests_properties(render_tests_lavapipe PROPERTIES
            ENVIRONMENT "VK_DRIVER_FILES=${SBX_LAVAPIPE_ICD};VK_ICD_FILENAMES=${SBX_LAVAPIPE_ICD}")
    else()
        message(STATUS "Sandbox: lavapipe ICD 를 찾지 못했다 — render_tests_lavapipe 는 로더가 고른 CPU 드라이버를 쓴다 "
                       "(Debian/Ubuntu: mesa-vulkan-drivers)")
    endif()
endif()
