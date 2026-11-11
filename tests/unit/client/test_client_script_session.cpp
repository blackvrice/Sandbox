// 클라이언트 스크립트 세션 (v0.3 M5-T3, ADR-0067): 로컬 서버에 EditContent Script 로 client 시스템을 더하면 서버는 그
// 시스템을 돌리지 않고 (어댑터 없음), NetworkSession 이 오버레이 콘텐츠로 ClientScriptSystems 를 만들어 프레임마다
// onUpdate — scriptOverlay() 의 줄, 되돌리면 사라진다. "스크립트" 칸 · 개체 위 글자 그리기.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <imgui.h>

#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "apps/client/ui/ScriptPanel.hpp"
#include "core/script/ClientScriptHost.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "editor/Editor.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using content::ContentKind;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

constexpr std::string_view kHud = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.hud",
  domain: "client",
  onUpdate(ctx) { ctx.print(`틱 ${ctx.tick} 프레임 ${ctx.frame}`) },
})
)";

} // namespace

TEST_SUITE("client") {
    TEST_CASE("client script (local server): 오버레이로 더한 client 시스템을 클라이언트만 돌린다 · 되돌리면 없다") {
        render::MaterialLibrary mats;
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = SBX_CONTENT_DIR;
        d.inlineServer = true;
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        NetworkSession& ns = **made;
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns.update(kDt);
            render::RenderWorld w;
            ns.extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, ns);
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        CHECK_FALSE(ns.scriptOverlay().has_value()); // client 시스템이 없다
        CHECK(ns.clientScripts() == nullptr);

        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.hud",
                               nlohmann::json{{"id", "eco.hud"}, {"source", std::string(kHud)}}));
        for (int i = 0; i < 30 && !ns.scriptOverlay(); ++i) {
            frame();
        }
        REQUIRE(ns.overlayError().empty());
        const auto so = ns.scriptOverlay();
        REQUIRE(so.has_value());
        REQUIRE(so->lines.size() == 1);
        CHECK(so->lines[0].starts_with(std::format("틱 {} 프레임 ", ns.serverTick())));
        // 서버는 client 시스템을 돌리지 않는다 — 서버 시스템이 없으니 어댑터도 없다
        CHECK(ns.localServer()->host().world().scriptSystems() == nullptr);
        CHECK(ns.localServer()->host().world().content().scriptPrograms()->systemCount() == 1);
        const u32 before = ns.clientScripts()->frames();
        frame();
        frame();
        CHECK(ns.clientScripts()->frames() == before + 2);

        // 그리기 — 칸 · 글자 (헤드리스 ImGui)
        platform::HeadlessWindow window;
        ImGuiLayer layer(window);
        platform::PlatformEventQueue q;
        window.pollEvents(q);
        layer.feed(q);
        layer.beginFrame(1.0 / 60.0);
        ScriptOverlayInfo info = *ns.scriptOverlay();
        info.labels.push_back({Vec2{1, 1}, "글자"});
        render::Camera2D cam;
        cam.viewportWidth = 800;
        cam.viewportHeight = 600;
        drawScriptOverlay(info, cam);
        drawScriptOverlay(ScriptOverlayInfo{}, cam); // 줄이 없으면 칸도 없다
        (void)layer.endFrame(platform::CursorShape::Arrow);
        layer.acknowledgeTextures();

        ed.undo(ns);
        for (int i = 0; i < 30 && ns.scriptOverlay(); ++i) {
            frame();
        }
        CHECK_FALSE(ns.scriptOverlay().has_value());
        CHECK(ns.content()->overlay().empty());
    }

    TEST_CASE("script profile (local server): 서버 표 (ServerStats — 프로토콜 14) · client 시스템의 1 초 창 · 패널") {
        render::MaterialLibrary mats;
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = SBX_CONTENT_DIR;
        d.inlineServer = true;
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        NetworkSession& ns = **made;
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns.update(kDt);
            render::RenderWorld w;
            ns.extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, ns);
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        CHECK_FALSE(ns.scriptProfile().has_value()); // 스크립트 시스템이 없다

        constexpr std::string_view kTicker = R"(import { defineSystem, i32 } from "sandbox"
