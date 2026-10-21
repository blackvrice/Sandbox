#include "script/lang/Lexer.hpp"

#include <array>
#include <charconv>
#include <format>
#include <string>

#include "foundation/text/Utf8.hpp"

#if !defined(__cpp_lib_to_chars)
// libc++ (macOS) 는 부동소수 from_chars 가 없다 → C 로케일에 묶은 strtod_l (로케일에 따른 파싱 금지 — 04 4.5)
#include <cstdlib>
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace sbx::script {

namespace {

struct KeywordEntry {
    std::string_view text;
    Tok tok;
};

constexpr std::array kKeywords{
    KeywordEntry{"break", Tok::KwBreak},
    KeywordEntry{"case", Tok::KwCase},
    KeywordEntry{"catch", Tok::KwCatch},
    KeywordEntry{"class", Tok::KwClass},
    KeywordEntry{"const", Tok::KwConst},
    KeywordEntry{"continue", Tok::KwContinue},
    KeywordEntry{"debugger", Tok::KwDebugger},
    KeywordEntry{"default", Tok::KwDefault},
    KeywordEntry{"delete", Tok::KwDelete},
    KeywordEntry{"do", Tok::KwDo},
    KeywordEntry{"else", Tok::KwElse},
    KeywordEntry{"enum", Tok::KwEnum},
    KeywordEntry{"export", Tok::KwExport},
    KeywordEntry{"extends", Tok::KwExtends},
    KeywordEntry{"false", Tok::KwFalse},
    KeywordEntry{"finally", Tok::KwFinally},
    KeywordEntry{"for", Tok::KwFor},
    KeywordEntry{"function", Tok::KwFunction},
    KeywordEntry{"if", Tok::KwIf},
    KeywordEntry{"import", Tok::KwImport},
    KeywordEntry{"in", Tok::KwIn},
    KeywordEntry{"instanceof", Tok::KwInstanceof},
    KeywordEntry{"new", Tok::KwNew},
    KeywordEntry{"null", Tok::KwNull},
    KeywordEntry{"return", Tok::KwReturn},
    KeywordEntry{"super", Tok::KwSuper},
    KeywordEntry{"switch", Tok::KwSwitch},
    KeywordEntry{"this", Tok::KwThis},
    KeywordEntry{"throw", Tok::KwThrow},
    KeywordEntry{"true", Tok::KwTrue},
    KeywordEntry{"try", Tok::KwTry},
    KeywordEntry{"typeof", Tok::KwTypeof},
    KeywordEntry{"var", Tok::KwVar},
    KeywordEntry{"void", Tok::KwVoid},
    KeywordEntry{"while", Tok::KwWhile},
    KeywordEntry{"with", Tok::KwWith},
    KeywordEntry{"yield", Tok::KwYield},
    KeywordEntry{"let", Tok::KwLet},
    KeywordEntry{"static", Tok::KwStatic},
    KeywordEntry{"implements", Tok::KwImplements},
    KeywordEntry{"interface", Tok::KwInterface},
    KeywordEntry{"package", Tok::KwPackage},
    KeywordEntry{"private", Tok::KwPrivate},
    KeywordEntry{"protected", Tok::KwProtected},
    KeywordEntry{"public", Tok::KwPublic},
    KeywordEntry{"await", Tok::KwAwait},
};

// 구두점 — 긴 것부터 (최장 일치)
struct PunctEntry {
    std::string_view text;
    Tok tok;
};
constexpr std::array kPuncts{
    PunctEntry{">>>=", Tok::UShrAssign},
    PunctEntry{"...", Tok::Ellipsis},
    PunctEntry{"===", Tok::EqEqEq},
    PunctEntry{"!==", Tok::NotEqEq},
    PunctEntry{"**=", Tok::StarStarAssign},
    PunctEntry{"<<=", Tok::ShlAssign},
    PunctEntry{">>=", Tok::ShrAssign},
    PunctEntry{">>>", Tok::UShr},
    PunctEntry{"&&=", Tok::AmpAmpAssign},
    PunctEntry{"||=", Tok::PipePipeAssign},
    PunctEntry{"?\?=", Tok::QuestionQuestionAssign},
    PunctEntry{"=>", Tok::Arrow},
    PunctEntry{"==", Tok::EqEq},
    PunctEntry{"!=", Tok::NotEq},
    PunctEntry{"<=", Tok::LessEq},
    PunctEntry{">=", Tok::GreaterEq},
    PunctEntry{"**", Tok::StarStar},
    PunctEntry{"++", Tok::PlusPlus},
    PunctEntry{"--", Tok::MinusMinus},
    PunctEntry{"<<", Tok::Shl},
    PunctEntry{">>", Tok::Shr},
    PunctEntry{"&&", Tok::AmpAmp},
    PunctEntry{"||", Tok::PipePipe},
    PunctEntry{"?\?", Tok::QuestionQuestion},
    PunctEntry{"?.", Tok::QuestionDot},
    PunctEntry{"+=", Tok::PlusAssign},
    PunctEntry{"-=", Tok::MinusAssign},
    PunctEntry{"*=", Tok::StarAssign},
    PunctEntry{"/=", Tok::SlashAssign},
    PunctEntry{"%=", Tok::PercentAssign},
    PunctEntry{"&=", Tok::AmpAssign},
    PunctEntry{"|=", Tok::PipeAssign},
    PunctEntry{"^=", Tok::CaretAssign},
    PunctEntry{"{", Tok::LBrace},
    PunctEntry{"}", Tok::RBrace},
    PunctEntry{"(", Tok::LParen},
    PunctEntry{")", Tok::RParen},
    PunctEntry{"[", Tok::LBracket},
    PunctEntry{"]", Tok::RBracket},
    PunctEntry{";", Tok::Semicolon},
    PunctEntry{",", Tok::Comma},
    PunctEntry{".", Tok::Dot},
    PunctEntry{"?", Tok::Question},
    PunctEntry{":", Tok::Colon},
    PunctEntry{"@", Tok::At},
    PunctEntry{"#", Tok::Hash},
    PunctEntry{"<", Tok::Less},
    PunctEntry{">", Tok::Greater},
    PunctEntry{"+", Tok::Plus},
    PunctEntry{"-", Tok::Minus},
    PunctEntry{"*", Tok::Star},
    PunctEntry{"/", Tok::Slash},
    PunctEntry{"%", Tok::Percent},
    PunctEntry{"&", Tok::Amp},
    PunctEntry{"|", Tok::Pipe},
    PunctEntry{"^", Tok::Caret},
    PunctEntry{"!", Tok::Bang},
    PunctEntry{"~", Tok::Tilde},
    PunctEntry{"=", Tok::Assign},
};

constexpr bool isIdStart(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
constexpr bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}
constexpr bool isIdPart(char c) noexcept {
    return isIdStart(c) || isDigit(c);
}
constexpr int digitValue(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 99;
}

// 2^53 — 이보다 큰 정수는 f64 로 정확하지 않다
constexpr u64 kMaxExactInteger = u64{1} << 53;

bool parseDecimal(const std::string& digits, f64& out) {
#if defined(__cpp_lib_to_chars)
    const auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), out);
    return ec == std::errc{} && p == digits.data() + digits.size();
#else
    static const locale_t cLocale = newlocale(LC_ALL_MASK, "C", locale_t{});
    char* end = nullptr;
    out = strtod_l(digits.c_str(), &end, cLocale);
    return end == digits.c_str() + digits.size();
#endif
}

