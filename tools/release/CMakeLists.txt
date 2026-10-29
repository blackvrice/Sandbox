# 배포 묶음 타깃 (v0.5 M1-T4): cmake --build --preset <구성> --target sbx_release → <빌드>/release/<구성>/ 에
# sandbox-<버전>-<플랫폼>.zip · -symbols.zip · SHA256SUMS.txt (make_release.cmake)
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _sbx_os)
if(_sbx_os STREQUAL "darwin")
    set(_sbx_os "macos")
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _sbx_cpu)
if(_sbx_cpu MATCHES "^(amd64|x86_64|x64)$")
    set(_sbx_cpu "x64")
elseif(_sbx_cpu MATCHES "^(arm64|aarch64)$")
    set(_sbx_cpu "arm64")
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(_sbx_cpu "${CMAKE_OSX_ARCHITECTURES}")
endif()

set(_sbx_release_client "")
set(_sbx_release_deps SandboxServer sbx)
if(TARGET SandboxClient)
    set(_sbx_release_client "-DCLIENT=$<TARGET_FILE:SandboxClient>")
    list(APPEND _sbx_release_deps SandboxClient)
endif()

add_custom_target(sbx_release
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DOUT=${CMAKE_BINARY_DIR}/release/$<CONFIG>"
            "-DVERSION=${PROJECT_VERSION}" "-DPLATFORM=${_sbx_os}-${_sbx_cpu}" "-DCONFIG=$<CONFIG>"
            "-DCOMMIT=${SBX_GIT_COMMIT}" "-DTOOLCHAIN=${SBX_TOOLCHAIN_KEY}"
            "-DSERVER=$<TARGET_FILE:SandboxServer>" "-DSBX=$<TARGET_FILE:sbx>" ${_sbx_release_client}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/make_release.cmake"
    DEPENDS ${_sbx_release_deps}
    COMMENT "배포 묶음 (zip · 기호 · SHA-256)"
    VERBATIM)
