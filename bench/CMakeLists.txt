# sbx_bench — docs/14-PERFORMANCE.md 2장. SBX_BUILD_BENCH=ON 일 때만 빌드한다.
add_executable(sbx_bench
    main.cpp
    BenchUtil.hpp
    EcsBench.cpp
    SimBench.cpp
    RenderBench.cpp
    NetBench.cpp
)
# render.* 는 백엔드와 무관한 순수 로직만 (SpriteBatcher) — GPU 없이 모든 OS 에서. net.* 는 ReplicationWriter (Phase 11)
target_link_libraries(sbx_bench PRIVATE SandboxCore SandboxNetwork SandboxRender sbx_warnings)
# sim.ecosystem 이 eco 팩을 읽는다 (저장소의 content/)
target_compile_definitions(sbx_bench PRIVATE SBX_DEFAULT_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")

if(SBX_BUILD_TESTS)
    # 동작 확인용 짧은 실행 (수치 판정은 하지 않는다 — 기준선 비교는 같은 머신에서 수동/야간으로)
    add_test(NAME bench_quick COMMAND sbx_bench --quick)
    set_tests_properties(bench_quick PROPERTIES LABELS "bench")
endif()
