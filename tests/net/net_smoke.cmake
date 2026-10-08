# 서버 + sbx_net_probe 를 실제 UDP 로 (Phase 9 완료 기준 "헤드리스 서버 CI 실행").
#   cmake -DSERVER=<SandboxServer> -DPROBE=<sbx_net_probe> -DPORT=<n> -P net_smoke.cmake
# execute_process 의 COMMAND 여럿은 파이프로 **동시에** 돈다 — 서버 표준 출력은 probe 의 표준 입력으로 가고(무시),
# probe 의 출력을 검사한다. 서버는 150 틱(5 초) 뒤 스스로 끝난다. probe 는 서버가 늦게 떠도 ENet 이 다시 보낸다.
# 같은 기계에서 이 테스트를 두 개 동시에 돌리면 포트가 겹친다 (CI 는 빌드 트리 하나).
foreach(v SERVER PROBE PORT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
execute_process(
    COMMAND "${SERVER}" --world random_walk_1k --port ${PORT} --default-role admin --ticks 150 --exit --log-level warn
    COMMAND "${PROBE}" --connect 127.0.0.1:${PORT} --name ctest --speed 2 --pause --resume --seconds 0.5 --log-level warn
    RESULTS_VARIABLE rcs
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    TIMEOUT 60)
message("${out}")
if(err)
    message("stderr:\n${err}")
endif()
list(GET rcs 0 serverRc)
list(GET rcs 1 probeRc)
if(NOT serverRc EQUAL 0 OR NOT probeRc EQUAL 0)
    message(FATAL_ERROR "서버 종료 코드 ${serverRc}, probe 종료 코드 ${probeRc}")
endif()
if(NOT out MATCHES "접속 client #1 role admin world random_walk_1k")
    message(FATAL_ERROR "probe 가 접속하지 못했습니다")
endif()
if(NOT out MATCHES "probe 끝: 명령 3 수락 3 거절 0")
    message(FATAL_ERROR "명령 결과가 기대와 다릅니다")
endif()
