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
    unit/foundation/test_small_vector.cpp
    unit/foundation/test_job_system.cpp
    unit/foundation/test_utf8.cpp
    unit/ecs/test_entity_manager.cpp
    unit/ecs/test_component.cpp
    unit/ecs/test_registry.cpp
    unit/ecs/test_view.cpp
    unit/ecs/test_ecb.cpp
    unit/ecs/test_catalog_serialization.cpp
    unit/ecs/test_field_access.cpp
    property/test_registry_property.cpp
    unit/core/test_sim_constants.cpp
    unit/core/test_random.cpp
    unit/core/test_command_queue.cpp
    unit/core/test_spatial_index.cpp
    unit/core/test_simulation_world.cpp
    unit/core/test_scenario.cpp
    unit/core/test_world_grid.cpp
    unit/core/test_prefab_lifecycle.cpp
    unit/core/test_pathfinding.cpp
    unit/core/test_ai_systems.cpp
    unit/core/test_replay.cpp
    unit/persist/test_world_save.cpp
    unit/persist/test_sample_saves.cpp
    unit/content/test_content_loader.cpp
    property/test_spatial_property.cpp
    unit/server/test_server_options.cpp
    unit/tools/test_sim_check_options.cpp
    unit/tools/test_net_probe_options.cpp
    unit/platform/test_input.cpp
    unit/platform/test_action_map.cpp
    unit/render/test_rhi_helpers.cpp
    unit/render/test_image.cpp
    unit/render/test_pipeline_validation.cpp
    unit/render/test_sprite_logic.cpp
    unit/render/test_world_logic.cpp
    unit/network/test_bitstream.cpp
    unit/network/test_messages.cpp
    unit/network/test_transport.cpp
    unit/network/test_server_host.cpp
    unit/network/test_enet.cpp
    unit/network/test_replication.cpp
    unit/network/test_client_view.cpp
    unit/network/test_interest.cpp
    # 앱·도구 코드 중 순수 로직은 테스트에 직접 컴파일해 넣는다 (exe 를 링크할 수 없으므로)
    "${PROJECT_SOURCE_DIR}/apps/server/ServerOptions.cpp"
    "${PROJECT_SOURCE_DIR}/tools/sim_check/SimCheckOptions.cpp"
    "${PROJECT_SOURCE_DIR}/tools/net_probe/NetProbeOptions.cpp"
)
if(SBX_BUILD_CLIENT)
    target_sources(SandboxTests PRIVATE
        unit/client/test_application.cpp
        unit/client/test_network_session.cpp
        unit/client/test_imgui_layer.cpp
        "${PROJECT_SOURCE_DIR}/apps/client/Application.cpp"
        "${PROJECT_SOURCE_DIR}/apps/client/NetworkSession.cpp"
        "${PROJECT_SOURCE_DIR}/apps/client/presentation/SpriteExtraction.cpp"
        "${PROJECT_SOURCE_DIR}/apps/client/presentation/SelectionOverlay.cpp"
        "${PROJECT_SOURCE_DIR}/apps/client/ui/ImGuiLayer.cpp"
        "${PROJECT_SOURCE_DIR}/apps/client/ui/DebugPanels.cpp"
        "${PROJECT_SOURCE_DIR}/apps/client/ClientOptions.cpp"
        "${PROJECT_SOURCE_DIR}/apps/client/DefaultInput.cpp")
    # ClientRenderer.cpp 는 넣지 않는다 — 클라이언트 테스트는 가짜 IFrameRenderer 를 쓴다
