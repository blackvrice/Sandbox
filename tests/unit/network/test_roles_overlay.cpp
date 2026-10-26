// Phase 12C (ADR-0030): 접속자 표 · 역할 바꾸기 (10-EDITOR 7장 권한 표) · 주인 열쇠 · 월드 오버레이 전달.
#include <doctest/doctest.h>

#include <memory>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/scenarios/Scenario.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "network/client/ClientSession.hpp"
#include "network/protocol/BitStream.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/LoopbackTransport.hpp"

using namespace sbx;
using namespace sbx::net;
using content::ContentKind;

#ifndef SBX_CONTENT_DIR
#error "SBX_CONTENT_DIR 가 정의되어야 한다 (tests/CMakeLists.txt)"
#endif

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const content::ContentDatabase& eco() {
    static const content::ContentDatabase db = [] {
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(SBX_CONTENT_DIR, ids, catalog());
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    return db;
}

constexpr f64 kFrame = 1.0 / 30.0;

class EmptyScenario final : public scenario::IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "empty"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "test"; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 100; }
    void setup(sim::SimulationWorld&) override {}
    void beforeTick(sim::SimulationWorld&) override {}
};

struct Rig {
    LoopbackNetwork hub;
    LoopbackTransport serverTransport{hub};
    std::unique_ptr<ServerHost> host;
    f64 now = 0;

    explicit Rig(ServerHostDesc desc = {}) {
        desc.randomSeed = 9;
        desc.packs = {"eco"};
        host = std::make_unique<ServerHost>(
            serverTransport,
            std::make_unique<scenario::ScenarioRunner>(catalog(), eco(), std::make_unique<EmptyScenario>(), 1), desc);
        REQUIRE(host->start({"srv", 7777}).has_value());
    }
    void step() {
        now += kFrame;
        host->update(kFrame);
    }
};

struct Peer {
    LoopbackTransport transport;
    ClientSession session;
    Peer(Rig& rig, std::string name, std::string ownerKey = {}, std::vector<std::byte> token = {})
        : transport(rig.hub), session(transport, desc(std::move(name), std::move(ownerKey), std::move(token))) {
        REQUIRE(session.connect({"srv", 7777}, rig.now).has_value());
    }
    static ClientSessionDesc desc(std::string name, std::string ownerKey, std::vector<std::byte> token) {
        ClientSessionDesc d;
        d.displayName = std::move(name);
        d.contentHash = eco().contentHash();
        d.ownerKey = std::move(ownerKey);
        d.token = std::move(token);
        return d;
    }
    [[nodiscard]] u16 id() const { return session.welcome()->clientId; }
};

void pump(Rig& rig, std::initializer_list<Peer*> peers, int frames = 4) {
    for (int i = 0; i < frames; ++i) {
        for (auto* p : peers) {
            p->session.update(rig.now);
        }
        rig.step();
        for (auto* p : peers) {
            p->session.update(rig.now);
        }
    }
}

// 요청 하나 → 결과 하나
SetRoleResultMsg setRole(Rig& rig, std::initializer_list<Peer*> all, Peer& who, u16 target, Role role) {
    REQUIRE(who.session.requestRole(target, role));
    pump(rig, all);
    auto results = who.session.takeRoleResults();
    REQUIRE(results.size() == 1);
    return results.front();
}

Role roleIn(const ClientSession& s, u16 clientId) {
    for (const PlayerEntry& p : s.players()) {
        if (p.clientId == clientId) {
            return p.role;
        }
    }
    FAIL("접속자 표에 없다");
    return Role::Observer;
}

nlohmann::json grazesWith(f64 energy) {
    nlohmann::json j = eco().findRule("eco.rabbit_grazes")->raw;
    j["effects"][0]["value"] = energy;
    return j;
}

CommandResultMsg command(Rig& rig, std::initializer_list<Peer*> all, Peer& who, cmd::CommandPayload payload) {
    const u32 seq = who.session.sendCommand(std::move(payload));
    REQUIRE(seq != 0);
    for (int i = 0; i < 20; ++i) {
        pump(rig, all, 1);
        for (auto& r : who.session.takeResults()) {
            if (r.sequence == seq) {
                return r;
            }
        }
    }
    FAIL("결과가 오지 않았다");
    return {};
}

} // namespace

