# GraphicsUI Core 공급 (ADR-0174 — UI-R1-01). GraphicsUI::Core = GPU · OS 창 · SDK 의존이 없는 정적 C++23 UI 코어 (트리 ·
# 상태 · 입력 · 배치 · 스타일 · 그리기 명령 · Dock).
#
#   external/graphics-ui — 고정 커밋의 Core 만 (tools/vendor_graphics_ui.py — 공개 헤더 · src/core · 버전 틀 · LICENSE, 수정 없음).
#   GraphicsUI 의 최상위 CMakeLists 는 쓰지 않는다: 연구 · 예제 타깃, Python 3.11 검사, 고정 SDK 스냅숏 (.deps) 이 함께 붙고,
#   그 SDK 렌더러 (GraphicsUI::SdkRenderer) 는 Sandbox 와 다른 GraphicsSDK 커밋에 묶여 있다 (UPSTREAM GUI-UP-01).
#   그래서 그 커밋의 add_library(GraphicsUICore ...) 목록 (CORE_SOURCES.txt) 으로 같은 이름 · 같은 별칭의 타깃을 만든다.
#
# 쓰는 곳: SandboxUiHost (uihost/) · SandboxClient · 시험. Foundation · Script · Core · Network · GameUI · Render · Platform ·
# EditorServices · Server 는 링크하지 않는다 (CMakeLists.txt 의 sbx_forbid_link, tools/check_includes.py).
include_guard(GLOBAL)

set(_sbx_gui_dir "${PROJECT_SOURCE_DIR}/external/graphics-ui")
foreach(_f VENDORED_COMMIT VERSION CORE_SOURCES.txt cmake/GraphicsUIVersion.hpp.in)
    if(NOT EXISTS "${_sbx_gui_dir}/${_f}")
        message(FATAL_ERROR "external/graphics-ui/${_f} 가 없다 — tools/vendor_graphics_ui.py 로 다시 가져온다")
    endif()
endforeach()
file(STRINGS "${_sbx_gui_dir}/VENDORED_COMMIT" _sbx_gui_commit LIMIT_COUNT 1)
file(STRINGS "${_sbx_gui_dir}/VERSION" _sbx_gui_version LIMIT_COUNT 1)
file(STRINGS "${_sbx_gui_dir}/CORE_SOURCES.txt" _sbx_gui_sources)
list(TRANSFORM _sbx_gui_sources PREPEND "${_sbx_gui_dir}/")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_sbx_gui_dir}/VENDORED_COMMIT" "${_sbx_gui_dir}/VERSION" "${_sbx_gui_dir}/CORE_SOURCES.txt")

add_library(GraphicsUICore STATIC ${_sbx_gui_sources})
add_library(GraphicsUI::Core ALIAS GraphicsUICore)
set_target_properties(GraphicsUICore PROPERTIES CXX_EXTENSIONS OFF FOLDER "external")
target_compile_features(GraphicsUICore PUBLIC cxx_std_23)

# 버전 헤더 — GraphicsUI 의 configure_file 과 같은 틀 · 같은 변수 이름 (PROJECT_VERSION_* 를 이 함수 범위에서만 바꾼다)
function(_sbx_gui_version_header out)
    string(REPLACE "." ";" _v "${_sbx_gui_version}")
    list(GET _v 0 PROJECT_VERSION_MAJOR)
    list(GET _v 1 PROJECT_VERSION_MINOR)
    list(GET _v 2 PROJECT_VERSION_PATCH)
    configure_file("${_sbx_gui_dir}/cmake/GraphicsUIVersion.hpp.in" "${out}" @ONLY)
endfunction()
set(_sbx_gui_generated "${CMAKE_BINARY_DIR}/external/graphics-ui/generated/include")
_sbx_gui_version_header("${_sbx_gui_generated}/graphics-ui/core/version.hpp")

# SYSTEM: 서드파티 헤더 — 우리 경고 정책 (SBX_WARNINGS_AS_ERRORS) 밖. 소스도 우리 경고 묶음 (sbx_warnings) 을 쓰지 않는다
target_include_directories(GraphicsUICore SYSTEM PUBLIC "${_sbx_gui_dir}/include" "${_sbx_gui_generated}")
if(MSVC)
    # GraphicsUI 자체 빌드와 같은 엄격 모드 · UTF-8 (/W4 는 넣지 않는다 — 경고는 업스트림 CI 가 본다)
    target_compile_options(GraphicsUICore PRIVATE /permissive- /utf-8)
    target_compile_definitions(GraphicsUICore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

message(STATUS "Sandbox: GraphicsUI Core ${_sbx_gui_version} — external/graphics-ui (${_sbx_gui_commit})")
