# 패키지 데모 (v0.4 M6-T1): samples/packages/inventory 를 설치하는 samples/stash 를 저장소 그대로 다른 폴더에 복사 (설치 폴더
# .sandbox 없이) → 빈 캐시로 sbx install (잠금 그대로) → sbx test (검사 오류 · 경고 0 + playthrough expect) → 그 다음은
# sample_playthrough.cmake 와 같다 (헤드리스 서버 = 로컬 플레이 · 게임 패키지로도 같은 결과 줄).
#   cmake -DSERVER=<SandboxServer> -DCLIENT=<SandboxClient> -DSBX=<sbx> -DSAMPLES=<samples> -DOUT=<작업 폴더>
#         -DPLAYTHROUGH=<sample_playthrough.cmake> -P sample_package.cmake
foreach(v SERVER CLIENT SBX SAMPLES OUT PLAYTHROUGH)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/samples")
# 저장소에 있는 것만 — 개발자가 samples/stash 에 설치해 둔 .sandbox 는 빼고
file(COPY "${SAMPLES}/packages" DESTINATION "${OUT}/samples" PATTERN ".sandbox" EXCLUDE)
file(COPY "${SAMPLES}/stash" DESTINATION "${OUT}/samples" PATTERN ".sandbox" EXCLUDE)
set(game "${OUT}/samples/stash")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
    message("${out}${err}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${out}${err}" PARENT_SCOPE)
endfunction()

run(1 "설치 전 check (잠금은 있고 설치 폴더가 없다)" "${SBX}" check "${game}")
if(NOT last MATCHES "P8 — 팩 'inventory' 가 \\.sandbox/packages/inventory 에 설치되지 않았다")
    message(FATAL_ERROR "설치 전 check 가 P8 을 내지 않았다")
endif()
run(0 "sbx install (빈 캐시)" "${SBX}" install "${game}" --cache-dir "${OUT}/cache")
if(NOT last MATCHES "inventory 1\\.0\\.0 · MIT · path \\.\\./packages/inventory")
    message(FATAL_ERROR "설치 출력이 다르다")
endif()
run(0 "sbx test" "${SBX}" test "${game}")
if(NOT last MATCHES "프로젝트 검사: 오류 0, 경고 0" OR NOT last MATCHES "통과 playthrough\\.json — 게임 없음 · tick 900")
    message(FATAL_ERROR "sbx test 가 경고 없이 통과하지 않았다")
endif()

run(0 "자동 플레이 (서버 · 로컬 플레이 · 패키지)" "${CMAKE_COMMAND}" -DSERVER=${SERVER} -DCLIENT=${CLIENT} -DSBX=${SBX}
    -DSAMPLE=${game} -DOUT=${OUT}/playthrough "-DRESULT_MATCH=게임 없음 · tick 900"
    "-DSERVER_MATCH=스크립트 stash\\.bonus: 호출 10 · 트랩 0" -P "${PLAYTHROUGH}")
file(REMOVE_RECURSE "${OUT}")
