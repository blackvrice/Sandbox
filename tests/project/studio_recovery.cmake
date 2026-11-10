# Studio 자동 저장 · 크래시 복구 (v0.8 M6-T7 — docs/28-CREATOR-UX-GATES.md 3장 "복구", 24 D3, ADR-0113):
#   sbx new → 헤드리스 SandboxClient --world <프로젝트> --autosave-seconds 1 → 강제 종료 (시간 초과로 죽인다) → 설정 폴더 recovery/<해시>/recovery.json
#   (clean false · tick T) → --recover 로 다시 시작 → "복구한다: worlds/recovered-1 (tick T" · 서버 "불러오기 'recovered-1' … tick T" (불러오기는
#   세이브의 WorldHash 를 확인한다 — 같은 해시) → 프로젝트의 시작 월드는 그대로 · 복구본 폴더는 지웠다 → 깨끗이 끝난 실행 뒤에는 제안하지 않는다.
#   cmake -DCLIENT=<SandboxClient> -DSBX=<sbx> -DOUT=<작업 폴더> -P studio_recovery.cmake
foreach(v CLIENT SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config")
set(game "${OUT}/Rec")

execute_process(COMMAND "${SBX}" new "${game}" --id ctest.rec --from ecosystem_small RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sbx new 실패 (${rc})")
endif()
file(READ "${game}/worlds/main/world.json" mainBefore)

# 1. 자동 저장하며 돌다가 충돌 — 두 번째 자동 저장 뒤 정리 없이 끝낸다 (--abort-after-autosaves — std::_Exit, 깨끗한 끝 표시 없음).
#    시간 초과로 죽이는 길은 쓰지 않는다: 처음 해 보니 execute_process 가 돌아온 뒤에도 프로세스가 몇 초 더 살아 자동 저장을 이어 썼다
execute_process(COMMAND "${CLIENT}" --headless --world "${game}" --autosave-seconds 1 --fps 60 --abort-after-autosaves 2
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("충돌한 실행 → ${rc}\n${out}${err}")
if(NOT rc EQUAL 3)
    message(FATAL_ERROR "충돌 흉내의 종료 코드가 3 이 아니다 (${rc})")
endif()
if(NOT "${out}${err}" MATCHES "자동 저장: tick [0-9]+")
    message(FATAL_ERROR "자동 저장이 없었다")
endif()
file(GLOB info "${OUT}/config/recovery/*/recovery.json")
list(LENGTH info n)
if(NOT n EQUAL 1)
    message(FATAL_ERROR "복구본 recovery.json 이 하나가 아니다 (${n})")
endif()
file(READ "${info}" infoText)
string(JSON clean GET "${infoText}" clean)
if(clean)
    message(FATAL_ERROR "충돌했는데 clean 이 참이다")
endif()
# tick · 해시의 정본 = 복구본 세이브의 world.json
get_filename_component(folder "${info}" DIRECTORY)
file(READ "${folder}/world/world.json" worldText)
string(JSON tick GET "${worldText}" tick)
string(JSON hash GET "${worldText}" worldHash)
string(REGEX REPLACE "^0x" "" hash "${hash}")

# 2. 다시 시작 → 복구 (--recover) → 같은 tick 의 세이브를 연다 (불러오기가 WorldHash 를 확인)
execute_process(COMMAND "${CLIENT}" --headless --world "${game}" --recover --frames 120
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
message("복구 실행 → ${rc}\n${out}${err}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "복구 실행 종료 코드 ${rc}")
endif()
if(NOT "${out}${err}" MATCHES "복구할 작업이 있다")
    message(FATAL_ERROR "복구 제안이 없었다")
endif()
if(NOT "${out}${err}" MATCHES "복구한다: worlds/recovered-1 \\(tick ${tick} · hash ${hash}")
    message(FATAL_ERROR "복구가 recovered-1 · tick ${tick} · hash ${hash} 가 아니다")
endif()
if(NOT "${out}${err}" MATCHES "불러오기 'recovered-1' \\(#[0-9]+\\): tick ${tick}")
    message(FATAL_ERROR "서버가 복구한 세이브 (tick ${tick}) 를 열지 않았다")
endif()
if(NOT EXISTS "${game}/worlds/recovered-1/world.json")
    message(FATAL_ERROR "worlds/recovered-1 이 없다")
endif()
file(READ "${game}/worlds/main/world.json" mainAfter)
if(NOT mainAfter STREQUAL mainBefore)
    message(FATAL_ERROR "프로젝트의 시작 월드가 바뀌었다 (복구는 덮어쓰지 않는다)")
endif()
# 복구본은 지웠다 — 다 지우지 못했으면 (다른 프로그램이 잠깐 잡음) 다시 제안하지 않게 clean 으로 남는다
file(GLOB left "${OUT}/config/recovery/*/recovery.json")
foreach(f IN LISTS left)
    file(READ "${f}" leftText)
    string(JSON leftClean GET "${leftText}" clean)
    if(NOT leftClean)
        message(FATAL_ERROR "복구한 뒤에도 깨끗하지 않은 복구본이 남았다: ${f}")
    endif()
endforeach()

# 3. 깨끗이 끝난 실행 (자동 저장 포함) 뒤에는 제안하지 않는다
execute_process(COMMAND "${CLIENT}" --headless --world "${game}" --autosave-seconds 1 --fps 60 --frames 240
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
if(NOT rc EQUAL 0 OR NOT "${out}${err}" MATCHES "자동 저장: tick")
    message(FATAL_ERROR "깨끗한 실행이 실패했거나 자동 저장이 없었다 (${rc})\n${out}${err}")
endif()
execute_process(COMMAND "${CLIENT}" --headless --world "${game}" --frames 30
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
if(NOT rc EQUAL 0 OR "${out}${err}" MATCHES "복구할 작업이 있다")
    message(FATAL_ERROR "깨끗이 끝난 뒤에 복구를 제안했다\n${out}${err}")
endif()
file(REMOVE_RECURSE "${OUT}")
