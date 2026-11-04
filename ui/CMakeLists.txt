# SandboxGameUI — 게임 UI (HUD · 메뉴 · 버튼) 문서 모델 · 검사 (v0.7 M3-T1, ADR-0079 · docs/20-CREATOR-ALPHA.md 2장).
# 의존: Foundation 만 (+ nlohmann/json). core · render · ImGui 를 모른다 — tools/check_includes.py 가 검사한다.
# 배치 (ui/layout) · 바인딩 (ui/binding) · 입력 (ui/input) · 문자열 (ui/strings) 은 M3-T2 ~ M4 [계획]
add_library(SandboxGameUI STATIC
    model/UiDocument.hpp
    model/UiDocument.cpp
)
add_library(Sandbox::GameUI ALIAS SandboxGameUI)

target_link_libraries(SandboxGameUI
    PUBLIC
        SandboxFoundation
    PRIVATE
        sbx_nlohmann_json
        sbx_warnings)
