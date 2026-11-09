#include "apps/client/ui/FontFallback.hpp"

#include <algorithm>
#include <format>

#include "foundation/text/Utf8.hpp"

namespace sbx::client {

std::vector<FontGroup> defaultFontGroups() {
#ifdef _WIN32
    // 맑은 고딕 (한국어 + 기본 라틴) · Segoe UI (라틴 확장 · 그리스 · 키릴) · Segoe UI Symbol (기호 · □)
    return {{"C:/Windows/Fonts/malgun.ttf"}, {"C:/Windows/Fonts/segoeui.ttf"}, {"C:/Windows/Fonts/seguisym.ttf"}};
#elif defined(__linux__)
    return {{"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
             "/usr/share/fonts/truetype/nanum/NanumGothic.ttf", "/usr/share/fonts/nanum/NanumGothic.ttf"},
            {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"},
            {"/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
             "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf"}};
#elif defined(__APPLE__)
    return {{"/System/Library/Fonts/AppleSDGothicNeo.ttc", "/Library/Fonts/NanumGothic.ttf",
             "/System/Library/Fonts/Supplemental/AppleGothic.ttf"},
            {"/System/Library/Fonts/Helvetica.ttc"},
            {"/System/Library/Fonts/Apple Symbols.ttf"}};
#else
    return {};
#endif
}

FontChain buildFontChain(const std::filesystem::path& font, std::span<const std::filesystem::path> settingsFonts,
                         std::span<const FontGroup> osGroups,
                         const std::function<bool(const std::filesystem::path&)>& exists) {
    FontChain chain;
    const auto add = [&](const std::filesystem::path& p) {
        if (std::find(chain.files.begin(), chain.files.end(), p) == chain.files.end()) {
            chain.files.push_back(p);
        }
    };
    if (!font.empty()) {
        if (exists(font)) {
            add(font);
        } else {
            chain.warnings.push_back(std::format("UI 폰트 '{}' 이 없다 — 다른 폰트로", font.generic_string()));
        }
    }
    for (usize i = 0; i < settingsFonts.size() && i < kMaxSettingsFonts; ++i) {
        if (exists(settingsFonts[i])) {
            add(settingsFonts[i]);
        } else {
            chain.warnings.push_back(
                std::format("client.json fonts 의 '{}' 이 없다 — 건너뛴다", settingsFonts[i].generic_string()));
        }
    }
    for (const FontGroup& g : osGroups) {
        const auto found = std::find_if(g.begin(), g.end(), exists);
        if (found != g.end()) {
            add(*found);
        }
    }
    return chain;
}

bool GlyphCoverage::has(char32_t cp) {
    if (cp < 0x20 || cp == m_replacement) {
        return true; // 제어 글자는 그리지 않는다 — 덮지 않는다
    }
    const auto it = m_cache.find(cp);
    if (it != m_cache.end()) {
        return it->second;
    }
    const bool found = m_has(cp);
    m_cache.emplace(cp, found);
    if (!found) {
        m_newMissing.push_back(cp);
    }
    return found;
}

std::string GlyphCoverage::cover(std::string_view text) {
    // 대부분은 모두 있다 — 바뀔 때만 새 글을 만든다
    usize pos = 0;
    bool all = true;
    while (pos < text.size()) {
        if (!has(utf8::decodeNext(text, pos))) {
            all = false;
        }
    }
    if (all) {
        return std::string(text);
    }
    std::string out;
    out.reserve(text.size());
    pos = 0;
    while (pos < text.size()) {
        const char32_t cp = utf8::decodeNext(text, pos);
        utf8::append(out, has(cp) ? cp : m_replacement);
    }
    return out;
}

std::vector<char32_t> GlyphCoverage::takeMissing() {
    std::vector<char32_t> out;
    out.swap(m_newMissing);
    return out;
}

std::string describeCodepoint(char32_t cp) {
    std::string glyph;
    utf8::append(glyph, cp);
    return std::format("U+{:04X} '{}'", static_cast<u32>(cp), glyph);
}

} // namespace sbx::client