class Lexer {
public:
    Lexer(std::string_view src, std::string_view file, DiagnosticList& diags)
        : m_src(src), m_file(file), m_diags(diags) {}

    std::vector<Token> run() {
        if (m_src.size() > kMaxSourceBytes) {
            m_diags.error("S100", m_file, {},
                          std::format("소스가 너무 크다 ({} 바이트 > {})", m_src.size(), kMaxSourceBytes));
            return finish();
        }
        if (!checkUtf8()) {
            return finish();
        }
        if (m_src.starts_with("\xEF\xBB\xBF")) { // BOM
            m_pos = 3;
            m_lineStart = 3;
        }
        while (true) {
            skipTrivia();
            if (m_pos >= m_src.size()) {
                break;
            }
            lexToken();
        }
        if (!m_templateDepths.empty()) {
            error("S108", here(), "닫히지 않은 템플릿 문자열 (`)");
        }
        return finish();
    }

private:
    std::vector<Token> finish() {
        Token eof;
        eof.kind = Tok::EndOfFile;
        eof.pos = here();
        eof.newlineBefore = true;
        m_out.push_back(std::move(eof));
        return std::move(m_out);
    }

    bool checkUtf8() {
        usize i = 0;
        while (i < m_src.size()) {
            const usize start = i;
            const char32_t cp = utf8::decodeNext(m_src, i);
            if (cp == utf8::kReplacement && m_src.substr(start, 3) != "\xEF\xBF\xBD") {
                m_pos = start;
                recomputeLine();
                error("S100", here(), "UTF-8 이 아닌 바이트열");
                return false;
            }
        }
        return true;
    }

