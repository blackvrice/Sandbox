// v0.2 M3-T3 클라이언트 (ADR-0049): 게임 키 — 팩 presentation/input.json 의 play.action.* (합치기 · 무시 · 사용자
// 덮어쓰기 · 겹침 경고 · 키 표시), 앱에서 키 = 커서 자리에 PlayerAction, 카메라 처음 자리 game.camera_start (월드마다 ·
// Home).
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/PlayKeys.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/game/CameraStart.hpp"
#include "core/project/GameProject.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_{}_{:x}", tag, std::random_device{}());
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

constexpr f64 kDt = 1.0 / 30.0;

ActionMap engineKeys() {
    auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
    REQUIRE(map.has_value());
    return std::move(*map);
}

bool hasWarning(const PlayKeys& k, std::string_view fragment) {
    for (const std::string& w : k.warnings) {
        if (w.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 서버 없는 세션 — 카메라 처음 자리 · 행동 목록을 시험이 정하고, 보낸 행동을 모은다
class FakeSession final : public IWorldSession {
public:
    std::optional<CameraStart> start;
    std::optional<ActionsInfo> acts;
    std::vector<std::pair<std::string, Vec2>> sent;

    void update(f64) override {}
    void extract(render::RenderWorld&) override {}
    [[nodiscard]] render::WorldRect bounds() const override { return {{-64.f, -64.f}, {64.f, 64.f}}; }
    void togglePause() override {}
    void stepOnce() override {}
    void changeSpeed(int) override {}
    [[nodiscard]] std::string status() const override { return "fake"; }
    [[nodiscard]] std::optional<ActionsInfo> playerActions() const override { return acts; }
    void playerAction(std::string id, Vec2 world) override { sent.emplace_back(std::move(id), world); }
    [[nodiscard]] std::optional<CameraStart> cameraStart() const override { return start; }
};

void tap(HeadlessWindow& window, Key key) {
    window.inject(KeyDown{key, 0, {}, false});
    window.inject(KeyUp{key, 0, {}});
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE("play keys: 팩 순서로 합친다 · play.* 만 · 사용자 파일이 이긴다 · 겹침 경고 · 형식 오류") {
        const std::vector<PackInput> packs{
            {"eco", R"({"format": "sandbox.input", "version": 1,
                        "actions": {"play.action.eco.feed": ["Q"], "play.action.eco.trap": ["E"],
                                    "camera.pan.up": ["I"]}})"},
            {"arena", R"({"format": "sandbox.input", "version": 1,
                          "actions": {"play.action.eco.trap": ["R"], "play.action.arena.jump": ["Space"]}})"},
            {"broken", R"({"format": "nope"})"},
        };
        ActionMap user = engineKeys();
        PlayKeys k = buildPlayKeys(packs, user);
        CHECK(k.map.size() == 3);
        CHECK(k.map.find("camera.pan.up") == std::nullopt); // 팩은 엔진 키를 못 바꾼다
        CHECK(hasWarning(k, "'camera.pan.up' 는 무시한다"));
        CHECK(hasWarning(k, "broken/presentation/input.json"));
        CHECK(hasWarning(k, "'play.action.arena.jump' 의 Space 가 엔진 키 'sim.toggle_pause' 와 겹친다"));
        CHECK(playKeyLabel(k.map, "eco.feed") == "Q");
        CHECK(playKeyLabel(k.map, "eco.trap") == "R"); // 뒤 팩이 이긴다
        CHECK(playKeyLabel(k.map, "eco.nope").empty());

        // 사용자 --input 의 play.* 가 마지막에 덮어쓴다 (빈 배열 = 해제)
        auto over = ActionMap::parse(R"({"format": "sandbox.input", "version": 1,
            "actions": {"play.action.eco.feed": ["F5", "Ctrl+Q"], "play.action.eco.trap": []}})",
                                     "<user>");
        REQUIRE(over.has_value());
        user.merge(*over);
        k = buildPlayKeys(packs, user);
        CHECK(playKeyLabel(k.map, "eco.feed") == "F5 / Ctrl+Q");
        CHECK(playKeyLabel(k.map, "eco.trap").empty());
        CHECK(hasWarning(k, "'play.action.eco.feed' 의 Ctrl+Q 가 엔진 키 'app.quit' 와 겹친다"));
        CHECK(buildPlayKeys({}, engineKeys()).map.size() == 0);
    }

    TEST_CASE("app: 게임 키 = 커서 자리에 PlayerAction, 역할이 없으면 보내지 않는다, 패널에 키 표시") {
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        FakeSession session;
        ActionsInfo acts;
        acts.actions = {{"eco.feed", "먹이 주기", 3, 2}, {"eco.trap", "덫", 1, 1}};
        acts.canAct = true;
        acts.packInput = {{"eco", R"({"format": "sandbox.input", "version": 1,
                                      "actions": {"play.action.eco.feed": ["Q"]}})"}};
        session.acts = acts;
        AppConfig cfg;
        cfg.world = &session;
        cfg.fixedDt = kDt;
        Application app(window, engineKeys(), audio, cfg);
        for (int i = 0; i < 10 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        REQUIRE(app.frame());
        CHECK(app.actionPanel().keys.at("eco.feed") == "Q");
        CHECK_FALSE(app.actionPanel().keys.contains("eco.trap"));

        window.inject(MouseMove{{100, 100}, {0, 0}});
        tap(window, Key::Q);
        REQUIRE(app.frame());
        REQUIRE(session.sent.size() == 1);
        CHECK(session.sent[0].first == "eco.feed");
        const Vec2 expected = app.camera().screenToWorld(Vec2{100, 100});
        CHECK(session.sent[0].second.x == doctest::Approx(expected.x));
        CHECK(session.sent[0].second.y == doctest::Approx(expected.y));
        REQUIRE(app.frame());
        CHECK(session.sent.size() == 1); // 누르고 있는 동안 한 번

        acts.canAct = false; // observer
        session.acts = acts;
        tap(window, Key::Q);
        REQUIRE(app.frame());
        CHECK(session.sent.size() == 1);
    }

    TEST_CASE("app: 카메라 처음 자리 — game.camera_start 가 보이면 맞추고, Home 은 그리로, 월드가 바뀌면 다시") {
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        FakeSession session;
        AppConfig cfg;
        cfg.world = &session;
        cfg.fixedDt = kDt;
        Application app(window, engineKeys(), audio, cfg);
        for (int i = 0; i < 10 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.frame());
        const f32 fitted = app.camera().pixelsPerUnit; // 지점이 없으면 월드 전체
        const f32 vh = static_cast<f32>(app.camera().viewportHeight);

        session.start = CameraStart{{10.f, 5.f}, 20.f, 0};
        REQUIRE(app.frame());
        CHECK(app.camera().center.x == doctest::Approx(10.f));
        CHECK(app.camera().center.y == doctest::Approx(5.f));
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(vh / 20.f));
        CHECK(app.camera().pixelsPerUnit != doctest::Approx(fitted));

        // 움직여도 다시 맞추지 않는다 — Home 이면 처음 자리로
        window.inject(KeyDown{Key::D, 0, {}, false});
        for (int i = 0; i < 5; ++i) {
            REQUIRE(app.frame());
        }
        window.inject(KeyUp{Key::D, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.camera().center.x > 10.5f);
        tap(window, Key::Home);
        REQUIRE(app.frame());
        CHECK(app.camera().center.x == doctest::Approx(10.f));

        // 새 월드 (불러오기 · 재시작) 면 다시 맞춘다
        window.inject(KeyDown{Key::D, 0, {}, false});
        for (int i = 0; i < 5; ++i) {
            REQUIRE(app.frame());
        }
        window.inject(KeyUp{Key::D, 0, {}});
        session.start = CameraStart{{-3.f, 2.f}, 40.f, 1};
        REQUIRE(app.frame());
        CHECK(app.camera().center.x == doctest::Approx(-3.f));
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(vh / 40.f));
    }

    TEST_CASE("session (local server): 팩 presentation/input.json 이 ActionsInfo 로, 복제된 game.camera_start") {
        TempDir root("playkeys_local");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Keys";
        project::NewProjectOptions o;
        o.gameId = "studio.keys";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const fs::path pack = dir / "content" / "ecosystem";
        std::ofstream(pack / "actions.json", std::ios::binary)
            << R"([{"id": "eco.poke", "effects": [{"op": "event", "name": "eco.poked"}]}])";
        fs::create_directories(pack / "presentation");
        std::ofstream(pack / "presentation" / "input.json", std::ios::binary)
            << R"({"format": "sandbox.input", "version": 1, "actions": {"play.action.eco.poke": ["Q"]}})";

        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        const auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) {
                ns->update(kDt);
                render::RenderWorld w;
                ns->extract(w);
            }
        };
        for (int i = 0; i < 120 && !(ns->ready() && ns->playerActions()); ++i) {
            frames(1);
        }
        const auto acts = ns->playerActions();
        REQUIRE(acts);
        REQUIRE(acts->packInput.size() == 1);
        CHECK(acts->packInput[0].pack == "eco");
        CHECK(buildPlayKeys(acts->packInput, engineKeys()).map.find("play.action.eco.poke").has_value());
        CHECK_FALSE(ns->cameraStart()); // 템플릿 월드에는 없다

        ns->submit(cmd::CreateEntity{
            {12.f, -4.f}, {cmd::ComponentValue{ecs::stableIdOf<comp::CameraStart>, ecs::Json{{"height", 30}}}}, {}});
        for (int i = 0; i < 60 && !ns->cameraStart(); ++i) {
            frames(1);
        }
        const auto start = ns->cameraStart();
        REQUIRE(start);
        CHECK(start->center.x == doctest::Approx(12.f));
        CHECK(start->center.y == doctest::Approx(-4.f));
        CHECK(start->height == doctest::Approx(30.f));
        CHECK(start->generation == ns->worldGeneration());
    }

} // TEST_SUITE
