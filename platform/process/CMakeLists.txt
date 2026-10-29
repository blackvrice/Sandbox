# SandboxProcess — 셸 없이 프로세스 띄우기 (v0.5 M1-T4 에 tools/sbx 에서 옮김, ADR-0084). 의존 Foundation 만 — sbx (git) 와
# 클라이언트 (curl — 릴리스 피드) 가 함께 쓴다. OS 헤더는 이 폴더의 .cpp 에만 (platform/common 의 규칙 밖 — crash 와 같다).
add_library(SandboxProcess STATIC
    Process.hpp
    Process.cpp
)
add_library(Sandbox::Process ALIAS SandboxProcess)
target_link_libraries(SandboxProcess PUBLIC SandboxFoundation PRIVATE sbx_warnings)
