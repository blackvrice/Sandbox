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

# 툴체인 키 — 골든 해시를 툴체인별로 기록하는 키 (ADR-0012). 결정론 범위가 "같은 바이너리" 이므로(ADR-0004)
# 컴파일러·아키텍처가 다르면 골든도 따로 둔다. MSVC 는 주 버전(19)이 오래 고정이라 부 버전까지 넣는다.
string(REGEX MATCH "^[0-9]+" _sbx_cxx_major "${CMAKE_CXX_COMPILER_VERSION}")
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    string(REGEX MATCH "^[0-9]+\\.[0-9]+" _sbx_cxx_major "${CMAKE_CXX_COMPILER_VERSION}")
endif()
set(_sbx_processor "${CMAKE_SYSTEM_PROCESSOR}")
if(NOT _sbx_processor)
    set(_sbx_processor "${CMAKE_HOST_SYSTEM_PROCESSOR}")  # 교차 컴파일 툴체인 파일이 지정하지 않은 경우
endif()
string(TOLOWER "${_sbx_processor}" _sbx_processor)
if(_sbx_processor STREQUAL "amd64")
    set(_sbx_processor "x86_64")  # Windows 는 AMD64, 나머지는 x86_64 로 보고한다 — 같은 것으로 취급
endif()
set(SBX_TOOLCHAIN_KEY "${CMAKE_SYSTEM_NAME}-${_sbx_processor}-${CMAKE_CXX_COMPILER_ID}-${_sbx_cxx_major}")

set(SBX_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file("${PROJECT_SOURCE_DIR}/cmake/BuildInfo.hpp.in"
               "${SBX_GENERATED_DIR}/foundation/BuildInfo.hpp" @ONLY)