    // m_pos 까지 줄 번호를 다시 센다 (오류 자리 계산용 — 드물다)
    void recomputeLine() {
        m_line = 1;
        m_lineStart = 0;
        for (usize i = 0; i < m_pos; ++i) {
            if (m_src[i] == '\n') {
                ++m_line;
                m_lineStart = i + 1;
            }
        }
    }

    [[nodiscard]] SourcePos here() const { return posAt(m_pos); }
    [[nodiscard]] SourcePos posAt(usize off) const {
        SourcePos p;
        p.offset = static_cast<u32>(off);
        p.line = m_line;
        p.column = static_cast<u32>(utf8::length(m_src.substr(m_lineStart, off - m_lineStart)) + 1);
        return p;
    }

    void error(const char* code, SourcePos pos, std::string message) {
        m_diags.error(code, m_file, pos, std::move(message));
    }

    [[nodiscard]] char peek(usize ahead = 0) const noexcept {
        return m_pos + ahead < m_src.size() ? m_src[m_pos + ahead] : '\0';
    }

    // 줄바꿈 하나를 먹었으면 true (\n · \r\n · \r · U+2028 · U+2029)
    bool consumeNewline() {
        const char c = peek();
        usize len = 0;
        if (c == '\n') {
            len = 1;
        } else if (c == '\r') {
            len = peek(1) == '\n' ? 2 : 1;
        } else if (m_src.substr(m_pos, 3) == "\xE2\x80\xA8" || m_src.substr(m_pos, 3) == "\xE2\x80\xA9") {
            len = 3;
        }
        if (len == 0) {
            return false;
        }
        m_pos += len;
        ++m_line;
        m_lineStart = m_pos;
        m_newline = true;
        return true;
    }

