# 데모 미니게임 자동 플레이 (v0.2 M6-T1, ADR-0055): samples/rabbit_guard 를 playthrough.json 으로 끝까지 —
# 헤드리스 서버 (SandboxServer --script) 와 로컬 플레이 (SandboxClient --headless --script) 의 "게임 결과:" 줄이 같고,
# 둘 다 스크립트의 expect 를 지킨다 (종료 코드 0). 그 다음 같은 샘플을 게임 패키지로 빌드해 (sbx build) 패키지로도 두 호스트가
# 같은 줄을 쓴다 — v0.2 요구 6 "하나의 GamePackage 를 헤드리스 서버와 로컬 플레이에서" (v0.2 M6-T2).
#   cmake -DSERVER=<SandboxServer> -DCLIENT=<SandboxClient> -DSBX=<sbx> -DSAMPLE=<samples/rabbit_guard> -DOUT=<작업 폴더>
#         -P sample_playthrough.cmake
foreach(v SERVER CLIENT SBX SAMPLE OUT)
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

# 게임 패키지로 (세이브 폴더는 패키지 옆 — OUT 안)
file(REMOVE_RECURSE "${OUT}")
execute_process(COMMAND "${SBX}" build "${SAMPLE}" --out "${OUT}/rabbit_guard.sbpkg"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "샘플을 패키지로 빌드하지 못했다:\n${out}${err}")
endif()
foreach(host SERVER CLIENT)
    if(host STREQUAL "SERVER")
        execute_process(COMMAND "${SERVER}" --world "${OUT}/rabbit_guard.sbpkg" --script "${script}" --port 0 --log-level warn
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 150)
    else()
        execute_process(COMMAND "${CLIENT}" --headless --world "${OUT}/rabbit_guard.sbpkg" --script "${script}" --log-level warn
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 150)
    endif()
    string(REGEX MATCH "게임 결과: [^\n]*" line "${out}")
    if(NOT rc EQUAL 0 OR NOT line STREQUAL serverLine)
        message(FATAL_ERROR "패키지 (${host}) 의 결과가 프로젝트와 다르다 (종료 코드 ${rc}):\n  ${serverLine}\n  ${line}\n${out}${err}")
    endif()
endforeach()
message("패키지도 같은 결과 (헤드리스 서버 · 로컬 플레이)")
file(REMOVE_RECURSE "${OUT}")
