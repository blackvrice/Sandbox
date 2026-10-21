# SandboxScript — SandboxScript 언어 컴파일러 · VM · Host API 표 (v0.3, ADR-0056 · 0057).
# 의존: Foundation 만. ECS · 그래픽 · OS 를 모른다 (docs/01-ARCHITECTURE.md 2장). 바인딩은 core/script [계획 M2].
add_library(SandboxScript STATIC
    lang/Diagnostic.hpp
    lang/Diagnostic.cpp
    lang/Token.hpp
    lang/Lexer.hpp
    lang/Lexer.cpp
    lang/Ast.hpp
    lang/Ast.cpp
    lang/Parser.hpp
    lang/Parser.cpp
    lang/Types.hpp
    lang/Types.cpp
    lang/Checker.hpp
    lang/Checker.cpp
    api/HostApi.hpp
    api/HostApi.cpp
)
add_library(Sandbox::Script ALIAS SandboxScript)

target_link_libraries(SandboxScript
    PUBLIC
        SandboxFoundation
        sbx_simulation_flags      # 숫자 리터럴 · (M2) VM 의 부동소수 규칙 — 04 4.6
    PRIVATE
        sbx_warnings
        sbx_strict_conversions)
