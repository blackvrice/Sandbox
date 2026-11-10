#include "editor/services/StudioSettings.hpp"

#include <cmath>
#include <format>
#include <system_error>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"

namespace sbx::editor::services {

namespace fs = std::filesystem;

namespace {

// 파일을 읽는다 — 없으면 빈 객체, 객체가 아니면 nullopt (깨짐)
std::optional<nlohmann::json> readObject(const fs::path& file, bool& exists) {
    std::error_code ec;
    exists = fs::exists(file, ec);
    if (!exists) {
        return nlohmann::json::object();
    }
    const Expected<std::string> text = io::readFile(file);
    if (!text) {
        return std::nullopt;
    }
    nlohmann::json j = nlohmann::json::parse(*text, nullptr, false);
    if (!j.is_object()) {
        return std::nullopt;
    }
    return j;
}

bool newer(const nlohmann::json& j) {
    const auto v = j.find("schemaVersion");
    return v != j.end() && v->is_number_unsigned() && v->get<u64>() > kStudioSettingsVersion;
}

} // namespace

std::string moveStudioFileAside(const fs::path& file) {
    fs::path aside = file;
    aside += ".broken";
    std::error_code ec;
    fs::remove(aside, ec);
    fs::rename(file, aside, ec);
    if (ec) {
        return std::format("Studio 설정 {} 을 읽지 못했다 — 옆으로 옮기지도 못했다 ({}), 기본값으로 시작한다",
                           io::displayPath(file), ec.message());
    }
    return std::format("Studio 설정 {} 을 읽지 못했다 — {} 로 옮기고 기본값으로 시작한다", io::displayPath(file),
                       io::displayPath(aside));
}

StudioSettingsLoad loadStudioSettings(const fs::path& file) {
    StudioSettingsLoad out;
    if (file.empty()) {
        return out;
    }
    bool exists = false;
    const std::optional<nlohmann::json> j = readObject(file, exists);
    if (!j) {
        out.warnings.push_back(moveStudioFileAside(file));
        return out;
    }
    if (newer(*j)) {
        out.newerVersion = true;
        out.warnings.push_back(std::format("Studio 설정 {} 은 더 새 판 (schemaVersion {}) — 읽기만 하고 바꾸지 않는다",
                                           io::displayPath(file), (*j)["schemaVersion"].get<u64>()));
    }
    if (const auto l = j->find("layout"); l != j->end()) {
        if (l->is_string()) {
            out.settings.layout = l->get<std::string>();
        } else {
            out.warnings.push_back("Studio 설정 layout 은 문자열이어야 한다 — 기본 레이아웃");
        }
    }
    if (const auto d = j->find("developerPanels"); d != j->end()) {
        if (d->is_boolean()) {
            out.settings.developerPanels = d->get<bool>();
        } else {
            out.warnings.push_back("Studio 설정 developerPanels 는 true · false 여야 한다 — 끔");
        }
    }
    if (const auto d = j->find("density"); d != j->end()) {
        if (d->is_string() && (*d == "compact" || *d == "comfortable")) {
            out.settings.compactDensity = *d == "compact";
        } else {
            out.warnings.push_back("Studio 설정 density 는 \"comfortable\" · \"compact\" — 보통 간격");
        }
    }
    if (const auto a = j->find("autosaveSeconds"); a != j->end()) {
        const double v = a->is_number() ? a->get<double>() : -1.0;
        if (std::isfinite(v) && v >= 0.0 && v <= kMaxAutosaveSeconds) {
            out.settings.autosaveSeconds = v;
        } else {
            out.warnings.push_back(std::format("Studio 설정 autosaveSeconds 는 0 ~ {:.0f} 의 수여야 한다 — 기본 주기",
                                               kMaxAutosaveSeconds));
        }
    }
    return out;
}

Expected<void> writeStudioSetting(const fs::path& file, std::string_view key, std::string_view jsonValue) {
    if (file.empty()) {
        return {};
    }
    nlohmann::json value = nlohmann::json::parse(jsonValue, nullptr, false);
    if (value.is_discarded()) {
        return makeError(ErrorCode::InvalidArgument, std::format("Studio 설정 '{}' 의 값이 JSON 이 아니다", key));
    }
    bool exists = false;
    std::optional<nlohmann::json> j = readObject(file, exists);
    if (!j) {
        (void)moveStudioFileAside(file); // 읽을 때 이미 알렸다 — 여기까지 왔으면 읽은 뒤 누가 망가뜨렸다
        j = nlohmann::json::object();
    }
    if (newer(*j)) {
        return makeError(ErrorCode::Unsupported, "Studio 설정이 더 새 판이라 바꾸지 않는다", io::displayPath(file));
    }
    (*j)["schemaVersion"] = kStudioSettingsVersion;
    (*j)[std::string(key)] = std::move(value);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    return io::writeFileAtomic(file, j->dump(2) + "\n");
}

} // namespace sbx::editor::services
