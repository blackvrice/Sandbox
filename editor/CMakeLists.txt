# SandboxEditorServices — UI 를 모르는 Studio 서비스 (v0.8 ST-06-1 — docs/25-STUDIO-ARCHITECTURE.md 2 · 3장, ADR-0089 3 · ADR-0108).
# Core 만 (+ Foundation · Script) — render · platform · ImGui · Network 를 모른다 (루트의 링크 경계 · tools/check_includes 가 검사한다).
add_library(SandboxEditorServices STATIC
    services/CommandService.hpp
    services/CommandService.cpp
    services/UndoService.hpp
    services/UndoService.cpp
    services/HierarchyService.hpp
    services/HierarchyService.cpp
    services/DataForm.hpp
    services/DataForm.cpp
    services/DocumentService.hpp
    services/DocumentService.cpp
    services/ProjectService.hpp
    services/ProjectService.cpp
    services/ProjectFiles.hpp
    services/ProjectFiles.cpp
    services/BuildService.hpp
    services/BuildService.cpp
    services/AutosaveService.hpp
    services/AutosaveService.cpp
    services/StudioSettings.hpp
    services/StudioSettings.cpp
    services/LayoutService.hpp
    services/LayoutService.cpp
    services/TestService.hpp
    services/TestService.cpp
    services/ScriptAssist.hpp
    services/ScriptAssist.cpp
    services/ScriptDebugService.hpp
    services/ScriptDebugService.cpp
    services/UiDesignService.hpp
    services/UiDesignService.cpp
    services/ProjectHome.hpp
    services/ProjectHome.cpp
    services/StudioTokens.hpp
    services/StudioTokens.cpp
)
add_library(Sandbox::EditorServices ALIAS SandboxEditorServices)
target_link_libraries(SandboxEditorServices PUBLIC SandboxCore PRIVATE sbx_warnings)

# SandboxStudioGui — GraphicsUI 로 그리는 Studio 화면 (editor/gui — UI-R2-01, ADR-0175). Editor Services (데이터 · 행동) ·
# uihost (GraphicsUI 호스트 · 입력) 위. ImGui · Render · 나머지 editor/ (ImGui 패널) 를 모른다 — check_includes · sbx_forbid_link.
add_library(SandboxStudioGui STATIC
    gui/StudioHomeScreen.hpp
    gui/StudioHomeScreen.cpp
    gui/StudioSettingsScreen.hpp
    gui/StudioSettingsScreen.cpp
    gui/PanelKit.hpp
    gui/TextField.hpp
    gui/TextField.cpp
    gui/DockWorkspace.hpp
    gui/DockWorkspace.cpp
    gui/StudioPanels.hpp
    gui/StudioPanels.cpp
    gui/StudioWorkspace.hpp
    gui/HierarchyPanel.hpp
    gui/HierarchyPanel.cpp
    gui/InspectorPanel.hpp
    gui/InspectorPanel.cpp
    gui/ContentPanel.hpp
    gui/ContentPanel.cpp
    gui/RunPanels.hpp
    gui/RunPanels.cpp
    gui/ToolPanel.hpp
    gui/ToolPanel.cpp
    gui/WorldPanel.hpp
    gui/WorldPanel.cpp
    gui/StudioWorkspace.cpp
)
add_library(Sandbox::StudioGui ALIAS SandboxStudioGui)
target_link_libraries(SandboxStudioGui PUBLIC SandboxEditorServices SandboxUiHost PRIVATE sbx_warnings)
# GXML · GCSS 화면 → 빌드 때 C++ (UI-R2-02, ADR-0175 — cmake/SbxGxml.cmake). Python ≥ 3.11 이 없으면 빠진다 (create → Unsupported)
sbx_gxml_core(SandboxStudioGui NAME studio_settings
    MARKUP "${CMAKE_CURRENT_SOURCE_DIR}/gui/gxml/studio_settings.gxml"
    STYLE "${CMAKE_CURRENT_SOURCE_DIR}/gui/gxml/studio_settings.gcss"
    MODEL "${CMAKE_CURRENT_SOURCE_DIR}/gui/gxml/studio_settings.model.json"
    THEMES) # 테마마다 변형 — 색은 editor/services/studio_tokens.json 의 토큰 (UI-R2-03, ADR-0176)

# SandboxEditor — 툴 · 선택 · 패널 · 편집 명령 빌더 (Phase 12, docs/10-EDITOR.md, ADR-0028).
# Core + Render 위. Network 를 모른다 (ICommandSink · IEditorHost 로 분리 — 경계 규칙이 검사한다).
add_library(SandboxEditor STATIC
    SpriteDrop.hpp
    PrefabFromEntity.hpp
    PrefabFromEntity.cpp
    SpriteDrop.cpp
    ui/UiDesignerPanel.hpp
    ui/UiDesignerPanel.cpp
    ui/AssetBrowserPanel.hpp
    ui/AssetBrowserPanel.cpp
    ui/ProjectExplorerPanel.hpp
    ui/ProjectExplorerPanel.cpp
    EditorHost.hpp
    Editor.hpp
    Editor.cpp
    Undo.hpp
    Undo.cpp
    ContentItems.hpp
    ContentItems.cpp
    PackGroups.hpp
    PackGroups.cpp
    ui/EditorPanel.hpp
    ui/EditorPanel.cpp
    ui/InspectorPanel.hpp
    ui/InspectorPanel.cpp
    ui/ContentPanel.hpp
    ui/ContentPanel.cpp
    ui/ScriptEditor.hpp
    ui/ScriptEditor.cpp
    ui/ScriptDebugPanel.hpp
    ui/ScriptDebugPanel.cpp
    ui/TriggerForm.hpp
    ui/TriggerForm.cpp
    ui/TriggerCanvas.hpp
    ui/TriggerCanvas.cpp
    ui/BuildPanel.hpp
    ui/BuildPanel.cpp
    ui/LayoutPanel.hpp
    ui/LayoutPanel.cpp
    ui/TestPanel.hpp
    ui/TestPanel.cpp
    ui/StudioTheme.hpp
    ui/StudioTheme.cpp
    ui/StudioWidgets.hpp
    ui/StudioWidgets.cpp
    ui/ConsolePanel.hpp
    ui/ConsolePanel.cpp
    ui/CommandPalette.hpp
    ui/CommandPalette.cpp
)
target_link_libraries(SandboxEditor PUBLIC SandboxCore SandboxRender SandboxEditorServices PRIVATE sbx_imgui sbx_warnings)
