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
#include "core/components/life/Life.hpp"
#include "core/project/GameProject.hpp"
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
}
