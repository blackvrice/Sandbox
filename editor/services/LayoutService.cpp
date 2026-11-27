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

fs::path LayoutService::dockFileFor(std::string_view name) const {
    return m_folder / (std::string(name) + ".guidock");
}

bool LayoutService::isDockBytes(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < kDockMagic.size() || bytes.size() > kMaxDockBytes) {
        return false;
    }
    for (usize i = 0; i < kDockMagic.size(); ++i) {
        if (static_cast<char>(bytes[i]) != kDockMagic[i]) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> LayoutService::names() const {
    std::vector<std::string> out;
    if (enabled()) {
        std::error_code ec;
        for (fs::directory_iterator it(m_folder, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            const std::string stem = p.stem().string();
            if ((p.extension() == ".ini" || p.extension() == ".guidock") && isName(stem) && stem != kDefault) {
                out.push_back(stem);
            }
        }
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end()); // 두 형식이 다 있는 이름
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
    moveAside(file, "깨졌다");
    return std::string();
}

void LayoutService::moveAside(const fs::path& file, std::string_view why) {
    std::error_code ec;
    fs::path aside = file;
    aside += ".broken";
    fs::remove(aside, ec);
    fs::rename(file, aside, ec);
    m_warnings.push_back(
        ec ? std::format("레이아웃 {} 이 {} — 옮기지 못했다 ({}), 기본 배치로", io::displayPath(file), why, ec.message())
           : std::format("레이아웃 {} 이 {} — {} 로 옮기고 기본 배치로", io::displayPath(file), why,
                         io::displayPath(aside)));
}

std::optional<std::vector<std::byte>> LayoutService::loadDock() {
    m_dockFor = m_active;
    m_lastDock.clear();
    if (!enabled()) {
        return std::nullopt;
    }
    const fs::path file = dockFileFor(m_active);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return std::nullopt;
    }
    const auto size = fs::file_size(file, ec);
    Expected<std::string> raw = !ec && size <= kMaxDockBytes ? io::readFile(file) : Expected<std::string>(std::string());
    if (!raw) {
        m_warnings.push_back(std::format("레이아웃 {} 을 읽지 못했다 — {} — 기본 배치로", io::displayPath(file),
                                         raw.error().describe()));
        return std::nullopt;
    }
    std::vector<std::byte> bytes(raw->size());
    std::ranges::transform(*raw, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    if (!isDockBytes(bytes)) {
        moveAside(file, "깨졌다 (GUIDOCK 이 아니거나 너무 크다)");
        return std::nullopt;
    }
    m_lastDock = bytes;
    return bytes;
}

Expected<void> LayoutService::saveDock(std::span<const std::byte> bytes) {
    if (!enabled()) {
        return {};
    }
    if (!isDockBytes(bytes)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("레이아웃 '{}' 의 Dock 배치가 GUIDOCK 바이트가 아니다 ({} 바이트)", m_active,
                                     bytes.size()));
    }
    if (m_dockFor == m_active && std::ranges::equal(bytes, m_lastDock)) {
        return {};
    }
    std::error_code ec;
    fs::create_directories(m_folder, ec);
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (Expected<void> w = io::writeFileAtomic(dockFileFor(m_active), text); !w) {
        return w;
    }
    m_dockFor = m_active;
    m_lastDock.assign(bytes.begin(), bytes.end());
    return {};
}

void LayoutService::quarantineDock(std::string_view why) {
    m_lastDock.clear();
    std::error_code ec;
    if (enabled() && fs::exists(dockFileFor(m_active), ec)) {
        moveAside(dockFileFor(m_active), std::format("복원되지 않는다 ({})", why));
    }
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
