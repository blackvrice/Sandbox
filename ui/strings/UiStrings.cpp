#include "ui/strings/UiStrings.hpp"

#include <algorithm>
#include <format>

#include <nlohmann/json.hpp>

namespace sbx::ui {
namespace {

using json = nlohmann::json;
using Severity = UiIssue::Severity;

bool alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

} // namespace

bool StringTableResult::ok() const noexcept {
    return std::none_of(issues.begin(), issues.end(), [](const UiIssue& i) { return i.severity == Severity::Error; });
}

std::string normalizeLocale(std::string_view locale) {
    // OS 값 "ko_KR.UTF-8@euro" → "ko-kr"
    if (const usize cut = locale.find_first_of(".@"); cut != std::string_view::npos) {
        locale = locale.substr(0, cut);
    }
    if (locale.empty() || locale.size() > 35) {
        return {};
    }
    std::string out;
    out.reserve(locale.size());
    for (const char c : locale) {
        if (c == '_' || c == '-') {
            out += '-';
        } else if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else if (alnum(c)) {
            out += c;
        } else {
            return {};
        }
    }
    // 언어 2 ~ 3 소문자 + 조각 (1 ~ 8 글자) 셋까지
    usize part = 0;
    usize start = 0;
    while (start <= out.size()) {
        usize end = out.find('-', start);
        if (end == std::string::npos) {
            end = out.size();
        }
        const std::string_view piece = std::string_view(out).substr(start, end - start);
        const bool letters = std::all_of(piece.begin(), piece.end(), [](char c) { return c >= 'a' && c <= 'z'; });
        const bool ok = part == 0 ? (piece.size() >= 2 && piece.size() <= 3 && letters)
                                  : (!piece.empty() && piece.size() <= 8 && part <= 3);
        if (!ok) {
            return {};
        }
        ++part;
        start = end + 1;
    }
    return out;
}

bool isLocaleCode(std::string_view locale) {
    return locale.find_first_of("._@") == std::string_view::npos && !normalizeLocale(locale).empty();
}

std::vector<std::string> localeChain(std::string_view locale) {
    const std::string full = normalizeLocale(locale);
    if (full.empty()) {
        return {};
    }
    std::vector<std::string> chain{full};
    if (const usize dash = full.find('-'); dash != std::string::npos) {
        chain.push_back(full.substr(0, dash));
    }
    return chain;
}

StringTableResult parseStringTable(std::string_view jsonText, std::string_view packId) {
    StringTableResult r;
    const auto error = [&](std::string rule, std::string pointer, std::string message) {
        r.issues.push_back({Severity::Error, std::move(rule), std::move(pointer), std::move(message)});
    };
    if (jsonText.size() > kMaxDocumentBytes) {
        error("U7", "", std::format("문자열 표가 {} 바이트를 넘는다", kMaxDocumentBytes));
        return r;
    }
    const json j = json::parse(jsonText, nullptr, false);
    if (!j.is_object()) {
        error("U1", "", R"({"<팩>.<키>": "텍스트 {0}"} 객체)");
        return r;
    }
    if (j.size() > kMaxStrings) {
        error("U7", "", std::format("문자열 {} 개 — 표마다 {} 개까지", j.size(), kMaxStrings));
        return r;
    }
    const std::string prefix = std::string(packId) + '.';
    for (const auto& [key, value] : j.items()) {
        const std::string pointer = "/" + key;
        if (!isUiKey(key)) {
            error("U2", pointer, std::format("문자열 키 '{}' — \"<팩>.<이름>\" (id 글자 · 64 바이트까지)", key));
            continue;
        }
        if (!key.starts_with(prefix)) {
            error("U2", pointer, std::format("문자열 키 '{}' 가 이 팩의 이름공간 ('{}') 밖이다", key, prefix));
            continue;
        }
        if (!value.is_string()) {
            error("U1", pointer, "글은 문자열");
            continue;
        }
        std::string text = value.get<std::string>();
        if (text.size() > kMaxStringBytes) {
            error("U7", pointer, std::format("글이 {} 바이트를 넘는다", kMaxStringBytes));
            continue;
        }
        r.entries.emplace(key, std::move(text));
    }
    return r;
}

std::string formatString(std::string_view pattern, std::span<const std::string> args) {
    std::string out;
    out.reserve(pattern.size());
    for (usize i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 1] >= '0' && pattern[i + 1] <= '9' &&
            pattern[i + 2] == '}') {
            const usize index = static_cast<usize>(pattern[i + 1] - '0');
            if (index < args.size()) {
                out += args[index];
            }
            i += 2;
            continue;
        }
        out += pattern[i];
    }
    return out;
}

