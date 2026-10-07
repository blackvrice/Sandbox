# 셰이더 빌드. docs/06-RENDERING.md 6장, ADR-0019.
#
#   HLSL ─ dxc ─▶ .dxil (D3D12)
#        └ dxc -spirv ─▶ .spv (리플렉션 · Phase 13 Vulkan)
#   .dxil + .spv ─ tools/shader/sbx_shader_gen.py ─▶ render/generated/<Pascal>Shader.{hpp,cpp} (바이트코드 내장 · 리플렉션 ·
#                                                     cbuffer 구조체 + static_assert) + <name>.reflect.json
#
# DXC 는 버전을 고정한다 (06 6.1). Windows 호스트는 NuGet 패키지 Microsoft.Direct3D.DXC 를 처음 구성할 때 내려받아
# <저장소>/.cache/dxc/<버전>/ 에 풀어 둔다 (약 53 MB, 프리셋끼리 공유, .gitignore). SBX_DXC 로 다른 dxc 를 지정할 수 있다.
# 셰이더 산출물은 빌드 폴더에만 있다 (저장소에 커밋하지 않는다).

set(SBX_DXC_VERSION "1.9.2609.5")
set(SBX_DXC_SHA256 "73cfa082245918c2cc1ec6740250eb7377e83a757b3b3a3e16e0d71535bfbfc3")

# 렌더 백엔드가 있는 플랫폼에서만 기본으로 켠다 (지금은 Windows D3D12). Linux Vulkan 은 Phase 13.
if(WIN32)
    set(_sbx_shaders_default ON)
else()
    set(_sbx_shaders_default OFF)
endif()
option(SBX_BUILD_SHADERS "HLSL 셰이더를 빌드해 SandboxRender 에 내장 (DXC · Python 3 필요)" ${_sbx_shaders_default})
set(SBX_DXC "" CACHE FILEPATH "dxc 실행 파일 (비면: Windows 는 고정 버전을 내려받는다)")

function(sbx_find_dxc out)
    if(SBX_DXC)
        set(${out} "${SBX_DXC}" PARENT_SCOPE)
        return()
    endif()
    if(NOT CMAKE_HOST_WIN32)
        message(FATAL_ERROR
            "SBX_BUILD_SHADERS=ON 인데 dxc 가 없습니다. -D SBX_DXC=<dxc 경로> 를 주거나 SBX_BUILD_SHADERS=OFF 로 구성하십시오. "
            "(교차 빌드 시험: tools/wine/dxc.sh — ADR-0019)")
    endif()
    set(root "${PROJECT_SOURCE_DIR}/.cache/dxc/${SBX_DXC_VERSION}")
    set(exe "${root}/build/native/bin/x64/dxc.exe")
    if(NOT EXISTS "${exe}")
        set(pkg "${PROJECT_SOURCE_DIR}/.cache/dxc/microsoft.direct3d.dxc.${SBX_DXC_VERSION}.nupkg")
        if(NOT EXISTS "${pkg}")
            message(STATUS "DXC ${SBX_DXC_VERSION} 내려받는 중 (NuGet, 약 53 MB — 처음 한 번)")
            file(DOWNLOAD
                "https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.dxc/${SBX_DXC_VERSION}/microsoft.direct3d.dxc.${SBX_DXC_VERSION}.nupkg"
                "${pkg}.part"
                EXPECTED_HASH SHA256=${SBX_DXC_SHA256}
                STATUS st
                TLS_VERIFY ON)
            list(GET st 0 code)
            if(NOT code EQUAL 0)
                file(REMOVE "${pkg}.part")
                list(GET st 1 msg)
                message(FATAL_ERROR "DXC 를 내려받지 못했습니다: ${msg}. 인터넷 연결을 확인하거나 -D SBX_DXC=<dxc.exe> 를 주십시오 "
                                    "(Windows SDK 의 bin\\<버전>\\x64\\dxc.exe 도 된다).")
            endif()
            file(RENAME "${pkg}.part" "${pkg}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${pkg}" DESTINATION "${root}" PATTERNS "build/native/bin/x64/*")
    endif()
    set(${out} "${exe}" PARENT_SCOPE)
