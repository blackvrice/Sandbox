# 패키지 설치 (v0.4 M3-T3, ADR-0071 7 · ADR-0073): sbx new → sbx add <팩> --path → check (설치 폴더에서) → 서버 --ticks →
# 원본 · 설치 폴더를 지우고 sbx install (잠금 그대로 — 캐시에서, 같은 잠금 바이트) → 설치 파일을 고치면 check · 서버 거절 →
# sbx update 로 고쳐짐 → sbx remove → 설치 폴더 · 잠금에서 빠짐 → 잘못된 add 는 매니페스트를 바꾸지 않는다.
#   cmake -DSBX=<sbx> -DSERVER=<SandboxServer> -DOUT=<작업 폴더> -DTICKS=<n> -P project_install.cmake
foreach(v SBX SERVER OUT TICKS)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
set(project "${OUT}/Game")
set(cache "${OUT}/cache")

function(run_ok what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
    message("${out}${err}")
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${what} 실패 (종료 코드 ${rc})")
    endif()
    set(last "${out}${err}" PARENT_SCOPE)
endfunction()

run_ok("sbx new" "${SBX}" new "${project}" --id ctest.install --from ecosystem_small)
file(WRITE "${OUT}/shared/inv/pack.json"
    "{\"id\": \"inv\", \"version\": \"1.2.0\", \"license\": \"MIT\", \"dependencies\": {\"eco\": \"^0.1\"}}\n")
file(WRITE "${OUT}/shared/inv/prefabs/crate.json" "{\"id\": \"inv.crate\", \"components\": {\"core.transform\": {}}}\n")

# add — 매니페스트 · 잠금 · 설치 폴더
run_ok("sbx add" "${SBX}" add "${project}" inv --path ../shared/inv --version ^1 --cache-dir "${cache}")
if(NOT last MATCHES "설치: \\.sandbox/packages 에 팩 1 · 잠금 sandbox\\.lock 팩 2 · 매니페스트 갱신"
   OR NOT last MATCHES "inv 1\\.2\\.0 · MIT · path \\.\\./shared/inv · digest [0-9a-f]+")
    message(FATAL_ERROR "sbx add 의 출력이 다르다")
endif()
file(READ "${project}/sandbox.project.json" manifest)
if(NOT manifest MATCHES "\"schemaVersion\": 3" OR NOT manifest MATCHES "\"path\": \"\\.\\./shared/inv\"")
    message(FATAL_ERROR "매니페스트에 의존이 없다:\n${manifest}")
endif()
run_ok("sbx check" "${SBX}" check "${project}")
if(NOT last MATCHES "inv \\^1 → 1\\.2\\.0 · MIT · path \\.\\./shared/inv \\(\\.sandbox/packages/inv\\)"
   OR NOT last MATCHES "프로젝트 검사: 오류 0")
    message(FATAL_ERROR "check 가 설치 폴더의 팩을 읽지 않았다")
endif()
run_ok("서버" "${SERVER}" --world "${project}" --port 0 --ticks ${TICKS} --exit)
if(NOT last MATCHES "tick ${TICKS} hash 0x")
    message(FATAL_ERROR "서버가 끝까지 돌지 않았다")
endif()

# install — 잠금 그대로, 원본 · 설치 폴더 없이 (캐시에서)
file(READ "${project}/sandbox.lock" lock)
file(REMOVE_RECURSE "${OUT}/shared" "${project}/.sandbox")
run_ok("sbx install (오프라인)" "${SBX}" install "${project}" --cache-dir "${cache}")
file(READ "${project}/sandbox.lock" lock2)
if(NOT lock STREQUAL lock2)
    message(FATAL_ERROR "잠금 그대로 설치했는데 잠금이 바뀌었다")
endif()

# 고친 설치 → check · 서버 거절 (P8) → install 로 고쳐진다
file(WRITE "${project}/.sandbox/packages/inv/prefabs/crate.json"
    "{\"id\": \"inv.crate\", \"components\": {\"core.transform\": {\"x\": 3}}}\n")
execute_process(COMMAND "${SBX}" check "${project}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
if(NOT rc EQUAL 1 OR NOT out MATCHES "P8 — \\.sandbox/packages/inv: digest")
    message(FATAL_ERROR "고친 설치를 check 가 통과시켰다 (종료 코드 ${rc})")
endif()
execute_process(COMMAND "${SERVER}" --world "${project}" --port 0 --ticks 5 --exit
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
if(rc EQUAL 0)
    message(FATAL_ERROR "고친 설치로 서버가 시작했다:\n${out}${err}")
endif()
run_ok("sbx install (고치기)" "${SBX}" install "${project}" --cache-dir "${cache}")
run_ok("sbx check (고친 뒤)" "${SBX}" check "${project}")

# 잘못된 add (없는 소스) — 실패하고 매니페스트 · 잠금은 그대로
file(READ "${project}/sandbox.project.json" before)
execute_process(COMMAND "${SBX}" add "${project}" ghost --path ../nowhere --cache-dir "${cache}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
file(READ "${project}/sandbox.project.json" after)
file(READ "${project}/sandbox.lock" lock3)
if(rc EQUAL 0 OR NOT before STREQUAL after OR NOT lock STREQUAL lock3)
    message(FATAL_ERROR "실패한 add 가 프로젝트를 바꿨다 (종료 코드 ${rc})")
endif()

# remove — 설치 폴더 · 잠금에서 빠진다
run_ok("sbx remove" "${SBX}" remove "${project}" inv --cache-dir "${cache}")
if(EXISTS "${project}/.sandbox/packages/inv")
    message(FATAL_ERROR "remove 뒤에도 설치 폴더에 inv 가 있다")
endif()
run_ok("sbx check (remove 뒤)" "${SBX}" check "${project}")
file(REMOVE_RECURSE "${OUT}")
