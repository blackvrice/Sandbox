# 빌드 옵션. 목록과 의미는 docs/15-BUILD.md 3장.
# 옵션은 해당 기능이 실제로 생기는 Phase 에 추가한다 (쓰이지 않는 옵션을 미리 만들지 않는다).

option(SBX_BUILD_SERVER         "SandboxServer 빌드"                                   ON)
option(SBX_BUILD_CLIENT         "SandboxClient 빌드 (Phase 6: 빈 창 + 앱 상태기계)"         ON)
option(SBX_BUILD_TESTS          "SandboxTests 와 CTest 등록"                           ON)
option(SBX_BUILD_TOOLS          "개발 도구(sbx_sim_check) 빌드"                          ON)
option(SBX_BUILD_BENCH          "sbx_bench 벤치마크 빌드 (docs/14-PERFORMANCE.md)"         OFF)
option(SBX_WARNINGS_AS_ERRORS   "경고를 에러로 (CI 에서 ON)"                            OFF)

# 렌더 백엔드 (docs/06-RENDERING.md 5장, ADR-0032). D3D12 는 Windows 에서 언제나. Vulkan 은 Linux 기본, Windows 는 켜서
# D3D12 와 함께 빌드한다 (--rhi vulkan 으로 고른다 — 같은 기준 이미지로 두 백엔드를 비교). macOS 는 Metal (Phase 14).
if(WIN32 OR APPLE)
    set(_sbx_vulkan_default OFF)
else()
    set(_sbx_vulkan_default ON)
endif()
option(SBX_ENABLE_VULKAN        "Vulkan 렌더 백엔드 (render/vulkan — volk 로 로더를 실행 중에 연다)" ${_sbx_vulkan_default})

# 경계 검사 자체를 검사하기 위한 내부 옵션. tests/ 의 arch_link_boundary_selftest 만 켠다.
# 켜면 SandboxServer 에 금지된 링크를 일부러 추가하므로 구성이 반드시 실패해야 한다.
option(SBX_BOUNDARY_SELFTEST    "내부: 경계 검사 실패 경로 시험"                       OFF)
mark_as_advanced(SBX_BOUNDARY_SELFTEST)
