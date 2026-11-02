# sbx test <프로젝트> — 검사 + 자동 플레이 (playthrough.json) 를 돌려 종료 코드 0 과 출력의 줄들을 본다 (v0.5 M8-T9).
#   cmake -DSBX=<sbx> -DPROJECT=<프로젝트 폴더> "-DMATCHES=<정규식>;<정규식>…" -P sbx_test_sample.cmake
# 출력은 UTF-8 로 읽는다 (콘솔 코드 페이지가 UTF-8 이 아니어도 — execute_process 의 기본 AUTO 는 그 코드 페이지로 바꾼다).
foreach(v SBX PROJECT)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
execute_process(
    COMMAND "${SBX}" test "${PROJECT}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    ENCODING UTF-8
    TIMEOUT 300)
set(all "${out}${err}")
message("${all}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sbx test 종료 코드 ${rc}")
endif()
foreach(m IN LISTS MATCHES)
    if(NOT all MATCHES "${m}")
        message(FATAL_ERROR "출력에 없다: ${m}")
    endif()
endforeach()
