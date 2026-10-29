# 진단 (v0.5 M1-T2 — docs/20-CREATOR-ALPHA.md 7.3): sbx doctor 가 일부러 만든 문제를 문장으로 · 고치는 법과 함께 —
#   좋은 프로젝트          오류 0, 종료 코드 0
#   더 새 엔진이 필요      engine ">=9.0.0" → "엔진 9.0.0 이상을 받는다", 종료 코드 1
#   옛 엔진용              engine ">=0.0.1 <0.1.0" → "옛 엔진용"
#   빠진 설치              path 의존 + 잠금이 있는데 .sandbox/packages 가 없다 → "sbx install", 설치하면 오류 0
#   깨진 패키지            sbx build 한 .sbpkg 끝에 한 바이트 → "패키지" 오류 · "다시 받거나" (내용 바꾸기는 단위 시험)
#   프로젝트가 아님 · 없음  "프로젝트도 패키지도 아니다" · "가 없다"
#   cmake -DSBX=<sbx> -DOUT=<작업 폴더> -DSAMPLE=<게임 프로젝트> -DSTASH=<path 의존 샘플> -DPACKAGES=<그 패키지 폴더>
#         -P project_doctor.cmake
foreach(v SBX OUT SAMPLE STASH PACKAGES)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

# doctor 를 돌리고 종료 코드 · 문장을 본다 (patterns = 모두 나와야 하는 정규식)
function(doctor target expectRc)
    execute_process(COMMAND "${SBX}" doctor "${target}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message("sbx doctor ${target} → ${rc}\n${out}${err}")
    if(NOT rc EQUAL expectRc)
        message(FATAL_ERROR "종료 코드 ${rc} (기대 ${expectRc})")
    endif()
    foreach(p IN LISTS ARGN)
        if(NOT out MATCHES "${p}")
            message(FATAL_ERROR "진단에 '${p}' 가 없다")
        endif()
    endforeach()
endfunction()

function(set_engine dir range)
    file(READ "${dir}/sandbox.project.json" m)
    string(REGEX REPLACE "\"engine\": \"[^\"]*\"" "\"engine\": \"${range}\"" m "${m}")
    file(WRITE "${dir}/sandbox.project.json" "${m}")
endfunction()

# 좋은 프로젝트
file(COPY "${SAMPLE}/" DESTINATION "${OUT}/good")
# 환경: Windows 는 지원 (통과), 그 밖은 지원 매트릭스상 "검증 안 됨" (경고 — 오류가 아니다, 20 7.8)
if(WIN32)
    set(env_level "통과")
else()
    set(env_level "경고")
endif()
doctor("${OUT}/good" 0 "\\[환경\\] ${env_level}" "\\[엔진 범위\\] 통과" "\\[시작 월드\\] 통과" "진단: 오류 0")

# 더 새 엔진이 필요한 게임
file(COPY "${SAMPLE}/" DESTINATION "${OUT}/newer")
set_engine("${OUT}/newer" ">=9.0.0")
doctor("${OUT}/newer" 1 "\\[엔진 범위\\] 오류  이 게임은 엔진 \">=9.0.0\" 이 필요한데" "엔진 9.0.0 이상을 받는다")

# 옛 엔진용 게임
file(COPY "${SAMPLE}/" DESTINATION "${OUT}/older")
set_engine("${OUT}/older" ">=0.0.1 <0.1.0")
doctor("${OUT}/older" 1 "\\[엔진 범위\\] 오류" "옛 엔진용")

# 빠진 설치 — 샘플 (잠금 포함) 을 복사하되 .sandbox 는 빼고, path 의존 (../packages/…) 도 같은 자리에
file(COPY "${STASH}/" DESTINATION "${OUT}/stash" PATTERN ".sandbox" EXCLUDE)
file(COPY "${PACKAGES}/" DESTINATION "${OUT}/packages")
doctor("${OUT}/stash" 1 "\\[의존 · 설치\\] 오류" "→ sbx install ")
execute_process(COMMAND "${SBX}" install "${OUT}/stash" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("sbx install → ${rc}\n${out}${err}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sbx install 실패")
endif()
doctor("${OUT}/stash" 0 "\\[의존 · 설치\\] 통과  의존 1" "진단: 오류 0")

# 깨진 패키지
execute_process(COMMAND "${SBX}" build "${OUT}/good" --out "${OUT}/good.sbpkg" RESULT_VARIABLE rc OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sbx build 실패\n${out}${err}")
endif()
doctor("${OUT}/good.sbpkg" 0 "\\[패키지\\] 통과  패키지 sandbox\\.rabbit_guard" "진단: 오류 0")
file(COPY_FILE "${OUT}/good.sbpkg" "${OUT}/bad.sbpkg")
file(APPEND "${OUT}/bad.sbpkg" "x")
doctor("${OUT}/bad.sbpkg" 1 "\\[패키지\\] 오류" "다시 받거나")

# 프로젝트가 아님 · 없음
file(MAKE_DIRECTORY "${OUT}/empty")
doctor("${OUT}/empty" 1 "프로젝트도 패키지도 아니다")
doctor("${OUT}/nothing" 1 "\\[대상\\] 오류")

file(REMOVE_RECURSE "${OUT}")
