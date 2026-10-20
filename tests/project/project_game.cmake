# 게임 모드가 있는 프로젝트 (v0.2 M2-T3, ADR-0045): sbx new --from ecosystem_small → 팩에 modes/quick.json 을 더하고 매니페스트를
# schemaVersion 2 + gameMode 로 → sbx check 가 모드를 보이고 오류 0 → SandboxServer --start-game 이 Lobby 에서 바로 시작해
# --ticks 까지 간다 (--start-game 이 없으면 Lobby 에 멈춰 틱이 오르지 않는다 — 그래서 CI 는 이 옵션을 쓴다).
#   cmake -DSBX=<sbx> -DSERVER=<SandboxServer> -DOUT=<새 프로젝트 폴더> -DTICKS=<n> -P project_game.cmake
foreach(v SBX SERVER OUT TICKS)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
execute_process(COMMAND "${SBX}" new "${OUT}" --id ctest.game --name "게임" --from ecosystem_small
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sbx new 실패 (종료 코드 ${rc})")
endif()
file(WRITE "${OUT}/content/ecosystem/modes/quick.json"
    "{\"id\": \"eco.quick\", \"name\": \"빠른 판\", \"start\": {\"countdownSeconds\": 0.5},\n"
    " \"end\": [{\"when\": {\"time\": {\"op\": \">=\", \"seconds\": 600}}, \"result\": \"draw\"}]}\n")
file(READ "${OUT}/sandbox.project.json" manifest)
string(JSON manifest SET "${manifest}" schemaVersion 2)
string(JSON manifest SET "${manifest}" gameMode "\"eco.quick\"")
file(WRITE "${OUT}/sandbox.project.json" "${manifest}\n")
execute_process(COMMAND "${SBX}" check "${OUT}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
# 팩 파일이 늘어 시작 월드의 contentHash 는 경고 (P5) — 오류는 0
if(NOT rc EQUAL 0 OR NOT out MATCHES "게임 모드 eco\\.quick" OR NOT out MATCHES "프로젝트 검사: 오류 0, [^\n]*시작 월드 열림")
    message(FATAL_ERROR "게임 모드 프로젝트가 검사를 통과하지 못했습니다 (종료 코드 ${rc})")
endif()
execute_process(COMMAND "${SERVER}" --world "${OUT}" --port 0 --start-game --ticks ${TICKS} --exit --log-level info
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
if(NOT rc EQUAL 0 OR NOT "${out}${err}" MATCHES "게임 모드 eco\\.quick — 바로 시작" OR NOT out MATCHES "tick ${TICKS} hash 0x")
    message(FATAL_ERROR "서버가 게임을 시작해 진행하지 못했습니다 (종료 코드 ${rc})")
endif()
file(REMOVE_RECURSE "${OUT}")
