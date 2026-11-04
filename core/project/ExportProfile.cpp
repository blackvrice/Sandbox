#include "core/project/ExportProfile.hpp"

#include <algorithm>
#include <array>
#include <format>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"

namespace sbx::project {
namespace {

namespace fs = std::filesystem;
using content::ContentIssue;
using json = nlohmann::json;
using Severity = ContentIssue::Severity;

constexpr usize kMaxNameBytes = 64;
constexpr usize kMaxProfileBytes = 64 * 1024;
constexpr usize kMaxGroups = 64;
constexpr std::array kKeys{"platform", "arch", "renderBackends", "assetGroups"};
constexpr std::array kPlatforms{"windows", "linux", "macos"};
constexpr std::array kArchs{"x64", "arm64"};

// 플랫폼마다 이 엔진이 낼 수 있는 백엔드 (render/rhi/RenderDeviceFactory — Windows 는 D3D12 기본 · Vulkan 선택 빌드)
struct PlatformBackends {
    std::string_view platform;
    std::array<std::string_view, 2> backends;
};
constexpr std::array kBackendsByPlatform{PlatformBackends{"windows", {"d3d12", "vulkan"}},
                                         PlatformBackends{"linux", {"vulkan", ""}},
                                         PlatformBackends{"macos", {"metal", ""}}};

template <usize N>
bool oneOf(std::string_view v, const std::array<const char*, N>& set) {
    return std::any_of(set.begin(), set.end(), [&](const char* s) { return v == s; });
}

bool platformHasBackend(std::string_view platform, std::string_view backend) {
    for (const PlatformBackends& p : kBackendsByPlatform) {
        if (p.platform == platform) {
            return !backend.empty() &&
                   std::find(p.backends.begin(), p.backends.end(), backend) != p.backends.end();
        }
    }
    return false;
}

bool validName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxNameBytes && std::all_of(name.begin(), name.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
               ch == '-';
    });
}

// 문자열 배열 하나 (모양 · 겹침). 문제면 오류를 남기고 false
bool stringArray(const json& j, const char* key, usize max, std::vector<std::string>& out, const std::string& file,
                 std::vector<ContentIssue>& issues) {
    const json& a = j[key];
    const std::string where = std::string("/") + key;
    if (!a.is_array() || a.size() > max) {
        issues.push_back({Severity::Error, "P10", file, where, std::format("{} 는 {} 개까지의 문자열 배열", key, max)});
        return false;
    }
    for (usize i = 0; i < a.size(); ++i) {
        if (!a[i].is_string() || a[i].get<std::string>().empty()) {
            issues.push_back({Severity::Error, "P10", file, std::format("{}/{}", where, i), "빈 문자열이 아닌 값"});
            return false;
        }
        std::string v = a[i].get<std::string>();
        if (std::find(out.begin(), out.end(), v) != out.end()) {
            issues.push_back({Severity::Error, "P10", file, std::format("{}/{}", where, i),
                              std::format("'{}' 가 두 번 있다", v)});
            return false;
        }
        out.push_back(std::move(v));
    }
    return true;
}

} // namespace

std::span<const ExportTarget> verifiedTargets() noexcept {
    return {}; // docs/qa/MANUAL-QA.md 에 실기 통과 기록이 생기면 (platform, arch, backend) 를 더한다
}

ExportTarget hostTarget() noexcept {
    ExportTarget t;
#if defined(_WIN32)
    t.platform = "windows";
#elif defined(__APPLE__)
    t.platform = "macos";
#elif defined(__linux__)
    t.platform = "linux";
#endif
#if defined(_M_X64) || defined(__x86_64__)
    t.arch = "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    t.arch = "arm64";
#endif
    return t;
}

std::string describeTarget(std::string_view platform, std::string_view arch) {
    return std::format("{}-{}", platform, arch);
}

bool ExportProfileParse::ok() const noexcept {
    return std::none_of(issues.begin(), issues.end(), [](const ContentIssue& i) { return i.severity == Severity::Error; });
}

std::string exportProfilePath(std::string_view name) {
    return std::format("{}/{}.json", kExportDir, name);
}