std::string defaultLocaleOf(const LocaleTables& tables) {
    if (tables.empty()) {
        return {};
    }
    if (tables.size() == 1) {
        return tables.front().first;
    }
    std::string first = tables.front().first;
    for (const auto& [locale, entries] : tables) {
        if (locale == "en") {
            return locale;
        }
        first = std::min(first, locale);
    }
    return first;
}

void StringTable::clear() {
    m_entries.clear();
    m_locales.clear();
    m_missing.clear();
    m_newMissing.clear();
}

void StringTable::addPack(const LocaleTables& tables) {
    const std::string fallbackLocale = defaultLocaleOf(tables);
    // 이름 순으로 돌아야 "키가 있는 표 중 이름이 가장 앞" 이 정해진다
    std::vector<const std::pair<std::string, StringEntries>*> ordered;
    for (const auto& t : tables) {
        ordered.push_back(&t);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
    std::set<std::string, std::less<>> fromDefault;
    std::set<std::string, std::less<>> touched;
    for (const auto* t : ordered) {
        m_locales.insert(t->first);
        for (const auto& [key, text] : t->second) {
            Entry& e = m_entries[key];
            e.byLocale.insert_or_assign(t->first, text);
            const bool isDefault = t->first == fallbackLocale;
            // 이 팩에서 처음 보는 키면 글을 덮어 둔다 (뒤 팩이 이긴다). 기본 표의 글이 나오면 그것으로 바꾼다
            if (!touched.contains(key) || (isDefault && !fromDefault.contains(key))) {
                e.fallback = text;
                touched.insert(key);
                if (isDefault) {
                    fromDefault.insert(key);
                }
            }
        }
    }
}

void StringTable::setLocale(std::string_view locale) {
    m_locale = normalizeLocale(locale);
    m_chain = localeChain(m_locale);
}

std::optional<std::string_view> StringTable::find(std::string_view key) const {
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    for (const std::string& locale : m_chain) {
        if (const auto t = it->second.byLocale.find(locale); t != it->second.byLocale.end()) {
            return std::string_view(t->second);
        }
    }
    return std::string_view(it->second.fallback);
}

std::string StringTable::text(std::string_view key) const {
    if (const auto found = find(key)) {
        return std::string(*found);
    }
    if (m_missing.insert(std::string(key)).second) {
        m_newMissing.emplace_back(key);
    }
    return std::string(key);
}

std::vector<std::string> StringTable::takeMissing() {
    std::vector<std::string> out;
    out.swap(m_newMissing);
    return out;
}

std::vector<std::string> StringTable::locales() const {
    return {m_locales.begin(), m_locales.end()};
}

std::set<std::string, std::less<>> StringTable::keys() const {
    std::set<std::string, std::less<>> out;
    for (const auto& [key, entry] : m_entries) {
        out.insert(key);
    }
    return out;
}

std::string resolveText(std::string_view text, const StringTable* strings) {
    if (text.starts_with("@@")) {
        return std::string(text.substr(1));
    }
    if (text.starts_with('@')) {
        const std::string_view key = text.substr(1);
        return strings != nullptr ? strings->text(key) : std::string(key);
    }
    return std::string(text);
}

} // namespace sbx::ui
