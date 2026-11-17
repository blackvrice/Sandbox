# v0.10 통합 시험 NG-18 (헤드리스 흐름 — NG-07 층 2, docs/28-CREATOR-UX-GATES.md 3.1, ADR-0147): 버튼 · 판 게임을 한 줄로
#   1. 생성     Studio 홈의 명령 (studio.new-project — --new-project --template board) → 워크스페이스
#   2. UI       sbx ui (UI 디자이너의 모델) 로 HUD 에 [기권] 버튼 — 신호 ttt.resign. 신호가 없으면 sbx check 가 막는다
#   3. 스크립트 신호 표에 ttt.resign, 서버 규칙 (rules.ts) 이 그 신호로 패배 표시를 만든다
#   4. 저장     sbx resave (Studio Ctrl+S 와 같다 — 시작 월드를 지금 콘텐츠로) → sbx check 오류 0 · 경고 0
#   5. 실행     헤드리스 서버 자동 플레이 (돌 하나 → 기권 = lose) + 기록, 원래 자동 플레이 (win) 도 그대로
#   6. 재실행   로컬 플레이 (같은 결과 줄) · 기록 재생 (해시 일치)
#   7. 패키지   sbx build → 빈 캐시에서 헤드리스 서버 · 로컬 플레이가 같은 결과 줄
#   cmake -DCLIENT=<SandboxClient> -DSERVER=<SandboxServer> -DSBX=<sbx> -DOUT=<작업 폴더> -P ng18_board_flow.cmake
cmake_minimum_required(VERSION 3.25)
foreach(v CLIENT SERVER SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config" "${OUT}/tmp")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config")
set(game "${OUT}/Omok")
set(hud "content/ttt/presentation/ui/hud.json")