ExportProfileParse parseExportProfile(std::string_view name, std::string_view jsonText,
                                      std::span<const ExportTarget> verified) {
    ExportProfileParse r;
    r.profile.name = std::string(name);
    const std::string file = exportProfilePath(name);
    const auto error = [&](std::string pointer, std::string message) {
        r.issues.push_back({Severity::Error, "P10", file, std::move(pointer), std::move(message)});
    };
    if (!validName(name)) {
        error("", std::format("프로필 이름은 [A-Za-z0-9_-] 1 ~ {} 바이트", kMaxNameBytes));
        return r;
    }
    if (jsonText.size() > kMaxProfileBytes) {
        error("", std::format("{} 바이트를 넘는다", kMaxProfileBytes));
        return r;
    }
    const json j = json::parse(jsonText, nullptr, false);
    if (!j.is_object()) {
        error("", "JSON 객체가 아니다");
        return r;
    }
    for (const auto& [key, value] : j.items()) {
        if (!oneOf(key, kKeys)) {
            error("/" + key, std::format("모르는 키 '{}' (platform · arch · renderBackends · assetGroups)", key));
        }
    }
    const auto text = [&](const char* key, const auto& allowed, std::string& out) {
        if (!j.contains(key) || !j[key].is_string()) {
            error(std::string("/") + key, std::format("{} 문자열이 필요하다", key));
            return;
        }
        out = j[key].get<std::string>();
        if (!oneOf(out, allowed)) {
            error(std::string("/") + key, std::format("모르는 {} '{}'", key, out));
        }
    };
    text("platform", kPlatforms, r.profile.platform);
    text("arch", kArchs, r.profile.arch);
    if (!j.contains("renderBackends")) {
        error("/renderBackends", "renderBackends 배열이 필요하다 (하나 이상)");
    } else if (stringArray(j, "renderBackends", 3, r.profile.renderBackends, file, r.issues)) {
        if (r.profile.renderBackends.empty()) {
            error("/renderBackends", "백엔드가 하나 이상 있어야 한다");
        }
        for (usize i = 0; i < r.profile.renderBackends.size(); ++i) {
            const std::string& b = r.profile.renderBackends[i];
            if (oneOf(r.profile.platform, kPlatforms) && !platformHasBackend(r.profile.platform, b)) {
                error(std::format("/renderBackends/{}", i),
                      std::format("{} 는 '{}' 백엔드를 낼 수 없다 (windows = d3d12 · vulkan, linux = vulkan, "
                                  "macos = metal)",
                                  r.profile.platform, b));
            }
        }
    }
    if (j.contains("assetGroups") && stringArray(j, "assetGroups", kMaxGroups, r.profile.assetGroups, file, r.issues)) {
        // Asset Group (NG-02) 은 아직 없다 — 어떤 이름도 찾을 수 없다
        for (usize i = 0; i < r.profile.assetGroups.size(); ++i) {
            error(std::format("/assetGroups/{}", i),
                  std::format("Asset Group '{}' 이 없다 (그룹은 NG-02 — 아직 만들 수 없다)", r.profile.assetGroups[i]));
        }
    }
    if (!r.ok()) {
        return r;
    }
    for (usize i = 0; i < r.profile.renderBackends.size(); ++i) {
        const ExportTarget t{r.profile.platform, r.profile.arch, r.profile.renderBackends[i]};
        if (std::find(verified.begin(), verified.end(), t) == verified.end()) {
            r.issues.push_back({Severity::Warning, "P10", file, std::format("/renderBackends/{}", i),
                                std::format("미검증 대상 {} · {} — 실기 기록이 없다 (docs/qa/MANUAL-QA.md). 막지 않는다",
                                            describeTarget(t.platform, t.arch), t.backend)});
        }
    }
    return r;
}

std::string writeExportProfile(const ExportProfile& profile) {
    json j{{"platform", profile.platform}, {"arch", profile.arch}, {"renderBackends", profile.renderBackends}};
    if (!profile.assetGroups.empty()) {
        j["assetGroups"] = profile.assetGroups;
    }
    return j.dump(2) + "\n";
}

ExportProfiles loadExportProfiles(const std::filesystem::path& projectDir, std::span<const ExportTarget> verified) {
    ExportProfiles out;
    const fs::path dir = projectDir / std::string(kExportDir);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return out;
    }
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        entries.push_back(it->path());
    }
    if (ec) {
        out.issues.push_back(
            {Severity::Error, "P10", std::string(kExportDir), "", std::format("폴더를 읽을 수 없다 — {}", ec.message())});
        return out;
    }
    std::sort(entries.begin(), entries.end());
    usize profiles = 0;
    for (const fs::path& p : entries) {
        const std::string file = std::format("{}/{}", kExportDir, p.filename().generic_string());
        std::error_code fec;
        if (!fs::is_regular_file(fs::symlink_status(p, fec)) || p.extension() != ".json") {
            out.issues.push_back(
                {Severity::Warning, "P10", file, "", "export/ 에는 <이름>.json 프로필 파일만 둔다 — 읽지 않았다"});
            continue;
        }
        if (++profiles > kMaxExportProfiles) {
            out.issues.push_back(
                {Severity::Error, "P10", file, "", std::format("프로필은 {} 개까지", kMaxExportProfiles)});
            break;
        }
        const auto text = io::readFile(p);
        if (!text) {
            out.issues.push_back(
                {Severity::Error, "P10", file, "", std::format("읽을 수 없다 — {}", text.error().describe())});
            continue;
        }
        ExportProfileParse parsed = parseExportProfile(p.stem().generic_string(), *text, verified);
        if (parsed.ok()) {
            out.profiles.push_back(parsed.profile);
        }
        out.issues.insert(out.issues.end(), std::make_move_iterator(parsed.issues.begin()),
                          std::make_move_iterator(parsed.issues.end()));
    }
    return out;
}

} // namespace sbx::project
