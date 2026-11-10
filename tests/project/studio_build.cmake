# Studio 빌드 (v0.8 M6-T3 — docs/28-CREATOR-UX-GATES.md 3장 G8, ADR-0112):
#   sbx new (ecosystem_small) + 에셋 하나 (가져옴 — 어디서도 부르지 않음) → 헤드리스 SandboxClient --world <프로젝트> --run-command studio.build
#   --quit-after-commands → "빌드를 끝냈다" · A8 경고 → 빈 패키지 캐시 (임시 폴더를 새로) 에서 sbx check <패키지> 오류 0 · 에셋 id 1 →
#   누락 에셋 (팩 materials.json 이 없는 파일을 부른다 — A6 오류) 이면 프로젝트가 열리지 않는다 (종료 코드 2) · 패키지를 쓰지 않는다.
#   cmake -DCLIENT=<SandboxClient> -DSBX=<sbx> -DOUT=<작업 폴더> -P studio_build.cmake
foreach(v CLIENT SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
set(game "${OUT}/Art")
set(package "${game}/build/ctest.build-0.1.0.sbpkg")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

run(0 "sbx new" "${SBX}" new "${game}" --id ctest.build --from ecosystem_small)
file(WRITE "${game}/assets/art/hero.png" "PNG-hero")
run(0 "sbx asset import" "${SBX}" asset import "${game}")

run(0 "Studio 빌드 (명령 studio.build)" "${CLIENT}" --headless --world "${game}" --run-command studio.build --quit-after-commands
    --frames 20000)
if(NOT last MATCHES "빌드를 시작했다" OR NOT last MATCHES "빌드를 끝냈다: [^\n]*ctest\\.build-0\\.1\\.0\\.sbpkg — ctest\\.build 0\\.1\\.0")
    message(FATAL_ERROR "Studio 명령이 패키지를 만들지 않았다")
endif()
if(NOT last MATCHES "경고: [^\n]*A8")
    message(FATAL_ERROR "부르지 않는 에셋 경고 (A8) 가 빌드 보고에 없다")
endif()
if(NOT EXISTS "${package}")
    message(FATAL_ERROR "패키지 파일이 없다: ${package}")
endif()

# 빈 패키지 캐시 — 임시 폴더를 새로 (defaultPackageCache = <임시>/sbx-packages)
file(MAKE_DIRECTORY "${OUT}/tmp")
set(ENV{TMP} "${OUT}/tmp")
set(ENV{TEMP} "${OUT}/tmp")
set(ENV{TMPDIR} "${OUT}/tmp")
run(0 "sbx check <패키지> (빈 캐시)" "${SBX}" check "${package}")
if(NOT last MATCHES "에셋: id 1 · id 없음 0" OR NOT last MATCHES "오류 0")
    message(FATAL_ERROR "빈 캐시에서 연 패키지의 검사가 기대와 다르다")
endif()
if(NOT EXISTS "${OUT}/tmp/sbx-packages")
    message(FATAL_ERROR "패키지를 새 캐시에 풀지 않았다 (빈 캐시가 아니었다)")
endif()

# 누락 에셋 (A6 — 오류) — 그 프로젝트는 열리지 않는다 (종료 코드 2 · 이유 A6) · 패키지를 쓰지 않는다 (옛 것을 지우고 본다).
# 열린 프로젝트를 고쳐 누락이 생긴 뒤의 [빌드] 거절은 test_build_service (같은 BuildService::buildNow) 가 본다
file(REMOVE "${package}")
file(WRITE "${game}/content/ecosystem/presentation/materials.json"
    [=[{"materials": {"eco/rabbit": {"sprite": "assets/art/nothere.png"}}}]=])
run(2 "Studio 빌드 (누락 에셋 — 열지 않음)" "${CLIENT}" --headless --world "${game}" --run-command studio.build
    --quit-after-commands --frames 20000)
if(NOT last MATCHES "A6" OR NOT last MATCHES "nothere\\.png")
    message(FATAL_ERROR "누락 에셋의 이유 (A6) 가 없다")
endif()
if(EXISTS "${package}")
    message(FATAL_ERROR "누락 에셋인데 패키지를 썼다")
endif()
file(REMOVE_RECURSE "${OUT}")
