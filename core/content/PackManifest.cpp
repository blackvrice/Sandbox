#include "core/content/PackManifest.hpp"

#include <format>

namespace sbx::content {

namespace {

using json = nlohmann::json;

bool isIdChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

// SPDX 식 — 재귀 하강 (or ← and ← with ← 단항). 낱말은 공백 · 괄호로 나뉜다
class SpdxParser {
public:
    explicit SpdxParser(std::string_view text) : m_text(text) {}

    std::optional<std::string> run() {
        if (!orExpr()) {
            return m_error;
        }
        skipSpace();
        if (m_pos != m_text.size()) {
            return std::format("'{}' 앞에서 식이 끝나야 한다", m_text.substr(m_pos));
        }
        return std::nullopt;
    }

private:
    void skipSpace() {
        while (m_pos < m_text.size() && m_text[m_pos] == ' ') {
            ++m_pos;
        }
    }
    // 다음 낱말 (식별자 · 연산자) 을 보기만
    std::string_view peekWord() {
        skipSpace();
        usize end = m_pos;
        while (end < m_text.size() && (isIdChar(m_text[end]) || m_text[end] == '+')) {
            ++end;
        }
        return m_text.substr(m_pos, end - m_pos);
    }
    bool fail(std::string message) {
        m_error = std::move(message);
        return false;
    }
    bool orExpr() {
        if (!andExpr()) {
            return false;
        }
        while (peekWord() == "OR") {
            m_pos += 2;
            if (!andExpr()) {
                return false;
            }
        }
        return true;
    }
    bool andExpr() {
        if (!withExpr()) {
            return false;
        }
        while (peekWord() == "AND") {
            m_pos += 3;
            if (!withExpr()) {
                return false;
            }
        }
        return true;
    }
    bool withExpr() {
        if (!unary()) {
            return false;
        }
        if (peekWord() == "WITH") {
            m_pos += 4;
            const std::string_view e = peekWord();
            if (e.empty() || !isPlainId(e)) {
                return fail("WITH 뒤에는 예외 식별자 (예: Classpath-exception-2.0)");
            }
            m_pos += e.size();
        }
        return true;
    }
    bool unary() {
        skipSpace();
        if (m_pos < m_text.size() && m_text[m_pos] == '(') {
            ++m_pos;
            if (!orExpr()) {
                return false;
            }
            skipSpace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ')') {
                return fail("괄호가 닫히지 않았다");
            }
            ++m_pos;
            return true;
        }
        const std::string_view w = peekWord();
        if (w.empty()) {
            return fail(m_pos >= m_text.size() ? "라이선스 식별자가 빠졌다"
                                               : std::format("'{}' 는 식별자가 아니다", m_text.substr(m_pos, 1)));
        }
        if (w == "AND" || w == "OR" || w == "WITH") {
            return fail(std::format("'{}' 앞에 라이선스 식별자가 있어야 한다", w));
        }
        std::string_view id = w;
        if (id.ends_with('+')) {
            id.remove_suffix(1); // "GPL-2.0+" — 그 버전 이상
        }
        if (!isPlainId(id)) {
            return fail(std::format("'{}' 는 SPDX 식별자 모양이 아니다 (문자 A-Z a-z 0-9 . -)", w));
        }
        if (id.starts_with("LicenseRef-") && id.size() == 11) {
            return fail("LicenseRef- 뒤에 이름이 있어야 한다");
        }
        m_pos += w.size();
        return true;
    }
    static bool isPlainId(std::string_view id) {
        if (id.empty() || id.front() == '.' || id.front() == '-') {
            return false;
        }
        for (const char c : id) {
            if (!isIdChar(c)) {
                return false;
            }
        }
        return true;
    }

    std::string_view m_text;
    usize m_pos = 0;
    std::string m_error;
};

bool isSafeRelativePath(std::string_view p) {
    if (p.empty() || p.front() == '/' || p.find('\\') != std::string_view::npos ||
        p.find(':') != std::string_view::npos) {
        return false; // 절대 경로 · 드라이브 · 역슬래시 (Windows · 다른 OS 에서 뜻이 다르다)
    }
    return true;
}

} // namespace

std::optional<std::string> checkSpdxExpression(std::string_view text) {
    if (text.empty()) {
        return "라이선스가 비었다 (예: \"MIT\" · \"Apache-2.0 OR MIT\" · \"LicenseRef-mystudio\")";
    }
    if (text.size() > 256) {
        return "라이선스 식이 256 바이트를 넘는다";
    }
    return SpdxParser(text).run();
}

json writePackDependency(const PackDependency& d) {
    if (d.source == PackSource::Root && d.range) {
        return d.range->text;
    }
    json o = json::object();
    if (d.range) {
        o["version"] = d.range->text;
    }
    if (d.source == PackSource::Path) {
        o["path"] = d.path;
    } else if (d.source == PackSource::Git) {
        o["git"] = d.git;
        o["rev"] = d.rev;
    }
    return o;
}

std::optional<PackDependencyError> parsePackDependency(const json& value, PackDependency& out) {
    const auto range = [&](const json& v, std::string_view at) -> std::optional<PackDependencyError> {
        if (!v.is_string()) {
            return PackDependencyError{std::string(at), "범위는 문자열 (예: \"^1.2\" · \"~0.3.1\" · \"*\")"};
        }
        auto r = parseVersionRange(v.get<std::string>());
        if (!r) {
            return PackDependencyError{std::string(at),
                                       std::format("범위 '{}' — {}", v.get<std::string>(), r.error().message)};
        }
        out.range = std::move(*r);
        return std::nullopt;
    };
    if (value.is_string()) {
        return range(value, "");
    }
    if (!value.is_object()) {
        return PackDependencyError{"", "의존은 범위 문자열 또는 {version, path | git + rev} 객체"};
    }
    for (const auto& [k, _] : value.items()) {
        if (k != "version" && k != "path" && k != "git" && k != "rev") {
            return PackDependencyError{"/" + k, std::format("모르는 키 '{}' (version · path · git · rev)", k)};
        }
    }
    if (value.contains("version")) {
        if (auto e = range(value["version"], "/version")) {
            return e;
        }
    }
    const bool hasPath = value.contains("path");
    const bool hasGit = value.contains("git");
    if (hasPath && hasGit) {
        return PackDependencyError{"", "path 와 git 중 하나만"};
    }
    if (hasPath) {
        if (!value["path"].is_string() || !isSafeRelativePath(value["path"].get<std::string>())) {
            return PackDependencyError{"/path", "path 는 상대 경로 문자열 ('/' 로 — 절대 경로 · 드라이브 · '\\' 없이)"};
        }
        if (value.contains("rev")) {
            return PackDependencyError{"/rev", "rev 는 git 소스에만"};
        }
        out.source = PackSource::Path;
        out.path = value["path"].get<std::string>();
    } else if (hasGit) {
        if (!value["git"].is_string() || value["git"].get<std::string>().empty()) {
            return PackDependencyError{"/git", "git 은 저장소 URL 문자열"};
        }
        if (!value.contains("rev") || !value["rev"].is_string() || value["rev"].get<std::string>().empty()) {
            return PackDependencyError{
                "/rev", "git 소스에는 rev (태그 · 커밋) 가 필요하다 — 재현할 수 없는 브랜치 끝을 쓰지 않는다"};
        }
        out.source = PackSource::Git;
        out.git = value["git"].get<std::string>();
        out.rev = value["rev"].get<std::string>();
    } else if (value.contains("rev")) {
        return PackDependencyError{"/rev", "rev 는 git 소스에만"};
    }
    return std::nullopt;
}

} // namespace sbx::content
