# Graphics SDK 공급 (ADR-0168 · ADR-0169). SandboxRender 의 유일한 그래픽 백엔드인 GraphicsSDK::Core.
#
#   기본    external/graphics-sdk — 고정 커밋의 SDK 코어 (include · src · cmake · CMakeLists.txt · LICENSE, 수정 없음 —
#           external/README.md) 를 Sandbox 와 같은 구성 · 같은 런타임으로 함께 빌드한다. CI · 새 클론에 따로 할 일이 없다
#   선택    SBX_SYSTEM_GRAPHICS_SDK=ON — 설치본 (CMAKE_PREFIX_PATH · GraphicsSDK_DIR 의 GraphicsSDKConfig.cmake) 을 쓴다
#           (SDK 를 고치며 Sandbox 로 시험할 때). 고정 커밋과 다른 SDK 일 수 있다 — 결과를 적을 때 커밋을 함께 적는다
#
# SDK 백엔드 옵션은 Sandbox 옵션을 따른다: D3D12 = Windows, Vulkan = SBX_ENABLE_VULKAN (헤더는 external/vulkan-headers),
# Linux Vulkan 은 XCB 표면 (GFX_VULKAN_XCB — libxcb 개발 파일 · pkg-config 필요), Metal = SBX_ENABLE_METAL.
# 갱신: tools/vendor_graphics_sdk.py --sdk <체크아웃> --commit <40자> (external/README.md 의 표 · ADR-0169 를 같은 커밋에서)
include_guard(GLOBAL)

option(SBX_SYSTEM_GRAPHICS_SDK "설치된 Graphics SDK 를 쓴다 (기본: external/graphics-sdk 를 함께 빌드)" OFF)

if(SBX_SYSTEM_GRAPHICS_SDK)
    find_package(GraphicsSDK CONFIG REQUIRED)
    message(STATUS "Sandbox: Graphics SDK — 설치본 ${GraphicsSDK_DIR}")
    return()
endif()

set(_sbx_gfx_dir "${PROJECT_SOURCE_DIR}/external/graphics-sdk")
if(NOT EXISTS "${_sbx_gfx_dir}/CMakeLists.txt")
    message(FATAL_ERROR "external/graphics-sdk 가 없다 — 저장소를 다시 받거나 SBX_SYSTEM_GRAPHICS_SDK=ON 으로 설치본을 쓴다")
endif()
# SDK 의 시험 · 예제는 빌드하지 않는다. 백엔드는 Sandbox 옵션을 따른다 (FORCE — SDK 의 기본값보다 우선)
set(GFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GFX_ENABLE_D3D12 ${WIN32} CACHE BOOL "" FORCE)
set(GFX_ENABLE_METAL ${SBX_ENABLE_METAL} CACHE BOOL "" FORCE)
set(GFX_ENABLE_VULKAN ${SBX_ENABLE_VULKAN} CACHE BOOL "" FORCE)
set(GFX_VULKAN_HEADERS "${PROJECT_SOURCE_DIR}/external/vulkan-headers" CACHE PATH "" FORCE)
if(SBX_ENABLE_VULKAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GFX_VULKAN_XCB ON CACHE BOOL "" FORCE)
else()
    set(GFX_VULKAN_XCB OFF CACHE BOOL "" FORCE)
endif()
# EXCLUDE_FROM_ALL: SDK 의 설치 규칙 · 타깃은 SandboxRender 가 링크할 때만 빌드된다. SYSTEM: SDK 헤더 경고는 우리 경고
# 정책 (SBX_WARNINGS_AS_ERRORS) 밖 — 서드파티와 같다
add_subdirectory("${_sbx_gfx_dir}" "${CMAKE_BINARY_DIR}/external/graphics-sdk" EXCLUDE_FROM_ALL SYSTEM)
if(NOT TARGET GraphicsSDK::Core)
    message(FATAL_ERROR "external/graphics-sdk 에 GraphicsSDK::Core 타깃이 없다")
endif()
file(READ "${_sbx_gfx_dir}/VENDORED_COMMIT" _sbx_gfx_commit)
string(STRIP "${_sbx_gfx_commit}" _sbx_gfx_commit)
message(STATUS "Sandbox: Graphics SDK — external/graphics-sdk (${_sbx_gfx_commit})")
