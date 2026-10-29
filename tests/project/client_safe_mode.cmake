# 안전 모드 · 사용자 설정 · 새 버전 알림 (v0.5 M1-T3 — docs/20-CREATOR-ALPHA.md 7.5 · 7.6). SBX_CONFIG_DIR 아래에서
# SandboxClient --headless 를 몇 프레임 돌린다 —
#   uiScale 2           ImGui 배율 줄 "× 2.00" (설정이 적용된다)
#   --safe-mode         같은 설정 · 깨진 input.json 이어도 "안전 모드" 알림 · 배율 1 · 파일은 그대로 (옮기지 않는다)
#   깨진 client.json    종료 코드 0 · "깨져 client.json.broken-" 알림 · 파일이 옆으로 · 다음 시작은 조용
#   피드 (파일)          --update-feed · client.json updateFeed → "새 버전 99.0.0", updateCheck false 면 설정의 피드는 읽지 않는다
#   cmake -DCLIENT=<SandboxClient> -DOUT=<작업 폴더> -P client_safe_mode.cmake
foreach(v CLIENT OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
set(config "${OUT}/config")
file(MAKE_DIRECTORY "${config}")

# 클라이언트를 돌린다 — 종료 코드 0 이어야 하고, 출력 (stdout + stderr) 을 out 에
function(run_client out)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "SBX_CONFIG_DIR=${config}"
        "${CLIENT}" --headless --frames 5 --fps 120 --log-level info ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 60)
    message("SandboxClient ${ARGN} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "종료 코드 ${rc}")
    endif()
    set(${out} "${o}${e}" PARENT_SCOPE)
endfunction()

function(expect text pattern)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "출력에 '${pattern}' 가 없다")
    endif()
endfunction()

function(expect_not text pattern)
    if(text MATCHES "${pattern}")
        message(FATAL_ERROR "출력에 '${pattern}' 가 있으면 안 된다")
    endif()
endfunction()

# 1. 설정이 적용된다 (UI 배율)
file(WRITE "${config}/client.json" "{\"schemaVersion\": 1, \"uiScale\": 2}")
run_client(out)
expect("${out}" "× 2\\.00")
expect_not("${out}" "알림:")

# 2. 안전 모드 — 깨진 input.json 이 있어도 읽지 않고 옮기지 않는다, 배율 1
file(WRITE "${config}/input.json" "not json")
run_client(out --safe-mode)
expect("${out}" "알림: 안전 모드")
expect("${out}" "× 1\\.00")
if(NOT EXISTS "${config}/input.json")
    message(FATAL_ERROR "안전 모드가 input.json 을 옮겼다")
endif()

# 3. 안전 모드가 아니면 깨진 input.json 을 옮기고 시작한다
run_client(out)
expect("${out}" "깨져 input\\.json\\.broken-")
if(EXISTS "${config}/input.json")
    message(FATAL_ERROR "깨진 input.json 이 그대로다")
endif()

# 4. 깨진 client.json → 옮김 · 기본값 (배율 1) · 다음 시작은 조용
file(WRITE "${config}/client.json" "{\"uiScale\": 2,")
run_client(out)
expect("${out}" "알림: 설정 파일 client\\.json 이 깨져 client\\.json\\.broken-")
expect("${out}" "× 1\\.00")
file(GLOB moved "${config}/client.json.broken-*")
list(LENGTH moved n)
if(NOT n EQUAL 1 OR EXISTS "${config}/client.json")
    message(FATAL_ERROR "깨진 client.json 이 옆으로 옮겨지지 않았다 (${n})")
endif()
run_client(out)
expect_not("${out}" "알림:")

# 5. 새 버전 알림 — 피드 파일 (GitHub Releases 형식)
file(WRITE "${OUT}/feed.json"
    "[{\"tag_name\": \"v99.0.0\", \"html_url\": \"https://example.invalid/v99\", \"draft\": false, \"prerelease\": false},"
    " {\"tag_name\": \"v100.0.0\", \"draft\": true}]")
run_client(out --update-feed "${OUT}/feed.json")
expect("${out}" "알림: 새 버전 99\\.0\\.0 이 있다")
file(WRITE "${config}/client.json" "{\"updateFeed\": \"${OUT}/feed.json\"}")
run_client(out)
expect("${out}" "새 버전 99\\.0\\.0")
file(WRITE "${config}/client.json" "{\"updateCheck\": false, \"updateFeed\": \"${OUT}/feed.json\"}")
run_client(out)
expect_not("${out}" "새 버전")
run_client(out --update-feed "${OUT}/missing.json") # 없는 피드는 조용히
expect_not("${out}" "알림:")

file(REMOVE_RECURSE "${OUT}")
