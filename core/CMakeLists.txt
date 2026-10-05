# SandboxCore — ECS · 시뮬레이션 · 월드 · 콘텐츠 · 명령 · 직렬화 · 리플레이.
# 의존: Foundation 만. 그래픽·OS·네트워크를 모른다 (docs/01-ARCHITECTURE.md 2장).
# Phase 1: 시뮬레이션 상수만. 나머지 모듈은 Phase 2 부터 추가된다 (docs/16-ROADMAP.md).
add_library(SandboxCore STATIC
    simulation/SimConstants.hpp
    simulation/SimConstants.cpp
)
add_library(Sandbox::Core ALIAS SandboxCore)

target_link_libraries(SandboxCore
    PUBLIC  SandboxFoundation
            sbx_simulation_flags      # 헤더 템플릿을 쓰는 소비자에게도 FP 규칙이 걸려야 한다
    PRIVATE sbx_warnings
            sbx_strict_conversions)
