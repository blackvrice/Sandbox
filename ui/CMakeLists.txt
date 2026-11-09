# SandboxGameUI — 게임 UI (HUD · 메뉴 · 버튼) 문서 모델 · 검사 (v0.7 M3-T1, ADR-0079 · docs/20-CREATOR-ALPHA.md 2장).
# 의존: Foundation 만 (+ nlohmann/json). core · render · ImGui 를 모른다 — tools/check_includes.py 가 검사한다.
# 배치 ui/layout (v0.7 M3-T2) · 그리기 명령 ui/render · 포인터 라우팅 ui/input (M3-T3).
# 바인딩 평가 ui/binding (M3-T4). 문자열 표 · 로케일 대체 ui/strings (M4-T1).
add_library(SandboxGameUI STATIC
    model/UiDocument.hpp
    model/UiDocument.cpp
    layout/UiLayout.hpp
    layout/UiLayout.cpp
    render/UiDraw.hpp
    render/UiDraw.cpp
    input/UiInputRouter.hpp
    input/UiInputRouter.cpp
    binding/UiBinding.hpp
    binding/UiBinding.cpp
    strings/UiStrings.hpp
    strings/UiStrings.cpp
)
add_library(Sandbox::GameUI ALIAS SandboxGameUI)

target_link_libraries(SandboxGameUI
    PUBLIC
        SandboxFoundation
    PRIVATE
        sbx_nlohmann_json
        sbx_warnings)
