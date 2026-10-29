# 배포 묶음 (v0.5 M1-T4 — docs/20-CREATOR-ALPHA.md 7.7): make_release.cmake 로 zip 을 만들고, 저장소와 상관없는 폴더에 풀어
#   - SHA256SUMS.txt 가 zip 과 맞는다, 묶음에 BUILD.txt · content/ · assets/ · samples/ · sdk/ 가 있고 .sandbox 는 없다
#   - 푼 SandboxServer 가 그 옆의 content/ 를 쓴다 ("콘텐츠 루트 <푼 폴더>/content") · 시나리오와 샘플 프로젝트를 돌린다
#   - 푼 sbx doctor samples/rabbit_guard 가 오류 0, 푼 SandboxClient (있으면) --headless 로 샘플을 돌린다
#   cmake -DSOURCE=<저장소> -DOUT=<작업 폴더> -DSERVER=<exe> -DSBX=<exe> [-DCLIENT=<exe>] -DVERSION=<x.y.z>
#         -P release_bundle.cmake
foreach(v SOURCE OUT SERVER SBX VERSION)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
set(platform "test-x")
set(name "sandbox-${VERSION}-${platform}")

execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${SOURCE}" "-DOUT=${OUT}/dist" "-DVERSION=${VERSION}"
                        "-DPLATFORM=${platform}" -DCONFIG=test -DCOMMIT=test -DTOOLCHAIN=test
                        "-DSERVER=${SERVER}" "-DSBX=${SBX}" "-DCLIENT=${CLIENT}"
                        -P "${SOURCE}/tools/release/make_release.cmake"
                RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
message("make_release → ${rc}\n${o}${e}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "make_release 실패")
endif()

# SHA-256 목록
file(READ "${OUT}/dist/SHA256SUMS.txt" sums)
file(SHA256 "${OUT}/dist/${name}.zip" h)
if(NOT sums MATCHES "${h}  ${name}\\.zip")
    message(FATAL_ERROR "SHA256SUMS.txt 가 zip 과 다르다:\n${sums}")
endif()

# 다른 폴더에 푼다 (저장소 · 빌드 폴더와 무관한 곳 — 실행 파일이 옆의 content/ 를 찾는지)
file(MAKE_DIRECTORY "${OUT}/unpacked")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${OUT}/dist/${name}.zip" WORKING_DIRECTORY "${OUT}/unpacked"
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "zip 을 풀지 못했다")
endif()
set(root "${OUT}/unpacked/${name}")
foreach(p BUILD.txt README.md content assets samples/rabbit_guard/sandbox.project.json sdk/sandbox.d.ts docs)
    if(NOT EXISTS "${root}/${p}")
        message(FATAL_ERROR "묶음에 ${p} 가 없다")
    endif()
endforeach()
file(GLOB samples LIST_DIRECTORIES true "${root}/samples/*")
foreach(s IN LISTS samples)
    if(IS_DIRECTORY "${s}/.sandbox")
        message(FATAL_ERROR "묶음에 설치한 패키지 (.sandbox) 가 들어갔다: ${s}")
    endif()
endforeach()

get_filename_component(serverName "${SERVER}" NAME)
get_filename_component(sbxName "${SBX}" NAME)
function(run_in_bundle out)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${root}" RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e
                    TIMEOUT 120)
    message("${ARGN} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "종료 코드 ${rc}")
    endif()
    set(${out} "${o}${e}" PARENT_SCOPE)
endfunction()

run_in_bundle(out "${root}/${serverName}" --world ecosystem_small --port 0 --ticks 30 --exit --log-level info)
if(NOT out MATCHES "콘텐츠 루트 [^\n]*/unpacked/${name}/content")
    message(FATAL_ERROR "서버가 묶음의 content/ 를 쓰지 않았다")
endif()
# 게임 모드 프로젝트 — --start-game 이 없으면 Lobby 에 멈춰 틱이 오르지 않는다 (project_game.cmake 와 같다)
run_in_bundle(out "${root}/${serverName}" --world samples/rabbit_guard --port 0 --start-game --ticks 30 --exit)
run_in_bundle(out "${root}/${sbxName}" doctor samples/rabbit_guard)
if(NOT out MATCHES "진단: 오류 0")
    message(FATAL_ERROR "sbx doctor 오류")
endif()
if(DEFINED CLIENT AND NOT CLIENT STREQUAL "")
    get_filename_component(clientName "${CLIENT}" NAME)
    run_in_bundle(out "${root}/${clientName}" --headless --world samples/rabbit_guard --frames 30)
    if(NOT out MATCHES "SandboxClient 끝: frames 30")
        message(FATAL_ERROR "클라이언트가 끝까지 돌지 않았다")
    endif()
endif()
file(REMOVE_RECURSE "${OUT}")