endif()
target_link_libraries(SandboxTests PRIVATE SandboxFoundation SandboxCore SandboxNetwork SandboxPlatform SandboxRender sbx_imgui sbx_doctest sbx_warnings)
# 커밋된 테스트 데이터 (샘플 세이브 등). 빌드 폴더로 복사하지 않고 소스 트리에서 읽는다.
target_compile_definitions(SandboxTests PRIVATE SBX_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
                                               SBX_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")

# 스위트별로 등록해 라벨로 고를 수 있게 한다
set(_sbx_suites foundation core ecs persist content network server tools platform render)
if(SBX_BUILD_CLIENT)
    list(APPEND _sbx_suites client)
endif()
foreach(suite IN ITEMS ${_sbx_suites})
    add_test(NAME unit_${suite} COMMAND SandboxTests --test-suite=${suite} --no-intro --no-version)
    # 케이스가 0 개면 실패 (예: 스위트를 등록했는데 실행 파일이 옛것 — doctest 는 0 개도 성공으로 친다)
    set_tests_properties(unit_${suite} PROPERTIES LABELS "unit" FAIL_REGULAR_EXPRESSION "test cases: +0 \\|")
endforeach()

# 복제 수렴 (08 12장 L3′): 서버 + 클라이언트, Loopback 과 Simulated(100 ms ± 20, 5 % 손실, 예산) — 수 초
add_test(NAME net_convergence COMMAND SandboxTests --test-suite=net --no-intro --no-version)
set_tests_properties(net_convergence PROPERTIES LABELS "net" FAIL_REGULAR_EXPRESSION "test cases: +0 \\|")

# 속성 테스트 (참조 모델 대조). 단위 테스트보다 느리므로 라벨을 나눈다.
add_test(NAME property_ecs COMMAND SandboxTests --test-suite=property --no-intro --no-version)
set_tests_properties(property_ecs PROPERTIES LABELS "property")

# ---------------------------------------------------------------------------
# SandboxServer 실행 파일 스모크
# ---------------------------------------------------------------------------
if(TARGET SandboxServer)
    add_test(NAME server_version COMMAND SandboxServer --version)
    set_tests_properties(server_version PROPERTIES LABELS "unit;server"
        PASS_REGULAR_EXPRESSION "SandboxServer ${PROJECT_VERSION} .*tick 30 Hz")

    add_test(NAME server_rejects_unknown_option COMMAND SandboxServer --no-such-option)
    set_tests_properties(server_rejects_unknown_option PROPERTIES LABELS "unit;server" WILL_FAIL TRUE)

    add_test(NAME server_scenario_smoke COMMAND SandboxServer --scenario random_walk_1k --ticks 60)
    set_tests_properties(server_scenario_smoke PROPERTIES LABELS "unit;server"
        PASS_REGULAR_EXPRESSION "tick 60 hash 0x[0-9a-f]+ entities [0-9]+")

    # Phase 9: 네트워크 서버가 월드를 30 TPS 로 돌리고 --ticks N --exit 에서 끝난다 (포트 0 = 빈 포트).
    add_test(NAME server_world_exit
        COMMAND SandboxServer --world random_walk_1k --port 0 --ticks 60 --exit --log-level warn)
    set_tests_properties(server_world_exit PROPERTIES LABELS "unit;server"
        PASS_REGULAR_EXPRESSION "tick 60 hash 0x[0-9a-f]+ entities [0-9]+")
    add_test(NAME server_world_bad_name COMMAND SandboxServer --world no_such_world --port 0)
    set_tests_properties(server_world_bad_name PROPERTIES LABELS "unit;server" WILL_FAIL TRUE)
    if(TARGET sbx_net_probe)
        # 서버 + probe 두 프로세스, 실제 UDP (핸드셰이크 · 명령 3개 · 서버 통계)
        add_test(NAME net_server_probe_smoke
            COMMAND "${CMAKE_COMMAND}" -DSERVER=$<TARGET_FILE:SandboxServer> -DPROBE=$<TARGET_FILE:sbx_net_probe>
                    -DPORT=47791 -P "${CMAKE_CURRENT_SOURCE_DIR}/net/net_smoke.cmake")
        set_tests_properties(net_server_probe_smoke PROPERTIES LABELS "net" TIMEOUT 90)
    endif()
endif()

# ---------------------------------------------------------------------------
# SandboxClient 실행 파일 스모크 (Phase 6). --headless: OS 창 없이 앱 루프 — 모든 OS·CI 에서 돈다.
# 실제 창(Win32)은 수동 QA (docs/qa/MANUAL-QA.md Phase 6).
# ---------------------------------------------------------------------------
if(TARGET SandboxClient)
    add_test(NAME client_version COMMAND SandboxClient --version)
    set_tests_properties(client_version PROPERTIES LABELS "unit;client"
        PASS_REGULAR_EXPRESSION "SandboxClient ${PROJECT_VERSION} .*window (Win32|none), render (D3D12|none)")
    add_test(NAME client_rejects_unknown_option COMMAND SandboxClient --no-such-option)
    set_tests_properties(client_rejects_unknown_option PROPERTIES LABELS "unit;client" WILL_FAIL TRUE)
    add_test(NAME client_headless_smoke COMMAND SandboxClient --headless --frames 30 --fps 120)
    set_tests_properties(client_headless_smoke PROPERTIES LABELS "unit;client"
        PASS_REGULAR_EXPRESSION "SandboxClient 끝: frames 30 state Shutdown")
    # Phase 10B: --world 가 같은 프로세스의 서버(헤드리스는 프레임 안에서 — 프레임마다 1/60 초)에 접속해 첫 스냅숏을 받고
    # InWorld 로 들어가 그린다. 같은 dt 열이라 틱 수가 고정된다
    add_test(NAME client_local_world_headless
        COMMAND SandboxClient --headless --world ecosystem_small --frames 60)
    set_tests_properties(client_local_world_headless PROPERTIES LABELS "unit;client"
        PASS_REGULAR_EXPRESSION "world ecosystem_small tick 28 개체 [1-9][0-9]* 스냅숏 [1-9][0-9]* \\(다시 맞춤 0\\)")
    add_test(NAME client_world_bad_name COMMAND SandboxClient --headless --world no_such_world --frames 5)
    set_tests_properties(client_world_bad_name PROPERTIES LABELS "unit;client" WILL_FAIL TRUE)
endif()

# ---------------------------------------------------------------------------
# 결정론 하네스 (docs/04-DETERMINISM.md 2장)
# ---------------------------------------------------------------------------
if(TARGET sbx_sim_check)
    # D1: 같은 입력 → 같은 해시 (두 월드를 나란히 돌려 매 체크포인트 비교)
    add_test(NAME det_random_walk_repeat
        COMMAND sbx_sim_check --scenario random_walk_1k --ticks 600 --repeat 2 --hash-every 30)
    # D4: 툴체인별 골든 (ADR-0012). 이 툴체인 항목이 없으면 종료 코드 3 → SKIP
    add_test(NAME det_random_walk_golden
        COMMAND sbx_sim_check --golden "${PROJECT_SOURCE_DIR}/tests/golden/random_walk_1k.json")
    # D2: 저장→로드한 월드가 저장하지 않은 월드와 끝까지 같은 해시 (Phase 4)
    add_test(NAME det_world_save_load
        COMMAND sbx_sim_check --scenario world_save_load --ticks 600 --save-at 250 --repeat 2
                --save-dir "${CMAKE_CURRENT_BINARY_DIR}/det_world_save_load")
    add_test(NAME det_random_walk_save_paused
        COMMAND sbx_sim_check --scenario random_walk_1k --ticks 420 --save-at 300
                --save-dir "${CMAKE_CURRENT_BINARY_DIR}/det_random_walk_save_paused")
    add_test(NAME det_world_save_load_golden
        COMMAND sbx_sim_check --golden "${PROJECT_SOURCE_DIR}/tests/golden/world_save_load.json")
    set_tests_properties(det_world_save_load_golden PROPERTIES SKIP_RETURN_CODE 3)
    set_tests_properties(det_world_save_load det_random_walk_save_paused det_world_save_load_golden
        PROPERTIES LABELS "det")

    # Phase 5A·5B: 생태계(감지·행동·상호작용·경로·충돌·생명 주기) — D1 + D2 (태그 표·Opaque render.sprite 를 지나는
    # 세이브 왕복) + D5 (경로 Job 의 Worker 0·1·8) + 골든
    add_test(NAME det_eco_lifecycle
        COMMAND sbx_sim_check --scenario eco_lifecycle --repeat 2 --save-at 900 --threads 0,1,8
                --save-dir "${CMAKE_CURRENT_BINARY_DIR}/det_eco_lifecycle")
    # Phase 5C: D3 — 시작 세이브 + 적용된 명령으로 기록한 리플레이를 재생해 같은 해시. 일시정지 편집(random_walk_1k)·
    # 지형 칠하기(world_save_load)·생태계(eco_lifecycle) 각각
    foreach(_rs IN ITEMS random_walk_1k world_save_load eco_lifecycle ecosystem_small)
        add_test(NAME det_replay_${_rs}
            COMMAND sbx_sim_check --scenario ${_rs} --ticks 600 --replay-roundtrip
                    --replay-dir "${CMAKE_CURRENT_BINARY_DIR}/det_replay_${_rs}")
        set_tests_properties(det_replay_${_rs} PROPERTIES LABELS "det"
            PASS_REGULAR_EXPRESSION "리플레이 일치 \\(D3\\)")
    endforeach()
    # Phase 5C: ecosystem_small (보충 없는 생태계) — D1 + D2 + D5 + 골든
    add_test(NAME det_ecosystem_small
        COMMAND sbx_sim_check --scenario ecosystem_small --repeat 2 --save-at 450 --threads 0,1,8
                --save-dir "${CMAKE_CURRENT_BINARY_DIR}/det_ecosystem_small")
    add_test(NAME det_ecosystem_small_golden
        COMMAND sbx_sim_check --golden "${PROJECT_SOURCE_DIR}/tests/golden/ecosystem_small.json")
    set_tests_properties(det_ecosystem_small_golden PROPERTIES SKIP_RETURN_CODE 3)
    set_tests_properties(det_ecosystem_small det_ecosystem_small_golden PROPERTIES LABELS "det")
    # 밸런스 (16 Phase 5 완료 조건): ecosystem_survival 18,000틱, 시드 1·2·3 에서 세 종 공존.
    # 최적화 빌드에서만 등록한다 (Debug 에서는 수 분 이상 걸린다). 라벨 balance — 빠른 실행은 ctest -LE balance
    foreach(_seed IN ITEMS 1 2 3)
        add_test(NAME balance_ecosystem_survival_${_seed} CONFIGURATIONS Release RelWithDebInfo
            COMMAND sbx_sim_check --scenario ecosystem_survival --seed ${_seed} --hash-every 1000 --threads 1
                    --require-prefabs eco.grass,eco.rabbit,eco.wolf)
        set_tests_properties(balance_ecosystem_survival_${_seed} PROPERTIES LABELS "balance" TIMEOUT 900)
    endforeach()
    add_test(NAME det_eco_lifecycle_golden
        COMMAND sbx_sim_check --golden "${PROJECT_SOURCE_DIR}/tests/golden/eco_lifecycle.json")
    set_tests_properties(det_eco_lifecycle_golden PROPERTIES SKIP_RETURN_CODE 3)
    set_tests_properties(det_eco_lifecycle det_eco_lifecycle_golden PROPERTIES LABELS "det")

    # 콘텐츠 검증기 (11 8장) — 저장소의 팩은 오류·경고 0 이어야 한다
    add_test(NAME content_eco_validate COMMAND sbx_sim_check --validate-content eco)
    set_tests_properties(content_eco_validate PROPERTIES LABELS "content"
        PASS_REGULAR_EXPRESSION "콘텐츠 검증: 오류 0, 경고 0")

    # 하네스 자체 시험: 일부러 만든 차이를 잡아 엔티티·필드까지 좁혀야 한다
    add_test(NAME det_harness_selftest
        COMMAND sbx_sim_check --ticks 90 --repeat 2 --inject-divergence 40)
    set_tests_properties(det_harness_selftest PROPERTIES
        PASS_REGULAR_EXPRESSION "불일치 \\(D1\\): tick 60.*core\\.transform\\.position")
    set_tests_properties(det_random_walk_repeat det_random_walk_golden det_harness_selftest
        PROPERTIES LABELS "det")
    set_tests_properties(det_random_walk_golden PROPERTIES SKIP_RETURN_CODE 3)
endif()

# ---------------------------------------------------------------------------
# 아키텍처 경계 (docs/01-ARCHITECTURE.md 2.2)
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    # 실제 소스 트리: 위반 0건이어야 한다.
    # -X utf8: Windows 의 cp949 콘솔에서 메시지 출력이 죽던 문제 (스크립트도 출력 인코딩을 UTF-8 로 고정한다)
    add_test(
            NAME arch_include_lint
            COMMAND ${Python3_EXECUTABLE}
            -X utf8
            ${CMAKE_SOURCE_DIR}/tools/check_includes.py
            --root ${CMAKE_SOURCE_DIR}
    )
    # 린터 자체 검사: 일부러 틀린 픽스처에서 정해진 수의 위반을 찾아야 한다
    add_test(
            NAME arch_include_lint_selftest
            COMMAND ${Python3_EXECUTABLE}
            -X utf8
            ${CMAKE_SOURCE_DIR}/tools/check_includes.py
            --root ${CMAKE_SOURCE_DIR}/tests/arch/fixtures
            --expect 8
    )
    set_tests_properties(arch_include_lint arch_include_lint_selftest PROPERTIES LABELS "arch")
    # 셰이더 코드 생성기 자체 검사 (손으로 만든 SPIR-V 로 리플렉션·병합 규칙) — dxc 없이 모든 OS 에서 (ADR-0019)
    add_test(NAME shader_gen_selftest
        COMMAND ${Python3_EXECUTABLE} -X utf8 ${CMAKE_SOURCE_DIR}/tools/shader/sbx_shader_gen.py --self-test)
    set_tests_properties(shader_gen_selftest PROPERTIES LABELS "unit" PASS_REGULAR_EXPRESSION "self-test OK")
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
    # IDE(CLion 등)는 Ninja 를 PATH 가 아니라 CMAKE_MAKE_PROGRAM 으로 넘긴다. 중첩 구성에도 그대로 전달해야
    # "unable to find a build program corresponding to Ninja" 로 실패하지 않는다 (Windows 에서 실제로 발생).
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND _selftest_args -D "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _selftest_args -D "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    add_test(NAME arch_link_boundary_selftest COMMAND "${CMAKE_COMMAND}" ${_selftest_args})
    # 종료 코드가 아니라 메시지로 판정한다 — 다른 이유(컴파일러 탐지 실패 등)로 실패해도 통과하면 안 되므로.
    # 중첩 구성이 컴파일러 자체를 못 쓰면(환경 문제) 실패가 아니라 SKIP 으로 보고한다.
    set_tests_properties(arch_link_boundary_selftest PROPERTIES LABELS "arch"
        PASS_REGULAR_EXPRESSION "SBX boundary violation: SandboxServer must not link SandboxPlatform"
        SKIP_REGULAR_EXPRESSION "is not able to compile a simple test program")
    # MSVC: IDE(CLion 등)는 vcvars 환경(LIB·INCLUDE·LIBPATH)을 구성·빌드에만 주고 CTest 에는 주지 않는다 →
    # 중첩 구성의 링크가 "kernel32.lib 를 열 수 없다"(LNK1104)로 실패한다 (Windows 에서 실제로 발생).
    # 구성 시점의 값을 테스트 환경으로 넘긴다. 값 안의 '\\'·';' 는 이스케이프한다.
    if(MSVC)
        set(_selftest_env)
        foreach(_var IN ITEMS LIB INCLUDE LIBPATH)
            if(DEFINED ENV{${_var}})
                string(REPLACE "\\" "\\\\" _value "$ENV{${_var}}")   # '\' 는 CTest 가 이스케이프로 읽는다
                string(REPLACE ";" "\;" _value "${_value}")
                list(APPEND _selftest_env "${_var}=set:${_value}")
            endif()
        endforeach()
        if(_selftest_env)
            set_tests_properties(arch_link_boundary_selftest PROPERTIES ENVIRONMENT_MODIFICATION "${_selftest_env}")
        endif()
    endif()
endif()
