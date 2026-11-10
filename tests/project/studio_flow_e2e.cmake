# Studio 흐름 E2E (v0.8 NG-07 층 2 — docs/28-CREATOR-UX-GATES.md 3.1, ADR-0120): 서비스 · 명령만으로 한 줄로
#   1. 새 프로젝트 (홈의 명령 studio.new-project — --new-project) → 워크스페이스에 들어간다
#   2. 에셋 가져오기 (sbx asset import — Studio 의 가져오기 화면은 v0.10 ST-06-3)
#   3. 빌드 (명령 studio.build — 홈 · 툴바의 빌드 단추와 같은 명령) → 패키지
#   4. 플레이 — 다른 빈 캐시에서 SandboxClient --world <패키지> 로 들어가 돈다 (받는 사람의 길)
#   cmake -DCLIENT=<SandboxClient> -DSBX=<sbx> -DOUT=<작업 폴더> -P studio_flow_e2e.cmake
foreach(v CLIENT SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config" "${OUT}/tmp")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config")
set(game "${OUT}/Flow")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

# 1. 새 프로젝트 → 워크스페이스
run(0 "새 프로젝트 (studio.new-project)" "${CLIENT}" --headless --new-project "${game}" --game-id ctest.flow --template ecosystem_small
    --project-name Flow --frames 120 --autosave-seconds 0)
if(NOT last MATCHES "Connecting → InWorld" OR NOT EXISTS "${game}/sandbox.project.json")
    message(FATAL_ERROR "새 프로젝트로 들어가지 않았다")
endif()
# 최근 목록에 남았다 (홈의 카드)
file(READ "${OUT}/config/studio.json" studio)
string(JSON recent GET "${studio}" recentProjects 0 gameId)
if(NOT recent STREQUAL "ctest.flow")
    message(FATAL_ERROR "최근 목록에 새 프로젝트가 없다")
endif()

# 2. 에셋 가져오기
file(MAKE_DIRECTORY "${game}/assets/art")
file(WRITE "${game}/assets/art/hero.png" "PNG-hero")
run(0 "sbx asset import" "${SBX}" asset import "${game}")

# 3. 빌드 (studio.build)
run(0 "빌드 (studio.build)" "${CLIENT}" --headless --world "${game}" --run-command studio.build --quit-after-commands --frames 20000
    --autosave-seconds 0)
if(NOT last MATCHES "빌드를 끝냈다: ([^\n]*\\.sbpkg) — ctest\\.flow")
    message(FATAL_ERROR "빌드가 패키지를 만들지 않았다")
endif()
set(package "${CMAKE_MATCH_1}")
if(NOT EXISTS "${package}")
    message(FATAL_ERROR "패키지 파일이 없다: ${package}")
endif()

# 4. 플레이 — 빈 패키지 캐시 (임시 폴더를 새로)
set(ENV{TMP} "${OUT}/tmp")
set(ENV{TEMP} "${OUT}/tmp")
set(ENV{TMPDIR} "${OUT}/tmp")
run(0 "패키지로 플레이" "${CLIENT}" --headless --world "${package}" --frames 120)
if(NOT last MATCHES "Connecting → InWorld")
    message(FATAL_ERROR "패키지로 월드에 들어가지 않았다")
endif()
message("studio_flow_e2e: 통과")
