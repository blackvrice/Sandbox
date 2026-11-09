# SandboxEditorServices — UI 를 모르는 Studio 서비스 (v0.8 ST-06-1 — docs/25-STUDIO-ARCHITECTURE.md 2 · 3장, ADR-0089 3 · ADR-0108).
# Core 만 (+ Foundation · Script) — render · platform · ImGui · Network 를 모른다 (루트의 링크 경계 · tools/check_includes 가 검사한다).
add_library(SandboxEditorServices STATIC
    services/CommandService.hpp
    services/CommandService.cpp
    services/UndoService.hpp
    services/UndoService.cpp
    services/DocumentService.hpp
    services/DocumentService.cpp
)
add_library(Sandbox::EditorServices ALIAS SandboxEditorServices)
target_link_libraries(SandboxEditorServices PUBLIC SandboxCore PRIVATE sbx_warnings)

# SandboxEditor — 툴 · 선택 · 패널 · 편집 명령 빌더 (Phase 12, docs/10-EDITOR.md, ADR-0028).
# Core + Render 위. Network 를 모른다 (ICommandSink · IEditorHost 로 분리 — 경계 규칙이 검사한다).
add_library(SandboxEditor STATIC
    EditorHost.hpp
    Editor.hpp
    Editor.cpp
    Undo.hpp
    Undo.cpp
    PackGroups.hpp
    PackGroups.cpp
    ui/EditorPanel.hpp
    ui/EditorPanel.cpp
    ui/InspectorPanel.hpp
    ui/InspectorPanel.cpp
    ui/ContentPanel.hpp
    ui/ContentPanel.cpp
    ui/TriggerForm.hpp
    ui/TriggerForm.cpp
)
target_link_libraries(SandboxEditor PUBLIC SandboxCore SandboxRender SandboxEditorServices PRIVATE sbx_imgui sbx_warnings)
