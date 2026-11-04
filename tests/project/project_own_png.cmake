# 자기 PNG 게임 (v0.6 M6-T9 — "자기 PNG 를 넣은 게임이 다른 폴더 · 빈 캐시에서 보인다", ADR-0090): A 가 sbx new (ecosystem_small) →
# assets/art/Hero.png (대소문자 섞인 이름 — 진짜 PNG) → sbx asset import → 팩 presentation/materials.json 에서 엔진 머티리얼 eco/rabbit 의
# sprite 를 "asset:<id>" 로 → sbx check (0 · 0) → sbx build (형식 3) → 패키지만 건네고 A 를 지움 → B 의 창 클라이언트 (렌더러) 가 빈 설정 ·
# 캐시 · 임시 폴더 · 다른 작업 폴더로 패키지를 열어 그 그림을 올린다 ("프로젝트 파일 1 (준비 1 · 실패 0)")
#   cmake -DSBX=<sbx> -DCLIENT=<SandboxClient> [-DXVFB=<xvfb-run>] -DPNG=<픽스처> -DOUT=<작업 폴더> -P project_own_png.cmake
#   끝에 "project_own_png OK" — CTest 는 이 줄로만 통과로 본다 (명령줄이 잘못 갈라져 스크립트가 돌지 않고 0 으로 끝나는 것을 막는다)
foreach(v SBX CLIENT PNG OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
set(game "${OUT}/a/Art")
# 창을 띄우는 명령 — 가상 화면 (xvfb-run) 이 있으면 그 안에서. 목록은 스크립트 안에서 만든다 (-D 로 목록을 넘기면 add_test 가 가른다)
if(DEFINED XVFB AND NOT XVFB STREQUAL "")
    set(clientCmd "${XVFB}" -a -s "-screen 0 1280x800x24 -noreset" "${CLIENT}")
else()
    set(clientCmd "${CLIENT}")
endif()

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 180)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

run(0 "sbx new" "${SBX}" new "${game}" --id ctest.own --from ecosystem_small)
file(MAKE_DIRECTORY "${game}/assets/art")
file(COPY_FILE "${PNG}" "${game}/assets/art/Hero.png")
run(0 "sbx asset import" "${SBX}" asset import "${game}")
file(READ "${game}/assets/art/Hero.png.meta" meta)
string(JSON id GET "${meta}" id)
file(WRITE "${game}/content/ecosystem/presentation/materials.json"
     "{\"materials\": {\"eco/rabbit\": {\"sprite\": \"asset:${id}\"}}}\n")
run(0 "sbx check" "${SBX}" check "${game}")
if(NOT last MATCHES "오류 0, 경고 0")
    message(FATAL_ERROR "검사 경고가 있다")
endif()
run(0 "sbx build" "${SBX}" build "${game}" --out "${OUT}/a/own.sbpkg")
file(MAKE_DIRECTORY "${OUT}/b/inbox" "${OUT}/b/home" "${OUT}/b/tmp")
file(COPY_FILE "${OUT}/a/own.sbpkg" "${OUT}/b/inbox/own.sbpkg")
file(REMOVE_RECURSE "${OUT}/a")

execute_process(COMMAND "${CMAKE_COMMAND}" -E env SBX_CONFIG_DIR=${OUT}/b/user/config SBX_CACHE_DIR=${OUT}/b/user/cache
                        SBX_REPORTS_DIR=${OUT}/b/user/reports TMP=${OUT}/b/tmp TEMP=${OUT}/b/tmp TMPDIR=${OUT}/b/tmp
                        ${clientCmd} --world "${OUT}/b/inbox/own.sbpkg" --frames 150 --width 640 --height 400
                WORKING_DIRECTORY "${OUT}/b/home" RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
message("클라이언트 → ${rc}\n${o}${e}")
if(NOT rc EQUAL 0 OR NOT "${o}" MATCHES "SandboxClient 끝: frames 150")
    message(FATAL_ERROR "클라이언트가 패키지를 끝까지 돌리지 못했다 (종료 코드 ${rc})")
endif()
if(NOT "${o}" MATCHES "프로젝트 파일 1 \\(준비 1 · 실패 0\\)")
    message(FATAL_ERROR "패키지의 자기 그림이 올라가지 않았다 (렌더러가 없으면 에셋 줄이 없다)")
endif()
file(REMOVE_RECURSE "${OUT}")
message("project_own_png OK")
