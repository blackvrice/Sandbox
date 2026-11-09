# Studio 홈의 새 프로젝트 (v0.8 M6-T1 — docs/28-CREATOR-UX-GATES.md 3장 G1, ADR-0111):
#   헤드리스 SandboxClient --new-project → 홈의 명령 studio.new-project → 프로젝트 → 그 프로젝트로 들어감 (InWorld) → sbx check 오류 0 · 경고 0,
#   최근 목록 (설정 폴더 studio.json) 에 남는다, 같은 폴더에 다시 하면 실패 (덮어쓰지 않는다 — 종료 코드 1), 모르는 템플릿도 실패.
#   cmake -DCLIENT=<SandboxClient> -DSBX=<sbx> -DOUT=<작업 폴더> -P studio_new_project.cmake
foreach(v CLIENT SBX OUT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/config")
set(project "${OUT}/Game")
set(ENV{SBX_CONFIG_DIR} "${OUT}/config") # 사용자 설정 폴더 — 시험마다 따로 (최근 목록)

execute_process(COMMAND "${CLIENT}" --headless --new-project "${project}" --game-id ctest.studio --project-name "스튜디오 게임"
                        --frames 90 --log-level info
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "클라이언트 종료 코드 ${rc}")
endif()
if(NOT "${out}${err}" MATCHES "프로젝트를 만들었다: [^\n]* ctest\\.studio \"스튜디오 게임\" \\(검사 오류 0 · 경고 0\\)")
    message(FATAL_ERROR "홈의 새 프로젝트 명령이 프로젝트를 만들지 않았다")
endif()
if(NOT "${out}${err}" MATCHES "앱 상태 Connecting → InWorld")
    message(FATAL_ERROR "만든 프로젝트로 들어가지 않았다 (InWorld 아님)")
endif()
file(READ "${OUT}/config/studio.json" recent)
if(NOT recent MATCHES "ctest\\.studio")
    message(FATAL_ERROR "최근 목록 (studio.json) 에 없다:\n${recent}")
endif()

execute_process(COMMAND "${SBX}" check "${project}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
if(NOT rc EQUAL 0 OR NOT out MATCHES "프로젝트 검사: 오류 0, 경고 0 [^\n]*시작 월드 열림")
    message(FATAL_ERROR "홈에서 만든 프로젝트가 sbx check 를 통과하지 못했다 (종료 코드 ${rc})")
endif()

# 같은 폴더 — 덮어쓰지 않는다 (실패 · 종료 코드 1)
execute_process(COMMAND "${CLIENT}" --headless --new-project "${project}" --game-id ctest.again --frames 30
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
if(rc EQUAL 0 OR NOT "${out}${err}" MATCHES "새 프로젝트: ")
    message(FATAL_ERROR "비어 있지 않은 폴더에 새 프로젝트가 성공했다 (종료 코드 ${rc})\n${out}${err}")
endif()
# 모르는 템플릿
execute_process(COMMAND "${CLIENT}" --headless --new-project "${OUT}/Other" --game-id ctest.other --template no_such --frames 30
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
if(rc EQUAL 0 OR NOT "${out}${err}" MATCHES "템플릿 'no_such' 가 없다")
    message(FATAL_ERROR "모르는 템플릿이 성공했다 (종료 코드 ${rc})\n${out}${err}")
endif()
file(REMOVE_RECURSE "${OUT}")
