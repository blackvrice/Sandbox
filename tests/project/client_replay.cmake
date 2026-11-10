# 리플레이 뷰어 (v0.8 M5-T1 — docs/20-CREATOR-ALPHA.md 4장, ADR-0116):
#   sbx_sim_check --replay-roundtrip 으로 기록 폴더 (start/ + replay.sbxr) → 헤드리스 SandboxClient --replay <폴더> → "리플레이 끝: …
#   해시 N개 일치" · 종료 코드 0 → 해시를 바꾼 기록은 "기록과 다르다" · 종료 코드 1 → replay.sbxr 없음 = 인자 오류 2 → 다른 게임
#   (프로젝트 worlds/ 에 넣은 다른 콘텐츠의 기록) 은 재생하지 않는다 (콘텐츠 해시 — 종료 코드 1).
#   cmake -DCLIENT=<SandboxClient> -DSIMCHECK=<sbx_sim_check> -DSBX=<sbx> -DOUT=<작업 폴더> -P client_replay.cmake
foreach(v CLIENT SIMCHECK SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

# 1. 기록 (생태계 시나리오 120 틱 — 시작 세이브 + 명령 + 30 틱마다 해시)
run(0 "기록 (sim_check)" "${SIMCHECK}" --scenario ecosystem_small --ticks 120 --replay-roundtrip --replay-dir "${OUT}/rec1")
if(NOT EXISTS "${OUT}/rec1/replay.sbxr" OR NOT EXISTS "${OUT}/rec1/start/world.json")
    message(FATAL_ERROR "기록 폴더가 없다")
endif()

# 2. 재생 — 끝 줄 · 해시 일치 · 종료 코드 0
run(0 "재생 (--replay)" "${CLIENT}" --headless --replay "${OUT}/rec1" --frames 20000)
if(NOT last MATCHES "리플레이 끝: rec1 — 재생 끝 — [0-9]+ call, 해시 ([0-9]+)개 일치")
    message(FATAL_ERROR "재생 끝 줄이 없다")
endif()
if(CMAKE_MATCH_1 LESS 4)
    message(FATAL_ERROR "비교한 해시가 너무 적다 (${CMAKE_MATCH_1})")
endif()

# 3. 해시를 바꾼 기록 — 기록과 다르다 · 종료 코드 1
file(COPY "${OUT}/rec1/" DESTINATION "${OUT}/rec2")
file(READ "${OUT}/rec2/replay.sbxr" sbxr)
# 해시 레코드 {"call":c,"hash":"0x…","k":"hash","tick":t} (키는 이름 순) 의 값을 모두 바꾼다 — 첫 비교에서 다르다
string(REGEX REPLACE "(\"call\":[0-9]+,\"hash\":\")0x[0-9a-fA-F]+(\",\"k\":\"hash\")" "\\10x0000000000000001\\2" changed "${sbxr}")
if(changed STREQUAL sbxr)
    message(FATAL_ERROR "replay.sbxr 의 해시 레코드를 찾지 못했다 (형식이 바뀌었나)")
endif()
file(WRITE "${OUT}/rec2/replay.sbxr" "${changed}")
run(1 "바꾼 기록 재생" "${CLIENT}" --headless --replay "${OUT}/rec2" --frames 20000)
if(NOT last MATCHES "리플레이 실패: rec2 — 재생 끝 — 기록과 다르다")
    message(FATAL_ERROR "바꾼 기록의 불일치를 알리지 않았다")
endif()

# 4. replay.sbxr 없는 폴더 — 인자 오류
run(2 "기록 아님" "${CLIENT}" --headless --replay "${OUT}/rec1/start" --frames 100)

# 5. 다른 게임 — 프로젝트 worlds/ 에 넣은 시나리오 기록은 그 프로젝트의 콘텐츠와 달라 재생하지 않는다
run(0 "sbx new" "${SBX}" new "${OUT}/Game" --id ctest.replay)
file(COPY "${OUT}/rec1/" DESTINATION "${OUT}/Game/worlds/rec1")
run(1 "다른 콘텐츠의 기록" "${CLIENT}" --headless --replay "${OUT}/Game/worlds/rec1" --frames 20000)
if(NOT last MATCHES "리플레이 실패: rec1 — 다른 콘텐츠로 기록한 리플레이")
    message(FATAL_ERROR "다른 게임의 기록을 거절하지 않았다")
endif()
message("client_replay: 통과")
