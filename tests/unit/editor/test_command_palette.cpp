// 명령 팔레트 · 테마 (v0.8 ST-06-2 — editor/ui/CommandPalette · StudioTheme, ADR-0122): 단축키 글자 → ImGui 키 조합,
// 테마 이름 왕복, 세 테마의 대비 (WCAG 상대 휘도 — 본문 7:1 · 보조 · 강조 위 글자 4.5:1 이상), 설정 파일의 theme 키
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "editor/ui/CommandPalette.hpp"
#include "editor/ui/StudioTheme.hpp"

using namespace sbx;
using namespace sbx::editor;

namespace {

double channel(float c) {
    return c <= 0.04045f ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const ImVec4& c) {
    return 0.2126 * channel(c.x) + 0.7152 * channel(c.y) + 0.0722 * channel(c.z);
}

double contrast(const ImVec4& a, const ImVec4& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("command palette (v0.8 ST-06-2): 단축키 글자 → 키 조합") {
        CHECK(shortcutChord("Ctrl+Shift+P") == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P));
        CHECK(shortcutChord("Ctrl+N") == (ImGuiMod_Ctrl | ImGuiKey_N));
        CHECK(shortcutChord("F5") == ImGuiKeyChord(ImGuiKey_F5));
        CHECK(shortcutChord("Shift+F5") == (ImGuiMod_Shift | ImGuiKey_F5));
        CHECK(shortcutChord("F12") == ImGuiKeyChord(ImGuiKey_F12));
        CHECK(shortcutChord("Alt+3") == (ImGuiMod_Alt | ImGuiKey_3));
        CHECK(shortcutChord("Escape") == ImGuiKeyChord(ImGuiKey_Escape));
        CHECK_FALSE(shortcutChord("").has_value());
        CHECK_FALSE(shortcutChord("Ctrl+").has_value());
        CHECK_FALSE(shortcutChord("Ctrl+Pigeon").has_value());
        CHECK_FALSE(shortcutChord("F13").has_value());
        CHECK_FALSE(shortcutChord("Hyper+A").has_value());
    }

    TEST_CASE("theme (v0.8 ST-06-2): 이름 왕복 · 대비") {
        using theme::Theme;
        for (const Theme t : {Theme::Dark, Theme::Light, Theme::HighContrast}) {
            CHECK(theme::themeFromName(theme::themeName(t)) == t);
            const theme::Tokens& k = theme::tokensOf(t);
            CAPTURE(theme::themeName(t));
            CHECK(contrast(k.text, k.background) >= 7.0);
            CHECK(contrast(k.text, k.surface) >= 7.0);
            CHECK(contrast(k.muted, k.surface) >= 4.5);
            CHECK(contrast(k.accentText, k.accent) >= 4.5);
            CHECK(contrast(k.error, k.surface) >= 4.5);
            CHECK(contrast(k.warning, k.surface) >= 4.5);
            CHECK(contrast(k.success, k.surface) >= 4.5);
        }
        // 고대비는 본문 · 강조가 7:1 이상 (AAA)
        const theme::Tokens& hc = theme::tokensOf(Theme::HighContrast);
        CHECK(contrast(hc.muted, hc.surface) >= 7.0);
        CHECK(contrast(hc.accent, hc.background) >= 7.0);
        CHECK_FALSE(theme::themeFromName("solarized").has_value());
        CHECK_FALSE(theme::themeFromName("").has_value());

        // 적용하면 tokens() 가 그 테마 — 창 바닥색도
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGuiStyle style;
        theme::applyStudioTheme(style, theme::Density::Comfortable, 1.f, Theme::Light);
        CHECK(theme::tokens().background.x == doctest::Approx(theme::tokensOf(Theme::Light).background.x));
        CHECK(style.Colors[ImGuiCol_Text].x == doctest::Approx(theme::tokensOf(Theme::Light).text.x));
        theme::applyStudioTheme(style, theme::Density::Comfortable, 1.f, Theme::HighContrast);
        CHECK(style.FrameBorderSize >= 1.f);
        theme::applyStudioTheme(style, theme::Density::Comfortable, 1.f);
        CHECK(theme::tokens().background.x == doctest::Approx(theme::tokensOf(Theme::Dark).background.x));
        ImGui::DestroyContext(ctx);
    }
}
