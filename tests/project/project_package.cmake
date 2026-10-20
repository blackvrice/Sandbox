# 게임 패키지 (v0.2 M5-T2, ADR-0054): sbx new (데모) → sbx build 두 번 (같은 바이트) → sbx check <패키지> → SandboxServer
# --world <패키지> --start-game 이 --ticks 까지, 세이브 폴더는 패키지 옆 → 한 바이트를 고친 패키지는 check · 서버 모두 거절.
#   cmake -DSBX=<sbx> -DSERVER=<SandboxServer> -DOUT=<작업 폴더> -DTICKS=<n> -P project_package.cmake
foreach(v SBX SERVER OUT TICKS)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
set(project "${OUT}/Demo")
execute_process(COMMAND "${SBX}" new "${project}" --id ctest.pkg --from ecosystem_small --mode eco.protect
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sbx new 실패:\n${out}${err}")
endif()
execute_process(COMMAND "${SBX}" build "${project}" --out "${OUT}/demo.sbpkg"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
if(NOT rc EQUAL 0 OR NOT out MATCHES "패키지를 만들었습니다: .*ctest\\.pkg 0\\.1\\.0" OR NOT out MATCHES "digest sha256:[0-9a-f]+")
    message(FATAL_ERROR "sbx build 실패 (종료 코드 ${rc})")
endif()
execute_process(COMMAND "${SBX}" build "${project}" --out "${OUT}/again.sbpkg" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
file(SHA256 "${OUT}/demo.sbpkg" a)
file(SHA256 "${OUT}/again.sbpkg" b)
if(NOT rc EQUAL 0 OR NOT a STREQUAL b)
    message(FATAL_ERROR "같은 프로젝트의 두 빌드가 다르다 (${a} · ${b})")
endif()
execute_process(COMMAND "${SBX}" check "${OUT}/demo.sbpkg" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
if(NOT rc EQUAL 0 OR NOT out MATCHES "패키지 ctest\\.pkg 0\\.1\\.0" OR NOT out MATCHES "프로젝트 검사: 오류 0, 경고 0")
    message(FATAL_ERROR "패키지 검사 실패 (종료 코드 ${rc})")
endif()
execute_process(COMMAND "${SERVER}" --world "${OUT}/demo.sbpkg" --port 0 --start-game --ticks ${TICKS} --exit --log-level info
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
message("${out}${err}")
if(NOT rc EQUAL 0 OR NOT "${out}${err}" MATCHES "게임 패키지 digest sha256:" OR NOT "${out}${err}" MATCHES "demo-saves"
   OR NOT out MATCHES "tick ${TICKS} hash 0x")
    message(FATAL_ERROR "서버가 패키지로 게임을 돌리지 못했다 (종료 코드 ${rc})")
endif()
# 고친 패키지: 복사본 끝에 한 바이트를 덧붙인다 (CMake 는 바이트를 바꿔 쓰기 어렵다 — 내용 한 바이트 바꾸기는 단위 시험
# test_game_package 가 본다). 길이가 index 와 달라 check · 서버 모두 거절해야 한다
file(COPY_FILE "${OUT}/demo.sbpkg" "${OUT}/bad.sbpkg")
file(APPEND "${OUT}/bad.sbpkg" "x")
execute_process(COMMAND "${SBX}" check "${OUT}/bad.sbpkg" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
if(NOT rc EQUAL 1 OR NOT out MATCHES "패키지 검사: 실패")
    message(FATAL_ERROR "고친 패키지를 sbx check 가 통과시켰다 (종료 코드 ${rc})")
endif()
execute_process(COMMAND "${SERVER}" --world "${OUT}/bad.sbpkg" --port 0 --ticks 5 --exit
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
if(rc EQUAL 0)
    message(FATAL_ERROR "고친 패키지로 서버가 시작했다:
${out}${err}")
endif()
file(REMOVE_RECURSE "${OUT}")
