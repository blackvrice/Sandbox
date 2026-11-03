# Technical Alpha A → B (v0.5 M7-T6 — docs/roadmap/releases.md v0.5, ADR-0096): 만든 사람 A 의 프로젝트에서 패키지 파일 하나만 B 에게
# 건네고, B 는 배포 묶음 (zip) 의 엔진으로 — 저장소 · 빌드 폴더 · A 의 프로젝트 · 캐시 없이 — 같은 게임 결과 (WorldHash 포함) 를 얻는다.
#   A  samples/stash (스크립트 + 의존 패키지 inventory) 를 저장소 그대로 복사 → 빈 캐시로 sbx install → sbx test → 헤드리스 서버로 자동 플레이
#      (결과 줄 = 기준) → sbx build → stash.sbpkg (digest A). 그다음 A 의 폴더를 통째로 지운다
#   B  make_release 의 zip 을 다른 폴더에 풀고, 사용자 폴더 (설정 · 캐시 · 보고) 를 빈 새 폴더로, 작업 폴더도 따로 →
#      임시 폴더 (패키지를 푸는 곳 — defaultPackageCache) 도 새 폴더로 → sbx check <패키지> (digest = A · 오류 · 경고 0 — 패키지에는
#      sdk/ 가 없으니 P6 를 보지 않는다) → 서버 · 로컬 플레이 (있으면) 가 패키지로 자동 플레이 → 결과 줄 = A. B 의 팩 캐시는 비어 있어야
#      한다 (의존 패키지는 .sbpkg 안에 — 내려받지 않는다). 한 바이트를 덧붙인 패키지는 B 의 서버가 거절한다
#   cmake -DSOURCE=<저장소> -DOUT=<작업 폴더> -DSERVER=<exe> -DSBX=<exe> [-DCLIENT=<exe>] -DVERSION=<x.y.z> -P tech_alpha_e2e.cmake
foreach(v SOURCE OUT SERVER SBX VERSION)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")

# 기계 A 의 명령 — A 의 사용자 폴더도 OUT 안 (개발자 PC 의 캐시 · 설정에 기대지 않게)
set(envA SBX_CONFIG_DIR=${OUT}/a/user/config SBX_CACHE_DIR=${OUT}/a/user/cache SBX_REPORTS_DIR=${OUT}/a/user/reports)
function(run_a expect what)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envA} ${ARGN} WORKING_DIRECTORY "${OUT}/a"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("[A] ${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "[A] ${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY "${OUT}/a/samples")
file(COPY "${SOURCE}/samples/packages" DESTINATION "${OUT}/a/samples" PATTERN ".sandbox" EXCLUDE)
file(COPY "${SOURCE}/samples/stash" DESTINATION "${OUT}/a/samples" PATTERN ".sandbox" EXCLUDE)
set(game "${OUT}/a/samples/stash")

run_a(0 "sbx install (빈 캐시)" "${SBX}" install "${game}")
run_a(0 "sbx test" "${SBX}" test "${game}")
if(NOT last MATCHES "프로젝트 검사: 오류 0, 경고 0")
    message(FATAL_ERROR "[A] 검사 경고가 있다")
endif()
run_a(0 "자동 플레이 (프로젝트)" "${SERVER}" --world "${game}" --script "${game}/playthrough.json" --port 0 --log-level warn)
string(REGEX MATCH "게임 결과: [^\n]*hash 0x[0-9a-f]+" resultA "${last}")
if(resultA STREQUAL "")
    message(FATAL_ERROR "[A] 결과 줄 (hash 포함) 이 없다")
endif()
run_a(0 "sbx build" "${SBX}" build "${game}" --out "${OUT}/a/out/stash.sbpkg")
if(NOT last MATCHES "digest (sha256:[0-9a-f]+)")
    message(FATAL_ERROR "[A] digest 줄이 없다")
endif()
set(digestA "${CMAKE_MATCH_1}")

# 건네기: 패키지와 자동 플레이 대본만 — 그다음 A 를 지운다 (B 가 A 의 경로를 몰래 쓰면 여기서 깨진다)
file(MAKE_DIRECTORY "${OUT}/inbox")
file(COPY_FILE "${OUT}/a/out/stash.sbpkg" "${OUT}/inbox/stash.sbpkg")
file(COPY_FILE "${game}/playthrough.json" "${OUT}/inbox/playthrough.json")
file(REMOVE_RECURSE "${OUT}/a")

# 기계 B: 배포 묶음의 엔진
set(platform "test-x")
set(name "sandbox-${VERSION}-${platform}")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${SOURCE}" "-DOUT=${OUT}/dist" "-DVERSION=${VERSION}"
                        "-DPLATFORM=${platform}" -DCONFIG=test -DCOMMIT=test -DTOOLCHAIN=test
                        "-DSERVER=${SERVER}" "-DSBX=${SBX}" "-DCLIENT=${CLIENT}"
                        -P "${SOURCE}/tools/release/make_release.cmake"
                RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "make_release 실패\n${o}${e}")
