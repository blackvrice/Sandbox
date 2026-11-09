// 폰트 대체 (v0.7 M4-T2 (2) — apps/client/ui/FontFallback · ImGuiLayer): 사슬 고르기 (--font → client.json fonts → OS
// 묶음의 처음 있는 파일, 없는 파일 경고 · 같은 파일 한 번), 글자 덮기 (없는 글자 → □ · '?', 처음 보는 것만 경고 · 캐시
// · 제어 글자 그대로), 실제 ImGui 사슬 (OS 폰트가 있으면 합친 이름 · 기본 라틴 · 한글)
#include <doctest/doctest.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include "apps/client/ui/FontFallback.hpp"
#include "apps/client/ui/GameUiImGui.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
namespace fs = std::filesystem;

TEST_SUITE("client") {
    TEST_CASE("font fallback: 사슬 — --font → 설정 폰트 → OS 묶음 (처음 있는 것), 없는 파일 경고 · 같은 파일 한 번") {
        const std::set<fs::path> present{"user.ttf", "a.ttf", "ko2.ttf", "latin.ttf", "sym.ttf"};
        const auto exists = [&](const fs::path& p) { return present.contains(p); };
        const std::vector<fs::path> settings{"a.ttf", "gone.ttf", "latin.ttf"};
        const std::vector<FontGroup> groups{{"ko1.ttf", "ko2.ttf"}, {"latin.ttf"}, {"sym.ttf"}, {"none.ttf"}};
        const FontChain c = buildFontChain("user.ttf", settings, groups, exists);
        CHECK(c.files == std::vector<fs::path>{"user.ttf", "a.ttf", "latin.ttf", "ko2.ttf", "sym.ttf"});
        REQUIRE(c.warnings.size() == 1);
        CHECK(c.warnings[0].find("gone.ttf") != std::string::npos);

        const FontChain missing = buildFontChain("nope.ttf", {}, groups, exists);
        CHECK(missing.files == std::vector<fs::path>{"ko2.ttf", "latin.ttf", "sym.ttf"});
        REQUIRE(missing.warnings.size() == 1);
        CHECK(missing.warnings[0].find("nope.ttf") != std::string::npos);
        CHECK(buildFontChain({}, {}, {}, exists).files.empty());

        // 설정 폰트는 8 개까지
        std::vector<fs::path> many(12, "a.ttf");
        many[9] = "user.ttf";
        CHECK(buildFontChain({}, many, {}, exists).files == std::vector<fs::path>{"a.ttf"});
        CHECK_FALSE(defaultFontGroups().empty());
    }

    TEST_CASE(
        "font fallback: 글자 덮기 — 없는 글자는 □ (또는 '?'), 처음 보는 코드 포인트만 경고, 캐시, 제어 글자 그대로") {
        usize asked = 0;
        GlyphCoverage g(
            [&](char32_t c) {
                ++asked;
                return c < 0x3000; // 한글 · 한자는 없다
            },
            kMissingGlyph);
        CHECK(g.cover("abc") == "abc");
        CHECK(g.takeMissing().empty());
        CHECK(g.cover("a가b나") == "a□b□");
        CHECK(g.cover("가가") == "□□");
        const std::vector<char32_t> missing = g.takeMissing();
        CHECK(missing == std::vector<char32_t>{0xAC00, 0xB098});
        CHECK(g.takeMissing().empty());
        const usize before = asked;
        CHECK(g.cover("a가b나") == "a□b□");
        CHECK(asked == before); // 캐시
        CHECK(g.cover("x\ny\t") == "x\ny\t");
        CHECK(describeCodepoint(0xAC00) == "U+AC00 '가'");

        GlyphCoverage ascii([](char32_t c) { return c < 0x80; }, U'?');
        CHECK(ascii.cover("é?") == "??");
        CHECK(ascii.replacement() == U'?');
    }

    TEST_CASE("font fallback: 실제 ImGui 사슬 — OS 폰트가 있으면 합친다 (이름 'a + b'), 라틴 · 한글 · □ 가 사슬에") {
        std::vector<FontGroup> groups = defaultFontGroups();
        std::vector<fs::path> found;
        for (const FontGroup& g : groups) {
            for (const fs::path& p : g) {
                std::error_code ec;
                if (fs::is_regular_file(p, ec)) {
                    found.push_back(p);
                    break;
                }
            }
        }
        HeadlessWindow window;
        ImGuiLayer layer(window);
        PlatformEventQueue q;
        window.pollEvents(q);
        layer.feed(q);
        layer.beginFrame(1.0 / 60.0);
        ImFont* font = ImGui::GetFont();
        REQUIRE(font != nullptr);
        CHECK(font->IsGlyphInFont(static_cast<ImWchar>('A')));
        if (found.size() >= 2) { // 이 기계에 OS 폰트가 둘 이상 — 합쳤다
            CHECK(layer.fontName().find(" + ") != std::string::npos);
            CHECK(font->IsGlyphInFont(static_cast<ImWchar>(0xAC00))); // 가
            MESSAGE("폰트 사슬: ", layer.fontName());
        }
        // 그리기 · 재기가 같은 덮은 글자를 쓴다 — U+E000 (사용자 영역) 은 어느 OS 폰트에도 보통 없다
        GlyphCoverage g([font](char32_t c) { return c <= 0xFFFF && font->IsGlyphInFont(static_cast<ImWchar>(c)); },
                        font->IsGlyphInFont(static_cast<ImWchar>(kMissingGlyph)) ? kMissingGlyph : U'?');
        const ImGuiTextMeasure plain;
        const ImGuiTextMeasure covered(&g);
        CHECK(covered.measure("AB", 16)[0] == plain.measure("AB", 16)[0]);
        CHECK(covered.measure("\xEE\x80\x80", 16)[0] > 0.0f); // 대체 글자의 너비
        (void)layer.endFrame(CursorShape::Arrow);
        layer.acknowledgeTextures();
    }
}
