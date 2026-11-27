#include "editor/ui/StudioTheme.hpp"

namespace sbx::editor::theme {

namespace {

ImVec4 fromToken(services::Rgba8 c) {
    return {static_cast<float>(c.r) / 255.f, static_cast<float>(c.g) / 255.f, static_cast<float>(c.b) / 255.f,
            static_cast<float>(c.a) / 255.f};
}

// 토큰 표 (editor/services/studio_tokens.json — UI-R2-03) → ImGui 색. 밝음: 글자/바닥 14.8:1 · 보조/표면 6.1:1 · 강조 위 흰 글자 5.9:1,
// 고대비: 흰 글자 21:1 · 노랑 강조/바닥 14.7:1 (29 3장)
Tokens fromStudioColors(const services::StudioColors& c) {
    return {fromToken(c.background), fromToken(c.surface), fromToken(c.elevated), fromToken(c.border),
            fromToken(c.text),       fromToken(c.muted),   fromToken(c.accent),   fromToken(c.accentHover),
            fromToken(c.accentText), fromToken(c.warning), fromToken(c.error),    fromToken(c.success)};
}

Theme g_theme = Theme::Dark; // 지금 테마 (applyStudioTheme — UI 스레드)

ImVec4 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

} // namespace

const Tokens& tokensOf(Theme t) noexcept {
    static const Tokens dark = fromStudioColors(services::studioColors(Theme::Dark));
    static const Tokens light = fromStudioColors(services::studioColors(Theme::Light));
    static const Tokens contrast = fromStudioColors(services::studioColors(Theme::HighContrast));
    switch (t) {
    case Theme::Light:
        return light;
    case Theme::HighContrast:
        return contrast;
    case Theme::Dark:
        break;
    }
    return dark;
}

const Tokens& tokens() noexcept {
    return tokensOf(g_theme);
}

std::string_view themeName(Theme t) noexcept {
    return services::studioThemeName(t);
}

std::optional<Theme> themeFromName(std::string_view name) noexcept {
    return services::studioThemeFromName(name);
}

ImVec4 statusColor(Status s) noexcept {
    const Tokens& t = tokens();
    switch (s) {
    case Status::Muted:
        return t.muted;
    case Status::Success:
        return t.success;
    case Status::Warning:
        return t.warning;
    case Status::Error:
        return t.error;
    case Status::Accent:
        return t.accent;
    case Status::Info:
        break;
    }
    return t.text;
}

ImU32 toU32(const ImVec4& c) noexcept {
    return ImGui::ColorConvertFloat4ToU32(c);
}

float space(Space s, float scale, Density d) noexcept {
    const float unit = d == Density::Compact ? 3.f : 4.f;
    return static_cast<float>(static_cast<u8>(s)) * unit * scale;
}

void applyStudioTheme(ImGuiStyle& style, Density density, float scale, Theme theme) {
    g_theme = theme;
    const Tokens& t = tokens();
    const services::StudioColors& sc = services::studioColors(theme); // 파생 색 (단추 · 머리 · 입력 누름 — 같은 표)
    const bool contrast = theme == Theme::HighContrast;
    const bool compact = density == Density::Compact;
    const float pad = compact ? 6.f : 8.f;
    style.WindowPadding = ImVec2(12.f * scale, 12.f * scale);
    style.FramePadding = ImVec2(pad * scale, (compact ? 4.f : 6.f) * scale);
    style.ItemSpacing = ImVec2(8.f * scale, (compact ? 5.f : 8.f) * scale);
    style.ItemInnerSpacing = ImVec2(6.f * scale, 6.f * scale);
    style.CellPadding = ImVec2(6.f * scale, (compact ? 3.f : 4.f) * scale);
    style.IndentSpacing = 18.f * scale;
    style.ScrollbarSize = 12.f * scale;
    style.GrabMinSize = 10.f * scale;
    style.WindowMinSize = ImVec2(kPanelMinWidth * 0.5f * scale, 64.f * scale);
    style.WindowRounding = 6.f * scale;
    style.ChildRounding = 6.f * scale;
    style.FrameRounding = 4.f * scale;
    style.PopupRounding = 6.f * scale;
    style.ScrollbarRounding = 6.f * scale;
    style.GrabRounding = 4.f * scale;
    style.TabRounding = 4.f * scale;
    style.WindowBorderSize = 1.f;
    style.ChildBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.PopupBorderSize = 1.f;
    style.TabBorderSize = 0.f;
    style.WindowTitleAlign = ImVec2(0.f, 0.5f);
    style.SeparatorTextBorderSize = 2.f * scale;
    style.SeparatorTextPadding = ImVec2(10.f * scale, 4.f * scale);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_TextDisabled] = t.muted;
    c[ImGuiCol_WindowBg] = t.surface;
    c[ImGuiCol_ChildBg] = withAlpha(t.background, 0.f);
    c[ImGuiCol_PopupBg] = t.elevated;
    c[ImGuiCol_Border] = t.border;
    c[ImGuiCol_BorderShadow] = withAlpha(t.background, 0.f);
    c[ImGuiCol_FrameBg] = t.elevated;
    c[ImGuiCol_FrameBgHovered] = withAlpha(t.border, 1.f);
    c[ImGuiCol_FrameBgActive] = fromToken(sc.frameActive);
    c[ImGuiCol_TitleBg] = t.background;
    c[ImGuiCol_TitleBgActive] = t.surface;
    c[ImGuiCol_TitleBgCollapsed] = t.background;
    c[ImGuiCol_MenuBarBg] = t.background;
    c[ImGuiCol_ScrollbarBg] = withAlpha(t.background, 0.f);
    c[ImGuiCol_ScrollbarGrab] = t.border;
    c[ImGuiCol_ScrollbarGrabHovered] = fromToken(sc.scrollHover);
    c[ImGuiCol_ScrollbarGrabActive] = t.muted;
    c[ImGuiCol_CheckMark] = t.accent;
    c[ImGuiCol_SliderGrab] = t.accent;
    c[ImGuiCol_SliderGrabActive] = t.accentHover;
    c[ImGuiCol_Button] = t.elevated;
    c[ImGuiCol_ButtonHovered] = fromToken(sc.controlHover);
    c[ImGuiCol_ButtonActive] = fromToken(sc.controlActive);
    // 묶음 머리 · 고른 줄: 중립 (강조 노랑을 옅게 쓰면 어두운 바탕 위에서 갈색으로 보인다 — 창 확인 2026-11-10)
    c[ImGuiCol_Header] = fromToken(sc.header);
    c[ImGuiCol_HeaderHovered] = fromToken(sc.headerHover);
    c[ImGuiCol_HeaderActive] = fromToken(sc.headerActive);
    if (contrast) { // 고대비: 테두리를 굵게 (색만으로 구분하지 않는다)
        style.FrameBorderSize = 1.f;
        style.WindowBorderSize = 2.f;
    }
    c[ImGuiCol_Separator] = t.border;
    c[ImGuiCol_SeparatorHovered] = withAlpha(t.accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = t.accent;
    c[ImGuiCol_ResizeGrip] = withAlpha(t.border, 0.6f);
    c[ImGuiCol_ResizeGripHovered] = withAlpha(t.accent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = t.accent;
    c[ImGuiCol_InputTextCursor] = t.accent;
    c[ImGuiCol_Tab] = t.background;
    c[ImGuiCol_TabHovered] = t.elevated;
    c[ImGuiCol_TabSelected] = t.surface;
    c[ImGuiCol_TabSelectedOverline] = t.accent;
    c[ImGuiCol_TabDimmed] = t.background;
    c[ImGuiCol_TabDimmedSelected] = t.surface;
    c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(t.accent, 0.4f);
    c[ImGuiCol_DockingPreview] = withAlpha(t.accent, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = t.background;
    c[ImGuiCol_PlotLines] = t.muted;
    c[ImGuiCol_PlotLinesHovered] = t.accent;
    c[ImGuiCol_PlotHistogram] = t.accent;
    c[ImGuiCol_PlotHistogramHovered] = t.accentHover;
    c[ImGuiCol_TableHeaderBg] = t.elevated;
    c[ImGuiCol_TableBorderStrong] = t.border;
    c[ImGuiCol_TableBorderLight] = withAlpha(t.border, 0.6f);
    c[ImGuiCol_TableRowBg] = withAlpha(t.background, 0.f);
    c[ImGuiCol_TableRowBgAlt] = withAlpha(t.elevated, 0.35f);
    c[ImGuiCol_TextLink] = t.accent;
    c[ImGuiCol_TreeLines] = t.border;
    c[ImGuiCol_UnsavedMarker] = t.accent;
    c[ImGuiCol_TextSelectedBg] = withAlpha(t.accent, 0.30f);
    c[ImGuiCol_DragDropTarget] = t.accent;
    c[ImGuiCol_NavCursor] = t.accent;
    c[ImGuiCol_NavWindowingHighlight] = withAlpha(t.text, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = withAlpha(t.background, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = withAlpha(t.background, 0.65f);
}

} // namespace sbx::editor::theme
