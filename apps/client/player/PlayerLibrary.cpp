#include "apps/client/player/PlayerLibrary.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/package/GamePackage.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::client {

namespace fs = std::filesystem;

namespace {

std::string utcNow() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto day = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day ymd{day};
    const std::chrono::hh_mm_ss hms{now - day};
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()), hms.hours().count(),
                       hms.minutes().count(), hms.seconds().count());
}

std::string utf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

fs::path fromUtf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

} // namespace

PlayerLibrary::PlayerLibrary(fs::path file) : m_file(std::move(file)) {
    load();
}

void PlayerLibrary::load() {
    m_recent.clear();
    std::error_code ec;
    if (m_file.empty() || !fs::exists(m_file, ec)) {
        return;
    }
    const Expected<std::string> text = io::readFile(m_file);
    const nlohmann::json j = text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
    const auto list = j.is_object() ? j.find("recentGames") : j.end();
    if (!j.is_object() || (list != j.end() && !list->is_array())) {
        fs::path aside = m_file;
        aside += ".broken";
        fs::remove(aside, ec);
        fs::rename(m_file, aside, ec);
        m_warnings.push_back(std::format("Player 설정 {} 을 읽지 못했다 — {} 로 옮기고 최근 게임을 비웠다",
                                         io::displayPath(m_file), io::displayPath(aside)));
        return;
    }
    if (list == j.end()) {
        return;
    }
    for (const nlohmann::json& e : *list) {
        if (m_recent.size() >= kMaxRecent) {
            break;
        }
        if (!e.is_object() || !e.contains("path") || !e["path"].is_string()) {
            continue;
        }
        RecentGame g;
        g.path = fromUtf8(e["path"].get<std::string>());
        g.gameId = e.value("gameId", std::string());
        g.name = e.value("name", std::string());
        g.version = e.value("version", std::string());
        g.playedAt = e.value("playedAt", std::string());
        g.missing = !fs::is_regular_file(g.path, ec);
        m_recent.push_back(std::move(g));
    }
}

void PlayerLibrary::save() {
    if (m_file.empty()) {
        return;
    }
    nlohmann::json list = nlohmann::json::array();
    for (const RecentGame& g : m_recent) {
        list.push_back({{"path", utf8(g.path)},
                        {"gameId", g.gameId},
                        {"name", g.name},
                        {"version", g.version},
                        {"playedAt", g.playedAt}});
    }
    const nlohmann::json j = {{"schemaVersion", 1}, {"recentGames", std::move(list)}};
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);
    if (const Expected<void> w = io::writeFileAtomic(m_file, j.dump(2) + "\n"); !w) {
        m_warnings.push_back(std::format("최근 게임을 저장하지 못했다 — {}", w.error().describe()));
    }
}

void PlayerLibrary::remember(RecentGame game) {
    std::error_code ec;
    game.path = fs::absolute(game.path, ec).lexically_normal();
    if (game.playedAt.empty()) {
        game.playedAt = utcNow();
    }
    game.missing = false;
    std::erase_if(m_recent, [&](const RecentGame& g) { return g.path == game.path; });
    m_recent.insert(m_recent.begin(), std::move(game));
    if (m_recent.size() > kMaxRecent) {
        m_recent.resize(kMaxRecent);
    }
    save();
}

void PlayerLibrary::forget(const fs::path& path) {
    std::error_code ec;
    const fs::path key = fs::absolute(path, ec).lexically_normal();
    std::erase_if(m_recent, [&](const RecentGame& g) { return g.path == key || g.path == path; });
    save();
}

std::vector<std::string> PlayerLibrary::takeWarnings() {
    std::vector<std::string> out;
    out.swap(m_warnings);
    return out;
}

PlayError describePlayError(const Error& error) {
    PlayError e;
    e.detail = error.describe();
    const std::string& m = error.message;
    switch (error.code) {
    case ErrorCode::NotFound:
        e.title = "게임 파일을 찾을 수 없다";
        e.advice = "파일이 옮겨졌거나 지워졌다 — 경로를 확인하거나 다시 받는다";
        break;
    case ErrorCode::VersionMismatch:
        e.title = "이 Sandbox 와 맞지 않는 게임이다";
        e.advice = "게임이 더 새 Sandbox 를 요구한다 — Sandbox 를 새 버전으로 올린다 (또는 만든 사람에게 판을 묻는다)";
        break;
    case ErrorCode::ValidationFailed:
    case ErrorCode::ParseError:
        if (m.find("패키지(.sbpkg)가 아니다") != std::string::npos) {
            e.title = "게임 파일 (.sbpkg) 이 아니다";
            e.advice = "Sandbox 게임 패키지를 연다 — 만든 사람이 Studio 의 빌드로 만든 .sbpkg";
        } else {
            e.title = "게임 파일이 손상됐다";
            e.advice = "받는 중에 깨졌을 수 있다 — 받은 곳에서 다시 받는다";
        }
        break;
    case ErrorCode::OutOfRange:
        e.title = "게임 파일이 너무 크다";
        e.advice = "이 Sandbox 가 여는 크기를 넘는다 — 만든 사람에게 알린다";
        break;
    default:
        e.title = "게임을 열 수 없다";
        e.advice = "보고 묶음을 만들어 만든 사람 · Sandbox 에 보낸다";
        break;
    }
    return e;
}

RecentGame recentGameOf(const fs::path& package) {
    RecentGame g;
    g.path = package;
    if (const auto idx = package::readPackageIndex(package)) {
        g.gameId = idx->gameId;
        g.name = idx->name;
        g.version = idx->version;
    }
    return g;
}

} // namespace sbx::client
