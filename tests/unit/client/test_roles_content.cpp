// Phase 12C 클라이언트 (docs/10-EDITOR.md 7 · 8장, ADR-0030): 원격 서버의 주인 열쇠 · 접속자 표 · 역할 바꾸기가 에디터
// 권한까지, 로컬 서버에서 콘텐츠 편집 → 오버레이가 돌아와 content() 에 · 되돌리기, "접속자" · "콘텐츠" 패널 그리기.
#include <doctest/doctest.h>

#include <memory>
#include <string>

#include <imgui.h>

#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "apps/client/ui/PlayersPanel.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "editor/Editor.hpp"
#include "editor/ui/ContentPanel.hpp"
#include "network/server/CommandValidator.hpp"
#include "network/transport/LoopbackTransport.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using content::ContentKind;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

std::unique_ptr<NetworkSession> remote(net::LoopbackNetwork& hub, render::MaterialLibrary& mats, std::string name,
                                       std::string ownerKey) {
    NetworkSessionDesc d;
    d.mode = NetworkSessionMode::Remote;
    d.connect = "srv:7777";
    d.displayName = std::move(name);
    d.ownerKey = std::move(ownerKey);
    d.contentRoot = SBX_CONTENT_DIR;
    d.transportFactory = [&hub]() -> Expected<std::unique_ptr<net::INetworkTransport>> {
        return std::unique_ptr<net::INetworkTransport>(std::make_unique<net::LoopbackTransport>(hub));
    };
    auto made = NetworkSession::create(d, mats);
    REQUIRE(made.has_value());
    return std::move(*made);
}

