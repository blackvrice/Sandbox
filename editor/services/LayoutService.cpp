#include "editor/services/LayoutService.hpp"

#include <algorithm>
#include <format>
#include <system_error>

#include "editor/services/StudioSettings.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::editor::services {

namespace fs = std::filesystem;

LayoutService::LayoutService(fs::path folder, fs::path settingsFile, std::string active)
    : m_folder(std::move(folder)), m_settingsFile(std::move(settingsFile)), m_active(std::move(active)) {
    if (m_active.empty()) {
        m_active = std::string(kDefault);
    } else if (!isName(m_active)) {
        m_warnings.push_back(std::format(
            "Studio 설정의 레이아웃 이름 '{}' 이 틀렸다 ([a-z0-9_-] 1 ~ {} 자) — 기본 레이아웃", m_active, kMaxName));
        m_active = std::string(kDefault);
    }
}

bool LayoutService::isName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxName) {
        return false;
    }
    return std::ranges::all_of(
        name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}

bool LayoutService::isLayoutText(std::string_view text) noexcept {
    if (text.size() > kMaxBytes || text.find('\0') != std::string_view::npos) {
        return false;
    }
    // UTF-8 (창 이름은 한국어) — 디코더가 대체 문자를 돌려줬는데 그 자리 바이트가 진짜 U+FFFD 가 아니면 틀린 바이트
    for (usize pos = 0; pos < text.size();) {
        const usize at = pos;
        if (utf8::decodeNext(text, pos) == utf8::kReplacement && text.substr(at, 3) != "\xEF\xBF\xBD") {
            return false;
        }
    }
    usize begin = 0;
    while (begin < text.size()) {
        usize end = text.find('\n', begin);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const bool header =
            line.size() >= 4 && line.front() == '[' && line.back() == ']' && line.find("][") != std::string_view::npos;
        if (!line.empty() && line.front() != ';' && !header && line.find('=') == std::string_view::npos) {
            return false;
        }
        begin = end + 1;
    }
    return true;
}

fs::path LayoutService::fileFor(std::string_view name) const {
    return m_folder / (std::string(name) + ".ini");
}

std::vector<std::string> LayoutService::names() const {
    std::vector<std::string> out;
    if (enabled()) {
        std::error_code ec;
        for (fs::directory_iterator it(m_folder, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            const std::string stem = p.stem().string();
            if (p.extension() == ".ini" && isName(stem) && stem != kDefault) {
                out.push_back(stem);
            }
        }
    }
    std::ranges::sort(out);
    out.insert(out.begin(), std::string(kDefault));
    return out;
}

Expected<std::string> LayoutService::readLayout(std::string_view name) {
    if (!enabled()) {
        return std::string();
    }
    const fs::path file = fileFor(name);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return std::string();
    }
    const auto size = fs::file_size(file, ec);
    Expected<std::string> text =
        !ec && size <= kMaxBytes ? io::readFile(file) : Expected<std::string>(std::string(1, '\0')); // 너무 크면 깨짐
    if (!text) {
        return std::unexpected(text.error());
    }
    if (isLayoutText(*text)) {
        return text;
    }
    fs::path aside = file;
    aside += ".broken";
    fs::remove(aside, ec);
    fs::rename(file, aside, ec);
    m_warnings.push_back(
        ec ? std::format("레이아웃 {} 이 깨졌다 — 옮기지 못했다 ({}), 기본 배치로", io::displayPath(file), ec.message())
           : std::format("레이아웃 {} 이 깨졌다 — {} 로 옮기고 기본 배치로", io::displayPath(file),
                         io::displayPath(aside)));
    return std::string();
}

std::string LayoutService::loadActive() {
    Expected<std::string> text = readLayout(m_active);
    if (!text) {
        m_warnings.push_back(
            std::format("레이아웃 '{}' 을 읽지 못했다 — {} — 기본 배치로", m_active, text.error().describe()));
        text = std::string();
    }
    m_lastSaved = *text;
    return *text;
}

Expected<std::string> LayoutService::select(std::string_view name) {
    if (!isName(name)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("레이아웃 이름 '{}' — [a-z0-9_-] 1 ~ {} 자", name, kMaxName));
    }
    Expected<std::string> text = readLayout(name);
    if (!text) {
        return text;
    }
    m_active = std::string(name);
    m_lastSaved = *text;
    remember();
    return text;
}

Expected<void> LayoutService::save(std::string_view text) {
    if (!enabled() || text == m_lastSaved) {
        return {};
    }
    if (!isLayoutText(text)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("레이아웃 '{}' 의 텍스트가 ini 모양이 아니다", m_active));
    }
    std::error_code ec;
    fs::create_directories(m_folder, ec);
    if (Expected<void> w = io::writeFileAtomic(fileFor(m_active), text); !w) {
        return w;
    }
    m_lastSaved = std::string(text);
    return {};
}

Expected<void> LayoutService::saveAs(std::string_view name, std::string_view text) {
    if (!isName(name)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("레이아웃 이름 '{}' — [a-z0-9_-] 1 ~ {} 자", name, kMaxName));
    }
    const std::string previous = m_active;
    m_active = std::string(name);
    m_lastSaved.clear();
    m_lastSaved.push_back('\0'); // 어떤 텍스트와도 다르다 — 새 이름의 파일을 꼭 쓴다
    if (Expected<void> w = save(text); !w) {
        m_active = previous;
        return w;
    }
    remember();
    return {};
}

void LayoutService::remember() {
    if (Expected<void> w = writeStudioSetting(m_settingsFile, "layout", "\"" + m_active + "\""); !w) {
        m_warnings.push_back(std::format("지금 레이아웃을 기억하지 못했다 — {}", w.error().describe()));
    }
}

std::vector<std::string> LayoutService::takeWarnings() {
    std::vector<std::string> out;
    out.swap(m_warnings);
    return out;
}

} // namespace sbx::editor::services
