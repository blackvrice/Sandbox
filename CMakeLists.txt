# Sandbox Simulation Maker — 루트 빌드 정의
#
# 타깃 경계와 의존 방향은 docs/01-ARCHITECTURE.md 2장이 정한다. 이 파일 끝의
# sbx_check_link_boundaries() 가 그 규칙을 구성 단계에서 강제한다.
cmake_minimum_required(VERSION 3.28)

project(Sandbox
        VERSION 0.1.0
        DESCRIPTION "Sandbox Simulation Maker"
        LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "소스 폴더 안에서 빌드하지 마십시오. cmake --preset <name> 을 사용하십시오.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# C++20 모듈을 쓰지 않는다 (docs/12-CODING-STANDARDS.md 1장). 끄지 않으면 CMake 3.28+ 가
# clang-scan-deps 를 요구하고, 그것이 없는 환경에서 모든 컴파일이 실패한다.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# 실행 파일은 build/<preset>/bin/<Config>/ 에 모은다 (Ninja Multi-Config 가 구성 이름을 붙인다).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(SbxOptions)
include(SbxCompilerSettings)
include(SbxBoundaries)
include(SbxBuildInfo)

if(SBX_BUILD_TESTS)
    include(CTest)
endif()

# --- 모듈 (의존 순서) -------------------------------------------------------
add_subdirectory(foundation)
add_subdirectory(core)

if(SBX_BUILD_SERVER)
    add_subdirectory(apps/server)
endif()

if(SBX_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# --- 경계 규칙 --------------------------------------------------------------
# 아직 존재하지 않는 타깃에 대한 규칙도 미리 선언해 둔다. 타깃이 생기는 순간부터 검사된다.
sbx_forbid_link(SandboxFoundation SandboxCore SandboxNetwork SandboxPlatform SandboxRender SandboxEditor)
sbx_forbid_link(SandboxCore       SandboxNetwork SandboxPlatform SandboxRender SandboxEditor)
sbx_forbid_link(SandboxNetwork    SandboxPlatform SandboxRender SandboxEditor)
sbx_forbid_link(SandboxPlatform   SandboxCore SandboxNetwork SandboxRender SandboxEditor)
sbx_forbid_link(SandboxRender     SandboxCore SandboxNetwork SandboxEditor)
sbx_forbid_link(SandboxEditor     SandboxNetwork)
sbx_forbid_link(SandboxServer     SandboxPlatform SandboxRender SandboxEditor imgui)

if(SBX_BOUNDARY_SELFTEST)
    include(SbxBoundarySelftest)
endif()

sbx_check_link_boundaries()
