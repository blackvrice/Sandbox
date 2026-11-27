// Studio 설정 · 레이아웃 (v0.8 M6-T6 — editor/services/StudioSettings · LayoutService, ADR-0114): studio.json 은 여러
// 서비스의 키를 같이 든다 (쓰기는 자기 키만 — 다른 키 그대로), 깨진 파일 옮기기, 틀린 키 하나는 그 키만 무시, 더 새
// 판은 쓰지 않음. 레이아웃 = studio/layouts/<이름>.ini — 이름 규칙, 저장 · 고르기 · 기억, 깨진 ini 옮기기, 안전 모드
// (폴더 없음) 는 파일을 쓰지 않는다
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/services/LayoutService.hpp"
#include "editor/services/ProjectService.hpp"
#include "editor/services/StudioSettings.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        std::random_device rd;
        path = fs::temp_directory_path() / std::format("sbx_layout_{}_{:08x}", name, rd());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void write(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

nlohmann::json readJson(const fs::path& p) {
    return nlohmann::json::parse(io::readFile(p).value());
}

constexpr std::string_view kIni = "[Window][편집]\nPos=123,45\nSize=390,330\nCollapsed=0\n\n"
                                  "[Docking][Data]\n";

} // namespace

TEST_CASE("Studio 설정: 키마다 쓰기 · 다른 키 그대로 · 깨진 파일 · 틀린 키 · 더 새 판 (v0.8 M6-T6)") {
    TempDir t("settings");
    const fs::path file = t.path / "studio.json";

    // 없는 파일 = 기본값, 경고 없음
    StudioSettingsLoad none = loadStudioSettings(file);
    CHECK(none.settings.layout.empty());
    CHECK_FALSE(none.settings.autosaveSeconds.has_value());
    CHECK(none.warnings.empty());

    // 자기 키만 — 모르는 키 · 다른 서비스의 키는 그대로
    write(file, R"({"recentProjects": [{"path": "A"}], "future": {"x": 1}})");
    REQUIRE(writeStudioSetting(file, "layout", "\"scene\"").has_value());
    REQUIRE(writeStudioSetting(file, "autosaveSeconds", "60").has_value());
    const nlohmann::json j = readJson(file);
    CHECK(j["schemaVersion"] == 1);
    CHECK(j["layout"] == "scene");
    CHECK(j["recentProjects"][0]["path"] == "A");
    CHECK(j["future"]["x"] == 1);
    StudioSettingsLoad l = loadStudioSettings(file);
    CHECK(l.settings.layout == "scene");
    REQUIRE(l.settings.autosaveSeconds.has_value());
    CHECK(*l.settings.autosaveSeconds == doctest::Approx(60.0));
    CHECK(l.warnings.empty());

    // 틀린 키 하나 — 그 키만 무시 (파일은 옮기지 않는다 — 다른 서비스의 키도 들고 있다)
    write(file, R"({"layout": 7, "autosaveSeconds": -1, "recentProjects": []})");
    l = loadStudioSettings(file);
    CHECK(l.settings.layout.empty());
    CHECK_FALSE(l.settings.autosaveSeconds.has_value());
    CHECK(l.warnings.size() == 2);
    CHECK(fs::exists(file));

    // 테마 (v0.8 ST-06-2) — 아는 이름만, 모르면 그 키만 무시
    write(file, R"({"theme": "high-contrast"})");
    l = loadStudioSettings(file);
    CHECK(l.settings.theme == "high-contrast");
    CHECK(l.warnings.empty());
    write(file, R"({"theme": "pink"})");
    l = loadStudioSettings(file);
    CHECK(l.settings.theme.empty());
    CHECK(l.warnings.size() == 1);

    // 켜진 창 (v0.13 ST-08-2) — 문자열 배열만. 없으면 nullopt (기본 배치의 창), 빈 배열 = 모두 닫음
    write(file, R"({"windows": ["hierarchy", "console"]})");
    l = loadStudioSettings(file);
    REQUIRE(l.settings.windows.has_value());
    CHECK(*l.settings.windows == std::vector<std::string>{"hierarchy", "console"});
    CHECK(l.warnings.empty());
    write(file, R"({"windows": []})");
    CHECK(loadStudioSettings(file).settings.windows == std::vector<std::string>{});
    write(file, R"({"windows": ["hierarchy", 3]})");
    l = loadStudioSettings(file);
    CHECK_FALSE(l.settings.windows.has_value());
    CHECK(l.warnings.size() == 1);
    REQUIRE(writeStudioSetting(file, "windows", R"(["assets"])").has_value());
    CHECK(loadStudioSettings(file).settings.windows == std::vector<std::string>{"assets"});

    // 깨진 파일 — studio.json.broken 으로 옮기고 기본값
    write(file, "{ broken");
    l = loadStudioSettings(file);
    REQUIRE(l.warnings.size() == 1);
    CHECK(l.warnings.front().find("옮기고") != std::string::npos);
    CHECK_FALSE(fs::exists(file));
    CHECK(fs::exists(t.path / "studio.json.broken"));
    // 깨진 파일 위에 쓰기 — 옮기고 새로 (같은 이름의 옛 .broken 은 바꾼다)
    write(file, "[1, 2]");
    REQUIRE(writeStudioSetting(file, "layout", "\"a\"").has_value());
    CHECK(readJson(file)["layout"] == "a");
    CHECK(io::readFile(t.path / "studio.json.broken").value() == "[1, 2]");

    // 더 새 판 — 읽기만, 쓰지 않는다
    write(file, R"({"schemaVersion": 2, "layout": "next"})");
    l = loadStudioSettings(file);
    CHECK(l.newerVersion);
    CHECK(l.settings.layout == "next");
    CHECK(writeStudioSetting(file, "layout", "\"x\"").error().code == ErrorCode::Unsupported);
    CHECK(readJson(file)["layout"] == "next");

    // 값이 JSON 이 아니면 거절 · 파일 이름이 비면 아무 것도 하지 않는다 (안전 모드)
    CHECK_FALSE(writeStudioSetting(t.path / "other.json", "layout", "not json").has_value());
    CHECK(writeStudioSetting({}, "layout", "\"x\"").has_value());
    CHECK(loadStudioSettings({}).warnings.empty());
}

