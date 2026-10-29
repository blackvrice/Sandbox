#include "apps/client/UserSettings.hpp"

#include <chrono>
#include <cstdlib>
#include <format>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"

namespace sbx::client {

namespace fs = std::filesystem;

namespace {

std::string env(const char* name) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // getenv — 읽기만, 시작할 때 한 번
#endif
    const char* v = std::getenv(name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return v != nullptr ? std::string(v) : std::string();
}

std::string utcStamp() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y%m%d-%H%M%S}", now);
}

} // namespace

ClientSettingsParse parseClientSettings(std::string_view jsonText) {
    ClientSettingsParse r;
    const nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        r.errors.emplace_back("JSON 객체가 아니다");
        return r;
    }
    if (const auto it = j.find("schemaVersion"); it != j.end()) {
        if (!it->is_number_unsigned() || it->get<u64>() == 0) {
            r.errors.emplace_back("schemaVersion 은 1 이상의 정수");
            return r;
        }
        if (it->get<u64>() > kClientSettingsVersion) {
            r.newerVersion = true;
            return r;
        }
    }
    for (const auto& [key, v] : j.items()) {
        if (key == "schemaVersion") {
            continue;
        }
        if (key == "uiScale") {
            if (!v.is_number()) {
                r.errors.emplace_back("uiScale 은 수");
                continue;
            }
            const f64 s = v.get<f64>();
            if (!(s >= kMinUiScale && s <= kMaxUiScale)) {
                r.errors.push_back(std::format("uiScale {} 는 {} ~ {} 밖", s, kMinUiScale, kMaxUiScale));
                continue;
            }
            r.settings.uiScale = static_cast<f32>(s);
        } else if (key == "updateCheck") {
            if (!v.is_boolean()) {
                r.errors.emplace_back("updateCheck 는 true · false");
                continue;
            }
            r.settings.updateCheck = v.get<bool>();
        } else if (key == "updateFeed") {
            if (!v.is_string()) {
                r.errors.emplace_back("updateFeed 는 문자열 (파일)");
                continue;
            }
            r.settings.updateFeed = v.get<std::string>();
        } else {
            r.warnings.push_back(std::format("모르는 키 '{}' 는 건너뛴다", key));
        }
    }
    return r;
}

fs::path defaultConfigRoot() {
    if (const std::string d = env("SBX_CONFIG_DIR"); !d.empty()) {
        return fs::path(d);
    }
#ifdef _WIN32
    if (const std::string d = env("APPDATA"); !d.empty()) {
        return fs::path(d) / "sandbox";
    }
#elif defined(__APPLE__)
    if (const std::string d = env("HOME"); !d.empty()) {
        return fs::path(d) / "Library" / "Application Support" / "sandbox"; // 07 8장
    }
#else
    if (const std::string d = env("XDG_CONFIG_HOME"); !d.empty()) {
        return fs::path(d) / "sandbox";
    }
    if (const std::string d = env("HOME"); !d.empty()) {
        return fs::path(d) / ".config" / "sandbox";
    }
#endif
    std::error_code ec;
    return fs::temp_directory_path(ec) / "sandbox-config";
}

Expected<fs::path> moveAside(const fs::path& file) {
    const std::string base = file.filename().string() + ".broken-" + utcStamp();
    for (int n = 1; n < 100; ++n) {
        const fs::path to = file.parent_path() / (n == 1 ? base : std::format("{}-{}", base, n));
        std::error_code ec;
        if (fs::exists(to, ec)) {
            continue;
        }
        fs::rename(file, to, ec);
        if (ec) {
            return makeError(ErrorCode::IoError, std::format("깨진 설정 파일을 옮길 수 없다 — {}", ec.message()),
                             io::displayPath(file));
        }
        return to;
    }
    return makeError(ErrorCode::IoError, "같은 초에 옮긴 파일이 너무 많다", io::displayPath(file));
}

UserSettings loadUserSettings(const fs::path& configRoot) {
    UserSettings out;
    std::error_code ec;
    // 깨진 파일: 옮기고 알림 (옮기지 못해도 기본값으로 — 다음 시작에 다시 알린다)
    const auto broken = [&](const fs::path& file, const std::string& why) {
        auto moved = moveAside(file);
        out.notices.push_back(moved ? std::format("설정 파일 {} 이 깨져 {} 로 옮겼다 ({}) — 기본값으로 시작한다",
                                                  file.filename().string(), moved->filename().string(), why)
                                    : std::format("설정 파일 {} 이 깨졌다 ({}) — 기본값으로 시작한다. {}",
                                                  file.filename().string(), why, moved.error().message));
    };

    const fs::path settingsFile = configRoot / std::string(kClientSettingsFile);
    if (fs::exists(settingsFile, ec)) {
        const auto text = io::readFile(settingsFile);
        if (!text) {
            out.notices.push_back(
                std::format("설정 파일 {} 을 읽을 수 없다 — 기본값으로 시작한다", io::displayPath(settingsFile)));
        } else {
            ClientSettingsParse p = parseClientSettings(*text);
            if (p.newerVersion) {
                out.notices.push_back(std::format("설정 파일 {} 은 더 새 클라이언트의 것이다 — 읽지 않고 기본값으로 "
                                                  "(파일은 그대로 둔다)",
                                                  settingsFile.filename().string()));
            } else if (!p.errors.empty()) {
                std::string why;
                for (const std::string& e : p.errors) {
                    why += (why.empty() ? "" : "; ") + e;
                }
                broken(settingsFile, why);
            } else {
                out.settings = p.settings;
                for (const std::string& w : p.warnings) {
                    out.notices.push_back(std::format("설정 파일 {}: {}", settingsFile.filename().string(), w));
                }
            }
        }
    }

    const fs::path inputFile = configRoot / std::string(kUserInputFile);
    if (fs::exists(inputFile, ec)) {
        auto map = platform::ActionMap::loadFile(inputFile);
        if (map) {
            out.input = std::move(*map);
        } else {
            broken(inputFile, map.error().message);
        }
    }
    return out;
}

} // namespace sbx::client
