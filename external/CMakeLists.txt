# vendored 서드파티를 CMake 타깃으로 노출한다. 목록·버전은 external/README.md.
# SYSTEM include: 서드파티 헤더의 경고를 우리 경고 정책에서 뺀다.

add_library(sbx_nlohmann_json INTERFACE)
target_include_directories(sbx_nlohmann_json SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/include")
# 암묵적 변환(json → T) 을 끈다. 타입이 틀린 읽기가 조용히 통과하지 않게.
target_compile_definitions(sbx_nlohmann_json INTERFACE JSON_USE_IMPLICIT_CONVERSIONS=0)
add_library(nlohmann_json::nlohmann_json ALIAS sbx_nlohmann_json)

add_library(sbx_doctest INTERFACE)
target_include_directories(sbx_doctest SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
# doctest 는 기본으로 std::ostream 등을 앞선언만 한다. MSVC 에서는 그 상태로 std::string_view 를 문자열화하지 못해
# CHECK(sv == ...) 가 컴파일되지 않는다 (Windows 에서 실제로 발생 — test_error.cpp). 표준 헤더를 직접 포함하게 한다.
target_compile_definitions(sbx_doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)

# stb_image · stb_image_write (공개 도메인 / MIT). 구현은 render/asset/StbImpl.cpp 한 곳에서 (STB_*_IMPLEMENTATION).
add_library(sbx_stb INTERFACE)
target_include_directories(sbx_stb SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")

# Dear ImGui (MIT, docking 브랜치 — 06 10장 I2). 정적 라이브러리 하나. 백엔드(imgui_impl_*)는 쓰지 않는다 —
# 렌더러는 render/imgui/ImGuiRenderer(RHI), 입력은 apps/client/ui/ImGuiLayer (ADR-0008 · ADR-0023).
add_library(sbx_imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
target_include_directories(sbx_imgui SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
target_compile_definitions(sbx_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_features(sbx_imgui PUBLIC cxx_std_20)
# 서드파티 코드의 경고는 우리 정책(경고 = 오류)에서 뺀다
if(MSVC)
    target_compile_options(sbx_imgui PRIVATE /w)
else()
    target_compile_options(sbx_imgui PRIVATE -w)
endif()
