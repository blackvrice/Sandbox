#pragma once
// 폰트 대체 (v0.7 M4-T2 (2) — ADR-0083 4, docs/20-CREATOR-ALPHA.md 5장). ImGui 를 모른다 — 사슬 고르기 · 글자 덮기만
// (ImGuiLayer 가 사슬의 파일을 한 ImFont 에 합치고, 게임 UI 그리기가 GlyphCoverage 로 글자를 덮는다).
//
//   사슬   --font → client.json "fonts" (차례대로) → OS 기본 묶음 (묶음마다 처음 있는 파일 — 한국어 · 라틴 · 기호).
//          없는 파일은 건너뛴다 (--font · 설정의 것이면 경고), 같은 파일은 한 번. 첫 파일이 바탕, 나머지는 합친다 —
//          글자가 바탕에 없으면 다음 파일에서 (ImGui MergeMode)
//   덮기   사슬 어디에도 없는 글자는 대체 글자 (□ U+25A1 — 그것도 없으면 '?') 로 그리고, 처음 보는 코드 포인트만
//          takeMissing 에 (앱이 경고 한 번). 결과는 코드 포인트마다 캐시한다

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::client {

inline constexpr char32_t kMissingGlyph = 0x25A1; // □
inline constexpr usize kMaxSettingsFonts = 8;     // client.json fonts

using FontGroup = std::vector<std::filesystem::path>; // 서로 대신하는 파일 (처음 있는 것 하나)

// 이 OS 의 기본 묶음 (한국어 · 라틴 · 기호 — 있는지는 보지 않는다)
[[nodiscard]] std::vector<FontGroup> defaultFontGroups();

struct FontChain {
    std::vector<std::filesystem::path> files; // 첫째 = 바탕, 나머지 = 합칠 대체
    std::vector<std::string> warnings;        // 없는 --font · 설정 폰트
};

[[nodiscard]] FontChain buildFontChain(const std::filesystem::path& font,
                                       std::span<const std::filesystem::path> settingsFonts,
                                       std::span<const FontGroup> osGroups,
                                       const std::function<bool(const std::filesystem::path&)>& exists);

class GlyphCoverage {
public:
    // has = 사슬에 그 글자가 있는가 (ImGui — ImFont::IsGlyphInFont). replacement = 없는 글자 자리
    GlyphCoverage(std::function<bool(char32_t)> has, char32_t replacement)
        : m_has(std::move(has)), m_replacement(replacement) {}

    // 없는 글자를 대체 글자로 바꾼 글 (모두 있으면 그대로)
    [[nodiscard]] std::string cover(std::string_view text);
    // 새로 본 없는 코드 포인트 (차례대로) — 꺼내면 비운다
    [[nodiscard]] std::vector<char32_t> takeMissing();
    [[nodiscard]] char32_t replacement() const noexcept { return m_replacement; }

private:
    [[nodiscard]] bool has(char32_t cp);
    std::function<bool(char32_t)> m_has;
    char32_t m_replacement;
    std::unordered_map<char32_t, bool> m_cache;
    std::vector<char32_t> m_newMissing;
};

// "U+25A1 '□'" (경고 문장)
[[nodiscard]] std::string describeCodepoint(char32_t cp);

} // namespace sbx::client
