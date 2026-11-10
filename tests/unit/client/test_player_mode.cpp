// Player 모드 (v0.8 NG-03 — apps/client/player, ADR-0121): 최근 게임 (player.json — 맨 앞 · 같은 경로 하나 · 10 개 ·
// 없어진 파일 · 깨진 파일 옮기기 · 안전 모드), 열지 못한 이유의 사람 말 (오류 코드마다), --play · 경로 하나 (파일 연결)
// 옵션, 헤드리스 앱 — Studio 창 · 셸 없음 · 월드 없으면 Player 화면 · 오류 화면
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "apps/client/Application.hpp"
#include "apps/client/ClientOptions.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/player/PlayerLibrary.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "foundation/io/FileIo.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(std::string_view name) {
    const fs::path p = fs::temp_directory_path() / std::format("sbx_player_{}_{}", name, std::random_device{}());
    fs::create_directories(p);
    return p;
}

bool active(const char* name) {
    const ImGuiWindow* w = ImGui::FindWindowByName(name);
    return w != nullptr && w->WasActive;
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE("player (v0.8 NG-03): 최근 게임 — 맨 앞 · 같은 경로 하나 · 10 개 · 다시 읽기 · 없는 파일 · 깨진 파일 · "
              "안전 모드") {
        const fs::path root = tempDir("lib");
        const fs::path file = root / "player.json";
        std::ofstream(root / "a.sbpkg") << "x";
        std::ofstream(root / "b.sbpkg") << "x";
        {
            PlayerLibrary lib(file);
            CHECK(lib.recent().empty());
            lib.remember({root / "a.sbpkg", "me.a", "A", "1.0.0", "", false});
            lib.remember({root / "b.sbpkg", "me.b", "B", "0.2.0", "", false});
            lib.remember({root / "a.sbpkg", "me.a", "A", "1.0.1", "", false}); // 같은 경로 — 맨 앞으로, 하나만
            REQUIRE(lib.recent().size() == 2);
            CHECK(lib.recent().front().gameId == "me.a");
            CHECK(lib.recent().front().version == "1.0.1");
            CHECK_FALSE(lib.recent().front().playedAt.empty());
            for (int i = 0; i < 12; ++i) {
                lib.remember({root / std::format("g{}.sbpkg", i), std::format("me.g{}", i), "", "", "", false});
            }
            CHECK(lib.recent().size() == PlayerLibrary::kMaxRecent);
        }
        PlayerLibrary again(file);
        REQUIRE(again.recent().size() == PlayerLibrary::kMaxRecent);
        CHECK(again.recent().front().gameId == "me.g11");
        CHECK(again.recent().front().missing); // g11.sbpkg 는 만들지 않았다 — 지우지 않고 표시
        again.forget(root / "g11.sbpkg");
        CHECK(again.recent().front().gameId == "me.g10");
        const nlohmann::json j = nlohmann::json::parse(io::readFile(file).value());
        CHECK(j["schemaVersion"] == 1);

        // 깨진 파일 — .broken 으로 옮기고 빈 목록 + 경고
        std::ofstream(file, std::ios::binary) << "{ broken";
        PlayerLibrary broken(file);
        CHECK(broken.recent().empty());
        CHECK(broken.takeWarnings().size() == 1);
        CHECK(fs::exists(root / "player.json.broken"));
        // 안전 모드 — 파일 없음
        PlayerLibrary safe({});
        safe.remember({root / "a.sbpkg", "me.a", "A", "", "", false});
        CHECK(safe.recent().size() == 1);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST_CASE("player (v0.8 NG-03): 열지 못한 이유의 사람 말 · 옵션") {
        CHECK(describePlayError(Error{ErrorCode::NotFound, "x", "p"}).title == "게임 파일을 찾을 수 없다");
        CHECK(describePlayError(Error{ErrorCode::VersionMismatch, "패키지 형식 9", ""}).title ==
              "이 Sandbox 와 맞지 않는 게임이다");
        CHECK(describePlayError(Error{ErrorCode::ValidationFailed, "digest 가 다르다", ""}).title ==
              "게임 파일이 손상됐다");
        CHECK(describePlayError(
                  Error{ErrorCode::ValidationFailed, "게임 패키지(.sbpkg)가 아니다 (머리 글자가 다르다)", ""})
                  .title == "게임 파일 (.sbpkg) 이 아니다");
        CHECK(describePlayError(Error{ErrorCode::OutOfRange, "x", ""}).title == "게임 파일이 너무 크다");
        const PlayError other = describePlayError(Error{ErrorCode::IoError, "디스크", "p"});
        CHECK(other.title == "게임을 열 수 없다");
        CHECK(other.detail.find("디스크") != std::string::npos);
        CHECK_FALSE(other.advice.empty());

        const auto parse = [](std::vector<std::string_view> v) { return parseClientOptions(v); };
        const auto play = parse({"--play", "D:/g.sbpkg"});
        REQUIRE(play.has_value());
        CHECK(play->player);
        CHECK(*play->world == "D:/g.sbpkg");
        const auto file = parse({"D:/g.sbpkg"}); // 파일 연결 — 경로 하나
        REQUIRE(file.has_value());
        CHECK(file->player);
        CHECK(*file->world == "D:/g.sbpkg");
        CHECK(parse({"--player"}).value().player);
        CHECK_FALSE(parse({"--play", "g.sbpkg", "--record", "r"}).has_value());
        CHECK_FALSE(parse({"--play", "g.sbpkg", "--connect", "127.0.0.1"}).has_value());
        CHECK_FALSE(parse({"notapackage.txt"}).has_value()); // .sbpkg 가 아닌 경로는 모르는 옵션
    }

    TEST_CASE("app (ImGui 헤드리스, v0.8 NG-03): Player 모드 — 월드 없으면 Player 화면 · 오류 화면, Studio 창 없음") {
        const fs::path root = tempDir("app");
        PlayerLibrary lib(root / "player.json");
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.fixedDt = 1.0 / 30.0;
        cfg.ui = &imgui;
        cfg.playerMode = true;
        cfg.library = &lib;
        cfg.engineVersion = "0.8.0";
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        {
            Application app(window, std::move(*map), audio, cfg);
            for (int i = 0; i < 4; ++i) {
                REQUIRE(app.frame());
            }
            CHECK(active("Sandbox Player"));
            CHECK_FALSE(active("Sandbox Studio")); // Studio 홈이 아니다
            CHECK_FALSE(active("##studio-toolbar"));
            CHECK_FALSE(active("시뮬레이션"));
            CHECK_FALSE(app.shell().developerPanels);
            CHECK_FALSE(app.shell().view.hierarchy);
        }
        // 오류 화면
        cfg.playError = describePlayError(Error{ErrorCode::ValidationFailed, "digest 가 다르다", "g.sbpkg"});
        auto map2 = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map2.has_value());
        Application app2(window, std::move(*map2), audio, cfg);
        for (int i = 0; i < 4; ++i) {
            REQUIRE(app2.frame());
        }
        CHECK(active("Sandbox Player — 오류"));
        CHECK_FALSE(active("Sandbox Player"));
        std::error_code ec;
        fs::remove_all(root, ec);
    }
}
