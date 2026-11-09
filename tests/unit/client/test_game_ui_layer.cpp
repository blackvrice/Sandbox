// 게임 UI 층 (v0.7 M3-T3 — apps/client/presentation/GameUiLayer · ui/GameUiImGui): 원문 → 문서 · 스타일, 배치, 버튼
// 이벤트 (action · signal · ui 보이기 · 숨기기), 누름 표시, 읽지 못한 문서 경고, 그리고 앱 (로컬 서버 + ImGui 헤드리스)
// 에서 HUD 버튼을 누르면 PlayerAction 이 가고 그 클릭은 게임 (에디터 · 선택) 에 가지 않는다
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/presentation/GameUiLayer.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/life/Life.hpp"
#include "core/project/GameProject.hpp"
#include "core/script/ScriptHost.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "foundation/io/FileIo.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
namespace fs = std::filesystem;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

GameUiInfo demoSources() {
    const fs::path ui = fs::path(SBX_SAMPLES_DIR) / "rabbit_guard/content/ecosystem/presentation/ui";
    GameUiInfo info;
    info.documents.push_back({"eco", "hud", *io::readFile(ui / "hud.json")});
    info.styles.push_back({"eco", "styles", *io::readFile(ui / "styles.json")});
    for (const char* locale : {"en", "ko"}) { // 로더와 같은 이름 순 (v0.7 M4-T1)
        info.strings.push_back(
            {"eco", locale, *io::readFile(ui.parent_path() / "strings" / std::format("{}.json", locale))});
    }
    return info;
}

const ui::UiLayoutNode* find(const GameUiLayer& layer, std::string_view id) {
    for (const ui::UiDocumentLayout& d : layer.layouts()) {
        for (const ui::UiLayoutNode& n : d.nodes) {
            if (n.node->id == id) {
                return &n;
            }
        }
    }
    return nullptr;
}

std::pair<i32, i32> centerOf(const ui::UiLayoutNode& n) {
    return {n.rect.x + n.rect.w / 2, n.rect.y + n.rect.h / 2};
}

class TextRenderer final : public ui::IUiRenderer {
public:
    std::vector<std::string> texts;
    void fillRect(const ui::UiRect&, ui::UiColor, const ui::UiRect&) override {}
    void strokeRect(const ui::UiRect&, ui::UiColor, f32, const ui::UiRect&) override {}
    void text(const ui::UiRect&, std::string_view t, f32, ui::UiColor, const ui::UiRect&) override {
        texts.emplace_back(t);
    }
    void image(const ui::UiRect&, std::string_view, const ui::UiRect&) override {}
};

class CountingRenderer final : public ui::IUiRenderer {
public:
    usize fills = 0;
    usize texts = 0;
    std::vector<ui::UiColor> colors;
    void fillRect(const ui::UiRect&, ui::UiColor c, const ui::UiRect&) override {
        ++fills;
        colors.push_back(c);
    }
    void strokeRect(const ui::UiRect&, ui::UiColor, f32, const ui::UiRect&) override {}
    void text(const ui::UiRect&, std::string_view, f32, ui::UiColor, const ui::UiRect&) override { ++texts; }
    void image(const ui::UiRect&, std::string_view, const ui::UiRect&) override {}
};

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

} // namespace

