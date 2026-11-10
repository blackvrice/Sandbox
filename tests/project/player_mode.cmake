# Player 모드 (v0.8 NG-03 — docs/25-STUDIO-ARCHITECTURE.md 12장, ADR-0121): 받는 사람의 길
#   1. sbx new + sbx build → .sbpkg
#   2. SandboxClient --headless --play <패키지> → "Player 모드" · 월드에 들어감 · player.json 최근 게임
#   3. .sbpkg 경로 하나만 (파일 연결 · 더블클릭) = --play 와 같다
#   4. 열지 못하는 파일 — 손상 · 패키지 아님 · 없음 — 마다 사람의 말 ("게임 파일이 손상됐다" …) · 종료 코드 2
#   5. --play 와 편집 옵션 (--record) 은 같이 쓰지 않는다 (2)
#   cmake -DCLIENT=<SandboxClient> -DSBX=<sbx> -DOUT=<작업 폴더> -P player_mode.cmake
foreach(v CLIENT SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config" "${OUT}/tmp")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config")
set(ENV{TMP} "${OUT}/tmp")
set(ENV{TEMP} "${OUT}/tmp")
set(ENV{TMPDIR} "${OUT}/tmp")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

run(0 "sbx new" "${SBX}" new "${OUT}/Game" --id ctest.player --from ecosystem_small)
set(pkg "${OUT}/ctest.player-0.1.0.sbpkg")
run(0 "sbx build" "${SBX}" build "${OUT}/Game" --out "${pkg}")

# 2. --play
run(0 "--play" "${CLIENT}" --headless --play "${pkg}" --frames 120)
if(NOT last MATCHES "Player 모드" OR NOT last MATCHES "Connecting → InWorld")
    message(FATAL_ERROR "Player 모드로 게임에 들어가지 않았다")
endif()
file(READ "${OUT}/config/player.json" lib)
string(JSON gid GET "${lib}" recentGames 0 gameId)
string(JSON gver GET "${lib}" recentGames 0 version)
if(NOT gid STREQUAL "ctest.player" OR NOT gver STREQUAL "0.1.0")
    message(FATAL_ERROR "최근 게임에 남지 않았다:\n${lib}")
endif()
if(EXISTS "${OUT}/config/studio.json")
    file(READ "${OUT}/config/studio.json" studio)
    if(studio MATCHES "ctest\\.player")
        message(FATAL_ERROR "Player 모드가 Studio 최근 프로젝트에 적었다")
    endif()
endif()

# 3. 경로 하나만 (파일 연결)
run(0 "경로 하나 (파일 연결)" "${CLIENT}" "${pkg}" --headless --frames 60)
if(NOT last MATCHES "Player 모드" OR NOT last MATCHES "Connecting → InWorld")
    message(FATAL_ERROR ".sbpkg 경로 하나로 Player 모드가 되지 않았다")
endif()

# 4. 열지 못하는 파일
# 끝이 잘린 파일 — 손상 (CMake 문자열은 NUL 을 담지 못해 그 앞에서 끊길 수도 있다 — 그러면 머리부터 다르다: 둘 다 열지 못한다)
file(SIZE "${pkg}" size)
math(EXPR cut "${size} - 64")
file(READ "${pkg}" keep LIMIT ${cut})
file(WRITE "${OUT}/truncated.sbpkg" "${keep}")
run(2 "손상된 파일" "${CLIENT}" --headless --play "${OUT}/truncated.sbpkg" --frames 60)
if(NOT last MATCHES "게임을 열 수 없다: (게임 파일이 손상됐다|게임 파일 \\(\\.sbpkg\\) 이 아니다)")
    message(FATAL_ERROR "손상된 파일의 사람 말이 없다")
endif()
file(WRITE "${OUT}/text.sbpkg" "이것은 게임이 아니다")
run(2 "패키지가 아닌 파일" "${CLIENT}" --headless --play "${OUT}/text.sbpkg" --frames 60)
if(NOT last MATCHES "게임을 열 수 없다: 게임 파일 \\(\\.sbpkg\\) 이 아니다")
    message(FATAL_ERROR "패키지가 아닌 파일의 사람 말이 없다")
endif()
run(2 "없는 파일" "${CLIENT}" --headless --play "${OUT}/nothing.sbpkg" --frames 60)
if(NOT last MATCHES "게임을 열 수 없다: 게임 파일을 찾을 수 없다")
    message(FATAL_ERROR "없는 파일의 사람 말이 없다")
endif()

# 5. 편집 옵션과 같이 쓰지 않는다
run(2 "--play --record" "${CLIENT}" --headless --play "${pkg}" --record r1 --frames 10)
message("player_mode: 통과")
