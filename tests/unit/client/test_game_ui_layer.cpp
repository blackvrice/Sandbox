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
        CHECK(feed->rect.w == 120);

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
}
