# SandboxCrash — 충돌 처리기 (v0.5 M1-T1, docs/20-CREATOR-ALPHA.md 7.1). 의존 Foundation 만 — 서버 (SandboxPlatform 을 링크하지
# 않는다) 와 클라이언트가 함께 쓴다. OS 헤더는 이 폴더의 .cpp 에만 (platform/common 의 규칙 밖).
add_library(SandboxCrash STATIC
    CrashHandler.hpp
    CrashHandlerCommon.hpp
    CrashHandlerWin.cpp
    CrashHandlerPosix.cpp
)
add_library(Sandbox::Crash ALIAS SandboxCrash)
target_link_libraries(SandboxCrash PUBLIC SandboxFoundation PRIVATE sbx_warnings)
if(WIN32)
    target_link_libraries(SandboxCrash PRIVATE dbghelp)
endif()
