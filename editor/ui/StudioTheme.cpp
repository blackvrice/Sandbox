#include "editor/ui/StudioTheme.hpp"

namespace sbx::editor::theme {

namespace {

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.f) {
    return {static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f, a};
}

constexpr Tokens kDark{
    rgb(0x15, 0x17, 0x1B), // background
    rgb(0x1D, 0x20, 0x26), // surface
    rgb(0x26, 0x2A, 0x31), // elevated
    rgb(0x32, 0x37, 0x41), // border
    rgb(0xE6, 0xE8, 0xEB), // text
    rgb(0x8B, 0x93, 0xA1), // muted
    rgb(0xF5, 0xB8, 0x3D), // accent
    rgb(0xFF, 0xC9, 0x5C), // accentHover
    rgb(0x15, 0x17, 0x1B), // accentText
    rgb(0xF0, 0xA0, 0x30), // warning
    rgb(0xEF, 0x64, 0x61), // error
    rgb(0x5B, 0xC4, 0x89), // success
};

// 밝음 (v0.8 ST-06-2): 글자/바닥 14.8:1 · 보조/표면 6.1:1 · 강조 (#8A5A00) 위 흰 글자 5.9:1 · 강조 글자/바닥 5.2:1 ·
// 오류 5.6 · 경고 5.0 · 성공 5.3 (표면 위)
constexpr Tokens kLight{
    rgb(0xEE, 0xF0, 0xF3), // background
    rgb(0xFF, 0xFF, 0xFF), // surface
    rgb(0xF4, 0xF5, 0xF7), // elevated
    rgb(0xC9, 0xCE, 0xD6), // border
    rgb(0x1A, 0x1D, 0x23), // text
    rgb(0x5B, 0x63, 0x70), // muted
    rgb(0x8A, 0x5A, 0x00), // accent
    rgb(0xA0, 0x6A, 0x00), // accentHover
    rgb(0xFF, 0xFF, 0xFF), // accentText
    rgb(0xA3, 0x5F, 0x00), // warning
    rgb(0xC4, 0x2B, 0x2B), // error
    rgb(0x1E, 0x7A, 0x46), // success
};

// 고대비 (v0.8 ST-06-2 · 28 G10): 검은 바탕 · 흰 글자 21:1 · 흰 테두리 · 노랑 강조/바닥 14.7:1 (강조 위 검은
// 글자 14.7:1) · 오류 7.1 · 경고 10.8 · 성공 15.2
constexpr Tokens kHighContrast{
    rgb(0x00, 0x00, 0x00), // background
    rgb(0x0A, 0x0A, 0x0A), // surface
    rgb(0x1A, 0x1A, 0x1A), // elevated
    rgb(0xFF, 0xFF, 0xFF), // border
    rgb(0xFF, 0xFF, 0xFF), // text
    rgb(0xD0, 0xD0, 0xD0), // muted
    rgb(0xFF, 0xD4, 0x00), // accent
    rgb(0xFF, 0xE6, 0x5C), // accentHover
    rgb(0x00, 0x00, 0x00), // accentText
    rgb(0xFF, 0xB0, 0x00), // warning
    rgb(0xFF, 0x6B, 0x6B), // error
    rgb(0x5C, 0xFF, 0x8F), // success
};

Theme g_theme = Theme::Dark; // 지금 테마 (applyStudioTheme — UI 스레드)

ImVec4 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

} // namespace

const Tokens& tokensOf(Theme t) noexcept {
    switch (t) {
    case Theme::Light:
        return kLight;
    case Theme::HighContrast:
        return kHighContrast;
    case Theme::Dark:
        break;
    }
    return kDark;
}

const Tokens& tokens() noexcept {
    return tokensOf(g_theme);
}

std::string_view themeName(Theme t) noexcept {
    switch (t) {
    case Theme::Light:
        return "light";
    case Theme::HighContrast:
        return "high-contrast";
    case Theme::Dark:
        break;
    }
    return "dark";
}

std::optional<Theme> themeFromName(std::string_view name) noexcept {
    for (const Theme t : {Theme::Dark, Theme::Light, Theme::HighContrast}) {
        if (themeName(t) == name) {
            return t;
        }
    }
    return std::nullopt;
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
    const bool light = theme == Theme::Light;
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
    c[ImGuiCol_FrameBgActive] = light      ? rgb(0xDC, 0xE0, 0xE6)
                                : contrast ? rgb(0x33, 0x33, 0x33)
                                           : rgb(0x3B, 0x41, 0x4C);
    c[ImGuiCol_TitleBg] = t.background;
    c[ImGuiCol_TitleBgActive] = t.surface;
    c[ImGuiCol_TitleBgCollapsed] = t.background;
    c[ImGuiCol_MenuBarBg] = t.background;
    c[ImGuiCol_ScrollbarBg] = withAlpha(t.background, 0.f);
    c[ImGuiCol_ScrollbarGrab] = t.border;
    c[ImGuiCol_ScrollbarGrabHovered] = light ? rgb(0xA9, 0xB0, 0xBA) : rgb(0x45, 0x4B, 0x57);
    c[ImGuiCol_ScrollbarGrabActive] = t.muted;
    c[ImGuiCol_CheckMark] = t.accent;
    c[ImGuiCol_SliderGrab] = t.accent;
    c[ImGuiCol_SliderGrabActive] = t.accentHover;
    c[ImGuiCol_Button] = t.elevated;
    c[ImGuiCol_ButtonHovered] = light      ? rgb(0xE3, 0xE6, 0xEB)
                                : contrast ? rgb(0x33, 0x33, 0x33)
                                           : rgb(0x34, 0x39, 0x43);
    c[ImGuiCol_ButtonActive] = light ? rgb(0xD5, 0xDA, 0xE1) : contrast ? rgb(0x4D, 0x4D, 0x4D) : rgb(0x40, 0x46, 0x52);
    // 묶음 머리 · 고른 줄: 중립 (강조 노랑을 옅게 쓰면 어두운 바탕 위에서 갈색으로 보인다 — 창 확인 2026-11-10)
    c[ImGuiCol_Header] = light ? rgb(0xE1, 0xE5, 0xEB) : contrast ? rgb(0x33, 0x33, 0x33) : rgb(0x34, 0x3A, 0x45);
    c[ImGuiCol_HeaderHovered] = light      ? rgb(0xD6, 0xDB, 0xE2)
                                : contrast ? rgb(0x4D, 0x4D, 0x4D)
                                           : rgb(0x3D, 0x44, 0x51);
    c[ImGuiCol_HeaderActive] = light ? rgb(0xCB, 0xD1, 0xDA) : contrast ? rgb(0x66, 0x66, 0x66) : rgb(0x47, 0x4F, 0x5D);
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
