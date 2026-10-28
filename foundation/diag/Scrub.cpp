#include "foundation/diag/Scrub.hpp"

#include <algorithm>
#include <cstdlib>

#include "foundation/types/Types.hpp"

namespace sbx::diag {
namespace {

std::string envOr(const char* name) {
    // getenv 는 이 함수 안에서만 읽고 바로 복사한다 (보고 경로 — 시뮬레이션과 무관)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* v = std::getenv(name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return v != nullptr ? std::string(v) : std::string();
}

char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool wordChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

// 경로 비교: 대소문자 무시, '\' 와 '/' 를 같게
bool pathCharEq(char a, char b) noexcept {
    const auto norm = [](char c) { return c == '\\' ? '/' : lower(c); };
    return norm(a) == norm(b);
}

// text 안의 needle (eq 로 비교) 을 모두 replacement 로. wholeWord 면 앞뒤가 낱말 글자가 아닐 때만
template <class Eq>
std::string replaceAll(std::string_view text, std::string_view needle, std::string_view replacement, Eq eq,
                       bool wholeWord) {
    if (needle.empty()) {
        return std::string(text);
    }
    std::string out;
    usize i = 0;
    while (i < text.size()) {
        bool hit = i + needle.size() <= text.size();
        for (usize k = 0; hit && k < needle.size(); ++k) {
            hit = eq(text[i + k], needle[k]);
        }
        if (hit && wholeWord) {
            const bool before = i > 0 && wordChar(text[i - 1]);
            const bool after = i + needle.size() < text.size() && wordChar(text[i + needle.size()]);
            hit = !before && !after;
        }
        if (hit) {
            out += replacement;
            i += needle.size();
        } else {
            out += text[i++];
        }
    }
    return out;
}

// IPv4 (네 마디 0~255) 를 찾아 바꾼다 — 앞뒤가 숫자 · 점이 아닐 때만
std::string maskIpv4(std::string_view text) {
    std::string out;
    usize i = 0;
    while (i < text.size()) {
        if (digit(text[i]) && (i == 0 || (!digit(text[i - 1]) && text[i - 1] != '.'))) {
            usize j = i;
            int parts = 0;
            bool ok = true;
            while (ok && parts < 4) {
                usize start = j;
                int value = 0;
                while (j < text.size() && digit(text[j]) && j - start < 3) {
                    value = value * 10 + (text[j] - '0');
                    ++j;
                }
                ok = j > start && value <= 255;
                ++parts;
                if (ok && parts < 4) {
                    ok = j < text.size() && text[j] == '.';
                    ++j;
                }
            }
            const bool tailOk =
                j >= text.size() || (!digit(text[j]) && !(text[j] == '.' && j + 1 < text.size() && digit(text[j + 1])));
            if (ok && parts == 4 && tailOk) {
                const std::string_view ip = text.substr(i, j - i);
                out += (ip == "127.0.0.1" || ip == "0.0.0.0") ? std::string(ip) : std::string("<IP>");
                i = j;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

bool emailChar(char c) noexcept {
    return wordChar(c) || c == '.' || c == '-' || c == '+';
}

std::string maskEmail(std::string_view text) {
    std::string out;
    usize i = 0;
    while (i < text.size()) {
        const usize at = text.find('@', i);
        if (at == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        usize left = at;
        while (left > i && emailChar(text[left - 1])) {
            --left;
        }
        usize right = at + 1;
        while (right < text.size() && emailChar(text[right])) {
            ++right;
        }
        const std::string_view domain = text.substr(at + 1, right - at - 1);
        if (left < at && domain.find('.') != std::string_view::npos && domain.front() != '.' && domain.back() != '.') {
            out.append(text.substr(i, left - i));
            out += "<EMAIL>";
        } else {
            out.append(text.substr(i, right - i));
        }
        i = right;
    }
    return out;
}

} // namespace

ScrubRules defaultScrubRules() {
    ScrubRules r;
    for (const char* name : {"USERPROFILE", "HOME"}) {
        std::string v = envOr(name);
        while (v.size() > 1 && (v.back() == '/' || v.back() == '\\')) {
            v.pop_back();
        }
        if (v.size() > 3 && std::find(r.homes.begin(), r.homes.end(), v) == r.homes.end()) {
            r.homes.push_back(std::move(v));
        }
    }
    r.userName = envOr("USERNAME");
    if (r.userName.empty()) {
        r.userName = envOr("USER");
    }
    return r;
}

std::string scrub(std::string_view text, const ScrubRules& rules) {
    std::string out(text);
    for (const std::string& s : rules.secrets) {
        out = replaceAll(out, s, "<SECRET>", [](char a, char b) { return a == b; }, false);
    }
    // 긴 경로부터 (한 경로가 다른 경로를 품을 때 — C:\Users\a 와 C:\Users\ab)
    std::vector<std::string> homes = rules.homes;
    std::sort(homes.begin(), homes.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    for (const std::string& h : homes) {
        out = replaceAll(out, h, "<HOME>", pathCharEq, false);
    }
    out = maskEmail(out);
    out = maskIpv4(out);
    if (rules.userName.size() >= 3) {
        out = replaceAll(out, rules.userName, "<USER>", [](char a, char b) { return lower(a) == lower(b); }, true);
    }
    return out;
}

} // namespace sbx::diag