TEST_SUITE("client") {
    TEST_CASE(
        "game ui layer: 데모 HUD — 배치 · 버튼 action 이벤트 · ui 토글 (자리도 빠짐) · 누름 표시 · 빈 곳은 통과") {
        GameUiLayer layer;
        layer.setLocale("ko");
        layer.setSources(demoSources());
        REQUIRE(layer.documentCount() == 1);
        CHECK(layer.takeWarnings().empty());
        const ui::FixedTextMeasure measure;
        layer.layout(1280, 720, 1.0f, measure);
        const ui::UiLayoutNode* feed = find(layer, "feed");
        REQUIRE(feed != nullptr);
        CHECK(feed->rect ==
              ui::UiRect{308, 13, 47, 30}); // 점수 글자가 "토끼 - · 점수 -" (11 자) 라 test_ui_layout 골든보다 32 앞

        std::vector<ui::UiAction> events;
        auto [fx, fy] = centerOf(*feed);
        ui::UiPointerRoute r = layer.route(fx, fy, false, true, true, events);
        CHECK(r.captured);
        REQUIRE(events.size() == 1);
        CHECK(events[0].kind == ui::UiAction::Kind::Action);
        CHECK(events[0].target == "eco.feed");
        // 누르고 있는 동안 버튼이 밝다 — 떼면 원래 색
        CountingRenderer down;
        layer.draw(down);
        CountingRenderer up;
        events.clear();
        (void)layer.route(fx, fy, false, false, false, events);
        layer.draw(up);
        CHECK(events.empty()); // 누른 프레임에만
        CHECK(down.colors != up.colors);
        CHECK(down.texts == 5); // 점수 · 버튼 셋 · 도움말

        // 빈 띠 위 · 글자 위는 게임으로
        CHECK_FALSE(layer.route(1000, 20, false, true, true, events).captured);
        CHECK_FALSE(layer.route(60, 25, false, true, true, events).captured); // 점수 글자
        // 도구 패널이 마우스를 원하면 hud 는 받지 않는다
        CHECK_FALSE(layer.route(fx, fy, true, true, true, events).captured);

        // "?" 버튼 — help 를 숨긴다 (다음 배치부터 없다), 다시 누르면 보인다
        const ui::UiLayoutNode* toggle = find(layer, "help-toggle");
        REQUIRE(toggle != nullptr);
        auto [tx, ty] = centerOf(*toggle);
        events.clear();
        (void)layer.route(tx, ty, false, true, true, events);
        CHECK(events.empty()); // ui 이벤트는 층 안에서
        CHECK(layer.nodeHidden("eco.hud", "help"));
        layer.layout(1280, 720, 1.0f, measure);
        CHECK(find(layer, "help") == nullptr);
        (void)layer.route(tx, ty, false, true, true, events);
        layer.layout(1280, 720, 1.0f, measure);
        CHECK(find(layer, "help") != nullptr);
        // 문서째 숨기기 — 라우팅 · 그리기 모두 없다
        layer.apply(ui::UiAction{ui::UiAction::Kind::Ui, "eco.hud", std::nullopt, "hide"});
        CHECK_FALSE(layer.documentVisible("eco.hud"));
        layer.layout(1280, 720, 1.0f, measure);
        CHECK_FALSE(layer.route(fx, fy, false, true, true, events).captured);
        CountingRenderer hidden;
        layer.draw(hidden);
        CHECK(hidden.fills == 0);
    }

    TEST_CASE(
        "game ui layer: 글자 자리 · signal 이벤트 · 읽지 못한 문서는 경고 한 번 · 같은 원문이면 다시 읽지 않는다") {
        ui::UiNode n;
        n.text = "토끼 {0} · 점수 {1} · {2} · {9}";
        n.args = {ui::UiValue{3, {}}, ui::UiValue{0, "session:score[0]"}, ui::UiValue{2.5f, {}}};
        CHECK(ui::formatText(n, nullptr) == "토끼 3 · 점수 - · 2.5 · "); // 데이터가 없으면 바인딩은 "-"

        GameUiInfo info;
        info.documents.push_back(
            {"eco", "menu", R"({"id": "eco.menu", "layer": "menu", "root": {"kind": "button", "id": "ring",
            "text": "Ring", "on": {"click": {"signal": "eco.ring", "value": 7}}}})"});
        info.documents.push_back({"eco", "broken", R"({"id": "eco.broken", "root": {"kind": "slider"}})"});
        GameUiLayer layer;
        layer.setSources(info);
        CHECK(layer.documentCount() == 1);
        const auto warnings = layer.takeWarnings();
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].find("eco/presentation/ui/broken.json") != std::string::npos);
        layer.setSources(info); // 같은 원문 — 경고도 다시 나지 않는다
        CHECK(layer.takeWarnings().empty());
        layer.layout(800, 600, 1.0f, ui::FixedTextMeasure{});
        std::vector<ui::UiAction> events;
        const ui::UiPointerRoute r = layer.route(5, 5, true, true, true, events); // menu 는 도구 패널보다 먼저
        CHECK(r.owner == ui::UiPointerRoute::Owner::Menu);
        REQUIRE(events.size() == 1);
        CHECK(events[0].kind == ui::UiAction::Kind::Signal);
        CHECK(events[0].value == 7);
        layer.setSources(std::nullopt);
        CHECK(layer.documentCount() == 0);
    }

    TEST_CASE(
        "app (local server + ImGui 헤드리스): HUD 버튼을 누르면 PlayerAction — 그 클릭은 에디터 · 선택에 가지 않는다") {
        TempDir root("game_ui_app");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Hud";
        project::NewProjectOptions o;
        o.gameId = "studio.hud";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const fs::path pack = dir / "content" / "ecosystem";
        std::ofstream(pack / "actions.json", std::ios::binary)
            << R"([{"id": "eco.feed", "name": "먹이 주기", "targets": {"tags": {"all": ["animal"]}}, "range": 32, "maxTargets": 64,
                   "effects": [{"op": "field.add", "who": "target", "field": "life.energy.value", "value": 5}]}])";
        fs::create_directories(pack / "presentation" / "ui");
        std::ofstream(pack / "presentation" / "ui" / "hud.json", std::ios::binary)
            << R"({"id": "eco.hud", "root": {"kind": "panel", "anchor": {"min": [1, 1], "max": [1, 1]}, "offset": [-160, -80, -20, -20], "children": [
                  {"kind": "button", "id": "feed", "text": "먹이 주기", "size": [120, 40], "on": {"click": {"action": "eco.feed"}}}]}})";

        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        cfg.gameUiScale = 1.5f; // v0.7 M4-T4 — 배율을 곱한 버튼을 눌러도 같은 길
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        REQUIRE(app.frame());
        REQUIRE(app.gameUi().documentCount() == 1);
        const ui::UiLayoutNode* feed = find(app.gameUi(), "feed");
        REQUIRE(feed != nullptr);
        const auto [bx, by] = centerOf(*feed);
        CHECK(feed->rect.w == 180); // size 120 × 1.5
        CHECK(feed->rect.h == 60);

        // 도구 패널 (F1) 을 감춘다 — ImGui 창이 버튼 위에 있으면 차례대로 도구가 먼저 가져간다 (라우팅 시험은 ui 쪽에)
        window.inject(KeyDown{Key::F1, 0, {}, false});
        window.inject(KeyUp{Key::F1, 0, {}});
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        // 배치 툴 + Prefab — HUD 버튼 위 클릭은 배치되지 않고 행동이 간다
        window.inject(KeyDown{Key::Digit3, 0, {}, false});
        window.inject(KeyUp{Key::Digit3, 0, {}});
        REQUIRE(app.frame());
        app.editor().settings().prefab = "eco.rabbit";
        window.inject(MouseMove{{static_cast<f32>(bx), static_cast<f32>(by)}, {0, 0}});
        REQUIRE(app.frame());
        window.inject(MouseButtonDown{MouseButton::Left, {static_cast<f32>(bx), static_cast<f32>(by)}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {static_cast<f32>(bx), static_cast<f32>(by)}});
        for (int i = 0; i < 60 && ns->playerActions()->message.empty(); ++i) {
            REQUIRE(app.frame());
        }
        CHECK_MESSAGE(ns->playerActions()->message.find("적용") != std::string::npos, ns->playerActions()->message);
        CHECK(app.gameUi().buttonPresses() == 1);
        CHECK(app.editor().stats().sent == 0);
    }

    TEST_CASE(
        "ui data (local server): session tick · var · component@owned (행동 spawn owner) · @selected = 복제된 값") {
        TempDir root("game_ui_data");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Data";
        project::NewProjectOptions o;
        o.gameId = "studio.data";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        std::ofstream(dir / "content" / "ecosystem" / "actions.json", std::ios::binary)
            << R"([{"id": "eco.claim", "targets": {"tags": {"all": ["animal"]}}, "range": 32, "maxTargets": 1,
                   "effects": [{"op": "spawn", "prefab": "eco.rabbit", "owner": true}]}])";
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
        for (int i = 0; i < 200 && !ns->ready(); ++i) {
            frames(1);
        }
        REQUIRE(ns->ready());
        frames(5);
        const ui::IUiDataSource* data = ns->uiData();
        REQUIRE(data != nullptr);
        const ui::UiScalar tick = ui::evaluate("session:tick", *data);
        CHECK(tick.number == static_cast<f64>(ns->clientWorld()->serverTick()));
        CHECK_FALSE(ui::evaluate("session:state", *data).known());                     // 게임 모드 없음
        CHECK_FALSE(ui::evaluate("component:life.energy.value@owned", *data).known()); // 아직 내 개체가 없다

        ns->setUiVar("eco.rabbits", ui::UiScalar::of(5.0));
        CHECK(ui::evaluate("var:eco.rabbits", *data).number == 5.0);

        // 행동이 내 주인의 토끼를 만든다 → @owned 가 그 개체의 복제된 기운
        const render::WorldRect b = ns->bounds();
        ns->playerAction("eco.claim", {(b.min.x + b.max.x) * 0.5f, (b.min.y + b.max.y) * 0.5f});
        for (int i = 0; i < 120 && !ui::evaluate("component:life.energy.value@owned", *data).known(); ++i) {
            frames(1);
        }
        const ui::UiScalar owned = ui::evaluate("component:life.energy.value@owned", *data);
        REQUIRE_MESSAGE(owned.known(), ns->playerActions()->message);
        CHECK(owned.number > 0);
        CHECK_FALSE(ui::evaluate("component:life.nothing.value@owned", *data).known());
        CHECK_FALSE(ui::evaluate("component:life.energy.nope@owned", *data).known());

        // @selected — 첫째 선택 개체의 값 (없는 컴포넌트면 모름) = 복제 월드에서 직접 읽은 값
        ns->selectBox(b, false);
        REQUIRE_FALSE(ns->selection().empty());
        const net::ClientWorld& w = *ns->clientWorld();
        const auto* energy = w.registry().tryRead<comp::Energy>(w.find(ns->selection().front()));
        const ui::UiScalar selected = ui::evaluate("component:life.energy.value@selected", *data);
        if (energy != nullptr) {
            CHECK(selected.number == static_cast<f64>(energy->value));
        } else {
            CHECK_FALSE(selected.known());
        }

        // 층이 데이터로 글자를 만든다
        GameUiInfo info;
        info.documents.push_back(
            {"eco", "hud", R"({"id": "eco.hud", "root": {"kind": "text", "id": "t", "text": "토끼 {0} · 틱 {1}",
            "args": [{"bind": "var:eco.rabbits"}, {"bind": "session:tick"}]}})"});
        GameUiLayer layer;
        layer.setSources(info);
        layer.setData(data);
        CountingRenderer r;
        layer.layout(800, 600, 1.0f, ui::FixedTextMeasure{});
        CHECK(ui::formatText(*layer.layouts()[0].nodes[0].node, data) ==
              std::format("토끼 5 · 틱 {}", ui::evaluate("session:tick", *data).display()));
    }

    TEST_CASE("app (local server + ImGui 헤드리스): 버튼 → PlayerSignal → 서버 스크립트 onEvent, 선언 없는 신호는 거절 "
              "(session:message), 버튼 행동은 선택한 개체 자리에 (v0.7 M3-T5)") {
        TempDir root("game_ui_events");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Ev";
        project::NewProjectOptions o;
        o.gameId = "studio.ev";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const fs::path pack = dir / "content" / "ecosystem";
        std::ofstream(pack / "signals.json", std::ios::binary) << R"([{"id": "eco.ring", "min": 0, "max": 10}])";
        std::ofstream(pack / "actions.json", std::ios::binary)
            << R"([{"id": "eco.poke", "range": 0.25, "maxTargets": 1, "effects": [{"op": "event", "name": "eco.poked"}]}])";
        fs::create_directories(pack / "scripts");
        std::ofstream(pack / "scripts" / "pong.ts", std::ios::binary) << R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.pong",
  events: { signal: {} },
  onEvent(ctx, e) { ctx.log(`signal ${e.signal} ${e.value} ${e.player}`) },
})
)";
        fs::create_directories(pack / "presentation" / "ui");
        std::ofstream(pack / "presentation" / "ui" / "hud.json", std::ios::binary)
            << R"({"id": "eco.hud", "root": {"kind": "panel", "anchor": {"min": [1, 1], "max": [1, 1]}, "offset": [-420, -80, -20, -20],
                  "stack": "row", "gap": 10, "children": [
                  {"kind": "button", "id": "ring", "text": "ring", "size": [120, 40], "on": {"click": {"signal": "eco.ring", "value": 3}}},
                  {"kind": "button", "id": "bad", "text": "bad", "size": [120, 40], "on": {"click": {"signal": "eco.nope"}}},
                  {"kind": "button", "id": "poke", "text": "poke", "size": [120, 40], "on": {"click": {"action": "eco.poke"}}}]}})";

        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        window.inject(KeyDown{Key::F1, 0, {}, false}); // 도구 패널을 감춘다 (ImGui 창이 버튼 위에 오지 않게)
        window.inject(KeyUp{Key::F1, 0, {}});
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        const auto click = [&](std::string_view id) {
            const ui::UiLayoutNode* b = find(app.gameUi(), id);
            REQUIRE(b != nullptr);
            const auto [x, y] = centerOf(*b);
            const Vec2 at{static_cast<f32>(x), static_cast<f32>(y)};
            window.inject(MouseMove{at, {0, 0}});
            REQUIRE(app.frame());
            window.inject(MouseButtonDown{MouseButton::Left, at});
            REQUIRE(app.frame());
            window.inject(MouseButtonUp{MouseButton::Left, at});
            REQUIRE(app.frame());
        };
        const auto message = [&] { return ui::evaluate("session:message", *ns->uiData()).display(); };
        const auto serverLogs = [&] {
            std::vector<std::string> out;
            if (const auto* sys = ns->localServer()->host().world().scriptSystems()) {
                for (const script::ScriptLogLine& l : sys->host().recentLogs()) {
                    out.push_back(l.text);
                }
            }
            return out;
        };

        // 신호 — 서버 스크립트가 같은 틱에 받는다, session:message = 적용
        click("ring");
        for (int i = 0; i < 60 && serverLogs().empty(); ++i) {
            REQUIRE(app.frame());
        }
        const auto& players = ns->localServer()->host().world().players();
        REQUIRE(players.size() == 1);
        CHECK(serverLogs() == std::vector<std::string>{std::format("signal eco.ring 3 {}", players.begin()->first)});
        for (int i = 0; i < 60 && message().find("PlayerSignal") == std::string::npos; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(message().starts_with("PlayerSignal 적용"));

        // 선언 없는 신호 — 서버가 거절, 그 문장이 session:message 로
        click("bad");
        for (int i = 0; i < 60 && message().find("거절") == std::string::npos; ++i) {
            REQUIRE(app.frame());
        }
        CHECK_MESSAGE(message().find("PlayerSignal 거절") != std::string::npos, message());
        CHECK(serverLogs().size() == 1);

        // 행동 — 선택한 개체 자리 (range 0.25 — 그 개체만), 선택이 없으면 내 개체 → 화면 가운데
        CHECK_FALSE(ns->uiActionPoint().has_value()); // 선택 · 내 개체 없음
        ns->selectBox(ns->bounds(), false);
        REQUIRE_FALSE(ns->selection().empty());
        const net::ClientWorld& w = *ns->clientWorld();
        const auto* tr = w.registry().tryRead<comp::Transform>(w.find(ns->selection().front()));
        REQUIRE(tr != nullptr);
        REQUIRE(ns->uiActionPoint().has_value());
        CHECK(ns->uiActionPoint()->x == tr->position.x);
        CHECK(ns->uiActionPoint()->y == tr->position.y);
        click("poke");
        for (int i = 0; i < 60 && message().find("PlayerAction") == std::string::npos; ++i) {
            REQUIRE(app.frame());
        }
        CHECK_MESSAGE(message().starts_with("PlayerAction 적용"), message());
    }
    TEST_CASE("game ui layer (v0.7 M3-T6): 테마 버튼 — 같은 배치 · 다른 색, none 이면 바탕, 없는 테마는 그대로") {
        GameUiInfo info = demoSources();
        info.documents[0].text =
            R"({"id": "eco.hud", "root": {"kind": "panel", "style": "eco.bar", "stack": "row", "children": [
            {"kind": "button", "id": "night", "text": "n", "on": {"click": {"ui": "theme", "target": "eco.night"}}},
            {"kind": "button", "id": "day", "text": "d", "on": {"click": {"ui": "theme", "target": "none"}}}]}})";
        GameUiLayer layer;
        layer.setSources(info);
        const ui::FixedTextMeasure measure;
        layer.layout(800, 600, 1.0f, measure);
        const std::vector<ui::UiLayoutNode> before = layer.layouts()[0].nodes;
        CountingRenderer day;
        layer.draw(day);
        const auto [nx, ny] = centerOf(*find(layer, "night"));
        std::vector<ui::UiAction> events;
        (void)layer.route(nx, ny, false, true, true, events);
        CHECK(layer.theme() == "eco.night");
        layer.layout(800, 600, 1.0f, measure);
        for (usize i = 0; i < before.size(); ++i) {
            CHECK(before[i].rect == layer.layouts()[0].nodes[i].rect);
        }
        CountingRenderer night;
        layer.draw(night);
        CHECK(night.colors.front() == ui::UiColor{8, 10, 24, 220}); // samples/rabbit_guard styles.json 의 eco.night
        CHECK(day.colors.front() != night.colors.front());
        CHECK_FALSE(layer.setTheme("eco.dawn"));
        CHECK(layer.theme() == "eco.night");
        const auto [dx, dy] = centerOf(*find(layer, "day"));
        (void)layer.route(dx, dy, false, true, true, events);
        CHECK(layer.theme().empty());
    }
    TEST_CASE("game ui layer (v0.7 M4-T1): 데모가 ko · en 으로 바뀐다 — 같은 문서, 로케일만, 없는 키는 키 그대로 · "
              "경고 한 번") {
        GameUiLayer layer;
        layer.setSources(demoSources());
        const ui::FixedTextMeasure measure;
        const auto texts = [&] {
            layer.layout(1280, 720, 1.0f, measure);
            TextRenderer r;
            layer.draw(r);
            return r.texts;
        };
        layer.setLocale("ko");
        CHECK(layer.locale() == "ko");
        CHECK(texts() == std::vector<std::string>{"토끼 - · 점수 -", "먹이 주기", "덫 놓기", "?",
                                                  "토끼를 고르고 먹이를 주세요 — 늑대 옆에는 덫을"});
        layer.setLocale("en-GB"); // 언어로 내려간다
        CHECK(texts() == std::vector<std::string>{"Rabbits - · Score -", "Feed", "Set trap", "?",
                                                  "Pick a rabbit and feed it — set traps near wolves"});
        const i32 enFeed = find(layer, "feed")->rect.w;
        layer.setLocale("fr"); // 표가 없는 로케일 — 팩의 기본 (en)
        CHECK(texts()[1] == "Feed");
        CHECK(find(layer, "feed")->rect.w == enFeed);
        layer.setLocale("ko");
        CHECK(texts()[1] == "먹이 주기");
        CHECK(find(layer, "feed")->rect.w != enFeed); // 글이 바뀌면 배치도 바뀐다
        CHECK(layer.takeWarnings().empty());

        // 로케일은 원문을 다시 읽어도 남는다, 없는 키는 키 그대로 + 경고 한 번
        GameUiInfo info = demoSources();
        info.documents[0].text =
            R"({"id": "eco.hud", "root": {"kind": "panel", "children": [{"kind": "text", "text": "@eco.hud.gone"}, {"kind": "text", "text": "@@eco"}]}})";
        layer.setSources(info);
        CHECK(layer.locale() == "ko");
        CHECK(texts() == std::vector<std::string>{"eco.hud.gone", "@eco"});
        (void)texts();
        const std::vector<std::string> warnings = layer.takeWarnings();
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].find("eco.hud.gone") != std::string::npos);
        CHECK(layer.takeWarnings().empty());

        // 깨진 표 — 경고, 성한 항목은 쓴다
        info = demoSources();
        info.strings[1].text = R"({"eco.hud.feed": "먹이", "eco.hud.trap": 3})";
        layer.setSources(info);
        CHECK(texts()[1] == "먹이");
        const std::vector<std::string> broken = layer.takeWarnings();
        CHECK(std::any_of(broken.begin(), broken.end(),
                          [](const std::string& w) { return w.find("strings/ko.json") != std::string::npos; }));
    }
    TEST_CASE(
        "game ui layer (v0.7 M4-T1 (2)): 표시 이름 — \"@<키>\" 는 표의 글 (로케일), \"@@\" 는 '@', 그 밖은 그대로") {
        GameUiLayer layer;
        layer.setSources(demoSources());
        layer.setLocale("en");
        CHECK(layer.displayName("@eco.hud.feed") == "Feed");
        layer.setLocale("ko");
        CHECK(layer.displayName("@eco.hud.feed") == "먹이 주기");
        CHECK(layer.displayName("먹이 주기") == "먹이 주기");
        CHECK(layer.displayName("@@home") == "@home");
        CHECK(layer.displayName("@eco.gone") == "eco.gone");
        CHECK(layer.displayName("").empty());
    }
    TEST_CASE(
        "game ui layer (v0.7 NG-06 (1)): 입력 컨트롤 — checkbox 누르기 · slider 끌기 (놓을 때 신호 하나) · var 쓰기, "
        "포커스 (hud 클릭은 아님 · menu 클릭 · Tab 차례 · 꺼진 것 건너뛰기 · 방향 · Esc)") {
        class Vars final : public ui::IUiDataSource {
        public:
            std::map<std::string, ui::UiScalar, std::less<>> values;
            ui::UiScalar var(std::string_view key) const override {
                const auto it = values.find(key);
                return it == values.end() ? ui::UiScalar{} : it->second;
            }
            ui::UiScalar session(std::string_view) const override { return {}; }
            ui::UiScalar component(std::string_view, ui::UiTarget) const override { return {}; }
        } vars;
        GameUiInfo info;
        info.documents.push_back(
            {"eco", "hud", R"({"id": "eco.hud", "root": {"kind": "panel", "stack": "row", "gap": 20, "children": [
            {"kind": "checkbox", "id": "chk", "text": "chk", "value": {"bind": "var:eco.chk"}, "on": {"change": {"signal": "eco.chk"}}},
            {"kind": "slider", "id": "vol", "value": {"bind": "var:eco.vol"}, "min": 0, "max": 10, "step": 2, "on": {"change": {"signal": "eco.vol"}}},
            {"kind": "toggle", "id": "off", "value": {"bind": "var:eco.off"}, "disabled": true}]}})"});
        info.documents.push_back(
            {"eco", "menu",
             R"({"id": "eco.menu", "layer": "menu", "root": {"kind": "panel", "anchor": {"min": [0, 1], "max": [0, 1]},
            "offset": [0, -40, 0, 0], "stack": "row", "children": [
            {"kind": "toggle", "id": "m", "value": {"bind": "var:eco.m"}, "tabIndex": 5}]}})"});
        GameUiLayer layer;
        layer.setSources(info);
        REQUIRE(layer.documentCount() == 2);
        layer.setData(&vars);
        std::vector<std::pair<std::string, ui::UiScalar>> writes;
        layer.setVarWriter([&](std::string_view key, const ui::UiScalar& v) {
            writes.emplace_back(key, v);
            vars.values.insert_or_assign(std::string(key), v);
        });
        const ui::FixedTextMeasure measure;
        layer.layout(800, 600, 1.0f, measure);
        const auto clickAt = [&](i32 x, i32 y) {
            std::vector<ui::UiAction> ev;
            (void)layer.route(x, y, false, true, true, ev);
            (void)layer.route(x, y, false, false, false, ev);
            return ev;
        };

        // checkbox — 켬 (var · 신호 1), 다시 누르면 끔 (0). hud 클릭은 포커스를 주지 않는다
        const auto [cx, cy] = centerOf(*find(layer, "chk"));
        std::vector<ui::UiAction> ev = clickAt(cx, cy);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].kind == ui::UiAction::Kind::Signal);
        CHECK(ev[0].target == "eco.chk");
        CHECK(ev[0].value == 1);
        CHECK(vars.values.at("eco.chk").flag);
        CHECK_FALSE(layer.hasFocus());
        CHECK(clickAt(cx, cy)[0].value == 0);

        // slider — 끄는 동안 var 만 (step 2 로 맞춤), 놓을 때 신호 하나 (반올림 i32)
        const ui::UiLayoutNode* vol = find(layer, "vol");
        REQUIRE(vol != nullptr);
        CHECK(vol->rect.w == 160);
        writes.clear();
        ev.clear();
        (void)layer.route(vol->rect.x + 40, vol->rect.y + 5, false, true, true, ev); // 1/4 → 2.5 → step 2 = 2
        CHECK(ev.empty());
        CHECK(vars.values.at("eco.vol").number == 2.0);
        (void)layer.route(vol->rect.x + 1000, vol->rect.y + 500, false, false, true, ev); // 밖으로 끌어도 → 10
        CHECK(vars.values.at("eco.vol").number == 10.0);
        CHECK(ev.empty());
        (void)layer.route(vol->rect.x + 1000, vol->rect.y + 500, false, false, false, ev);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].target == "eco.vol");
        CHECK(ev[0].value == 10);
        CHECK(writes.size() == 2);

        // 꺼진 toggle — 누르기 무시
        const auto [ox, oy] = centerOf(*find(layer, "off"));
        CHECK(clickAt(ox, oy).empty());
        CHECK_FALSE(vars.values.contains("eco.off"));

        // menu 의 컨트롤을 누르면 포커스, Esc 가 놓는다
        const auto [mx, my] = centerOf(*find(layer, "m"));
        (void)clickAt(mx, my);
        CHECK(layer.hasFocus());
        CHECK(layer.focused()->id == "m");
        CHECK(vars.values.at("eco.m").flag);
        CHECK(layer.key(ui::UiKey::Release, ev));
        CHECK_FALSE(layer.hasFocus());
        CHECK_FALSE(layer.key(ui::UiKey::Activate, ev)); // 포커스가 없으면 게임의 키
        CHECK_FALSE(layer.key(ui::UiKey::Left, ev));

        // Tab 차례 — tabIndex 0 (chk → vol, 꺼진 off 는 빠짐) 다음 m (5), 끝에서 처음으로, Shift+Tab 은 뒤로
        CHECK(layer.key(ui::UiKey::Next, ev));
        CHECK(layer.focused()->id == "chk");
        CHECK(layer.key(ui::UiKey::Next, ev));
        CHECK(layer.focused()->id == "vol");
        CHECK(layer.key(ui::UiKey::Next, ev));
        CHECK(layer.focused()->id == "m");
        CHECK(layer.key(ui::UiKey::Next, ev));
        CHECK(layer.focused()->id == "chk");
        CHECK(layer.key(ui::UiKey::Previous, ev));
        CHECK(layer.focused()->id == "m");

        // 키로 누르기 · slider 한 칸 (신호 바로), 방향 = 가장 가까운 컨트롤
        ev.clear();
        CHECK(layer.key(ui::UiKey::Previous, ev)); // m → vol
        CHECK(layer.focused()->id == "vol");
        CHECK(layer.key(ui::UiKey::Left, ev));
        CHECK(vars.values.at("eco.vol").number == 8.0);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].value == 8);
        CHECK(layer.key(ui::UiKey::Up, ev)); // 위에 아무것도 없다 — 그대로
        CHECK(layer.focused()->id == "vol");
        CHECK(layer.key(ui::UiKey::Down, ev)); // 아래 (menu 의 m)
        CHECK(layer.focused()->id == "m");
        ev.clear();
        CHECK(layer.key(ui::UiKey::Activate, ev));
        CHECK_FALSE(vars.values.at("eco.m").flag); // toggle 끔 (신호 없음 — on.change 없다)
        CHECK(ev.empty());

        // 그리기 — 포커스 고리 (m), checkbox 값은 var 에서
        CountingRenderer r;
        layer.draw(r);
        CHECK(r.fills > 0);
        // 원문이 바뀌면 포커스를 놓는다
        info.documents[1].text += " ";
        layer.setSources(info);
        CHECK_FALSE(layer.hasFocus());
    }
    TEST_CASE("app (local server + ImGui 헤드리스, v0.7 NG-06 (1)): Tab 이 HUD checkbox 에 포커스 — Space 는 checkbox "
              "를 바꾸고 "
              "(신호 → 서버 onEvent) 일시정지는 아니다, Esc 로 놓으면 Space 가 다시 게임 키") {
        TempDir root("game_ui_focus");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Focus";
        project::NewProjectOptions o;
        o.gameId = "studio.focus";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const fs::path pack = dir / "content" / "ecosystem";
        std::ofstream(pack / "signals.json", std::ios::binary) << R"([{"id": "eco.snd", "min": 0, "max": 1}])";
        fs::create_directories(pack / "scripts");
        std::ofstream(pack / "scripts" / "snd.ts", std::ios::binary) << R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.snd",
  events: { signal: {} },
  onEvent(ctx, e) { ctx.log(`signal ${e.signal} ${e.value}`) },
})
)";
        fs::create_directories(pack / "presentation" / "ui");
        std::ofstream(pack / "presentation" / "ui" / "hud.json", std::ios::binary)
            << R"({"id": "eco.hud", "root": {"kind": "panel", "anchor": {"min": [1, 1], "max": [1, 1]}, "offset": [-200, -60, -20, -20],
                  "children": [{"kind": "checkbox", "id": "snd", "text": "sound", "value": {"bind": "var:eco.snd"},
                  "on": {"change": {"signal": "eco.snd"}}}]}})";

        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        const auto tap = [&](Key k) {
            window.inject(KeyDown{k, 0, {}, false});
            window.inject(KeyUp{k, 0, {}});
            REQUIRE(app.frame());
        };
        tap(Key::F1); // 도구 패널을 감춘다 (ImGui 가 키보드를 원하지 않게)
        REQUIRE(app.frame());
        const auto serverLogs = [&] {
            std::vector<std::string> out;
            if (const auto* sys = ns->localServer()->host().world().scriptSystems()) {
                for (const script::ScriptLogLine& l : sys->host().recentLogs()) {
                    out.push_back(l.text);
                }
            }
            return out;
        };
        const bool pausedBefore = ns->paused();

        tap(Key::Tab);
        REQUIRE(app.gameUi().hasFocus());
        CHECK(app.gameUi().focused()->id == "snd");
        tap(Key::Space); // checkbox 켬 — 게임의 Space (일시정지) 는 아니다
        for (int i = 0; i < 60 && serverLogs().empty(); ++i) {
            REQUIRE(app.frame());
        }
        CHECK(serverLogs() == std::vector<std::string>{"signal eco.snd 1"});
        CHECK(ui::evaluate("var:eco.snd", *ns->uiData()).flag);
        for (int i = 0; i < 10; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(ns->paused() == pausedBefore);

        tap(Key::Escape); // 포커스를 놓는다 — 이제 Space 는 게임 키
        CHECK_FALSE(app.gameUi().hasFocus());
        tap(Key::Space);
        for (int i = 0; i < 60 && ns->paused() == pausedBefore; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(ns->paused() != pausedBefore);
        CHECK(serverLogs().size() == 1);
    }
    TEST_CASE("game ui layer (v0.7 NG-06 (2)): 글자 칸 — hud 클릭도 포커스, 넣기 · 캐럿 · 지우기 · maxLength, Enter 는 "
              "보내기 (값 = 코드 포인트 수), 떠날 때 바뀌었으면 보내기, multiline 의 줄 바꿈 · 줄 이동") {
        class Vars final : public ui::IUiDataSource {
        public:
            std::map<std::string, ui::UiScalar, std::less<>> values;
            ui::UiScalar var(std::string_view key) const override {
                const auto it = values.find(key);
                return it == values.end() ? ui::UiScalar{} : it->second;
            }
            ui::UiScalar session(std::string_view) const override { return {}; }
            ui::UiScalar component(std::string_view, ui::UiTarget) const override { return {}; }
        } vars;
        GameUiInfo info;
        info.documents.push_back(
            {"eco", "hud", R"({"id": "eco.hud", "root": {"kind": "panel", "stack": "row", "gap": 20, "children": [
            {"kind": "textinput", "id": "name", "value": {"bind": "var:eco.name"}, "maxLength": 5, "on": {"change": {"signal": "eco.name"}}},
            {"kind": "checkbox", "id": "chk", "value": {"bind": "var:eco.chk"}},
            {"kind": "textinput", "id": "note", "value": {"bind": "var:eco.note"}, "multiline": true, "on": {"change": {"signal": "eco.note"}}},
            {"kind": "textinput", "id": "off", "value": {"bind": "var:eco.off"}, "disabled": true}]}})"});
        GameUiLayer layer;
        layer.setSources(info);
        REQUIRE(layer.documentCount() == 1);
        layer.setData(&vars);
        layer.setVarWriter(
            [&](std::string_view key, const ui::UiScalar& v) { vars.values.insert_or_assign(std::string(key), v); });
        const ui::FixedTextMeasure measure;
        layer.layout(800, 600, 1.0f, measure);
        const auto clickAt = [&](std::string_view id) {
            const auto [x, y] = centerOf(*find(layer, id));
            std::vector<ui::UiAction> ev;
            (void)layer.route(x, y, false, true, true, ev);
            (void)layer.route(x, y, false, false, false, ev);
            return ev;
        };
        const auto name = [&] { return ui::evaluate("var:eco.name", vars).text; };

        CHECK_FALSE(layer.text("x")); // 포커스가 없으면 글자는 게임 UI 의 것이 아니다
        CHECK(clickAt("name").empty());
        REQUIRE(layer.hasFocus());
        CHECK(layer.editingText());
        CHECK(layer.text("가나"));
        CHECK(name() == "가나");
        CHECK(layer.caret() == 2);
        std::vector<ui::UiAction> ev;
        CHECK(layer.key(ui::UiKey::Left, ev)); // 왼 · 오른은 캐럿 (다른 컨트롤로 가지 않는다)
        CHECK(layer.text("x"));
        CHECK(name() == "가x나");
        CHECK(layer.key(ui::UiKey::Backspace, ev));
        CHECK(name() == "가나");
        CHECK(layer.key(ui::UiKey::Home, ev));
        CHECK(layer.caret() == 0);
        CHECK(layer.key(ui::UiKey::Delete, ev));
        CHECK(name() == "나");
        CHECK(layer.text("abcdefgh")); // maxLength 5 — 남은 네 자리만
        CHECK(name() == "abcd나");
        CHECK(layer.key(ui::UiKey::End, ev));
        CHECK(layer.caret() == 5);
        CHECK(ev.empty());
        // Enter = 보내기 (값 = 코드 포인트 수 — 글은 var 에), 포커스는 남는다
        CHECK(layer.key(ui::UiKey::Activate, ev));
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].kind == ui::UiAction::Kind::Signal);
        CHECK(ev[0].target == "eco.name");
        CHECK(ev[0].value == 5);
        CHECK(layer.editingText());
        ev.clear();
        CHECK(layer.key(ui::UiKey::Release, ev)); // 보낸 뒤 바뀌지 않았다 — 신호 없음
        CHECK(ev.empty());
        CHECK_FALSE(layer.hasFocus());

        // 다시 누르면 캐럿은 끝, 바꾸고 다른 곳 (빈 자리) 을 누르면 놓으며 보낸다
        (void)clickAt("name");
        CHECK(layer.caret() == 5);
        CHECK(layer.key(ui::UiKey::Backspace, ev));
        (void)layer.route(700, 500, false, true, true, ev);
        (void)layer.route(700, 500, false, false, false, ev);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].value == 4);
        CHECK_FALSE(layer.hasFocus());

        // multiline — Enter 는 줄 바꿈, 위 · 아래는 줄 (첫 줄에서 위는 다른 컨트롤 찾기), Tab 으로 떠나면 보내기
        ev.clear();
        (void)clickAt("note");
        CHECK(layer.text("ab"));
        CHECK(layer.key(ui::UiKey::Activate, ev));
        CHECK(layer.text("c"));
        CHECK(ui::evaluate("var:eco.note", vars).text == "ab\nc");
        CHECK(ev.empty());
        CHECK(layer.key(ui::UiKey::Up, ev));
        CHECK(layer.caret() == 1);
        CHECK(layer.focused()->id == "note");
        CHECK(layer.key(ui::UiKey::Next, ev)); // note → (off 는 꺼짐) → name
        CHECK(layer.focused()->id == "name");
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].target == "eco.note");
        CHECK(ev[0].value == 4);

        // checkbox 포커스는 글자 칸이 아니다 — 글자는 받지 않는다
        CHECK(layer.key(ui::UiKey::Next, ev));
        CHECK(layer.focused()->id == "chk");
        CHECK_FALSE(layer.editingText());
        CHECK_FALSE(layer.text("q"));
        // 꺼진 칸 — 눌러도 포커스 없음
        CHECK(layer.key(ui::UiKey::Release, ev));
        CHECK(clickAt("off").empty());
        CHECK_FALSE(layer.hasFocus());

        // 그리기 (캐럿은 재기가 있을 때)
        (void)clickAt("name");
        CountingRenderer r;
        layer.draw(r, &measure);
        CHECK(r.fills > 0);
    }
    TEST_CASE(
        "app (local server + ImGui 헤드리스, v0.7 NG-06 (2)): HUD 글자 칸을 누르면 창의 글자 입력이 켜지고, 친 글 "
        "(Space 포함) 은 var 에 · 게임 키 (Space = 일시정지) 에는 가지 않고, Enter 가 신호 (서버 onEvent), Esc 가 "
        "놓으며 글자 입력을 끈다") {
        TempDir root("game_ui_text");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Text";
        project::NewProjectOptions o;
        o.gameId = "studio.text";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const fs::path pack = dir / "content" / "ecosystem";
        std::ofstream(pack / "signals.json", std::ios::binary) << R"([{"id": "eco.say", "min": 0, "max": 64}])";
        fs::create_directories(pack / "scripts");
        std::ofstream(pack / "scripts" / "say.ts", std::ios::binary) << R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.say",
  events: { signal: {} },
  onEvent(ctx, e) { ctx.log(`signal ${e.signal} ${e.value}`) },
})
)";
        fs::create_directories(pack / "presentation" / "ui");
        std::ofstream(pack / "presentation" / "ui" / "hud.json", std::ios::binary)
            << R"({"id": "eco.hud", "root": {"kind": "panel", "anchor": {"min": [0, 1], "max": [0, 1]}, "offset": [20, -60, 0, 0],
                  "children": [{"kind": "textinput", "id": "say", "value": {"bind": "var:eco.say"},
                  "on": {"change": {"signal": "eco.say"}}}]}})";

        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        const auto tap = [&](Key k) {
            window.inject(KeyDown{k, 0, {}, false});
            window.inject(KeyUp{k, 0, {}});
            REQUIRE(app.frame());
        };
        tap(Key::F1); // 도구 패널을 감춘다 (ImGui 가 키보드를 원하지 않게)
        REQUIRE(app.frame());
        const auto serverLogs = [&] {
            std::vector<std::string> out;
            if (const auto* sys = ns->localServer()->host().world().scriptSystems()) {
                for (const script::ScriptLogLine& l : sys->host().recentLogs()) {
                    out.push_back(l.text);
                }
            }
            return out;
        };
        const bool pausedBefore = ns->paused();
        CHECK_FALSE(window.textInputActive());

        const ui::UiLayoutNode* say = find(app.gameUi(), "say");
        REQUIRE(say != nullptr);
        const auto [sx, sy] = centerOf(*say);
        const Vec2 at{static_cast<f32>(sx), static_cast<f32>(sy)};
        window.inject(MouseMove{at, {0, 0}});
        window.inject(MouseButtonDown{MouseButton::Left, at});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, at});
        REQUIRE(app.frame());
        REQUIRE(app.gameUi().editingText());
        CHECK(window.textInputActive());

        // 친 글 — Space 는 글자 (키로는 누르기 · 일시정지가 아니다)
        for (const char32_t c : std::u32string(U"hi 안녕")) {
            if (c == U' ') {
                window.inject(KeyDown{Key::Space, 0, {}, false});
            }
            window.inject(TextInput{c});
            if (c == U' ') {
                window.inject(KeyUp{Key::Space, 0, {}});
            }
        }
        REQUIRE(app.frame());
        CHECK(ui::evaluate("var:eco.say", *ns->uiData()).text == "hi 안녕");
        tap(Key::Backspace);
        CHECK(ui::evaluate("var:eco.say", *ns->uiData()).text == "hi 안");
        tap(Key::Enter);
        for (int i = 0; i < 60 && serverLogs().empty(); ++i) {
            REQUIRE(app.frame());
        }
        CHECK(serverLogs() == std::vector<std::string>{"signal eco.say 4"});
        CHECK(ns->paused() == pausedBefore);

        tap(Key::Escape);
        CHECK_FALSE(app.gameUi().hasFocus());
        REQUIRE(app.frame());
        CHECK_FALSE(window.textInputActive());
        CHECK(serverLogs().size() == 1); // 보낸 뒤 바뀌지 않았다
    }
    TEST_CASE("game ui layer (v0.7 NG-06 (3)): dropdown — 누르면 목록 (문서들 위), 목록 위 포인터는 잡고 가리킨 줄, "
              "누르면 고르기 (var · 신호 = 번호), 자신 · 밖을 누르면 닫기, 키 (Enter 열기 · 위아래 · Enter 고르기 · "
              "Esc 닫기 · "
              "왼 · 오른 한 칸). tabs — 머리 칸 누르기 · 왼 · 오른, 고른 자식만 배치") {
        class Vars final : public ui::IUiDataSource {
        public:
            std::map<std::string, ui::UiScalar, std::less<>> values;
            ui::UiScalar var(std::string_view key) const override {
                const auto it = values.find(key);
                return it == values.end() ? ui::UiScalar{} : it->second;
            }
            ui::UiScalar session(std::string_view) const override { return {}; }
            ui::UiScalar component(std::string_view, ui::UiTarget) const override { return {}; }
        } vars;
        GameUiInfo info;
        info.documents.push_back(
            {"eco", "hud", R"({"id": "eco.hud", "root": {"kind": "panel", "stack": "row", "gap": 20, "children": [
            {"kind": "dropdown", "id": "size", "value": {"bind": "var:eco.size"}, "options": ["작게", "보통", "아주 크게"],
             "on": {"change": {"signal": "eco.size"}}},
            {"kind": "tabs", "id": "tabs", "value": {"bind": "var:eco.tab"}, "labels": ["가", "나다"],
             "on": {"change": {"signal": "eco.tab"}},
             "children": [{"kind": "text", "id": "pa", "text": "A"}, {"kind": "text", "id": "pb", "text": "BBBB"}]}]}})"});
        info.documents.push_back(
            {"eco", "menu",
             R"({"id": "eco.menu", "layer": "menu", "root": {"kind": "panel", "anchor": {"min": [0, 1], "max": [0, 1]},
            "offset": [0, -40, 0, 0], "children": [
            {"kind": "dropdown", "id": "q", "value": {"bind": "var:eco.q"}, "options": ["lo", "mid", "hi"],
             "on": {"change": {"signal": "eco.q"}}}]}})"});
        GameUiLayer layer;
        layer.setSources(info);
        REQUIRE(layer.documentCount() == 2);
        layer.setData(&vars);
        layer.setVarWriter(
            [&](std::string_view key, const ui::UiScalar& v) { vars.values.insert_or_assign(std::string(key), v); });
        const ui::FixedTextMeasure measure;
        layer.layout(800, 600, 1.0f, measure);
        const auto press = [&](i32 x, i32 y) {
            std::vector<ui::UiAction> ev;
            (void)layer.route(x, y, false, true, true, ev);
            (void)layer.route(x, y, false, false, false, ev);
            return ev;
        };
        const auto clickId = [&](std::string_view id) {
            const auto [x, y] = centerOf(*find(layer, id));
            return press(x, y);
        };

        // hud dropdown — 누르면 열린다 (포커스는 아니다), 목록 = 상자 아래 {0, 28, 68, 84}
        const ui::UiLayoutNode* size = find(layer, "size");
        REQUIRE(size != nullptr);
        CHECK(size->rect == ui::UiRect{0, 0, 68, 28});
        CHECK(clickId("size").empty());
        CHECK(layer.listOpen());
        CHECK_FALSE(layer.hasFocus());
        CountingRenderer drawn;
        layer.draw(drawn);
        CHECK(drawn.fills > 0);
        std::vector<ui::UiAction> ev;
        const ui::UiPointerRoute over = layer.route(34, 98, false, false, false, ev); // 셋째 줄 위
        CHECK(over.captured);
        CHECK(over.owner == ui::UiPointerRoute::Owner::Hud);
        CHECK(layer.highlighted() == 2);
        ev = press(34, 98);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].target == "eco.size");
        CHECK(ev[0].value == 2);
        CHECK(vars.values.at("eco.size").number == 2.0);
        CHECK_FALSE(layer.listOpen());
        CHECK(layer.selectedIndex(*size->node) == 2);
        // 다시 열고 자신을 누르면 닫기만, 밖을 누르면 닫기만 (고르지 않는다)
        (void)clickId("size");
        CHECK(layer.listOpen());
        CHECK(layer.highlighted() == 2); // 고른 줄부터
        CHECK(clickId("size").empty());
        CHECK_FALSE(layer.listOpen());
        (void)clickId("size");
        CHECK(press(700, 300).empty());
        CHECK_FALSE(layer.listOpen());
        // 같은 줄을 다시 고르면 신호 없음
        (void)clickId("size");
        CHECK(press(34, 98).empty());

        // tabs — 머리 칸 누르기 (나다 → 1), 고른 자식만 배치
        const ui::UiLayoutNode* tabs = find(layer, "tabs");
        REQUIRE(tabs != nullptr);
        REQUIRE(tabs->parts.size() == 2);
        CHECK(find(layer, "pa") != nullptr);
        CHECK(find(layer, "pb") == nullptr);
        const ui::UiRect head = tabs->parts[1];
        ev = press(head.x + 2, head.y + 2);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].target == "eco.tab");
        CHECK(ev[0].value == 1);
        layer.layout(800, 600, 1.0f, measure);
        CHECK(find(layer, "pa") == nullptr);
        CHECK(find(layer, "pb") != nullptr);
        CHECK(press(head.x + 2, head.y + 2).empty()); // 이미 고른 칸

        // menu dropdown — 누르면 포커스 + 목록, 키: 아래 · Enter 고르기, Enter 열기 · Esc 닫기 (포커스는 남음) · Esc
        // 놓기
        (void)clickId("q");
        REQUIRE(layer.hasFocus());
        CHECK(layer.focused()->id == "q");
        CHECK(layer.listOpen());
        CHECK(layer.highlighted() == 0); // 모르는 값 — 첫 줄
        ev.clear();
        CHECK(layer.key(ui::UiKey::Down, ev));
        CHECK(layer.key(ui::UiKey::Down, ev));
        CHECK(layer.key(ui::UiKey::Down, ev)); // 끝에서 멈춘다
        CHECK(layer.highlighted() == 2);
        CHECK(layer.key(ui::UiKey::Up, ev));
        CHECK(layer.key(ui::UiKey::Activate, ev));
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].target == "eco.q");
        CHECK(ev[0].value == 1);
        CHECK_FALSE(layer.listOpen());
        CHECK(layer.key(ui::UiKey::Activate, ev));
        CHECK(layer.listOpen());
        CHECK(layer.key(ui::UiKey::Release, ev));
        CHECK_FALSE(layer.listOpen());
        CHECK(layer.hasFocus());
        // 닫힌 채 왼 · 오른 = 한 칸 (신호 바로)
        ev.clear();
        CHECK(layer.key(ui::UiKey::Right, ev));
        CHECK(layer.key(ui::UiKey::Right, ev)); // 끝 — 그대로 (신호 없음)
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].value == 2);
        // Tab 으로 다른 컨트롤에 가면 열린 목록은 닫힌다
        CHECK(layer.key(ui::UiKey::Activate, ev));
        CHECK(layer.listOpen());
        CHECK(layer.key(ui::UiKey::Next, ev));
        CHECK_FALSE(layer.listOpen());
        // tabs 에 포커스 — 왼 · 오른이 머리 칸
        while (layer.focused()->id != "tabs") {
            REQUIRE(layer.key(ui::UiKey::Next, ev));
        }
        ev.clear();
        CHECK(layer.key(ui::UiKey::Left, ev));
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].value == 0);
        CHECK(vars.values.at("eco.tab").number == 0.0);
        CHECK(layer.key(ui::UiKey::Release, ev));
        CHECK_FALSE(layer.hasFocus());
        // 원문이 바뀌면 목록을 닫는다
        (void)clickId("size");
        info.documents[0].text += " ";
        layer.setSources(info);
        CHECK_FALSE(layer.listOpen());
    }
    TEST_CASE("game ui layer (v0.7 NG-06 (3b)): scroll — 휠은 포인터 아래 scroll 을 한 칸 40 씩 (범위로 자른다 · 밖은 "
              "쓰지 않는다), Tab 포커스가 가려지면 보이게 민다, modal 이 보이면 hud 휠은 쓰지 않는다") {
        GameUiInfo info;
        std::string rows;
        for (int i = 0; i < 5; ++i) {
            rows += std::format(R"({}{{"kind": "button", "id": "b{}", "text": "{}", "size": [80, 20]}})",
                                i > 0 ? ", " : "", i, i);
        }
        info.documents.push_back(
            {"eco", "hud",
             std::format(R"({{"id": "eco.hud", "root": {{"kind": "scroll", "id": "list", "size": [100, 50],
            "stack": "column", "children": [{}]}}}})",
                         rows)});
        info.documents.push_back({"eco", "pop",
                                  R"({"id": "eco.pop", "layer": "modal", "visible": false, "root": {"kind": "panel",
            "anchor": {"min": [0.5, 0.5], "max": [0.5, 0.5]}, "size": [10, 10]}})"});
        GameUiLayer layer;
        layer.setSources(info);
        REQUIRE(layer.documentCount() == 2);
        const ui::FixedTextMeasure measure;
        const auto relayout = [&] { layer.layout(800, 600, 1.0f, measure); };
        const auto yOf = [&](std::string_view id) { return find(layer, id)->rect.y; };
        relayout();
        CHECK(find(layer, "list")->scrollRange == 50.0f);

        CHECK(layer.wheel(10, 10, -1)); // 아래로 한 칸 = 40
        relayout();
        CHECK(yOf("b0") == -40);
        CHECK(layer.wheel(10, 10, -5)); // 끝에서 자른다 (그래도 쓴다 — 게임 확대로 가지 않는다)
        relayout();
        CHECK(yOf("b0") == -50);
        CHECK_FALSE(layer.wheel(300, 300, -1)); // scroll 밖
        CHECK_FALSE(layer.wheel(10, 10, 0));
        CHECK(layer.wheel(10, 10, 10));
        relayout();
        CHECK(yOf("b0") == 0);

        // Tab — b3 (60 ~ 80) 은 보이는 50 아래 → 30 밀어 30 ~ 50, b4 → 50, 처음 (b0) 으로 돌면 0
        std::vector<ui::UiAction> ev;
        for (int i = 0; i < 4; ++i) {
            REQUIRE(layer.key(ui::UiKey::Next, ev));
            relayout();
        }
        CHECK(layer.focused()->id == "b3");
        CHECK(yOf("b3") == 30);
        REQUIRE(layer.key(ui::UiKey::Next, ev));
        relayout();
        CHECK(yOf("b4") == 30);
        CHECK(layer.scrollOf(*find(layer, "list")->node, -1) == 50.0f);
        REQUIRE(layer.key(ui::UiKey::Next, ev));
        relayout();
        CHECK(layer.focused()->id == "b0");
        CHECK(yOf("b0") == 0);
        CHECK(ev.empty());

        // modal 이 보이면 hud scroll 은 휠을 받지 않는다
        layer.apply(ui::UiAction{ui::UiAction::Kind::Ui, "eco.pop", std::nullopt, "show"});
        relayout();
        CHECK_FALSE(layer.wheel(10, 10, -1));
        // 원문이 바뀌면 밀기를 잊는다
        layer.apply(ui::UiAction{ui::UiAction::Kind::Ui, "eco.pop", std::nullopt, "hide"});
        relayout();
        CHECK(layer.wheel(10, 10, -1));
        info.documents[0].text += " ";
        layer.setSources(info);
        relayout();
        CHECK(yOf("b0") == 0);
    }
    TEST_CASE(
        "app (local server + ImGui 헤드리스, v0.7 NG-06 (3b)): HUD scroll 위의 휠은 scroll 을 밀고 카메라를 확대하지 "
        "않는다, 밖의 휠은 카메라 확대") {
        TempDir root("game_ui_scroll");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Scroll";
        project::NewProjectOptions o;
        o.gameId = "studio.scroll";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const fs::path pack = dir / "content" / "ecosystem";
        fs::create_directories(pack / "presentation" / "ui");
        std::ofstream(pack / "presentation" / "ui" / "hud.json", std::ios::binary)
            << R"({"id": "eco.hud", "root": {"kind": "scroll", "id": "list", "size": [120, 60], "stack": "column", "children": [
                  {"kind": "panel", "size": [100, 50]}, {"kind": "panel", "size": [100, 50]}, {"kind": "panel", "size": [100, 50]}]}})";

        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        window.inject(KeyDown{Key::F1, 0, {}, false}); // 도구 패널을 감춘다 (ImGui 가 마우스를 원하지 않게)
        window.inject(KeyUp{Key::F1, 0, {}});
        REQUIRE(app.frame());
        REQUIRE(app.frame());

        const f32 zoom = app.camera().pixelsPerUnit;
        window.inject(MouseMove{{20, 20}, {0, 0}});
        window.inject(MouseWheel{{0, -1}});
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK(app.camera().pixelsPerUnit == zoom);
        const ui::UiLayoutNode* list = find(app.gameUi(), "list");
        REQUIRE(list != nullptr);
        CHECK(list->scroll == 40.0f);

        window.inject(MouseMove{{600, 400}, {0, 0}});
        window.inject(MouseWheel{{0, -1}});
        REQUIRE(app.frame());
        CHECK(app.camera().pixelsPerUnit != zoom);
    }
    TEST_CASE("app (local server + ImGui 헤드리스, v0.7 NG-06 (4)): 게임패드 — A 가 첫 포커스 · 누르기, 십자 · 왼 스틱 "
              "(넘을 때 한 번) 이 방향, B 가 놓기, 창 포커스가 없으면 패드는 아무것도 하지 않는다") {
        class FakePads final : public IGamepadSource {
        public:
            GamepadState pad;
            void poll(std::span<GamepadState, kMaxGamepads> out) override {
                for (GamepadState& s : out) {
                    s = GamepadState{};
                }
                out[0] = pad;
            }
        } fake;
        fake.pad.connected = true;
        TempDir root("game_ui_pad");
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const fs::path dir = root.path / "Pad";
        project::NewProjectOptions o;
        o.gameId = "studio.pad";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const fs::path pack = dir / "content" / "ecosystem";
        fs::create_directories(pack / "presentation" / "ui");
        std::ofstream(pack / "presentation" / "ui" / "hud.json", std::ios::binary)
            << R"({"id": "eco.hud", "root": {"kind": "panel", "stack": "row", "gap": 20, "children": [
                  {"kind": "checkbox", "id": "a", "text": "a", "value": {"bind": "var:eco.a"}},
                  {"kind": "checkbox", "id": "b", "text": "b", "value": {"bind": "var:eco.b"}}]}})";

        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = dir.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        cfg.gamepads = &fake;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        window.inject(KeyDown{Key::F1, 0, {}, false}); // 도구 패널을 감춘다
        window.inject(KeyUp{Key::F1, 0, {}});
        REQUIRE(app.frame());
        const auto tapPad = [&](GamepadButton b) {
            fake.pad.buttons = 1u << static_cast<u32>(b);
            REQUIRE(app.frame());
            fake.pad.buttons = 0;
            REQUIRE(app.frame());
        };
        const auto stick = [&](f32 x) {
            fake.pad.axes[static_cast<usize>(GamepadAxis::LeftX)] = x;
            REQUIRE(app.frame());
        };
        const auto flag = [&](std::string_view key) { return ui::evaluate(key, *ns->uiData()).flag; };

        CHECK_FALSE(app.gameUi().hasFocus());
        tapPad(GamepadButton::South); // 포커스가 없으면 첫 컨트롤에 포커스만
        REQUIRE(app.gameUi().hasFocus());
        CHECK(app.gameUi().focused()->id == "a");
        CHECK_FALSE(flag("var:eco.a"));
        tapPad(GamepadButton::DpadRight);
        CHECK(app.gameUi().focused()->id == "b");
        tapPad(GamepadButton::South);
        CHECK(flag("var:eco.b"));
        // 왼 스틱 — 넘을 때 한 번 (그대로 두어도 다시 가지 않는다), 0.3 아래로 돌아오면 다시
        stick(-0.9f);
        CHECK(app.gameUi().focused()->id == "a");
        stick(-0.9f);
        stick(-0.4f); // 0.3 ~ 0.5 — 그대로
        stick(0.9f);
        CHECK(app.gameUi().focused()->id == "b");
        stick(0.0f);
        tapPad(GamepadButton::LeftShoulder);
        CHECK(app.gameUi().focused()->id == "a");
        tapPad(GamepadButton::East); // 놓기
        CHECK_FALSE(app.gameUi().hasFocus());
        tapPad(GamepadButton::East); // 포커스가 없으면 아무것도 아니다
        CHECK_FALSE(app.gameUi().hasFocus());

        // 창 포커스가 없으면 패드는 쓰지 않는다 (I3)
        window.inject(FocusLost{});
        REQUIRE(app.frame());
        tapPad(GamepadButton::South);
        CHECK_FALSE(app.gameUi().hasFocus());
        window.inject(FocusGained{});
        REQUIRE(app.frame());
        tapPad(GamepadButton::South);
        CHECK(app.gameUi().hasFocus());
    }
}
