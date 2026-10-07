# 컴파일러 공통 설정. docs/12-CODING-STANDARDS.md 11장, docs/04-DETERMINISM.md 4.6.

# ---------------------------------------------------------------------------
# sbx_warnings — 엔진 코드 전체에 적용하는 경고 수준
# ---------------------------------------------------------------------------
add_library(sbx_warnings INTERFACE)
if(MSVC)
    # /utf-8 : 소스에 한국어 주석이 있다. 없으면 MSVC 가 코드 페이지로 읽어 C4819 와 오컴파일이 난다.
    # /Zc:__cplusplus : __cplusplus 가 실제 표준 값을 보고하도록.
    # /Zc:preprocessor : 표준 전처리기 (__VA_OPT__ 등).
    target_compile_options(sbx_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    if(SBX_WARNINGS_AS_ERRORS)
        target_compile_options(sbx_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(sbx_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
    if(SBX_WARNINGS_AS_ERRORS)
        target_compile_options(sbx_warnings INTERFACE -Werror)
    endif()
endif()

# 더 엄격한 수치 변환 경고. Foundation·Core 에만 건다 (docs/12-CODING-STANDARDS.md 11장).
add_library(sbx_strict_conversions INTERFACE)
if(MSVC)
    # C4242/C4244/C4267: 축소 변환. /W4 에 대부분 포함되지만 명시한다.
    target_compile_options(sbx_strict_conversions INTERFACE /w44242 /w44244 /w44267)
else()
    target_compile_options(sbx_strict_conversions INTERFACE -Wconversion -Wsign-conversion)
endif()

# ---------------------------------------------------------------------------
# sbx_simulation_flags — 같은 바이너리 안의 결정론을 위한 부동소수 규칙
# ---------------------------------------------------------------------------
# SandboxCore 가 PUBLIC 으로 링크한다. ECS·시스템 코드 상당수가 헤더 템플릿이라
# Core 를 쓰는 모든 번역 단위(Network, Server, Tests)에도 같은 규칙이 걸려야 하기 때문이다.
# Render 는 Core 를 링크하지 않으므로 이 규칙에서 자유롭다 (fast-math 허용).
add_library(sbx_simulation_flags INTERFACE)
if(MSVC)
    target_compile_options(sbx_simulation_flags INTERFACE /fp:precise)
else()
    # Clang 의 기본값은 -ffp-contract=on (문장 안 FMA 결합) 이라 명시적으로 끈다.
    target_compile_options(sbx_simulation_flags INTERFACE -ffp-contract=off -fno-fast-math)
endif()

# ---------------------------------------------------------------------------
# 필수 표준 라이브러리 기능 확인
# ---------------------------------------------------------------------------
# std::expected 는 Foundation 의 오류 처리 기반이다 (foundation/types/Error.hpp).
# Clang 18 + libstdc++ 13 조합은 <expected> 를 비활성화한다 (__cpp_concepts 값 차이).
# 이런 툴체인에서 수백 줄의 템플릿 오류 대신 한 줄의 원인을 보이도록 구성 단계에서 확인한다.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_FLAGS}")
if(MSVC)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /std:c++latest")
else()
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++23")
endif()
check_cxx_source_compiles([=[
    #include <expected>
    #include <format>
    #include <source_location>
    int main() { std::expected<int, int> e = 1; return *e - 1 + static_cast<int>(std::format("{}", 0).size()) - 1; }
]=] SBX_HAS_REQUIRED_STDLIB)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT SBX_HAS_REQUIRED_STDLIB)
    message(FATAL_ERROR
        "이 컴파일러/표준 라이브러리 조합은 std::expected·std::format 을 지원하지 않습니다 "
        "(${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}). "
        "Linux 에서는 Clang 19 이상(-D CMAKE_CXX_COMPILER=clang++-19) 또는 GCC 13 이상을 쓰십시오. docs/15-BUILD.md 1장.")
endif()

# ---------------------------------------------------------------------------
# 지원하지 않는 툴체인 알림 (ADR-0009)
# ---------------------------------------------------------------------------
# MinGW 는 지원 대상이 아니다. 지금(Phase 2)까지의 코드는 빌드되지만, Phase 7 의 D3D12 디버그 도구·PIX·
# D3D12MA 는 MSVC 기준이다. 막지는 않고 경고만 한다 — CLion 기본 프로필로 구성하면 이 경고가 보인다.
if(MINGW)
    message(WARNING
        "MinGW 툴체인으로 구성했습니다. 이 프로젝트의 Windows 지원 툴체인은 MSVC(또는 clang-cl)입니다 (ADR-0009). "
        "CLion: Settings → Toolchains 에서 Visual Studio 툴체인을 추가하고 CMakePresets 의 windows-msvc 프로필을 쓰십시오. "
        "docs/15-BUILD.md 1.1")
endif()
