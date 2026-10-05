# SBX_BOUNDARY_SELFTEST=ON 일 때만 포함된다.
# 금지된 링크를 일부러 만들어 sbx_check_link_boundaries() 가 실제로 실패하는지 확인한다.
# tests/CMakeLists.txt 의 arch_link_boundary_selftest 가 이 경로를 실행한다.
if(NOT TARGET SandboxServer)
    message(FATAL_ERROR "boundary selftest 는 SBX_BUILD_SERVER=ON 이 필요합니다.")
endif()
if(NOT TARGET SandboxPlatform)
    add_library(SandboxPlatform INTERFACE)   # 진짜 Platform 이 생기기 전의 대역
endif()
add_library(sbx_selftest_middle INTERFACE)
target_link_libraries(sbx_selftest_middle INTERFACE SandboxPlatform)
# 직접이 아니라 중간 타깃을 거쳐 링크한다 — 전이 검사까지 확인하기 위해서.
target_link_libraries(SandboxServer PRIVATE sbx_selftest_middle)