TEST_SUITE("network") {

    TEST_CASE("messages (12C): PlayerList · SetRole · SetRoleResult · ContentOverlay · Auth.ownerKey round-trip") {
        const auto roundTrip = [](const Message& m) {
            auto back = decodeMessage(encodeMessage(m));
            REQUIRE(back.has_value());
            return *back;
        };
        PlayerList pl;
        pl.players = {{1, Role::Owner, "주인"}, {7, Role::Player, "손님"}};
        CHECK(std::get<PlayerList>(roundTrip(pl)).players == pl.players);
        const auto sr = std::get<SetRoleRequest>(roundTrip(SetRoleRequest{7, Role::Editor}));
        CHECK(sr.clientId == 7);
        CHECK(sr.role == Role::Editor);
        const auto res = std::get<SetRoleResultMsg>(roundTrip(SetRoleResultMsg{7, Role::Admin, false, "안 됨"}));
        CHECK_FALSE(res.accepted);
        CHECK(res.detail == "안 됨");
        ContentOverlayMsg ov;
        ov.reset = true;
        ov.items = {{ContentKind::Rule, "eco.r", R"({"id":"eco.r"})"}, {ContentKind::Prefab, "eco.p", ""}};
        const auto ovBack = std::get<ContentOverlayMsg>(roundTrip(ov));
        CHECK(ovBack.reset);
        CHECK(ovBack.items == ov.items);
        CHECK(channelOf(ov) == Channel::Bulk);
        CHECK(channelOf(pl) == Channel::Control);
        Auth a;
        a.displayName = "x";
        a.ownerKey = "비밀";
        CHECK(std::get<Auth>(roundTrip(a)).ownerKey == "비밀");

        // 64 KB 를 넘는 오버레이도 (Bulk — 16 MB 까지)
        ContentOverlayMsg big;
        for (int i = 0; i < 8; ++i) {
            big.items.push_back({ContentKind::Rule, "eco.r" + std::to_string(i), std::string(12000, 'x')});
        }
        CHECK(decodeMessage(encodeMessage(big)).has_value());

        BitWriter badRole;
        badRole.writeVarU(static_cast<u8>(MessageId::SetRole));
        badRole.writeVarU(1);
        badRole.writeU8(9);
        CHECK_FALSE(decodeMessage(badRole.bytes()).has_value());
        BitWriter badKind;
        badKind.writeVarU(static_cast<u8>(MessageId::ContentOverlay));
        badKind.writeBool(false);
        badKind.writeVarU(1);
        badKind.writeU8(5); // 0~4 (4 = script, v0.3 M5-T2)
        badKind.writeString("eco.x");
        badKind.writeString("");
        CHECK_FALSE(decodeMessage(badKind.bytes()).has_value());
    }

    TEST_CASE("roles: the owner key makes the owner, a wrong key is rejected") {
        ServerHostDesc desc;
        desc.ownerKey = "열쇠";
        Rig rig(desc);
        Peer owner(rig, "주인", "열쇠");
        Peer guest(rig, "손님");
        pump(rig, {&owner, &guest});
        REQUIRE(owner.session.state() == ClientState::Connected);
        CHECK(owner.session.role() == Role::Owner);
        CHECK(guest.session.role() == Role::Editor);
        // 둘 다 같은 표를 본다
        REQUIRE(guest.session.players().size() == 2);
        CHECK(guest.session.players() == owner.session.players());
        CHECK(roleIn(guest.session, owner.id()) == Role::Owner);
        CHECK(guest.session.players()[0].name == "주인");

        Peer wrong(rig, "도둑", "아님");
        pump(rig, {&owner, &guest, &wrong});
        CHECK(wrong.session.state() == ClientState::Rejected);
        REQUIRE(wrong.session.reject().has_value());
        CHECK(wrong.session.reject()->reason == RejectReason::BadHandshake);

        Rig noKey;
        Peer claims(noKey, "주장", "무엇이든");
        pump(noKey, {&claims});
        CHECK(claims.session.state() == ClientState::Rejected);
    }

    TEST_CASE("roles: the permission matrix for role changes (10-EDITOR 7)") {
        ServerHostDesc desc;
        desc.ownerKey = "k";
        Rig rig(desc);
        Peer owner(rig, "owner", "k");
        Peer a(rig, "a");
        Peer b(rig, "b");
        Peer c(rig, "c");
        const std::initializer_list<Peer*> all{&owner, &a, &b, &c};
        pump(rig, all);

        // Editor 는 역할을 못 바꾼다
        auto r = setRole(rig, all, b, c.id(), Role::Player);
        CHECK_FALSE(r.accepted);
        CHECK(roleIn(owner.session, c.id()) == Role::Editor);

        // Owner → a 를 Admin 으로
        r = setRole(rig, all, owner, a.id(), Role::Admin);
        CHECK(r.accepted);
        CHECK(a.session.role() == Role::Admin);
        CHECK(roleIn(c.session, a.id()) == Role::Admin);

        // Admin: Admin 아래인 사람을 Editor 이하로만
        CHECK(setRole(rig, all, a, b.id(), Role::Player).accepted);
        CHECK(b.session.role() == Role::Player);
        CHECK_FALSE(setRole(rig, all, a, c.id(), Role::Admin).accepted);
        CHECK_FALSE(setRole(rig, all, a, owner.id(), Role::Observer).accepted);
        CHECK(owner.session.role() == Role::Owner);

        // 자기 자신 · 없는 번호
        CHECK_FALSE(setRole(rig, all, owner, owner.id(), Role::Observer).accepted);
        CHECK_FALSE(setRole(rig, all, owner, 999, Role::Editor).accepted);

        // Owner 는 Admin 도 내릴 수 있다
        CHECK(setRole(rig, all, owner, a.id(), Role::Observer).accepted);
        CHECK(a.session.role() == Role::Observer);
        CHECK(rig.host->players().players.size() == 4);

        // 역할은 명령 권한에 바로 쓰인다: Player 가 된 b 는 편집 · 콘텐츠를 못 한다
        cmd::CreateEntity create;
        create.position = {1, 1};
        auto res = command(rig, all, b, create);
        CHECK_FALSE(res.accepted);
        CHECK(res.reason == ErrorCode::PermissionDenied);
        res = command(rig, all, b, cmd::EditContent{ContentKind::Rule, "eco.rabbit_grazes", grazesWith(20)});
        CHECK(res.reason == ErrorCode::PermissionDenied);
        CHECK(command(rig, all, c, create).accepted); // Editor 는 그대로
    }

    TEST_CASE("roles: a changed role survives a reconnect, and leaving updates everyone's list") {
        ServerHostDesc desc;
        desc.ownerKey = "k";
        Rig rig(desc);
        Peer owner(rig, "owner", "k");
        auto b = std::make_unique<Peer>(rig, "b");
        pump(rig, {&owner, b.get()});
        const u16 bid = b->id();
        REQUIRE(setRole(rig, {&owner, b.get()}, owner, bid, Role::Player).accepted);
        const auto token = b->session.welcome()->sessionToken;
        // 네트워크만 끊긴다 (ClientQuit 이 아니라 — 토큰이 남는다)
        b->transport.severAll();
        pump(rig, {&owner, b.get()}, 3);
        b.reset();
        pump(rig, {&owner}, 2);
        CHECK(owner.session.players().size() == 1);

        Peer again(rig, "b2", {}, token);
        pump(rig, {&owner, &again});
        REQUIRE(again.session.state() == ClientState::Connected);
        CHECK(again.id() == bid);
        CHECK(again.session.role() == Role::Player);
        CHECK(roleIn(owner.session, bid) == Role::Player);
        CHECK(owner.session.players().size() == 2);
    }

    TEST_CASE("overlay: edits reach every client, late joiners get the whole overlay, removals too") {
        Rig rig;
        Peer a(rig, "a");
        Peer b(rig, "b");
        pump(rig, {&a, &b});
        CHECK(a.session.overlay().empty());
        const u64 rev0 = b.session.overlayRevision();
        CHECK(rev0 >= 1); // 들어올 때 reset 하나

        auto res = command(rig, {&a, &b}, a, cmd::EditContent{ContentKind::Rule, "eco.rabbit_grazes", grazesWith(33)});
        REQUIRE(res.accepted);
        pump(rig, {&a, &b});
        CHECK(a.session.overlay() == rig.host->world().contentOverlay());
        CHECK(b.session.overlay() == rig.host->world().contentOverlay());
        CHECK(b.session.overlayRevision() > rev0);
        REQUIRE(b.session.overlay().find(ContentKind::Rule, "eco.rabbit_grazes") != nullptr);
        CHECK((*b.session.overlay().find(ContentKind::Rule, "eco.rabbit_grazes"))["effects"][0]["value"] == 33);

        // 검증 실패는 거절 — 아무에게도 아무것도 가지 않는다
        nlohmann::json bad = grazesWith(1);
        bad["action"] = "Bad Action";
        const u64 revBefore = b.session.overlayRevision();
        res = command(rig, {&a, &b}, a, cmd::EditContent{ContentKind::Rule, "eco.rabbit_grazes", bad});
        CHECK_FALSE(res.accepted);
        CHECK(res.reason == ErrorCode::ValidationFailed);
        pump(rig, {&a, &b});
        CHECK(b.session.overlayRevision() == revBefore);

        Peer late(rig, "late");
        pump(rig, {&a, &b, &late});
        CHECK(late.session.overlay() == rig.host->world().contentOverlay());

        res = command(rig, {&a, &b, &late}, b, cmd::EditContent{ContentKind::Rule, "eco.rabbit_grazes", nullptr});
        REQUIRE(res.accepted);
        pump(rig, {&a, &b, &late});
        CHECK(a.session.overlay().empty());
        CHECK(late.session.overlay().empty());
    }

    TEST_CASE(
        "overlay (v0.3 M5-T2): a script file edit travels as kind 4, the server runs it, clients compile the same") {
        Rig rig;
        Peer a(rig, "a");
        Peer b(rig, "b");
        pump(rig, {&a, &b});
        CHECK(rig.host->world().scriptSystems() == nullptr); // eco 팩에는 스크립트가 없다
        const std::string source =
            "import { defineSystem } from \"sandbox\"\n"
            "export default defineSystem({ id: \"eco.watch\", onTick(ctx) { ctx.log(\"watch\") } })\n";
        auto res =
            command(rig, {&a, &b}, a,
                    cmd::EditContent{ContentKind::Script, "eco.watch", {{"id", "eco.watch"}, {"source", source}}});
        REQUIRE_MESSAGE(res.accepted, res.detail);
        pump(rig, {&a, &b});
        REQUIRE(rig.host->world().scriptSystems() != nullptr); // 서버가 새 시스템을 돌린다
        CHECK(rig.host->world().scriptSystems()->systemId(0) == "eco.watch");
        CHECK(b.session.overlay() == rig.host->world().contentOverlay());
        // 클라이언트도 같은 오버레이로 콘텐츠를 다시 만든다 (같은 컴파일러 — 같은 바이트코드)
        const content::ContentLoadResult mine =
            content::ContentLoader::applyOverlay(eco(), b.session.overlay(), catalog());
        REQUIRE(mine.ok());
        REQUIRE(mine.db.scriptPrograms() != nullptr);
        CHECK(script::bc::serialize(mine.db.scriptPrograms()->packs[0].module) ==
              script::bc::serialize(rig.host->world().content().scriptPrograms()->packs[0].module));

        // 컴파일 오류는 거절 (S423) — 오버레이 그대로
        res = command(rig, {&a, &b}, a,
                      cmd::EditContent{
                          ContentKind::Script, "eco.watch", {{"id", "eco.watch"}, {"source", "export default 3\n"}}});
        CHECK_FALSE(res.accepted);
        CHECK(res.reason == ErrorCode::ValidationFailed);
        CHECK(res.detail.find("S423") != std::string::npos);
    }
}
