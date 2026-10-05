# 빌드 옵션. 목록과 의미는 docs/15-BUILD.md 3장.
# 옵션은 해당 기능이 실제로 생기는 Phase 에 추가한다 (쓰이지 않는 옵션을 미리 만들지 않는다).

option(SBX_BUILD_SERVER         "SandboxServer 빌드"                                   ON)
option(SBX_BUILD_TESTS          "SandboxTests 와 CTest 등록"                           ON)
option(SBX_WARNINGS_AS_ERRORS   "경고를 에러로 (CI 에서 ON)"                            OFF)

# 경계 검사 자체를 검사하기 위한 내부 옵션. tests/ 의 arch_link_boundary_selftest 만 켠다.
# 켜면 SandboxServer 에 금지된 링크를 일부러 추가하므로 구성이 반드시 실패해야 한다.
option(SBX_BOUNDARY_SELFTEST    "내부: 경계 검사 실패 경로 시험"                       OFF)
mark_as_advanced(SBX_BOUNDARY_SELFTEST)
