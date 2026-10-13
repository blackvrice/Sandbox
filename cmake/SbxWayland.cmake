# Wayland 클라이언트 의존 (Phase 13C, docs/07-PLATFORM.md 6.2, ADR-0034).
#   pkg-config: wayland-client · wayland-cursor · xkbcommon · libdecor-0 → PkgConfig::SBX_WAYLAND
#   프로토콜: wayland-protocols 의 XML → wayland-scanner 로 <빌드>/generated/wayland/<이름>-client-protocol.{h,c}
# 없으면 구성 오류 + 패키지 안내 (-D SBX_ENABLE_WAYLAND=OFF 면 X11 만).
enable_language(C) # wayland-scanner 가 만든 C 코드
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SBX_WAYLAND IMPORTED_TARGET wayland-client wayland-cursor xkbcommon libdecor-0)
    pkg_get_variable(SBX_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    pkg_get_variable(_sbx_scanner_bin wayland-scanner wayland_scanner)
endif()
find_program(SBX_WAYLAND_SCANNER NAMES wayland-scanner HINTS "${_sbx_scanner_bin}")
mark_as_advanced(SBX_WAYLAND_SCANNER)
if(NOT SBX_WAYLAND_FOUND OR NOT SBX_WAYLAND_PROTOCOLS_DIR OR NOT SBX_WAYLAND_SCANNER)
    message(FATAL_ERROR "Wayland 개발 파일이 없습니다 — Debian/Ubuntu: sudo apt install libwayland-dev wayland-protocols "
                        "libxkbcommon-dev libdecor-0-dev (Wayland 창 없이 빌드하려면 -D SBX_ENABLE_WAYLAND=OFF)")
endif()

# sbx_wayland_protocols(<타깃> <wayland-protocols 안 상대 경로>...)
function(sbx_wayland_protocols target)
    set(_dir "${CMAKE_BINARY_DIR}/generated/wayland")
    file(MAKE_DIRECTORY "${_dir}")
    foreach(xml IN LISTS ARGN)
        get_filename_component(_name "${xml}" NAME_WE)
        set(_src "${SBX_WAYLAND_PROTOCOLS_DIR}/${xml}")
        set(_h "${_dir}/${_name}-client-protocol.h")
        set(_c "${_dir}/${_name}-protocol.c")
        add_custom_command(OUTPUT "${_h}" "${_c}"
            COMMAND "${SBX_WAYLAND_SCANNER}" client-header "${_src}" "${_h}"
            COMMAND "${SBX_WAYLAND_SCANNER}" private-code "${_src}" "${_c}"
            DEPENDS "${_src}"
            COMMENT "wayland-scanner ${_name}"
            VERBATIM)
        target_sources(${target} PRIVATE "${_h}" "${_c}")
        # 생성 C 코드는 우리 경고 정책 밖
        set_source_files_properties("${_c}" PROPERTIES COMPILE_OPTIONS "-w")
    endforeach()
    target_include_directories(${target} SYSTEM PRIVATE "${_dir}")
endfunction()