TEST_CASE("Studio 설정: 최근 프로젝트와 레이아웃이 한 파일 — 서로의 키를 지우지 않는다 (v0.8 M6-T6)") {
    TempDir t("shared");
    const fs::path file = t.path / "studio.json";
    write(file, R"({"schemaVersion": 1, "layout": "scene"})"); // 최근 목록이 아직 없다 — 깨진 파일이 아니다
    ecs::ComponentCatalog catalog;
    REQUIRE(comp::registerCoreComponents(catalog).has_value());
    ProjectService ps(catalog, SemVer{0, 8, 0, {}, {}}, SBX_CONTENT_DIR, file, {});
    CHECK(ps.takeWarnings().empty());
    CHECK(ps.recent().empty());
    REQUIRE(ps.create({t.path / "Game", "studio.shared", "", "empty"}).has_value());
    const nlohmann::json j = readJson(file);
    CHECK(j["layout"] == "scene"); // 최근 목록을 쓰며 레이아웃을 지우지 않았다
    REQUIRE(j["recentProjects"].size() == 1);

    LayoutService layouts(t.path / "layouts", file, "scene");
    REQUIRE(layouts.saveAs("test", kIni).has_value());
    const nlohmann::json k = readJson(file);
    CHECK(k["layout"] == "test");
    CHECK(k["recentProjects"].size() == 1); // 레이아웃을 쓰며 최근 목록을 지우지 않았다
}

TEST_CASE("레이아웃: 이름 · ini 모양 (v0.8 M6-T6)") {
    CHECK(LayoutService::isName("default"));
    CHECK(LayoutService::isName("scene-2_a"));
    CHECK_FALSE(LayoutService::isName(""));
    CHECK_FALSE(LayoutService::isName("Scene"));
    CHECK_FALSE(LayoutService::isName("../x"));
    CHECK_FALSE(LayoutService::isName("a.b"));
    CHECK_FALSE(LayoutService::isName(std::string(33, 'a')));
    CHECK(LayoutService::isName(std::string(32, 'a')));

    CHECK(LayoutService::isLayoutText(""));
    CHECK(LayoutService::isLayoutText(kIni));
    CHECK(LayoutService::isLayoutText("; 주석\r\n[Window][a]\r\nPos=1,2\r\n"));
    CHECK(LayoutService::isLayoutText("[Window][x\xEF\xBF\xBD]\n")); // 진짜 U+FFFD 는 괜찮다
    CHECK_FALSE(LayoutService::isLayoutText("hello world\n"));
    CHECK_FALSE(LayoutService::isLayoutText("[Window]\n"));
    CHECK_FALSE(LayoutService::isLayoutText(std::string_view("[a][b]\n\0", 8)));
    CHECK_FALSE(LayoutService::isLayoutText("[Window][\xFF]\n")); // UTF-8 아님
    CHECK_FALSE(LayoutService::isLayoutText(std::string(LayoutService::kMaxBytes + 1, '=')));
}

