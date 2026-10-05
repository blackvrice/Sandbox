# 테스트. 레벨·라벨은 docs/13-TESTING.md 2장.
#   ctest --preset <p> -L unit     빠른 단위 테스트
#   ctest --preset <p> -L arch     아키텍처 경계

# ---------------------------------------------------------------------------
# SandboxTests (doctest)
# ---------------------------------------------------------------------------
add_executable(SandboxTests
    main.cpp
    unit/foundation/test_hash.cpp
    unit/foundation/test_handle.cpp
    unit/foundation/test_error.cpp
    unit/foundation/test_assert.cpp
    unit/foundation/test_log.cpp
    unit/core/test_sim_constants.cpp
    unit/server/test_server_options.cpp
    # 앱 코드 중 순수 로직은 테스트에 직접 컴파일해 넣는다 (exe 를 링크할 수 없으므로)
    "${PROJECT_SOURCE_DIR}/apps/server/ServerOptions.cpp"
)
target_include_directories(SandboxTests SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/external")
target_link_libraries(SandboxTests PRIVATE SandboxFoundation SandboxCore sbx_warnings)

# 스위트별로 등록해 라벨로 고를 수 있게 한다
foreach(suite IN ITEMS foundation core server)
    add_test(NAME unit_${suite} COMMAND SandboxTests --test-suite=${suite} --no-intro --no-version)
    set_tests_properties(unit_${suite} PROPERTIES LABELS "unit")
endforeach()

# ---------------------------------------------------------------------------
# SandboxServer 실행 파일 스모크
# ---------------------------------------------------------------------------
if(TARGET SandboxServer)
    add_test(NAME server_version COMMAND SandboxServer --version)
    set_tests_properties(server_version PROPERTIES LABELS "unit;server"
        PASS_REGULAR_EXPRESSION "SandboxServer ${PROJECT_VERSION} .*tick 30 Hz")

    add_test(NAME server_rejects_unknown_option COMMAND SandboxServer --no-such-option)
    set_tests_properties(server_rejects_unknown_option PROPERTIES LABELS "unit;server" WILL_FAIL TRUE)
endif()

# ---------------------------------------------------------------------------
# 아키텍처 경계 (docs/01-ARCHITECTURE.md 2.2)
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    # 실제 소스 트리: 위반 0건이어야 한다
    add_test(NAME arch_include_lint
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_includes.py"
                --root "${PROJECT_SOURCE_DIR}")
    # 린터 자체 검사: 일부러 틀린 픽스처에서 정해진 수의 위반을 찾아야 한다
    add_test(NAME arch_include_lint_selftest
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_includes.py"
                --root "${CMAKE_CURRENT_SOURCE_DIR}/arch/fixtures" --expect 8)
    set_tests_properties(arch_include_lint arch_include_lint_selftest PROPERTIES LABELS "arch")
else()
    message(WARNING "Python3 를 찾지 못해 include 경계 테스트(arch_include_lint)를 등록하지 않습니다. CI 에서는 검사됩니다.")
endif()

# 링크 경계 검사 자체 검사: 금지 링크를 일부러 넣은 구성이 실패해야 한다.
# 별도 빌드 폴더에서 구성만 한다 (컴파일하지 않는다).
if(SBX_BUILD_SERVER)
    set(_selftest_args
        -S "${PROJECT_SOURCE_DIR}"
        -B "${CMAKE_BINARY_DIR}/boundary-selftest"
        -G "${CMAKE_GENERATOR}"
        -D CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -D SBX_BUILD_TESTS=OFF
        -D SBX_BOUNDARY_SELFTEST=ON)
    add_test(NAME arch_link_boundary_selftest COMMAND "${CMAKE_COMMAND}" ${_selftest_args})
    # 종료 코드가 아니라 메시지로 판정한다 — 다른 이유(컴파일러 탐지 실패 등)로 실패해도 통과하면 안 되므로.
    set_tests_properties(arch_link_boundary_selftest PROPERTIES LABELS "arch"
        PASS_REGULAR_EXPRESSION "SBX boundary violation: SandboxServer must not link SandboxPlatform")
endif()
