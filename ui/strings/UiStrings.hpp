#pragma once
// 문자열 표 · 로케일 대체 (v0.7 M4-T1 — ADR-0083, docs/20-CREATOR-ALPHA.md 5장).
//
// 팩의 presentation/strings/<로케일>.json 하나가 표 하나: {"<팩>.<키>": "텍스트 {0}"}. 키는 그 팩의 이름공간 (다른 팩의
// 키를 덮지 못한다 — U2). 로케일은 BCP 47 꼴 (언어 2 ~ 3 글자 + "-" 뒤 조각 1 ~ 8 글자, 셋까지) 이고 소문자로 맞춰
// 비교한다 ("pt-BR" = "pt-br", OS 의 "ko_KR" 도 "ko-kr").
//
// 찾는 차례 (LocaleChain): 사용자 로케일 → 그 언어 ("en-us" → "en") → 키를 정의한 팩의 기본 글 → 키 그대로 (경고 한
// 번). 팩의 기본 표는 표가 하나면 그것, 여럿이면 "en", 그것도 없으면 이름이 가장 앞인 표 (pack.json 에 새 키를 두지
// 않는다). 기본 표에 그 키가 없으면 키가 있는 표 중 이름이 가장 앞인 것. 인자는 "{0}" ~ "{9}" (없는 인자는 빈 글자).
// 서버는 문자열을 모른다 — 표시 전용, contentHash 밖 (presentation/)

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/model/UiDocument.hpp"

namespace sbx::ui {

inline constexpr usize kMaxStrings = 4096; // 표마다 키 (U7)
inline constexpr usize kMaxLocales = 32;   // 팩마다 표 (로더가 자른다 — U7)

using StringEntries = std::map<std::string, std::string, std::less<>>;

struct StringTableResult {
    StringEntries entries; // 문제가 없는 항목만
    std::vector<UiIssue> issues;
    [[nodiscard]] bool ok() const noexcept;
};

// 로케일 꼴을 맞춘다 (소문자 · "_" → "-", OS 값의 ".UTF-8" · "@…" 꼬리는 뗀다). 꼴이 아니면 (C · POSIX 포함) 빈 글자
[[nodiscard]] std::string normalizeLocale(std::string_view locale);
[[nodiscard]] bool isLocaleCode(std::string_view locale); // presentation/strings 의 파일 이름 (꼬리 없이)
// 사용자 로케일의 찾는 차례 — "pt-br" → {"pt-br", "pt"}, 빈 글자 → {}
[[nodiscard]] std::vector<std::string> localeChain(std::string_view locale);

// 표 하나 (U1 모양 · U2 키 이름공간 · U7 상한)
[[nodiscard]] StringTableResult parseStringTable(std::string_view jsonText, std::string_view packId);

// "{0}" ~ "{9}" 를 args 로 (없는 인자는 빈 글자, 그 밖의 중괄호는 그대로)
[[nodiscard]] std::string formatString(std::string_view pattern, std::span<const std::string> args);

using LocaleTables = std::vector<std::pair<std::string, StringEntries>>; // (로케일, 항목) — 한 팩
[[nodiscard]] std::string defaultLocaleOf(const LocaleTables& tables);   // 위 규칙. 표가 없으면 빈 글자

class StringTable {
public:
    void clear();
    // 팩 순서대로 부른다. 로케일은 맞춘 꼴로 (같은 키 · 로케일이면 뒤가 이긴다 — 이름공간이라 보통 없다)
    void addPack(const LocaleTables& tables);
    void setLocale(std::string_view locale); // 사용자 로케일 (맞춘 꼴로 — 꼴이 아니면 없음 = 기본 글만)
    [[nodiscard]] const std::string& locale() const noexcept { return m_locale; }

    [[nodiscard]] std::optional<std::string_view> find(std::string_view key) const;
    // find, 없으면 키 그대로 (처음 보는 키면 takeMissing 에 남긴다 — 경고 한 번)
    [[nodiscard]] std::string text(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> takeMissing();

    [[nodiscard]] bool contains(std::string_view key) const { return m_entries.contains(key); }
    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }
    [[nodiscard]] std::vector<std::string> locales() const; // 표가 있는 로케일 (이름 순)
    [[nodiscard]] std::set<std::string, std::less<>> keys() const;

private:
    struct Entry {
        std::map<std::string, std::string, std::less<>> byLocale;
        std::string fallback; // 팩의 기본 글
    };
    std::map<std::string, Entry, std::less<>> m_entries;
    std::set<std::string, std::less<>> m_locales;
    std::string m_locale;
    std::vector<std::string> m_chain;
    mutable std::set<std::string, std::less<>> m_missing;
    mutable std::vector<std::string> m_newMissing;
};

// 노드 글자의 바탕 — "@<키>" 는 표의 글 (표가 없으면 키), "@@…" 는 "@…", 그 밖은 그대로
[[nodiscard]] std::string resolveText(std::string_view text, const StringTable* strings);

} // namespace sbx::ui
