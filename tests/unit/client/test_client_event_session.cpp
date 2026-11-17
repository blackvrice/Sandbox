// client onEvent — 로컬 서버 끝에서 끝 (v0.10 RT-06-7 조각 2, ADR-0141): 오버레이로 더한 client 시스템이 spawn 을
// 구독하면 서버가 그 틱의 생성을 GameEvents (프로토콜 19) 로 보내고, NetworkSession 이 복제 월드가 그 틱에 닿은
// 프레임에 개체를 netId 로 풀어 onEvent 에 넘긴다 (개체가 복제 월드에 있다). 구독이 없으면 서버는 보내지 않는다.
#include <doctest/doctest.h>

#include <string>

#include "apps/client/NetworkSession.hpp"
#include "editor/Editor.hpp"

using namespace sbx;
using namespace sbx::client;
using content::ContentKind;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

// 생성마다 UI 변수 — 받은 수 · 개체가 복제 월드에 있었던 수 · 시험이 만든 토끼 (7, -3) 를 받았나
constexpr std::string_view kFx = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "eco.fx",
  domain: "client",
  events: { spawn: {} },
  onEvent(ctx, e) {
    ctx.setUi("eco.spawns", ctx.uiVar("eco.spawns") + 1)
    if (e.entity !== null) {
      ctx.setUi("eco.found", ctx.uiVar("eco.found") + 1)
      if (e.x > 6.5 && e.x < 7.5 && e.y > -3.5 && e.y < -2.5) { ctx.setUi("eco.rabbit", 1) }
    }
  },
})
)";

f64 uiNumber(const NetworkSession& ns, std::string_view key) {
    return ns.uiData()->var(key).number;
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE(
        "client onEvent (local server, v0.10 RT-06-7 조각 2): 서버의 생성 → GameEvents → 복제 월드가 닿은 프레임에 "
        "onEvent (개체가 있다)") {
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
        CHECK(ns.localServer()->host().world().clientEventKinds() == 0); // 구독이 없으면 모으지도 보내지도 않는다

        REQUIRE(ed.editContent(ns, ContentKind::Script, "eco.fx",
                               nlohmann::json{{"id", "eco.fx"}, {"source", std::string(kFx)}}));
        for (int i = 0; i < 60 && (ns.clientScripts() == nullptr || !ns.clientScripts()->started()); ++i) {
            frame();
        }
        REQUIRE(ns.overlayError().empty());
        REQUIRE(ns.clientScripts() != nullptr);
        CHECK(ns.localServer()->host().world().clientEventKinds() ==
              (1u << static_cast<u32>(script::api::GameEventKind::Spawn)));
        const f64 before = uiNumber(ns, "eco.spawns"); // 생태계가 스스로 만든 것 (풀 · 새끼) 이 있을 수 있다

        ns.submit(cmd::CreateEntity{{7.f, -3.f}, {}, "eco.rabbit"});
        for (int i = 0; i < 60 && uiNumber(ns, "eco.rabbit") != 1.0; ++i) {
            frame();
        }
        CHECK(uiNumber(ns, "eco.rabbit") == 1.0);
        CHECK(uiNumber(ns, "eco.spawns") > before);
        CHECK(uiNumber(ns, "eco.found") >= 1); // 토끼 (x 7) 는 그 틱에 닿은 복제 월드에 있었다
        CHECK(ns.clientScripts()->counters(0).traps == 0);
    }
}