endfunction()

if(SBX_BUILD_SHADERS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    sbx_find_dxc(SBX_DXC_EXECUTABLE)
    message(STATUS "셰이더: dxc = ${SBX_DXC_EXECUTABLE}")
    file(GLOB SBX_SHADER_COMMON CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/shaders/common/*.hlsli")
endif()

# sbx_add_shader(TARGET <target> NAME <snake_name> SOURCE <shaders/x.hlsl> STAGES vs:VSMain ps:PSMain)
#   → #include "render/generated/<Pascal>Shader.hpp"  (namespace sbx::render::shaders::<name>)
function(sbx_add_shader)
    cmake_parse_arguments(S "" "TARGET;NAME;SOURCE" "STAGES" ${ARGN})
    if(NOT SBX_BUILD_SHADERS)
        return()
    endif()
    set(src "${PROJECT_SOURCE_DIR}/${S_SOURCE}")
    set(work "${CMAKE_BINARY_DIR}/shaders/${S_NAME}")
    set(gen "${SBX_GENERATED_DIR}/render/generated")
    # snake_name → PascalName
    string(REPLACE "_" ";" parts "${S_NAME}")
    set(pascal "")
    foreach(p IN LISTS parts)
        string(SUBSTRING "${p}" 0 1 first)
        string(SUBSTRING "${p}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        string(APPEND pascal "${first}${rest}")
    endforeach()

    set(common_flags -HV 2021 -Zpc -O3 -Qstrip_debug -Wno-ignored-attributes -I "${PROJECT_SOURCE_DIR}/shaders")
    set(stage_args "")
    set(stage_outputs "")
    foreach(st IN LISTS S_STAGES)
        string(REPLACE ":" ";" kv "${st}")
        list(GET kv 0 key)
        list(GET kv 1 entry)
        set(dxil "${work}/${key}.dxil")
        set(spv "${work}/${key}.spv")
        add_custom_command(
            OUTPUT "${dxil}" "${spv}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${work}"
            COMMAND "${SBX_DXC_EXECUTABLE}" -T ${key}_6_0 -E ${entry} ${common_flags} -Qstrip_reflect -Fo "${dxil}" "${src}"
            COMMAND "${SBX_DXC_EXECUTABLE}" -T ${key}_6_0 -E ${entry} ${common_flags} -spirv -fspv-target-env=vulkan1.3
                    -fvk-use-dx-layout -fspv-reflect -Fo "${spv}" "${src}"
            DEPENDS "${src}" ${SBX_SHADER_COMMON}
            COMMENT "HLSL ${S_SOURCE} ${key}:${entry}"
            VERBATIM)
        list(APPEND stage_args --stage "${key}:${entry}:${dxil}:${spv}")
        list(APPEND stage_outputs "${dxil}" "${spv}")
    endforeach()

    set(hpp "${gen}/${pascal}Shader.hpp")
    set(cpp "${gen}/${pascal}Shader.cpp")
    set(json "${gen}/${S_NAME}.reflect.json")
    add_custom_command(
        OUTPUT "${hpp}" "${cpp}" "${json}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${PROJECT_SOURCE_DIR}/tools/shader/sbx_shader_gen.py"
                --name "${S_NAME}" --source "${S_SOURCE}" --out-dir "${gen}" ${stage_args}
        DEPENDS ${stage_outputs} "${PROJECT_SOURCE_DIR}/tools/shader/sbx_shader_gen.py"
        COMMENT "셰이더 코드 생성 ${pascal}Shader"
        VERBATIM)
    target_sources(${S_TARGET} PRIVATE "${hpp}" "${cpp}")
endfunction()
