# 기록 · 보고 묶음 (v0.8 M5-T3 — docs/20-CREATOR-ALPHA.md 4장 "기록" · 7.1, ADR-0119):
#   예제 rabbit_guard 를 복사한 프로젝트에서
#   1. SandboxServer --world <프로젝트> --script playthrough.json --record srv → worlds/srv/replay.sbxr → SandboxClient --replay = 기록과 같다
#   2. SandboxClient --headless --world <프로젝트> --script playthrough.json --record cli --report-on-exit → worlds/cli · 보고 묶음에 replay/ →
#      sbx report 가 리플레이 폴더를 알린다 → 묶음의 replay/ 를 프로젝트 worlds/ 에 넣고 --replay = 기록과 같다
#   3. 다른 게임 (sbx new) 의 worlds/ 에 넣은 기록 = "다른 게임" 으로 재생하지 않는다 (게임 결속 — 종료 코드 1)
#   4. 이미 있는 이름으로 --record = 서버가 시작하지 않는다
#   cmake -DCLIENT=<SandboxClient> -DSERVER=<SandboxServer> -DSBX=<sbx> -DSAMPLES=<samples> -DOUT=<작업 폴더> -P replay_record.cmake
foreach(v CLIENT SERVER SBX SAMPLES OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config" "${OUT}/reports")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config")
set(ENV{SBX_REPORTS_DIR} "${OUT}/reports")
set(game "${OUT}/Guard")
file(COPY "${SAMPLES}/rabbit_guard/" DESTINATION "${game}")
file(REMOVE_RECURSE "${game}/.sandbox" "${game}/worlds/srv" "${game}/worlds/cli")
set(play "${SAMPLES}/rabbit_guard/playthrough.json")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

# 1. 서버 자동 플레이 + 기록
run(0 "서버 자동 플레이 --record" "${SERVER}" --world "${game}" --script "${play}" --record srv --port 0 --bind 127.0.0.1)
if(NOT EXISTS "${game}/worlds/srv/replay.sbxr" OR NOT last MATCHES "기록 'srv': tick [0-9]+ 부터 기록")
    message(FATAL_ERROR "서버가 첫 틱부터 기록하지 않았다")
endif()
run(0 "서버 기록 재생" "${CLIENT}" --headless --replay "${game}/worlds/srv" --frames 200000)
if(NOT last MATCHES "리플레이 끝: srv — 재생 끝 — [0-9]+ call, 해시 [0-9]+개 일치")
    message(FATAL_ERROR "서버 기록이 같게 재생되지 않았다")
endif()

# 2. 클라이언트 자동 플레이 + 기록 + 보고 묶음
run(0 "클라이언트 자동 플레이 --record --report-on-exit" "${CLIENT}" --headless --world "${game}" --script "${play}" --record cli
    --report-on-exit --frames 200000)
if(NOT EXISTS "${game}/worlds/cli/replay.sbxr")
    message(FATAL_ERROR "클라이언트 (로컬 서버) 가 기록하지 않았다")
endif()
if(NOT last MATCHES "보고 묶음: ([^\n]*) \\(리플레이 포함\\)")
    message(FATAL_ERROR "보고 묶음에 리플레이가 없다")
endif()
set(bundle "${CMAKE_MATCH_1}")
if(NOT EXISTS "${bundle}/replay/replay.sbxr" OR NOT EXISTS "${bundle}/report.json" OR NOT EXISTS "${bundle}/log.txt")
    message(FATAL_ERROR "보고 묶음 ${bundle} 에 report.json · log.txt · replay/ 가 다 없다")
endif()
run(0 "sbx report" "${SBX}" report "${bundle}")
if(NOT last MATCHES "폴더 replay/ — 리플레이 기록. 다시 보기: 그 게임 프로젝트의 worlds/ 에 복사해")
    message(FATAL_ERROR "sbx report 가 리플레이 폴더를 알리지 않았다")
endif()
file(COPY "${bundle}/replay/" DESTINATION "${game}/worlds/fromreport")
run(0 "보고 묶음의 리플레이 재생" "${CLIENT}" --headless --replay "${game}/worlds/fromreport" --frames 200000)
if(NOT last MATCHES "리플레이 끝: fromreport — 재생 끝 — [0-9]+ call, 해시 [0-9]+개 일치")
    message(FATAL_ERROR "보고 묶음의 리플레이가 같게 재생되지 않았다")
endif()

# 3. 다른 게임의 기록 — 결속
run(0 "sbx new (다른 게임)" "${SBX}" new "${OUT}/Other" --id ctest.other --from ecosystem_small)
file(COPY "${game}/worlds/srv/" DESTINATION "${OUT}/Other/worlds/srv")
run(1 "다른 게임의 기록" "${CLIENT}" --headless --replay "${OUT}/Other/worlds/srv" --frames 200000)
if(NOT last MATCHES "다른 게임 '[^']+' 의 리플레이입니다")
    message(FATAL_ERROR "다른 게임의 기록을 결속으로 거절하지 않았다")
endif()

# 4. 있는 이름으로 기록 — 서버가 시작하지 않는다
run(1 "있는 이름 --record" "${SERVER}" --world "${game}" --record srv --port 0 --bind 127.0.0.1 --ticks 5 --exit)
message("replay_record: 통과")
