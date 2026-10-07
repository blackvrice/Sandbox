# SandboxFoundation — 의존 없음. docs/01-ARCHITECTURE.md 2장.
add_library(SandboxFoundation STATIC
    types/Types.hpp
    types/Error.hpp
    types/Error.cpp
    assert/Assert.hpp
    assert/Assert.cpp
    log/Log.hpp
    log/Log.cpp
    hash/Fnv1a.hpp
    handle/Handle.hpp
    container/SmallVector.hpp
    container/FixedString.hpp
    math/Vec2.hpp
    io/FileIo.hpp
    io/FileIo.cpp
    io/Console.hpp
    io/Console.cpp
    job/JobSystem.hpp
    job/JobSystem.cpp
)
add_library(Sandbox::Foundation ALIAS SandboxFoundation)

# include 루트는 저장소 루트다: #include "foundation/hash/Fnv1a.hpp"
# 모듈 간 include 방향은 tools/check_includes.py 가 검사한다.
target_include_directories(SandboxFoundation PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${SBX_GENERATED_DIR}")
target_compile_features(SandboxFoundation PUBLIC cxx_std_23)
target_link_libraries(SandboxFoundation PRIVATE sbx_warnings sbx_strict_conversions)
# JobSystem (std::thread)
find_package(Threads REQUIRED)
target_link_libraries(SandboxFoundation PUBLIC Threads::Threads)