    void skipTrivia() {
        while (m_pos < m_src.size()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
                ++m_pos;
            } else if (consumeNewline()) {
            } else if (m_src.substr(m_pos, 2) == "\xC2\xA0") { // NBSP
                m_pos += 2;
            } else if (m_src.substr(m_pos, 3) == "\xEF\xBB\xBF") { // ZWNBSP
                m_pos += 3;
            } else if (c == '/' && peek(1) == '/') {
                while (m_pos < m_src.size() && peek() != '\n' && peek() != '\r' &&
                       m_src.substr(m_pos, 3) != "\xE2\x80\xA8" && m_src.substr(m_pos, 3) != "\xE2\x80\xA9") {
                    ++m_pos;
                }
            } else if (c == '/' && peek(1) == '*') {
                const SourcePos start = here();
                m_pos += 2;
                bool closed = false;
                while (m_pos < m_src.size()) {
                    if (peek() == '*' && peek(1) == '/') {
                        m_pos += 2;
                        closed = true;
                        break;
                    }
                    if (!consumeNewline()) {
                        ++m_pos;
                    }
                }
                if (!closed) {
                    error("S104", start, "닫히지 않은 주석 (/* … */)");
                }
            } else {
                return;
            }
        }
    }

    Token& push(Tok kind, usize start, SourcePos pos) {
        Token t;
        t.kind = kind;
        t.pos = pos;
        t.length = static_cast<u32>(m_pos - start);
        t.newlineBefore = m_newline;
        m_newline = false;
        m_out.push_back(std::move(t));
        return m_out.back();
    }

    void lexToken() {
        const usize start = m_pos;
        const SourcePos pos = here();
        const char c = peek();

        if (isIdStart(c)) {
            while (isIdPart(peek())) {
                ++m_pos;
            }
            const std::string_view word = m_src.substr(start, m_pos - start);
            Tok kind = Tok::Identifier;
            for (const KeywordEntry& k : kKeywords) {
                if (k.text == word) {
                    kind = k.tok;
                    break;
                }
            }
            Token& t = push(kind, start, pos);
            if (kind == Tok::Identifier) {
                t.value = std::string(word);
            }
            return; // 이름 뒤에 붙은 한글 등은 다음 낱말로 — 공백 (NBSP) 이 아니면 S101
        }
        if (isDigit(c) || (c == '.' && isDigit(peek(1)))) {
            lexNumber(start, pos);
            return;
        }
        if (c == '"' || c == '\'') {
            lexString(start, pos);
            return;
        }
        if (c == '`') {
            ++m_pos;
            lexTemplatePart(start, pos, /*head=*/true);
            return;
        }
        if (static_cast<unsigned char>(c) >= 0x80) {
            nonAscii();
            return;
        }
        // 템플릿 끼움을 닫는 } — 중괄호 깊이가 ${ 를 열 때와 같으면
        if (c == '}' && !m_templateDepths.empty() && m_templateDepths.back() == m_braceDepth) {
            m_templateDepths.pop_back();
            ++m_pos;
            lexTemplatePart(start, pos, /*head=*/false);
            return;
        }
        for (const PunctEntry& p : kPuncts) {
            if (m_src.substr(m_pos, p.text.size()) == p.text) {
                // "?." 다음이 숫자면 "?" "." 이 아니라 삼항의 ? 와 소수 (a ? .5 : 1) — JS 규칙
                if (p.tok == Tok::QuestionDot && isDigit(peek(2))) {
                    continue;
                }
                m_pos += p.text.size();
                if (p.tok == Tok::LBrace) {
                    ++m_braceDepth;
                } else if (p.tok == Tok::RBrace && m_braceDepth > 0) {
                    --m_braceDepth;
                }
                push(p.tok, start, pos);
                return;
            }
        }
        ++m_pos;
        error("S101", pos, std::format("알 수 없는 글자 '{}'", c));
    }

    void nonAscii() {
        const SourcePos pos = here();
        usize i = m_pos;
        const char32_t cp = utf8::decodeNext(m_src, i);
        m_pos = i;
        error(
            "S101", pos,
            std::format(
                "코드에는 ASCII 글자만 쓴다 — U+{:04X} (문자열 · 주석 안은 무엇이든, 이름의 한글 등은 [v0.3 뒤 검토])",
                static_cast<u32>(cp)));
    }

    void lexNumber(usize start, SourcePos pos) {
        bool integer = true;
        bool bad = false;
        bool reported = false; // 더 구체적인 오류를 이미 냈다
        f64 value = 0.0;
        const char c = peek();
        const char base = c == '0' ? static_cast<char>(peek(1) | 0x20) : '\0';
        if (base == 'x' || base == 'b' || base == 'o') {
            const int radix = base == 'x' ? 16 : base == 'b' ? 2 : 8;
            m_pos += 2;
            u64 acc = 0;
            usize digits = 0;
            bool lastSep = false;
            bool overflow = false;
            while (true) {
                const char d = peek();
                if (d == '_') {
                    if (digits == 0 || lastSep) {
                        bad = true;
                    }
                    lastSep = true;
                    ++m_pos;
                    continue;
                }
                const int v = digitValue(d);
                if (v >= radix) {
                    break;
                }
                lastSep = false;
                ++digits;
                ++m_pos;
                if (acc > (kMaxExactInteger - static_cast<u64>(v)) / static_cast<u64>(radix)) {
                    overflow = true;
                } else {
                    acc = acc * static_cast<u64>(radix) + static_cast<u64>(v);
                }
            }
            if (digits == 0 || lastSep) {
                bad = true;
            }
            if (overflow) {
                error("S106", pos, "숫자 리터럴이 2^53 을 넘는다 — f64 로 정확하지 않다");
            }
            value = static_cast<f64>(acc);
        } else {
            std::string digits;
            bool lastSep = false;
            auto readDigits = [&](bool required) {
                usize n = 0;
                lastSep = false;
                while (true) {
                    const char d = peek();
                    if (d == '_') {
                        if (n == 0 || lastSep) {
                            bad = true;
                        }
                        lastSep = true;
                        ++m_pos;
                        continue;
                    }
                    if (!isDigit(d)) {
                        break;
                    }
                    digits += d;
                    lastSep = false;
                    ++n;
                    ++m_pos;
                }
                if ((required && n == 0) || lastSep) {
                    bad = true;
                }
                return n;
            };
            if (c == '0' && isDigit(peek(1))) {
                error("S106", pos, "0 으로 시작하는 10진수는 없다 (옛 8진수) — 0o17 처럼 쓴다");
                reported = true;
            }
            if (readDigits(false) == 0) {
                digits += '0'; // .5 → 0.5 (from_chars · strtod 가 같은 모양을 받게)
            }
            if (peek() == '.') {
                integer = false;
                digits += '.';
                ++m_pos;
                if (readDigits(false) == 0) {
                    digits += '0'; // 1. → 1.0
                }
            }
            if ((peek() | 0x20) == 'e') {
                integer = false;
                digits += 'e';
                ++m_pos;
                if (peek() == '+' || peek() == '-') {
                    digits += peek();
                    ++m_pos;
                }
                readDigits(true);
            }
            if (!bad && !parseDecimal(digits, value)) {
                bad = true;
            }
        }
        if (peek() == 'n') {
            ++m_pos;
            error("S310", pos, "BigInt 리터럴 (123n) 은 없다 — number · i32 를 쓴다");
        } else if (isIdPart(peek())) {
            while (isIdPart(peek())) {
                ++m_pos;
            }
            bad = true;
        }
        if (bad && !reported) {
            error("S106", pos, std::format("잘못된 숫자 리터럴 '{}'", m_src.substr(start, m_pos - start)));
        }
        Token& t = push(Tok::Number, start, pos);
        t.number = value;
        t.integer = integer;
    }

    // \ 다음 이스케이프 하나를 풀어 out 에 붙인다. m_pos 는 \ 다음
    void lexEscape(std::string& out) {
        const SourcePos pos = posAt(m_pos - 1);
        const char e = peek();
        if (m_pos >= m_src.size()) {
            return;
        }
        if (consumeNewline()) { // 줄 이음 — 아무것도 넣지 않는다 (m_newline 은 부른 쪽이 되돌린다)
            return;
        }
        ++m_pos;
        switch (e) {
        case 'n':
            out += '\n';
            return;
        case 't':
            out += '\t';
            return;
        case 'r':
            out += '\r';
            return;
        case 'b':
            out += '\b';
            return;
        case 'f':
            out += '\f';
            return;
        case 'v':
            out += '\v';
            return;
        case '0':
            if (isDigit(peek())) {
                error("S105", pos, "8진 이스케이프 (\\0 다음 숫자) 는 없다");
            }
            out += '\0';
            return;
        case 'x': {
            const int hi = digitValue(peek());
            const int lo = digitValue(peek(1));
            if (hi > 15 || lo > 15) {
                error("S105", pos, "\\x 다음에는 16진 숫자 둘");
                return;
            }
            m_pos += 2;
            utf8::append(out, static_cast<char32_t>(hi * 16 + lo));
            return;
        }
        case 'u': {
            u32 cp = 0;
            if (peek() == '{') {
                ++m_pos;
                usize n = 0;
                while (digitValue(peek()) <= 15 && n < 7) {
                    cp = cp * 16 + static_cast<u32>(digitValue(peek()));
                    ++m_pos;
                    ++n;
                }
                if (n == 0 || peek() != '}' || cp > 0x10FFFF) {
                    error("S105", pos, "\\u{…} 는 16진 1~6 자리, U+10FFFF 까지");
                    return;
                }
                ++m_pos;
            } else {
                for (int i = 0; i < 4; ++i) {
                    const int v = digitValue(peek());
                    if (v > 15) {
                        error("S105", pos, "\\u 다음에는 16진 숫자 넷 (또는 \\u{…})");
                        return;
                    }
                    cp = cp * 16 + static_cast<u32>(v);
                    ++m_pos;
                }
                // 서로게이트 짝 😀 → 코드 포인트 하나
                if (cp >= 0xD800 && cp <= 0xDBFF && peek() == '\\' && peek(1) == 'u') {
                    u32 lo = 0;
                    bool ok = true;
                    for (int i = 0; i < 4; ++i) {
                        const int v = digitValue(peek(2 + static_cast<usize>(i)));
                        if (v > 15) {
                            ok = false;
                            break;
                        }
                        lo = lo * 16 + static_cast<u32>(v);
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                        m_pos += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
            }
            if (cp >= 0xD800 && cp <= 0xDFFF) {
                error("S105", pos, "짝 없는 서로게이트 — 문자열은 UTF-8 이라 담을 수 없다");
                return;
            }
            utf8::append(out, static_cast<char32_t>(cp));
            return;
        }
        default:
            if (isDigit(e)) {
                error("S105", pos, "8진 이스케이프 (\\1 …) 는 없다");
                return;
            }
            // \" \' \\ \` \$ 와 그 밖의 글자는 그 글자 자신 (JS 와 같다). UTF-8 다중 바이트면 통째로
            out += e;
            while (m_pos < m_src.size() && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80) {
                out += peek();
                ++m_pos;
            }
            return;
        }
    }

    void lexString(usize start, SourcePos pos) {
        const char quote = peek();
        const bool newlineBefore = m_newline;
        ++m_pos;
        std::string value;
        while (true) {
            if (m_pos >= m_src.size() || peek() == '\n' || peek() == '\r') {
                error("S103", pos, "닫히지 않은 문자열");
                break;
            }
            const char c = peek();
            if (c == quote) {
                ++m_pos;
                break;
            }
            if (c == '\\') {
                ++m_pos;
                lexEscape(value);
                continue;
            }
            value += c;
            ++m_pos;
        }
        m_newline = newlineBefore; // 줄 이음 (\ 줄바꿈) 은 낱말 사이의 줄바꿈이 아니다
        Token& t = push(Tok::String, start, pos);
        t.value = std::move(value);
    }

    // ` 나 } 다음부터 다음 ` 나 ${ 까지
    void lexTemplatePart(usize start, SourcePos pos, bool head) {
        const bool newlineBefore = m_newline;
        std::string value;
        bool closed = false;
        bool substitution = false;
        while (m_pos < m_src.size()) {
            const char c = peek();
            if (c == '`') {
                ++m_pos;
                closed = true;
                break;
            }
            if (c == '$' && peek(1) == '{') {
                m_pos += 2;
                closed = true;
                substitution = true;
                break;
            }
            if (c == '\\') {
                ++m_pos;
                lexEscape(value);
                continue;
            }
            if (c == '\r') { // 템플릿 안의 \r\n · \r 은 \n 이다 (JS)
                consumeNewline();
                value += '\n';
                continue;
            }
            if (consumeNewline()) {
                value += '\n';
                continue;
            }
            value += c;
            ++m_pos;
        }
        m_newline = newlineBefore; // 템플릿 안의 줄바꿈은 낱말 사이의 줄바꿈이 아니다
        if (!closed) {
            error("S108", pos, "닫히지 않은 템플릿 문자열 (`)");
        }
        Tok kind;
        if (head) {
            kind = substitution ? Tok::TemplateHead : Tok::Template;
        } else {
            kind = substitution ? Tok::TemplateMiddle : Tok::TemplateTail;
        }
        if (substitution) {
            m_templateDepths.push_back(m_braceDepth);
        }
        Token& t = push(kind, start, pos);
        t.value = std::move(value);
    }

    std::string_view m_src;
    std::string_view m_file;
    DiagnosticList& m_diags;
    usize m_pos = 0;
    u32 m_line = 1;
    usize m_lineStart = 0;
    bool m_newline = false;
    u32 m_braceDepth = 0;
    std::vector<u32> m_templateDepths; // ${ 를 열 때의 중괄호 깊이
    std::vector<Token> m_out;
};

} // namespace

std::vector<Token> tokenize(std::string_view source, std::string_view file, DiagnosticList& diags) {
    return Lexer(source, file, diags).run();
}

std::string_view tokName(Tok t) noexcept {
    switch (t) {
    case Tok::EndOfFile:
        return "파일 끝";
    case Tok::Identifier:
        return "이름";
    case Tok::Number:
        return "숫자";
    case Tok::String:
        return "문자열";
    case Tok::Template:
    case Tok::TemplateHead:
    case Tok::TemplateMiddle:
    case Tok::TemplateTail:
        return "템플릿 문자열";
    default:
        break;
    }
    for (const PunctEntry& p : kPuncts) {
        if (p.tok == t) {
            return p.text;
        }
    }
    for (const KeywordEntry& k : kKeywords) {
        if (k.tok == t) {
            return k.text;
        }
    }
    return "?";
}

bool isKeyword(Tok t) noexcept {
    return t >= Tok::KwBreak;
}

bool isAssignOp(Tok t) noexcept {
    return t >= Tok::Assign && t <= Tok::QuestionQuestionAssign;
}

} // namespace sbx::script
