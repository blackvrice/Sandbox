# GXML · GCSS → C++ (AOT) — UI-R2-02, ADR-0175.
#
#   sbx_gxml_core(<target> NAME <이름> MARKUP <a.gxml> [STYLE <a.gcss>] [MODEL <a.model.json>])
#
# 빌드 때 external/graphics-ui/tools/gxmlc.py --compile-core 로 ${CMAKE_BINARY_DIR}/generated/gxml/sandbox/gxml/<이름>.hpp 를
# 만들고 <target> 에 붙인다 (생성물은 커밋하지 않는다 — 원본이 바뀌면 다시 만든다). 헤더는 graphics_ui::generated::document_<루트 id>()
# 하나를 정의한다. 컴파일러 = 고정 커밋의 6 파일 (tools/vendor_graphics_ui.py), 순수 Python ≥ 3.11 + 표준 expat — 배포 실행 파일에는
# 파서 · Python 이 들어가지 않는다. 생성 헤더는 SYSTEM include (생성 코드의 경고는 우리 경고 정책 밖).
#
# SBX_GXML_AVAILABLE — Python ≥ 3.11 이 있으면 ON. 없으면 GXML 화면을 빼고 (SBX_HAS_GXML 정의 없음) 구성 로그에 알린다.
include_guard(GLOBAL)

find_package(Python3 3.11 QUIET COMPONENTS Interpreter)
set(_sbx_gxml_tools "${PROJECT_SOURCE_DIR}/external/graphics-ui/tools")
set(_sbx_gxml_compiler_inputs gxmlc.py gxml_schema.py gxml_core.py gcss_syntax.py gcss_core.py gxml_ir.py)
set(SBX_GXML_AVAILABLE OFF)
if(Python3_Interpreter_FOUND AND EXISTS "${_sbx_gxml_tools}/gxmlc.py")
    set(SBX_GXML_AVAILABLE ON)
    message(STATUS "Sandbox: GXML AOT — Python ${Python3_VERSION} (${Python3_EXECUTABLE})")
else()
    message(STATUS "Sandbox: GXML AOT 꺼짐 — Python ≥ 3.11 이 없다 (GXML 화면 없이 빌드한다 — ADR-0175)")
endif()

function(sbx_gxml_core target)
    cmake_parse_arguments(ARG "" "NAME;MARKUP;STYLE;MODEL" "" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_MARKUP)
        message(FATAL_ERROR "sbx_gxml_core: NAME 과 MARKUP 이 필요하다")
    endif()
    if(NOT SBX_GXML_AVAILABLE)
        return()
    endif()
    set(out_dir "${CMAKE_BINARY_DIR}/generated/gxml")
    set(out "${out_dir}/sandbox/gxml/${ARG_NAME}.hpp")
    set(args --compile-core --markup "${ARG_MARKUP}" --output "${out}")
    set(deps "${ARG_MARKUP}")
    if(ARG_STYLE)
        list(APPEND args --style "${ARG_STYLE}")
        list(APPEND deps "${ARG_STYLE}")
    endif()
    if(ARG_MODEL)
        list(APPEND args --model "${ARG_MODEL}")
        list(APPEND deps "${ARG_MODEL}")
    endif()
    list(TRANSFORM _sbx_gxml_compiler_inputs PREPEND "${_sbx_gxml_tools}/" OUTPUT_VARIABLE tools)
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}/sandbox/gxml"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${_sbx_gxml_tools}/gxmlc.py" ${args}
        DEPENDS ${deps} ${tools}
        COMMENT "GXML → C++ (${ARG_NAME})"
        VERBATIM)
    target_sources(${target} PRIVATE "${out}")
    target_include_directories(${target} SYSTEM PRIVATE "${out_dir}")
    target_compile_definitions(${target} PRIVATE SBX_HAS_GXML=1)
endfunction()
