# 데모 미니게임 자동 플레이 (v0.2 M6-T1, ADR-0055): samples/rabbit_guard 를 playthrough.json 으로 끝까지 —
# 헤드리스 서버 (SandboxServer --script) 와 로컬 플레이 (SandboxClient --headless --script) 의 "게임 결과:" 줄이 같고,
# 둘 다 스크립트의 expect 를 지킨다 (종료 코드 0).
#   cmake -DSERVER=<SandboxServer> -DCLIENT=<SandboxClient> -DSAMPLE=<samples/rabbit_guard> -P sample_playthrough.cmake
foreach(v SERVER CLIENT SAMPLE)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
set(script "${SAMPLE}/playthrough.json")

execute_process(COMMAND "${SERVER}" --world "${SAMPLE}" --script "${script}" --port 0 --log-level warn
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 150)
message("[서버] ${out}${err}")
string(REGEX MATCH "게임 결과: [^\n]*" serverLine "${out}")
if(NOT rc EQUAL 0 OR serverLine STREQUAL "")
    message(FATAL_ERROR "헤드리스 서버의 자동 플레이 실패 (종료 코드 ${rc})")
endif()

execute_process(COMMAND "${CLIENT}" --headless --world "${SAMPLE}" --script "${script}" --log-level warn
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 150)
message("[로컬 플레이] ${out}${err}")
string(REGEX MATCH "게임 결과: [^\n]*" clientLine "${out}")
if(NOT rc EQUAL 0 OR clientLine STREQUAL "")
    message(FATAL_ERROR "로컬 플레이의 자동 플레이 실패 (종료 코드 ${rc})")
endif()

if(NOT serverLine STREQUAL clientLine)
    message(FATAL_ERROR "헤드리스 서버와 로컬 플레이의 결과가 다르다:\n  ${serverLine}\n  ${clientLine}")
endif()
if(NOT serverLine MATCHES "게임 Finished")
    message(FATAL_ERROR "게임이 끝나지 않았다: ${serverLine}")
endif()
message("같은 결과: ${serverLine}")