TEST_CASE("레이아웃: 저장 · 고르기 · 기억 · 깨진 파일 · 안전 모드 (v0.8 M6-T6)") {
    TempDir t("service");
    const fs::path folder = t.path / "studio" / "layouts";
    const fs::path settings = t.path / "studio.json";

    LayoutService l(folder, settings, "");
    CHECK(l.active() == "default");
    CHECK(l.loadActive().empty()); // 파일이 없다 = 기본 배치
    CHECK(l.names() == std::vector<std::string>{"default"});

    // 지금 것 (default) 에 저장 — 같은 텍스트면 다시 쓰지 않는다
    REQUIRE(l.save(kIni).has_value());
    CHECK(io::readFile(l.fileFor("default")).value() == kIni);
    fs::remove(l.fileFor("default"));
    REQUIRE(l.save(kIni).has_value());
    CHECK_FALSE(fs::exists(l.fileFor("default")));
    CHECK_FALSE(l.save("not an ini line").has_value());

    // 다른 이름으로 저장 → 지금 것 · 기억 · 목록
    REQUIRE(l.saveAs("scene", kIni).has_value());
    CHECK(l.active() == "scene");
    CHECK(readJson(settings)["layout"] == "scene");
    CHECK(io::readFile(folder / "scene.ini").value() == kIni);
    CHECK_FALSE(l.saveAs("Bad Name", kIni).has_value());
    CHECK(l.active() == "scene");
    write(folder / "zz.ini", "[Window][a]\nPos=1,1\n");
    write(folder / "Not-A-Name.ini", "");
    write(folder / "notes.txt", "");
    CHECK(l.names() == std::vector<std::string>{"default", "scene", "zz"});

    // 고르기 — 그 텍스트 · 기억. 없는 이름은 기본 배치에서 시작 (그 이름으로 저장된다)
    const Expected<std::string> zz = l.select("zz");
    REQUIRE(zz.has_value());
    CHECK(*zz == "[Window][a]\nPos=1,1\n");
    CHECK(readJson(settings)["layout"] == "zz");
    const Expected<std::string> fresh = l.select("fresh");
    REQUIRE(fresh.has_value());
    CHECK(fresh->empty());
    CHECK(l.active() == "fresh");
    CHECK_FALSE(l.select("../escape").has_value());

    // 다시 시작 — 기억한 것을 읽는다
    LayoutService again(folder, settings, loadStudioSettings(settings).settings.layout);
    CHECK(again.active() == "fresh");

    // 깨진 ini — <이름>.ini.broken 으로 옮기고 기본 배치 + 경고 (한 번)
    write(folder / "scene.ini", "garbage that is not ini\n");
    REQUIRE(again.select("scene").has_value());
    CHECK(again.select("scene")->empty());
    CHECK(fs::exists(folder / "scene.ini.broken"));
    CHECK_FALSE(fs::exists(folder / "scene.ini"));
    const std::vector<std::string> w = again.takeWarnings();
    REQUIRE(w.size() == 1);
    CHECK(w.front().find("깨졌다") != std::string::npos);
    CHECK(again.takeWarnings().empty());
    // 너무 큰 파일도 깨짐
    write(folder / "big.ini", std::string(LayoutService::kMaxBytes + 10, '='));
    CHECK(again.select("big")->empty());
    CHECK(fs::exists(folder / "big.ini.broken"));

    // studio.json 의 틀린 이름 — 기본 + 경고
    LayoutService wrong(folder, settings, "../x");
    CHECK(wrong.active() == "default");
    CHECK(wrong.takeWarnings().size() == 1);

    // 안전 모드 (폴더 · 설정 파일 없음) — 고르기 · 저장은 되지만 아무 파일도 쓰지 않는다
    TempDir s("safe");
    LayoutService safe({}, {}, "scene");
    CHECK_FALSE(safe.enabled());
    CHECK(safe.loadActive().empty());
    REQUIRE(safe.save(kIni).has_value());
    REQUIRE(safe.saveAs("other", kIni).has_value());
    REQUIRE(safe.select("zz").has_value());
    CHECK(safe.names() == std::vector<std::string>{"default"});
    CHECK(fs::is_empty(s.path));
}

