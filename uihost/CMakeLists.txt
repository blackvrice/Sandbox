# SandboxUiHost — GraphicsUI 통합 계층 (UI-R1, ADR-0174 · docs/graphics-ui-refactor/ARCHITECTURE.md): GraphicsUI Core 의 트리 ·
# 상태 · 입력을 소유하는 응용 호스트 · PlatformEvent → GraphicsUI 입력 · 글자 측정 계약. 화면 (Studio · 게임 UI 어댑터) 은 이것을 쓴다.
# 의존: Foundation · Platform (common 의 이벤트 · 키 타입만) · GraphicsUI::Core. GPU · Render · ImGui · Core (ECS) 를 모른다 —
# tools/check_includes.py · 루트의 sbx_forbid_link 가 검사한다. Server 는 링크하지 않는다.
add_library(SandboxUiHost STATIC
    UiHost.hpp
    UiHost.cpp
    InputRouting.hpp
    InputRouting.cpp
    DocumentView.hpp
    DocumentView.cpp
)
add_library(Sandbox::UiHost ALIAS SandboxUiHost)

target_link_libraries(SandboxUiHost
    PUBLIC
        SandboxFoundation
        SandboxPlatform
        GraphicsUI::Core
    PRIVATE
        sbx_warnings)
