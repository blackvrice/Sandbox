# 배포 묶음 (v0.5 M1-T4 — docs/20-CREATOR-ALPHA.md 7.7 · docs/15-BUILD.md). 빌드한 실행 파일과 데이터 폴더를 한 폴더에
# 모아 zip 둘 (실행 · 기호) 과 SHA256SUMS.txt 를 만든다. 실행 파일은 옆의 content/ · assets/ 를 먼저 찾는다 (io::dataDir).
#
#   sandbox-<버전>-<플랫폼>/                 (zip 안의 폴더 — 풀면 바로 실행)
#     SandboxClient(.exe) SandboxServer(.exe) sbx(.exe)
#     content/ assets/ samples/ sdk/ docs/ README.md
#     BUILD.txt                              버전 · 커밋 · 툴체인 · 구성
#   sandbox-<버전>-<플랫폼>-symbols.zip      PDB (Windows — 충돌 보고 콜스택의 기호 · 덤프 열기, M1-T2)
#   SHA256SUMS.txt                           "<sha256>  <파일>" (sha256sum -c 형식)
#
#   cmake -DSOURCE=<저장소> -DOUT=<폴더> -DVERSION=<x.y.z> -DPLATFORM=<windows-x64 …> -DCONFIG=<구성>
#         -DCOMMIT=<커밋> -DTOOLCHAIN=<…> -DSERVER=<exe> -DSBX=<exe> [-DCLIENT=<exe>] -P make_release.cmake
# 빌드에서는 타깃 sbx_release (tools/release/CMakeLists.txt) 가 부른다.
foreach(v SOURCE OUT VERSION PLATFORM SERVER SBX)
    if(NOT DEFINED ${v} OR "${${v}}" STREQUAL "")
        message(FATAL_ERROR "${v} 가 필요합니다")
    endif()
endforeach()

set(name "sandbox-${VERSION}-${PLATFORM}")
set(stage "${OUT}/${name}")
set(symbols "${OUT}/${name}-symbols")
file(REMOVE_RECURSE "${stage}" "${symbols}" "${OUT}/${name}.zip" "${OUT}/${name}-symbols.zip" "${OUT}/SHA256SUMS.txt")
file(MAKE_DIRECTORY "${stage}" "${symbols}")

# 실행 파일 (+ 옆의 PDB 는 기호 묶음으로)
set(exes "${SERVER}" "${SBX}")
if(DEFINED CLIENT AND NOT CLIENT STREQUAL "")
    list(PREPEND exes "${CLIENT}")
endif()
set(haveSymbols FALSE)
foreach(exe IN LISTS exes)
    if(NOT EXISTS "${exe}")
        message(FATAL_ERROR "실행 파일이 없다: ${exe}")
    endif()
    file(COPY "${exe}" DESTINATION "${stage}")
    get_filename_component(dir "${exe}" DIRECTORY)
    get_filename_component(stem "${exe}" NAME_WE)
    if(EXISTS "${dir}/${stem}.pdb")
        file(COPY "${dir}/${stem}.pdb" DESTINATION "${symbols}")
        set(haveSymbols TRUE)
    endif()
endforeach()

# 데이터 · 문서 — 설치한 패키지 (.sandbox — 잠금에서 다시 만든다) · 빌드 산출물 · 세이브는 넣지 않는다
foreach(d content assets samples sdk docs)
    file(COPY "${SOURCE}/${d}" DESTINATION "${stage}"
         PATTERN ".sandbox" EXCLUDE PATTERN "build" EXCLUDE PATTERN "*-saves" EXCLUDE)
endforeach()
file(COPY "${SOURCE}/README.md" DESTINATION "${stage}")
string(TIMESTAMP built "%Y-%m-%dT%H:%M:%SZ" UTC)
file(WRITE "${stage}/BUILD.txt"
    "Sandbox ${VERSION} (${PLATFORM})\n"
    "commit    ${COMMIT}\n"
    "toolchain ${TOOLCHAIN}\n"
    "config    ${CONFIG}\n"
    "built     ${built}\n"
    "\n"
    "실행: SandboxClient --world samples/rabbit_guard  ·  SandboxServer --world samples/rabbit_guard\n"
    "진단: sbx doctor samples/rabbit_guard  ·  충돌 보고: sbx report\n"
    "기호: ${name}-symbols.zip 의 .pdb 를 실행 파일 옆에 두면 충돌 보고의 콜스택에 함수 이름 · 줄이 나온다\n"
    "지원: Windows 10 · 11 x64 (D3D12) — 그 밖은 검증 안 됨 (docs/20-CREATOR-ALPHA.md 7.8)\n")

# zip (cmake -E tar --format=zip — 세 OS 같은 방법)
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${name}.zip" --format=zip "${name}"
                WORKING_DIRECTORY "${OUT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "zip 실패 (${rc})")
endif()
set(outputs "${name}.zip")
if(haveSymbols)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${name}-symbols.zip" --format=zip "${name}-symbols"
                    WORKING_DIRECTORY "${OUT}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "기호 zip 실패 (${rc})")
    endif()
    list(APPEND outputs "${name}-symbols.zip")
endif()
file(REMOVE_RECURSE "${symbols}")

set(sums "")
foreach(f IN LISTS outputs)
    file(SHA256 "${OUT}/${f}" h)
    file(SIZE "${OUT}/${f}" size)
    string(APPEND sums "${h}  ${f}\n")
    message(STATUS "배포 묶음: ${OUT}/${f} (${size} 바이트, sha256 ${h})")
endforeach()
file(WRITE "${OUT}/SHA256SUMS.txt" "${sums}")
message(STATUS "SHA256SUMS.txt: ${OUT}/SHA256SUMS.txt")
