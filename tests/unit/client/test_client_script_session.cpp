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
        REQUIRE(so->systems.size() == 1);
        CHECK(so->systems[0].starts_with("클라이언트 스크립트 eco.hud: 호출 "));
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
        drawScriptOverlay(info, cam, true);
        drawScriptOverlay(ScriptOverlayInfo{}, cam, false); // 줄이 없고 패널을 숨겼으면 칸도 없다
        (void)layer.endFrame(platform::CursorShape::Arrow);
        layer.acknowledgeTextures();

        ed.undo(ns);
        for (int i = 0; i < 30 && ns.scriptOverlay(); ++i) {
            frame();
        }
        CHECK_FALSE(ns.scriptOverlay().has_value());
        CHECK(ns.content()->overlay().empty());
    }
}
