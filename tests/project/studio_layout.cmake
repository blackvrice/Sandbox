# Studio 레이아웃 · 설정 (v0.8 M6-T6 — docs/25-STUDIO-ARCHITECTURE.md 4장, docs/28-CREATOR-UX-GATES.md 3장, ADR-0114):
#   설정 폴더 studio/layouts/qa.ini 에 "편집" 창 자리 (Pos=123,45) → 헤드리스 SandboxClient --world <프로젝트> --layout qa
#   --run-command studio.save-layout → qa.ini 에 같은 자리 (ImGui 가 적용하고 다시 꺼냈다) + 실제로 그린 창 ("빌드") · studio.json
#   layout = qa (최근 목록 · autosaveSeconds 는 그대로) → --layout 없이 다시 열면 qa (기억) → studio.reset-layout 곧바로 끝내도 기본 배치가 남는다 →
#   깨진 qa.ini 는 qa.ini.broken 으로 옮기고 기본 배치 (종료 코드 0) → 틀린 이름은 종료 코드 2 → 안전 모드는 아무 파일도 쓰지 않는다.
#   cmake -DCLIENT=<SandboxClient> -DSBX=<sbx> -DOUT=<작업 폴더> -P studio_layout.cmake
foreach(v CLIENT SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config")
set(game "${OUT}/Lay")
set(layouts "${OUT}/config/studio/layouts")

execute_process(COMMAND "${SBX}" new "${game}" --id ctest.layout --from ecosystem_small RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sbx new 실패 (${rc})")
endif()

function(run expect what)
    execute_process(COMMAND "${CLIENT}" --headless --world "${game}" --frames 20000 ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e TIMEOUT 300)
    message("${what} → ${rc}\n${o}${e}")
    if(NOT rc EQUAL ${expect})
        message(FATAL_ERROR "${what}: 종료 코드 ${rc} (기대 ${expect})")
    endif()
    set(last "${o}${e}" PARENT_SCOPE)
endfunction()

# 1. 저장된 배치로 시작 → 저장 — 같은 자리 · 실제 창
file(WRITE "${layouts}/qa.ini" "[Window][편집]\nPos=123,45\nSize=390,330\nCollapsed=0\n\n")
# Studio 설정의 다른 키 (최근 목록 · 자동 저장 주기) 는 레이아웃을 기억하며 그대로 남아야 한다
file(WRITE "${OUT}/config/studio.json" [=[{"recentProjects": [{"path": "C:/Elsewhere", "name": "else"}], "autosaveSeconds": 0}]=])
run(0 "--layout qa · 레이아웃 저장" --layout qa --run-command studio.save-layout --quit-after-commands)
if(NOT last MATCHES "레이아웃 'qa'" OR NOT last MATCHES "레이아웃 'qa' 을 저장했다: [^\n]*studio/layouts/qa\\.ini")
    message(FATAL_ERROR "레이아웃 qa 를 열고 저장했다는 로그가 없다")
endif()
file(READ "${layouts}/qa.ini" ini)
message("qa.ini:\n${ini}")
if(NOT ini MATCHES "\\[Window\\]\\[편집\\]\nPos=123,45\nSize=390,330")
    message(FATAL_ERROR "qa.ini 에 '편집' 창의 자리가 그대로 없다 (적용 → 꺼내기 왕복)")
endif()
if(NOT ini MATCHES "\\[Window\\]\\[빌드\\]")
    message(FATAL_ERROR "qa.ini 에 그린 창 ('빌드') 이 없다 — 실제 배치를 꺼내지 않았다")
endif()
file(READ "${OUT}/config/studio.json" studio)
string(JSON active GET "${studio}" layout)
string(JSON recent GET "${studio}" recentProjects 0 name)
string(JSON autosave GET "${studio}" autosaveSeconds)
if(NOT active STREQUAL "qa" OR NOT recent STREQUAL "else" OR NOT autosave EQUAL 0)
    message(FATAL_ERROR "studio.json 에 layout = qa 와 원래 키 (최근 목록 · autosaveSeconds) 가 같이 없다:\n${studio}")
endif()

# 2. --layout 없이 — 기억한 qa
run(0 "다시 열기 (기억)" --run-command studio.save-layout --quit-after-commands)
if(NOT last MATCHES "레이아웃 'qa'")
    message(FATAL_ERROR "다시 열었을 때 기억한 레이아웃 qa 가 아니다")
endif()

# 3. 기본 배치로 — 곧바로 끝내도 남는다 (편집 창이 그 자리가 아니다)
run(0 "기본 배치로" --run-command studio.reset-layout --quit-after-commands)
if(NOT last MATCHES "레이아웃 'qa' — 기본 배치로")
    message(FATAL_ERROR "기본 배치로 되돌린 로그가 없다")
endif()
file(READ "${layouts}/qa.ini" ini)
if(ini MATCHES "Pos=123,45")
    message(FATAL_ERROR "기본 배치로 되돌렸는데 qa.ini 에 옛 자리가 남았다:\n${ini}")
endif()

# 4. 깨진 ini — 옆으로 옮기고 기본 배치 (실패가 아니다)
file(WRITE "${layouts}/qa.ini" "this is not an ini file\n")
run(0 "깨진 qa.ini" --quit-after-commands --run-command studio.save-layout)
if(NOT last MATCHES "깨졌다" OR NOT EXISTS "${layouts}/qa.ini.broken")
    message(FATAL_ERROR "깨진 레이아웃을 옮기지 않았다")
endif()
file(READ "${layouts}/qa.ini" ini)
if(NOT ini MATCHES "\\[Window\\]\\[편집\\]")
    message(FATAL_ERROR "깨진 레이아웃 뒤 기본 배치를 다시 저장하지 않았다:\n${ini}")
endif()

# 5. 틀린 이름 — 인자 오류
run(2 "틀린 레이아웃 이름" --layout "../Bad" --quit-after-commands --run-command studio.save-layout)

# 6. 안전 모드 — 설정 폴더에 아무 것도 쓰지 않는다
file(REMOVE_RECURSE "${OUT}/safe")
file(MAKE_DIRECTORY "${OUT}/safe")
set(ENV{SBX_CONFIG_DIR} "${OUT}/safe")
run(0 "안전 모드" --safe-mode --layout qa --run-command studio.save-layout --quit-after-commands)
if(NOT last MATCHES "안전 모드 — 저장하지 않는다")
    message(FATAL_ERROR "안전 모드의 레이아웃 로그가 없다")
endif()
file(GLOB_RECURSE written "${OUT}/safe/*")
if(written)
    message(FATAL_ERROR "안전 모드가 설정 폴더에 썼다: ${written}")
endif()
message("studio_layout: 통과")
