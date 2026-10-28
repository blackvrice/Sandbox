# 패키지 틀 · 프로젝트 시험 (v0.4 M4-T2): sbx new --pack kit → 게임 프로젝트에 sbx add kit --path → check (kit.tick) →
# sbx test (자동 플레이 없음 = 통과) → tests/smoke.json (expect tick) 통과 → expect 가 틀린 스크립트면 종료 1 →
# 샘플 rabbit_guard 의 playthrough.json 도 sbx test 로 통과.
#   cmake -DSBX=<sbx> -DOUT=<작업 폴더> -DSAMPLE=<samples/rabbit_guard> -P project_pack.cmake
foreach(v SBX OUT SAMPLE)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
    message("${out}${err}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${out}${err}" PARENT_SCOPE)
endfunction()

run(0 "sbx new --pack" "${SBX}" new "${OUT}/kit" --pack --id kit --license MIT)
if(NOT last MATCHES "패키지 틀을 만들었습니다: .* 팩 kit 0\\.1\\.0 · MIT · 파일 4")
    message(FATAL_ERROR "new --pack 의 출력이 다르다")
endif()
run(2 "sbx new --pack (비지 않은 폴더)" "${SBX}" new "${OUT}/kit" --pack --id kit)

run(0 "sbx new" "${SBX}" new "${OUT}/Game" --id ctest.kit --from ecosystem_small)
run(0 "sbx add" "${SBX}" add "${OUT}/Game" kit --path ../kit --cache-dir "${OUT}/cache")
run(0 "sbx check" "${SBX}" check "${OUT}/Game")
if(NOT last MATCHES "시스템 kit\\.tick — \\.sandbox/packages/kit/scripts/tick\\.ts · onTick \\(30 틱마다\\)")
    message(FATAL_ERROR "check 가 설치한 kit 의 시스템을 보이지 않았다")
endif()

run(0 "sbx test (자동 플레이 없음)" "${SBX}" test "${OUT}/Game")
if(NOT last MATCHES "자동 플레이 0 개" OR NOT last MATCHES "시험: 통과 0 · 실패 0")
    message(FATAL_ERROR "sbx test 의 출력이 다르다")
endif()
file(WRITE "${OUT}/Game/tests/smoke.json"
    "{\"format\": 1, \"name\": \"smoke\", \"maxTicks\": 30, \"steps\": [], \"expect\": {\"tick\": 31}}\n")
run(0 "sbx test (smoke)" "${SBX}" test "${OUT}/Game")
if(NOT last MATCHES "통과 tests/smoke\\.json — 게임 없음 · tick 31")
    message(FATAL_ERROR "smoke 가 통과하지 않았다")
endif()
file(WRITE "${OUT}/Game/tests/wrong.json"
    "{\"format\": 1, \"name\": \"wrong\", \"maxTicks\": 30, \"steps\": [], \"expect\": {\"tick\": 99}}\n")
run(1 "sbx test (틀린 expect)" "${SBX}" test "${OUT}/Game")
if(NOT last MATCHES "실패 tests/wrong\\.json" OR NOT last MATCHES "시험: 통과 1 · 실패 1")
    message(FATAL_ERROR "틀린 expect 를 실패로 보이지 않았다")
endif()

# 샘플의 자동 플레이 (게임 모드 — 시작 · 승리 expect)
run(0 "sbx test (rabbit_guard)" "${SBX}" test "${SAMPLE}")
if(NOT last MATCHES "통과 playthrough\\.json — 게임 Finished")
    message(FATAL_ERROR "rabbit_guard 의 자동 플레이가 통과하지 않았다")
endif()
file(REMOVE_RECURSE "${OUT}")