endif()
file(MAKE_DIRECTORY "${OUT}/b/engine" "${OUT}/b/home")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${OUT}/dist/${name}.zip" WORKING_DIRECTORY "${OUT}/b/engine" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "zip 을 풀지 못했다")
endif()
file(REMOVE_RECURSE "${OUT}/dist")
set(engine "${OUT}/b/engine/${name}")
get_filename_component(serverName "${SERVER}" NAME)
get_filename_component(sbxName "${SBX}" NAME)
set(serverB "${engine}/${serverName}")
set(sbxB "${engine}/${sbxName}")
set(pkg "${OUT}/inbox/stash.sbpkg")

set(envB SBX_CONFIG_DIR=${OUT}/b/user/config SBX_CACHE_DIR=${OUT}/b/user/cache SBX_REPORTS_DIR=${OUT}/b/user/reports
         TMP=${OUT}/b/tmp TEMP=${OUT}/b/tmp TMPDIR=${OUT}/b/tmp)
file(MAKE_DIRECTORY "${OUT}/b/tmp")
function(run_b expect what)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envB} ${ARGN} WORKING_DIRECTORY "${OUT}/b/home"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("[B] ${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "[B] ${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

run_b(0 "sbx check <패키지>" "${sbxB}" check "${pkg}")
if(NOT last MATCHES "프로젝트 검사: 오류 0, 경고 0" OR NOT last MATCHES "${digestA}")
    message(FATAL_ERROR "[B] 패키지 검사 · digest 가 A 와 다르다 (A ${digestA})")
endif()
run_b(0 "자동 플레이 (서버 · 패키지)" "${serverB}" --world "${pkg}" --script "${OUT}/inbox/playthrough.json" --port 0 --log-level warn)
string(REGEX MATCH "게임 결과: [^\n]*hash 0x[0-9a-f]+" resultB "${last}")
if(NOT resultB STREQUAL resultA)
    message(FATAL_ERROR "[B] 서버 결과가 A 와 다르다:\n  A ${resultA}\n  B ${resultB}")
endif()
if(DEFINED CLIENT AND NOT CLIENT STREQUAL "")
    get_filename_component(clientName "${CLIENT}" NAME)
    run_b(0 "자동 플레이 (로컬 플레이 · 패키지)" "${engine}/${clientName}" --headless --world "${pkg}"
          --script "${OUT}/inbox/playthrough.json" --log-level warn)
    string(REGEX MATCH "게임 결과: [^\n]*hash 0x[0-9a-f]+" resultC "${last}")
    if(NOT resultC STREQUAL resultA)
        message(FATAL_ERROR "[B] 로컬 플레이 결과가 A 와 다르다:\n  A ${resultA}\n  B ${resultC}")
    endif()
endif()
# 패키지는 B 의 임시 폴더에 풀렸다 (A · 개발자 PC 의 풀어 둔 것을 쓰지 않았다)
if(NOT IS_DIRECTORY "${OUT}/b/tmp/sbx-packages")
    message(FATAL_ERROR "[B] 패키지를 B 의 임시 폴더에 풀지 않았다")
endif()
# 의존 패키지는 .sbpkg 안에 있다 — B 의 캐시에 아무것도 내려받지 않았다
file(GLOB_RECURSE cached "${OUT}/b/user/cache/*")
if(cached)
    message(FATAL_ERROR "[B] 캐시에 파일이 생겼다 (패키지가 의존을 다 싣지 않았다):\n${cached}")
endif()
# 한 바이트를 덧붙인 패키지는 B 의 서버가 거절한다
file(COPY_FILE "${pkg}" "${OUT}/inbox/bad.sbpkg")
file(APPEND "${OUT}/inbox/bad.sbpkg" "x")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${envB} "${serverB}" --world "${OUT}/inbox/bad.sbpkg" --port 0 --ticks 5 --exit
                WORKING_DIRECTORY "${OUT}/b/home" RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 60)
if(rc EQUAL 0)
    message(FATAL_ERROR "[B] 고친 패키지로 서버가 시작했다:\n${o}${e}")
endif()
message("같은 결과 (A 프로젝트 = B 패키지 · 배포 묶음 엔진): ${resultA}")
file(REMOVE_RECURSE "${OUT}")