function(run expect what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

# 1. 생성 (Studio 홈 — 게임 템플릿 board)
run(0 "새 프로젝트 (studio.new-project · board)" "${CLIENT}" --headless --new-project "${game}" --game-id ctest.omok
    --template board --project-name Omok --frames 120 --autosave-seconds 0)
if(NOT last MATCHES "Connecting → InWorld" OR NOT EXISTS "${game}/${hud}")
    message(FATAL_ERROR "board 템플릿으로 새 프로젝트에 들어가지 않았다")
endif()
file(READ "${game}/sandbox.project.json" manifest)
string(JSON gameId GET "${manifest}" gameId)
if(NOT gameId STREQUAL "ctest.omok")
    message(FATAL_ERROR "gameId 가 새로 쓰이지 않았다: ${gameId}")
endif()

# 2. UI — [기권] 버튼 (root panel 의 끝)
run(0 "sbx ui list" "${SBX}" ui list "${game}" "${hud}")
if(NOT last MATCHES "/1/0  button #c0")
    message(FATAL_ERROR "HUD 목록에 칸 버튼이 없다")
endif()
run(0 "sbx ui add" "${SBX}" ui add "${game}" "${hud}" / button)
if(NOT last MATCHES "추가: (/[0-9]+) button")
    message(FATAL_ERROR "버튼을 더하지 못했다")
endif()
set(button "${CMAKE_MATCH_1}")
run(0 "sbx ui set id" "${SBX}" ui set "${game}" "${hud}" "${button}" id "\"resign\"")
run(0 "sbx ui set text" "${SBX}" ui set "${game}" "${hud}" "${button}" text "\"기권\"")
run(0 "sbx ui set on" "${SBX}" ui set "${game}" "${hud}" "${button}" on "{\"click\":{\"signal\":\"ttt.resign\"}}")
run(2 "sbx ui set children (거절)" "${SBX}" ui set "${game}" "${hud}" "${button}" children "[]")
run(0 "sbx ui list (뒤)" "${SBX}" ui list "${game}" "${hud}")
if(NOT last MATCHES "${button}  button #resign '기권'")
    message(FATAL_ERROR "목록에 [기권] 버튼이 없다")
endif()
# 신호 표에 없는 신호 → 검사가 막는다 (UI → 스크립트 연결)
run(1 "sbx check (신호 없음)" "${SBX}" check "${game}")
if(NOT last MATCHES "ttt\\.resign")
    message(FATAL_ERROR "없는 신호 ttt.resign 을 검사가 알리지 않았다")
endif()

# 3. 스크립트 — 신호 표 · 서버 규칙
file(READ "${game}/content/ttt/signals.json" signals)
string(JSON n LENGTH "${signals}")
string(JSON signals SET "${signals}" ${n} "{\"id\": \"ttt.resign\", \"min\": 0, \"max\": 0}")
file(WRITE "${game}/content/ttt/signals.json" "${signals}\n")
set(rules "${game}/content/ttt/scripts/rules.ts")
file(READ "${rules}" src)
set(old [=[    if (e.kind !== GameEventKind.Signal || e.signal !== "ttt.place") {
      return
    }
    const b = ctx.state(Board)
]=])
set(new [=[    if (e.kind !== GameEventKind.Signal) {
      return
    }
    const b = ctx.state(Board)
    if (e.signal === "ttt.resign") { // NG-18 — HUD 의 [기권]
      if (!b.over) {
        b.over = true
        finish(ctx, "ttt.defeat")
        ctx.setState(Board, b)
      }
      return
    }
    if (e.signal !== "ttt.place") {
      return
    }
]=])
string(FIND "${src}" "${old}" at)
if(at EQUAL -1)
    message(FATAL_ERROR "rules.ts 에 고칠 자리가 없다 (템플릿이 바뀌었나)")
endif()
string(REPLACE "${old}" "${new}" src "${src}")
file(WRITE "${rules}" "${src}")

# 4. 저장 → 검사 깨끗
run(0 "sbx resave" "${SBX}" resave "${game}")
run(0 "sbx check" "${SBX}" check "${game}")
if(NOT last MATCHES "프로젝트 검사: 오류 0, 경고 0")
    message(FATAL_ERROR "저장한 뒤의 검사에 오류나 경고가 있다")
endif()

# 5. 실행 — 돌 하나 → 기권 (lose) + 기록, 원래 자동 플레이 (win)
set(resign "${OUT}/resign.json")
file(WRITE "${resign}" [=[{
  "format": 1,
  "name": "NG-18 — 돌 하나 두고 기권",
  "maxTicks": 120,
  "steps": [
    { "at": 2, "command": { "op": "game", "action": "start" } },
    { "at": 10, "command": { "op": "signal", "name": "ttt.place", "value": 4 } },
    { "at": 20, "command": { "op": "signal", "name": "ttt.resign", "value": 0 } }
  ],
  "expect": { "state": "Finished", "result": "lose", "team": 0 }
}
]=])
run(0 "서버 자동 플레이 (기권) --record" "${SERVER}" --world "${game}" --script "${resign}" --record ng18 --port 0
    --bind 127.0.0.1 --log-level warn)
string(REGEX MATCH "게임 결과: [^\n]*" serverLine "${last}")
if(NOT serverLine MATCHES "Finished · lose" OR NOT EXISTS "${game}/worlds/ng18/replay.sbxr")
    message(FATAL_ERROR "기권이 패배로 끝나지 않았거나 기록이 없다: ${serverLine}")
endif()
run(0 "서버 자동 플레이 (원래 — 이긴다)" "${SERVER}" --world "${game}" --script "${game}/playthrough.json" --port 0
    --bind 127.0.0.1 --log-level warn)
if(NOT last MATCHES "게임 결과: [^\n]*Finished · win")
    message(FATAL_ERROR "원래 자동 플레이가 이기지 않았다")
endif()

# 6. 재실행 — 로컬 플레이 · 기록 재생
run(0 "로컬 플레이 (기권)" "${CLIENT}" --headless --world "${game}" --script "${resign}" --log-level warn --frames 200000)
string(REGEX MATCH "게임 결과: [^\n]*" line "${last}")
if(NOT line STREQUAL serverLine)
    message(FATAL_ERROR "로컬 플레이의 결과가 서버와 다르다:\n  ${serverLine}\n  ${line}")
endif()
run(0 "기록 재생" "${CLIENT}" --headless --replay "${game}/worlds/ng18" --frames 200000)
if(NOT last MATCHES "리플레이 끝: ng18 — 재생 끝 — [0-9]+ call, 해시 [0-9]+개 일치")
    message(FATAL_ERROR "기록이 같게 재생되지 않았다")
endif()

# 7. 패키지 — 빈 캐시에서
run(0 "sbx build" "${SBX}" build "${game}" --out "${OUT}/omok.sbpkg")
set(ENV{TMP} "${OUT}/tmp")
set(ENV{TEMP} "${OUT}/tmp")
set(ENV{TMPDIR} "${OUT}/tmp")
run(0 "패키지 서버 (기권)" "${SERVER}" --world "${OUT}/omok.sbpkg" --script "${resign}" --port 0 --bind 127.0.0.1
    --log-level warn)
string(REGEX MATCH "게임 결과: [^\n]*" line "${last}")
if(NOT line STREQUAL serverLine)
    message(FATAL_ERROR "패키지 (서버) 의 결과가 프로젝트와 다르다:\n  ${serverLine}\n  ${line}")
endif()
run(0 "패키지 로컬 플레이 (기권)" "${CLIENT}" --headless --world "${OUT}/omok.sbpkg" --script "${resign}" --log-level warn
    --frames 200000)
string(REGEX MATCH "게임 결과: [^\n]*" line "${last}")
if(NOT line STREQUAL serverLine)
    message(FATAL_ERROR "패키지 (로컬 플레이) 의 결과가 프로젝트와 다르다:\n  ${serverLine}\n  ${line}")
endif()
message("ng18_board_flow OK — ${serverLine}")
