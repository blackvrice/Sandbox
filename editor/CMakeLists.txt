# SandboxEditor — 툴 · 선택 · 패널 · 편집 명령 빌더 (Phase 12, docs/10-EDITOR.md, ADR-0028).
# Core + Render 위. Network 를 모른다 (ICommandSink · IEditorHost 로 분리 — 경계 규칙이 검사한다).
add_library(SandboxEditor STATIC
    EditorHost.hpp
    Editor.hpp
    Editor.cpp
    ui/EditorPanel.hpp
    ui/EditorPanel.cpp
)
target_link_libraries(SandboxEditor PUBLIC SandboxCore SandboxRender PRIVATE sbx_imgui sbx_warnings)