TEST_CASE("레이아웃: GraphicsUI 작업 공간 배치 (.guidock) — 이름별 저장 · 같으면 쓰지 않기 · 깨짐 · 거절 옮기기 · 안전 모드 (UI-R2-07)") {
    TempDir t("dock");
    const fs::path folder = t.path / "studio" / "layouts";
    const fs::path settings = t.path / "studio.json";
    const auto bytesOf = [](std::string_view s) {
        std::vector<std::byte> out;
        for (const char c : s) {
            out.push_back(static_cast<std::byte>(c));
        }
        return out;
    };
    const std::vector<std::byte> dockA = bytesOf("GUIDOCK\x01\x02payload-a");
    const std::vector<std::byte> dockB = bytesOf("GUIDOCK\x01\x02payload-b");

    CHECK(LayoutService::isDockBytes(dockA));
    CHECK_FALSE(LayoutService::isDockBytes(bytesOf("GUIDOC")));
    CHECK_FALSE(LayoutService::isDockBytes(bytesOf("NOTDOCK....")));
    std::vector<std::byte> big = dockA;
    big.resize(LayoutService::kMaxDockBytes + 1);
    CHECK_FALSE(LayoutService::isDockBytes(big));

    LayoutService l(folder, settings, "");
    CHECK_FALSE(l.loadDock()); // 없음 = 프리셋으로
    REQUIRE(l.saveDock(dockA).has_value());
    CHECK(l.dockFileFor("default") == folder / "default.guidock");
    CHECK(io::readFile(folder / "default.guidock").value() == "GUIDOCK\x01\x02payload-a");
    CHECK_FALSE(fs::exists(l.fileFor("default"))); // ini 와 따로
    // 같은 바이트면 다시 쓰지 않는다
    fs::remove(folder / "default.guidock");
    REQUIRE(l.saveDock(dockA).has_value());
    CHECK_FALSE(fs::exists(folder / "default.guidock"));
    REQUIRE(l.saveDock(dockB).has_value());
    CHECK(fs::exists(folder / "default.guidock"));
    CHECK_FALSE(l.saveDock(bytesOf("garbage")).has_value());

    // 이름마다 — .guidock 만 있는 이름도 목록에, 두 형식이 다 있으면 한 번
    REQUIRE(l.select("play").has_value());
    CHECK_FALSE(l.loadDock());
    REQUIRE(l.saveDock(dockA).has_value()); // 다른 이름 — default 에 쓴 바이트와 같아도 쓴다
    CHECK(fs::exists(folder / "play.guidock"));
    write(folder / "play.ini", "[Window][a]\nPos=1,1\n");
    CHECK(l.names() == std::vector<std::string>{"default", "play"});
    REQUIRE(l.select("default").has_value());
    const auto back = l.loadDock();
    REQUIRE(back);
    CHECK(*back == dockB);

    // 깨진 파일 (머리 글자가 아님) — .guidock.broken 으로 옮기고 nullopt + 경고
    write(folder / "default.guidock", "not a dock");
    CHECK_FALSE(l.loadDock());
    CHECK(fs::exists(folder / "default.guidock.broken"));
    CHECK_FALSE(fs::exists(folder / "default.guidock"));
    std::vector<std::string> w = l.takeWarnings();
    REQUIRE(w.size() == 1);
    CHECK(w.front().find("깨졌다") != std::string::npos);

    // 복원이 거절한 바이트 (패널 목록이 바뀐 판) — 앱이 옮기게 한다
    REQUIRE(l.saveDock(dockA).has_value());
    l.quarantineDock("패널 목록이 다르다");
    CHECK_FALSE(fs::exists(folder / "default.guidock"));
    CHECK(io::readFile(folder / "default.guidock.broken").value() == "GUIDOCK\x01\x02payload-a");
    w = l.takeWarnings();
    REQUIRE(w.size() == 1);
    CHECK(w.front().find("패널 목록이 다르다") != std::string::npos);
    REQUIRE(l.saveDock(dockA).has_value()); // 옮긴 뒤에는 같은 바이트도 다시 쓴다
    CHECK(fs::exists(folder / "default.guidock"));

    // 안전 모드 — 아무 파일도 읽거나 쓰지 않는다
    TempDir s("dock_safe");
    LayoutService safe({}, {}, "scene");
    CHECK_FALSE(safe.loadDock());
    REQUIRE(safe.saveDock(dockA).has_value());
    safe.quarantineDock("x");
    CHECK(fs::is_empty(s.path));
}