std::unique_ptr<NetworkSession> local(render::MaterialLibrary& mats) {
    NetworkSessionDesc d;
    d.world = "ecosystem_small";
    d.contentRoot = SBX_CONTENT_DIR;
    d.inlineServer = true;
    auto ns = NetworkSession::create(d, mats);
    REQUIRE(ns.has_value());
    return std::move(*ns);
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE("roles (remote): the owner key, everyone's player list, a role change reaches the editor") {
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        auto src = scenario::openWorldSource("ecosystem_small", SBX_CONTENT_DIR, 1, catalog);
        REQUIRE(src.has_value());
        net::LoopbackNetwork hub;
        net::LoopbackTransport serverT(hub);
        net::ServerHostDesc hd;
        hd.worldName = src->name;
        hd.packs = src->packs;
        hd.defaultRole = net::Role::Player; // 들어오면 보기만
        hd.ownerKey = "열쇠";
        hd.randomSeed = 3;
        hd.snapshotBytesPerSecond = 0;
        net::ServerHost host(serverT, std::move(src->runner), hd);
        REQUIRE(host.start({"srv", 7777}).has_value());

        render::MaterialLibrary mats;
        auto owner = remote(hub, mats, "주인", "열쇠");
        auto guest = remote(hub, mats, "손님", "");
        const auto frame = [&] {
            owner->update(kDt);
            guest->update(kDt);
            host.update(kDt);
            render::RenderWorld w;
            owner->extract(w);
            guest->extract(w);
        };
        for (int i = 0; i < 300 && !(owner->ready() && guest->ready()); ++i) {
            frame();
        }
        REQUIRE(owner->ready());
        REQUIRE(guest->ready());
        for (int i = 0; i < 5; ++i) {
            frame();
        }

        CHECK(owner->permissions() == editor::EditPermissions{true, true, true, true});
        CHECK_FALSE(guest->permissions().anyEdit());
        CHECK_FALSE(guest->info().canControl);
        CHECK(guest->info().net->role == "player");
        const auto list = owner->players();
        REQUIRE(list.has_value());
        REQUIRE(list->players.size() == 2);
        CHECK(list->myRole == static_cast<u8>(net::Role::Owner));
        CHECK(list->players[0].self);
        CHECK(list->players[1].name == "손님");

        // player 의 에디터: 이동 툴을 고를 수 없다
        editor::Editor guestEditor;
        editor::EditorInput in;
        in.tool = editor::Tool::Move;
        guestEditor.update(in, *guest);
        CHECK(guestEditor.tool() == editor::Tool::Select);

        // 손님은 역할을 못 바꾼다 — 거절 사유가 패널 줄에
        guest->requestRole(list->players[0].clientId, static_cast<u8>(net::Role::Observer));
        for (int i = 0; i < 5; ++i) {
            frame();
        }
        CHECK(guest->players()->message.find("거절") != std::string::npos);

        // 주인이 손님을 editor 로 → 손님의 권한 · 에디터가 바로 따라온다
        owner->requestRole(list->players[1].clientId, static_cast<u8>(net::Role::Editor));
        for (int i = 0; i < 5; ++i) {
            frame();
        }
        CHECK(guest->permissions().entities);
        CHECK_FALSE(guest->permissions().simulation);
        CHECK(guest->info().net->role == "editor");
        CHECK(owner->players()->players[1].role == static_cast<u8>(net::Role::Editor));
        CHECK(owner->players()->message.find("editor") != std::string::npos);
        guestEditor.update(in, *guest);
        CHECK(guestEditor.tool() == editor::Tool::Move);

        // 패널과 서버가 같은 표를 쓴다
        for (u8 me = 0; me <= 4; ++me) {
            for (u8 target = 0; target <= 4; ++target) {
                for (u8 role = 0; role <= 4; ++role) {
                    CHECK(canAssignRole(me, target, role) == net::roleCanAssign(static_cast<net::Role>(me),
                                                                                static_cast<net::Role>(target),
                                                                                static_cast<net::Role>(role)));
                }
            }
        }
        CHECK_FALSE(canAssignRole(9, 0, 0));
        host.stop();
    }

    TEST_CASE("content (local server): an edit comes back as the overlay in content(), undo takes it out") {
        render::MaterialLibrary mats;
        auto ns = local(mats);
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns->update(kDt);
            render::RenderWorld w;
            ns->extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, *ns);
        };
        for (int i = 0; i < 120 && !ns->ready(); ++i) {
            frame();
        }
        REQUIRE(ns->ready());
        const content::ContentDatabase* base = ns->content();
        REQUIRE(base != nullptr);
        CHECK(base->overlay().empty());
        CHECK(ns->permissions().content); // 싱글플레이는 owner

        ecs::Json rule = base->findRule("eco.rabbit_grazes")->raw;
        rule["effects"][0]["value"] = 44;
        REQUIRE(ed.editContent(*ns, ContentKind::Rule, "eco.rabbit_grazes", rule));
        for (int i = 0; i < 30 && ns->content()->overlay().empty(); ++i) {
            frame();
        }
        REQUIRE_FALSE(ns->content()->overlay().empty());
        CHECK(ns->content()->findRule("eco.rabbit_grazes")->effects[0].value == doctest::Approx(44));
        CHECK(ns->content()->contentHash() == base->contentHash());
        CHECK(ns->overlayError().empty());
        // 서버 월드도 같은 오버레이 (Inline 이라 볼 수 있다)
        CHECK(ns->localServer()->host().world().contentOverlay() == ns->content()->overlay());
        for (int i = 0; i < 5 && !ed.canUndo(); ++i) {
            frame();
        }
        REQUIRE(ed.canUndo());

        ed.undo(*ns);
        for (int i = 0; i < 30 && !ns->content()->overlay().empty(); ++i) {
            frame();
        }
        CHECK(ns->content()->overlay().empty());
        CHECK(ns->content()->findRule("eco.rabbit_grazes")->effects[0].value == doctest::Approx(15));
        CHECK(ns->localServer()->host().world().contentOverlay().empty());
    }

    TEST_CASE("panels (12C): players and content panels draw, the content panel edits and applies") {
        render::MaterialLibrary mats;
        auto ns = local(mats);
        for (int i = 0; i < 120 && !ns->ready(); ++i) {
            ns->update(kDt);
        }
        REQUIRE(ns->ready());
        platform::HeadlessWindow window;
        ImGuiLayer layer(window);
        editor::Editor ed;
        editor::ContentPanelState st;
        const auto uiFrame = [&](auto&& ui) {
            platform::PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            ui();
            (void)layer.endFrame(platform::CursorShape::Arrow);
            layer.acknowledgeTextures();
        };
        std::optional<RoleRequest> req;
        bool open = false;
        uiFrame([&] {
            editor::drawContentPanel(ed, st, *ns, open); // 닫혀 있으면 그리지 않는다
            req = drawPlayersPanel(*ns->players());
        });
        CHECK_FALSE(req.has_value()); // 혼자 — 바꿀 사람이 없다
        open = true;
        uiFrame([&] { editor::drawContentPanel(ed, st, *ns, open); });
        // 행동 · 규칙을 골라 그린다 (패널 상태를 손으로 — 클릭 대신)
        st.kind = ContentKind::Behavior;
        st.selected = "eco.herbivore";
        st.text = st.loaded = ns->content()->findBehavior("eco.herbivore")->raw.dump(2);
        uiFrame([&] { editor::drawContentPanel(ed, st, *ns, open); });
        st.kind = ContentKind::Rule;
        st.selected = "eco.rabbit_grazes";
        st.text = st.loaded = ns->content()->findRule("eco.rabbit_grazes")->raw.dump(2);
        uiFrame([&] { editor::drawContentPanel(ed, st, *ns, open); });
        CHECK(open);
        CHECK(st.selected == "eco.rabbit_grazes");
        CHECK(st.text == st.loaded);
    }
}