export default defineSystem({
  id: "eco.ticker",
  onTick(ctx) { let n: i32 = 0; for (let i: i32 = 0; i < 100; i = i + 1) { n = n + i } },
})
)";
        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.ticker",
                               nlohmann::json{{"id", "eco.ticker"}, {"source", std::string(kTicker)}}));
        for (int i = 0; i < 5 && !ed.canUndo(); ++i) {
            frame();
        }
        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.hud",
                               nlohmann::json{{"id", "eco.hud"}, {"source", std::string(kHud)}}));
        // 서버 통계는 1 초마다 (Inline — dt 시간), client 창도 1 초
        const auto ready = [&] {
            const auto p = ns.scriptProfile();
            return p && !p->server.empty() && p->server[0].callsPerSecond > 0 && !p->client.empty() &&
                   p->client[0].callsPerSecond > 0;
        };
        for (int i = 0; i < 200 && !ready(); ++i) {
            frame();
        }
        REQUIRE(ready());
        const ScriptProfileInfo p = *ns.scriptProfile();
        REQUIRE(p.server.size() == 1);
        const ScriptProfileRow& srv = p.server[0];
        CHECK(srv.id == "eco.ticker");
        CHECK(srv.callsPerSecond == doctest::Approx(30).epsilon(0.2)); // 30 Hz 틱마다 한 번
        CHECK(srv.fuelPerCall > 100);
        CHECK(srv.maxFuel > 100);
        CHECK(srv.msPerSecond > 0); // 서버의 진단 시계
        CHECK(srv.maxMs > 0);
        CHECK(srv.traps == 0);
        CHECK_FALSE(srv.disabled);
        REQUIRE(p.client.size() == 1);
        CHECK(p.client[0].id == "eco.hud");
        CHECK(p.client[0].callsPerSecond == doctest::Approx(30).epsilon(0.2)); // 프레임마다 (kDt = 1/30)
        CHECK(p.client[0].msPerCall > 0);
        // 함수 표 (v0.9 M6-T4 — 프로토콜 18): 서버 onTick · 클라이언트 onUpdate 의 자리와 몫
        REQUIRE_FALSE(p.serverFunctions.empty());
        CHECK(p.serverFunctions[0].site.ends_with(" (onTick)"));
        CHECK(p.serverFunctions[0].site.find("/scripts/") != std::string::npos);
        CHECK(p.serverFunctions[0].calls > 0);
        CHECK(p.serverFunctions[0].share > 0.5);
        REQUIRE_FALSE(p.clientFunctions.empty());
        CHECK(p.clientFunctions[0].site.ends_with(" (onUpdate)"));
        CHECK(p.clientFunctions[0].fuel > 0);
        // 서버 월드의 끝 줄에도 시간
        const auto* sys = ns.localServer()->host().world().scriptSystems();
        REQUIRE(sys != nullptr);
        CHECK(sys->timed());
        CHECK(script::describeScriptSystems(*sys)[0].find(" · 시간 ") != std::string::npos);

        // 패널 그리기 (헤드리스 ImGui) · 줄 계산
        platform::HeadlessWindow window;
        ImGuiLayer layer(window);
        platform::PlatformEventQueue q;
        window.pollEvents(q);
        layer.feed(q);
        layer.beginFrame(1.0 / 60.0);
        drawScriptProfilePanel(p);
        drawScriptProfilePanel(ScriptProfileInfo{.server = {}, .serverOmitted = 3, .client = {}});
        (void)layer.endFrame(platform::CursorShape::Arrow);
        layer.acknowledgeTextures();

        const ScriptProfileRow r = scriptProfileRow("a.b", 10, 5000, 2'000'000, 2.0, 400'000, 900, 1, 2, 3, true);
        CHECK(r.callsPerSecond == 5);
        CHECK(r.fuelPerCall == 500);
        CHECK(r.msPerSecond == 1);
        CHECK(r.msPerCall == doctest::Approx(0.2));
        CHECK(r.maxMs == doctest::Approx(0.4));
        CHECK(r.disabled);
        const ScriptProfileRow idle = scriptProfileRow("a.c", 0, 0, 0, 0, 0, 0, 0, 0, 0, false);
        CHECK(idle.callsPerSecond == 0);
        CHECK(idle.fuelPerCall == 0);
    }

    TEST_CASE("client 입력 → send → 서버 onEvent \"signal\" (로컬 세션, v0.5 M2-T6) · 소리 이벤트는 앱이 가져간다") {
        // eco 팩 사본에 signals.json (eco.ping 0~9)
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "client-script-session" / "signal";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
        fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", root / "ecosystem", fs::copy_options::recursive);
        std::ofstream(root / "ecosystem" / "signals.json", std::ios::binary)
            << R"([{"id": "eco.ping", "min": 0, "max": 9}])";

        render::MaterialLibrary mats;
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = root;
        d.inlineServer = true;
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        NetworkSession& ns = **made;
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns.update(kDt);
            render::RenderWorld w;
            ns.extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, ns);
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.ping",
                               nlohmann::json{{"id", "eco.ping"}, {"source", R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.ping",
  domain: "client",
  onUpdate(ctx) {
    if (ctx.keyPressed("ping")) { ctx.send("eco.ping", 5); ctx.playSound("eco.pop") }
  },
})
)"}}));
        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.pong",
                               nlohmann::json{{"id", "eco.pong"}, {"source", R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.pong",
  events: { signal: {} },
  onEvent(ctx, e) { ctx.log(`signal ${e.signal} ${e.value} ${e.player}`) },
})
)"}}));
        for (int i = 0;
             i < 60 && (ns.clientScripts() == nullptr || ns.localServer()->host().world().scriptSystems() == nullptr);
             ++i) {
            frame();
        }
        REQUIRE_MESSAGE(ns.overlayError().empty(), ns.overlayError());
        REQUIRE(ns.clientScripts() != nullptr);
        REQUIRE(ns.localServer()->host().world().scriptSystems() != nullptr);

        script::ClientScriptInput in;
        in.keys = {{"ping", true, true, false}};
        ns.setScriptInput(in);
        frame(); // 이 프레임에 누름 → 신호 · 소리
        in.keys = {{"ping", true, false, false}};
        ns.setScriptInput(in);
        for (int i = 0; i < 30; ++i) {
            frame();
        }
        CHECK(ns.scriptSignalsSent() == 1);
        CHECK(ns.takeScriptSounds() == std::vector<std::string>{"eco.pop"});
        CHECK(ns.takeScriptSounds().empty()); // 가져가면 비운다
        std::vector<std::string> logs;
        for (const script::ScriptLogLine& l : ns.localServer()->host().world().scriptSystems()->host().recentLogs()) {
            logs.push_back(l.text);
        }
        // 보낸 쪽 = 이 클라이언트 (서버의 플레이어 표 — 로컬 세션은 하나)
        const auto& players = ns.localServer()->host().world().players();
        REQUIRE(players.size() == 1);
        CHECK(logs == std::vector<std::string>{std::format("signal eco.ping 5 {}", players.begin()->first)});
    }
    TEST_CASE("client script (local server): ctx.setUi · setUiText · showUi (v0.7 M3-T4) — var 바인딩 · 문서 보이기, "
              "트랩난 호출의 것은 버린다") {
        render::MaterialLibrary mats;
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = SBX_CONTENT_DIR;
        d.inlineServer = true;
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        NetworkSession& ns = **made;
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns.update(kDt);
            render::RenderWorld w;
            ns.extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, ns);
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.ui",
                               nlohmann::json{{"id", "eco.ui"}, {"source", R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.ui",
  domain: "client",
  onUpdate(ctx) {
    ctx.setUi("eco.frame", ctx.frame)
    ctx.setUiText("eco.name", "토끼 왕")
    if (ctx.frame === 3) { ctx.showUi("eco.hud", false) }
  },
})
)"}}));
        for (int i = 0; i < 60 && ns.clientScripts() == nullptr && ns.overlayError().empty(); ++i) {
            frame();
        }
        REQUIRE_MESSAGE(ns.overlayError().empty(), ns.overlayError());
        REQUIRE(ns.clientScripts() != nullptr);
        REQUIRE(
            ed.editContent(ns, ContentKind::Script, "eco.zz_trap",
                           nlohmann::json{{"id", "eco.zz_trap"}, {"source", R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.zz_trap",
  domain: "client",
  onUpdate(ctx) {
    ctx.setUi("eco.bad", 1)
    while (true) {}
  },
})
)"}}));
        for (int i = 0; i < 60 && ns.clientScripts() == nullptr; ++i) {
            frame();
        }
        REQUIRE_MESSAGE(ns.overlayError().empty(), ns.overlayError());
        REQUIRE(ns.clientScripts() != nullptr);
        std::vector<std::pair<std::string, bool>> shows;
        for (int i = 0; i < 6; ++i) {
            frame();
            for (auto& s : ns.takeUiShows()) {
                shows.push_back(std::move(s));
            }
        }
        const ui::IUiDataSource* data = ns.uiData();
        REQUIRE(data != nullptr);
        const ui::UiScalar f = ui::evaluate("var:eco.frame", *data);
        CHECK(f.known());
        CHECK(f.number >= 6);
        CHECK(ui::evaluate("var:eco.name", *data).text == "토끼 왕");
        // eco.zz_trap 은 id 순으로 뒤 — 앞 시스템의 값은 남고, 연료가 다해 트랩난 호출의 것은 버렸다
        CHECK_FALSE(ui::evaluate("var:eco.bad", *data).known());
        CHECK(shows == std::vector<std::pair<std::string, bool>>{{"eco.hud", false}});
        CHECK(ns.takeUiShows().empty());
    }
    TEST_CASE(
        "client script (local server): ctx.uiText · uiFormat (v0.7 M4-T1 (2)) — 앱의 문자열 표 · 로케일, 인자, 없는 키 "
        "· 표 없음은 키 그대로") {
        render::MaterialLibrary mats;
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = SBX_CONTENT_DIR;
        d.inlineServer = true;
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        NetworkSession& ns = **made;
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns.update(kDt);
            render::RenderWorld w;
            ns.extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, ns);
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.words",
                               nlohmann::json{{"id", "eco.words"}, {"source", R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.words",
  domain: "client",
  onUpdate(ctx) {
    ctx.setUiText("eco.title", ctx.uiText("eco.title"))
    ctx.setUiText("eco.count", ctx.uiFormat("eco.count", "" + 3, "토끼"))
    ctx.setUiText("eco.none", ctx.uiText("eco.none"))
  },
})
)"}}));
        for (int i = 0; i < 60 && ns.clientScripts() == nullptr && ns.overlayError().empty(); ++i) {
            frame();
        }
        REQUIRE_MESSAGE(ns.overlayError().empty(), ns.overlayError());
        REQUIRE(ns.clientScripts() != nullptr);
        const ui::IUiDataSource* data = ns.uiData();
        REQUIRE(data != nullptr);
        // 표가 없으면 키 그대로
        frame();
        CHECK(ui::evaluate("var:eco.title", *data).text == "eco.title");

        ui::StringTable table;
        table.addPack(
            {{"en", ui::parseStringTable(R"({"eco.title": "Guard", "eco.count": "{1} x{0}"})", "eco").entries},
             {"ko", ui::parseStringTable(R"({"eco.title": "지키기", "eco.count": "{1} {0} 마리"})", "eco").entries}});
        table.setLocale("ko");
        ns.setUiStrings(&table);
        frame();
        CHECK(ui::evaluate("var:eco.title", *data).text == "지키기");
        CHECK(ui::evaluate("var:eco.count", *data).text == "토끼 3 마리");
        CHECK(ui::evaluate("var:eco.none", *data).text == "eco.none");
        table.setLocale("en");
        frame();
        CHECK(ui::evaluate("var:eco.count", *data).text == "토끼 x3");
        CHECK(table.takeMissing() == std::vector<std::string>{"eco.none"}); // 경고 한 번 (프레임마다 불러도)
        ns.setUiStrings(nullptr);
    }
    TEST_CASE(
        "client script (local server): ctx.uiVar · uiVarText (v0.7 NG-06 (2)) — 게임 UI 컨트롤이 쓴 var 를 읽는다 "
        "(수 · 참거짓 · 글자, 없음은 0 · \"\")") {
        render::MaterialLibrary mats;
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = SBX_CONTENT_DIR;
        d.inlineServer = true;
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        NetworkSession& ns = **made;
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns.update(kDt);
            render::RenderWorld w;
            ns.extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, ns);
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.echo",
                               nlohmann::json{{"id", "eco.echo"}, {"source", R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.echo",
  domain: "client",
  onUpdate(ctx) {
    ctx.setUiText("eco.echo", "[" + ctx.uiVarText("eco.name") + "]")
    ctx.setUi("eco.next", ctx.uiVar("eco.vol") + 1)
    ctx.setUi("eco.flag", ctx.uiVar("eco.on"))
    ctx.setUiText("eco.volText", ctx.uiVarText("eco.vol"))
    ctx.setUi("eco.nameNum", ctx.uiVar("eco.name"))
  },
})
)"}}));
        for (int i = 0; i < 60 && ns.clientScripts() == nullptr && ns.overlayError().empty(); ++i) {
            frame();
        }
        REQUIRE_MESSAGE(ns.overlayError().empty(), ns.overlayError());
        REQUIRE(ns.clientScripts() != nullptr);
        const ui::IUiDataSource* data = ns.uiData();
        REQUIRE(data != nullptr);
        frame();
        CHECK(ui::evaluate("var:eco.echo", *data).text == "[]"); // 없음 = ""
        CHECK(ui::evaluate("var:eco.next", *data).number == 1.0);

        // 게임 UI 컨트롤이 쓰는 길 (세션 setUiVar — GameUiLayer 의 VarWriter)
        ns.setUiVar("eco.name", ui::UiScalar::of(std::string("토끼")));
        ns.setUiVar("eco.vol", ui::UiScalar::of(2.5));
        ns.setUiVar("eco.on", ui::UiScalar::ofBool(true));
        frame();
        CHECK(ui::evaluate("var:eco.echo", *data).text == "[토끼]");
        CHECK(ui::evaluate("var:eco.next", *data).number == 3.5);
        CHECK(ui::evaluate("var:eco.flag", *data).number == 1.0);
        CHECK(ui::evaluate("var:eco.volText", *data).text == "2.5");
        CHECK(ui::evaluate("var:eco.nameNum", *data).number == 0.0); // 글자는 수로 0
    }
}
