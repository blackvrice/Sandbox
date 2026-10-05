# 빌드 정보 헤더 생성: ${CMAKE_BINARY_DIR}/generated/foundation/BuildInfo.hpp
# 커밋 해시는 구성 시점 값이다 (재구성 전까지 갱신되지 않는다). 진단용이지 버전 판정용이 아니다.

set(SBX_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE _sbx_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _sbx_git_result)
    if(_sbx_git_result EQUAL 0 AND _sbx_commit)
        set(SBX_GIT_COMMIT "${_sbx_commit}")
    endif()
endif()

set(SBX_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file("${PROJECT_SOURCE_DIR}/cmake/BuildInfo.hpp.in"
               "${SBX_GENERATED_DIR}/foundation/BuildInfo.hpp" @ONLY)
