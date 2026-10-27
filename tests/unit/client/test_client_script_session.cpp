// 클라이언트 스크립트 세션 (v0.3 M5-T3, ADR-0067): 로컬 서버에 EditContent Script 로 client 시스템을 더하면 서버는 그
// 시스템을 돌리지 않고 (어댑터 없음), NetworkSession 이 오버레이 콘텐츠로 ClientScriptSystems 를 만들어 프레임마다
// onUpdate — scriptOverlay() 의 줄, 되돌리면 사라진다. "스크립트" 칸 · 개체 위 글자 그리기.
#include <doctest/doctest.h>

#include <memory>
#include <string>

#include <imgui.h>

#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "apps/client/ui/ScriptPanel.hpp"
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
}
