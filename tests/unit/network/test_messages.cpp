#include <doctest/doctest.h>

#include <limits>

#include "core/command/CommandJson.hpp"
#include "network/protocol/BitStream.hpp"
#include "network/protocol/CommandCodec.hpp"
#include "network/protocol/Messages.hpp"
#include "network/server/CommandValidator.hpp"

using namespace sbx;
using namespace sbx::net;

namespace {

template <class T>
T roundTrip(const T& in) {
    const auto bytes = encodeMessage(Message{in});
    const auto back = decodeMessage(bytes);
    REQUIRE_MESSAGE(back.has_value(), back.error().describe());
    REQUIRE(std::holds_alternative<T>(*back));
    return std::get<T>(*back);
}

ecs::Json toJson(const cmd::CommandPayload& p) {
    return cmd::payloadToJson(p, [](NetEntityId id) { return u64{id}; });
}

std::vector<cmd::CommandPayload> everyPayload() {
    cmd::CreateEntity create;
    create.position = {1.5f, -2.25f};
    create.prefab = "eco.rabbit";
    create.components.push_back({0x1234, ecs::Json{{"speed", 2.5}, {"name", "토끼"}}});
    cmd::MoveEntity move;
    move.targets = {1, 2, 300000};
    move.value = {-3.f, 4.f};
    move.absolute = true;
    cmd::PaintTerrain paint;
    paint.materialId = "core.water";
    paint.cells = {{-5, 7}, {100000, -100000}};
    paint.center = {-1, 2};
    paint.shape = cmd::BrushShape::Square;
    paint.radius = 31;
    return {create,
            cmd::DeleteEntity{{5, 6}},
            move,
            cmd::AddComponent{9, {0xABCDEF, ecs::Json::object()}},
            cmd::RemoveComponent{9, 77},
            cmd::ChangeComponent{10, 0x55, ecs::Json{{"energy", 3}}},
            paint,
            cmd::PauseSimulation{},
            cmd::ResumeSimulation{},
            cmd::StepSimulation{12},
            cmd::SetSimulationSpeed{2.0f},
            cmd::EditContent{content::ContentKind::Rule, "eco.rule", ecs::Json{{"id", "eco.rule"}, {"range", 1.5}}},
            cmd::EditContent{content::ContentKind::Behavior, "eco.graph", nullptr},
            cmd::EditContent{content::ContentKind::Trigger, "eco.lost", ecs::Json{{"id", "eco.lost"}}}, // v0.2 M4-T3
            cmd::GameControl{cmd::GameControlOp::Start},
            cmd::GameControl{cmd::GameControlOp::Abort},
            cmd::PlayerAction{"eco.feed", {1.25f, -3.5f}}};
}

} // namespace

TEST_SUITE("network") {

    TEST_CASE("messages: every Phase 9 message round-trips") {
        Hello h;
        h.buildId = 0xDEADBEEF12345678ull;
        h.caps = 3;
        CHECK(roundTrip(h).buildId == h.buildId);
        CHECK(roundTrip(Challenge{0xFFFF'FFFF'FFFF'FFFFull}).nonce == 0xFFFF'FFFF'FFFF'FFFFull);

        Auth a;
        a.displayName = "관찰자";
        a.contentHash = 42;
        a.nonce = 7;
        a.token = {std::byte{1}, std::byte{2}};
        const Auth a2 = roundTrip(a);
        CHECK(a2.displayName == "관찰자");
        CHECK(a2.token.size() == 2);
        CHECK(a2.nonce == 7);

        Welcome w;
        w.clientId = 65535;
        w.role = Role::Admin;
        w.world.name = "ecosystem_small";
        w.world.minChunkX = -8;
        w.world.maxChunkY = 7;
        w.world.paused = true;
        w.world.speed = 0.5f;
        w.serverTick = 123456789;
        w.sessionToken.assign(kSessionTokenBytes, std::byte{9});
        const Welcome w2 = roundTrip(w);
        CHECK(w2.clientId == 65535);
        CHECK(w2.role == Role::Admin);
        CHECK(w2.world.minChunkX == -8);
        CHECK(w2.world.maxChunkY == 7);
        CHECK(w2.world.paused);
        CHECK(w2.world.speed == 0.5f);
        CHECK(w2.serverTick == 123456789);
        CHECK(w2.sessionToken.size() == kSessionTokenBytes);

        Reject rj;
        rj.reason = RejectReason::ContentMismatch;
        rj.detail = "콘텐츠가 다릅니다";
        rj.contentHash = 99;
        rj.packs = {"eco", "core"};
        const Reject rj2 = roundTrip(rj);
        CHECK(rj2.reason == RejectReason::ContentMismatch);
        CHECK(rj2.packs == rj.packs);

        CommandResultMsg cr;
        cr.sequence = 5;
        cr.accepted = false;
        cr.reason = ErrorCode::RateLimited;
        cr.detail = "too fast";
        cr.created = {1, 2, 3};
        const CommandResultMsg cr2 = roundTrip(cr);
        CHECK(cr2.reason == ErrorCode::RateLimited);
        CHECK(cr2.created.size() == 3);

        ServerStats st;
        st.serverTick = 30;
        st.entities = 1000;
        st.tickMsAvg = 1.25f;
        st.clients = 2;
        CHECK(roundTrip(st).entities == 1000);
        CHECK(roundTrip(DisconnectMsg{DisconnectReason::ServerShutdown}).reason == DisconnectReason::ServerShutdown);
    }

    TEST_CASE("messages: every command payload kind round-trips (compared through the replay JSON form)") {
        u32 seq = 1;
        for (const auto& p : everyPayload()) {
            CAPTURE(cmd::commandName(p));
            const CommandMsg back = roundTrip(CommandMsg{seq, p});
            CHECK(back.sequence == seq);
            CHECK(toJson(back.payload) == toJson(p));
            ++seq;
        }
    }

    TEST_CASE("messages: 플레이어 행동 (프로토콜 10 — PlayerAction 명령, 역할 player)") {
        CHECK(kProtocolVersion == 11); // 와이어를 바꾸면 이 줄과 08 4 · 5장 표를 함께 (v0.2 M4-T3)
        CHECK(classifyCommand(cmd::PlayerAction{"x.y", {}}) == CommandClass::PlayerAction);
        CHECK(roleAllows(Role::Player, CommandClass::PlayerAction));
        CHECK_FALSE(roleAllows(Role::Observer, CommandClass::PlayerAction));
        BitWriter bad; // 위치가 유한하지 않다
        bad.writeU8(static_cast<u8>(PayloadTag::PlayerAction));
        bad.writeString("x.y");
        bad.writeF32(std::numeric_limits<f32>::infinity());
        bad.writeF32(0);
        BitReader r(bad.bytes());
        cmd::CommandPayload out;
        CHECK_FALSE(readPayload(r, out));
    }

    TEST_CASE("messages: 게임 진행 (프로토콜 9 — GameControl · WorldOp Restart · WorldChanged Restart)") {
        CHECK(kProtocolVersion >= 9); // v0.2 M2-T3 에서 9 (지금 값은 "플레이어 행동" 케이스가 고정한다)
        const WorldOpRequest op = roundTrip(WorldOpRequest{WorldOpKind::Restart, {}});
        CHECK(op.op == WorldOpKind::Restart);
        CHECK(worldOpName(WorldOpKind::Restart) == "게임 재시작");
        WorldChanged wc;
        wc.generation = 3;
        wc.reason = WorldChangeReason::Restart;
        CHECK(roundTrip(wc).reason == WorldChangeReason::Restart);
        CHECK_FALSE(decodeMessage(encodeMessage(Message{WorldOpRequest{static_cast<WorldOpKind>(8), {}}})).has_value());
    }

    TEST_CASE("messages: 게임 정체성 (프로토콜 8 — Welcome · Reject)") {
        CHECK(kProtocolVersion >= 8); // v0.2 M1-T3 에서 8 (지금 값은 아래 "게임 진행" 케이스가 고정한다)
        Welcome w;
        w.world.name = "Arena";
        const Welcome plain = roundTrip(w);
        CHECK(plain.game.empty());
        w.game = GameIdentity{"studio.arena", "아레나 Arena", "1.2.0-beta.1+b7"};
        const Welcome w2 = roundTrip(w);
        CHECK(w2.game == w.game);
        CHECK_FALSE(w2.game.empty());

        Reject rj;
        rj.reason = RejectReason::ContentMismatch;
        rj.packs = {"arena"};
        rj.game = w.game;
        CHECK(roundTrip(rj).game == w.game);

        project::GameManifest m;
        m.gameId = "studio.arena";
        m.name = "Arena";
        m.version = SemVer{0, 3, 1, "rc.1", {}};
        CHECK(gameIdentityOf(m) == GameIdentity{"studio.arena", "Arena", "0.3.1-rc.1"});

        // gameId 없이 이름 · 버전만 있는 것은 형식 오류
        Welcome bad;
        bad.game.name = "이름만";
        CHECK_FALSE(decodeMessage(encodeMessage(Message{bad})).has_value());
        // 상한을 넘는 문자열은 거절 (보내는 쪽 ServerHost 가 자른다)
        Welcome longId;
        longId.game = GameIdentity{std::string(kMaxGameIdBytes + 1, 'a'), "n", "1.0.0"};
        CHECK_FALSE(decodeMessage(encodeMessage(Message{longId})).has_value());
        Welcome longName;
        longName.game = GameIdentity{"a.b", std::string(kMaxGameNameBytes + 1, 'n'), "1.0.0"};
        CHECK_FALSE(decodeMessage(encodeMessage(Message{longName})).has_value());
        Welcome maxName;
        maxName.game = GameIdentity{"a.b", std::string(kMaxGameNameBytes, 'n'), std::string(kMaxGameVersionBytes, '1')};
        CHECK(decodeMessage(encodeMessage(Message{maxName})).has_value());
    }

    TEST_CASE("messages: reserved ids, trailing bytes, bad enums and oversize messages are rejected") {
        BitWriter reserved;
        reserved.writeVarU(static_cast<u8>(MessageId::Snapshot));
        CHECK_FALSE(decodeMessage(reserved.bytes()).has_value());

        auto bytes = encodeMessage(Message{Challenge{1}});
        bytes.push_back(std::byte{0});
        CHECK_FALSE(decodeMessage(bytes).has_value());

        BitWriter badRole;
        badRole.writeVarU(static_cast<u8>(MessageId::Disconnect));
        badRole.writeU8(200);
        CHECK_FALSE(decodeMessage(badRole.bytes()).has_value());

        const std::vector<std::byte> huge(kMaxControlMessageBytes + 1, std::byte{0});
        CHECK_FALSE(decodeMessage(huge).has_value());

        const std::vector<std::byte> empty;
        CHECK_FALSE(decodeMessage(empty).has_value());

        // 잘린 메시지
        auto welcome = encodeMessage(Message{Welcome{}});
        welcome.resize(welcome.size() / 2);
        CHECK_FALSE(decodeMessage(welcome).has_value());
    }

    TEST_CASE("messages: Inspect · InspectResult round-trip, channel and limits (10B)") {
        InspectRequest q;
        q.ids = {3, 70000, 1};
        CHECK(roundTrip(q).ids == q.ids);
        CHECK(channelOf(Message{q}) == Channel::Control);
        CHECK(roundTrip(InspectRequest{}).ids.empty());

        InspectResult r;
        r.serverTick = 12345;
        InspectEntry e;
        e.netId = 7;
        e.state = "flee";
        e.sensorRadius = 6.5f;
        e.path = {{1, 2}, {-3.5f, 4}};
        e.goal = Vec2{9, -9};
        e.target = 42;
        e.components = {{0x1234'5678'9ABC'DEF0ull, R"({"maxSpeed":2.5})"}, {7, "{}"}}; // 프로토콜 5 (12B)
        InspectEntry e8;
        e8.netId = 8;
        r.entries = {e, e8};
        const InspectResult r2 = roundTrip(r);
        CHECK(channelOf(Message{r}) == Channel::Snapshot);
        CHECK(r2.serverTick == 12345);
        REQUIRE(r2.entries.size() == 2);
        CHECK(r2.entries[0].state == "flee");
        CHECK(r2.entries[0].sensorRadius == 6.5f);
        CHECK(r2.entries[0].path == e.path);
        CHECK(r2.entries[0].goal == Vec2{9, -9});
        CHECK(r2.entries[0].target == 42);
        CHECK(r2.entries[0].components == e.components);
        CHECK(r2.entries[1].components.empty());
        CHECK(r2.entries[1].netId == 8);
        CHECK_FALSE(r2.entries[1].goal.has_value());
        CHECK(r2.entries[1].state.empty());

        // 상한: 33 개 요청은 형식 오류
        BitWriter many;
        many.writeVarU(static_cast<u8>(MessageId::Inspect));
        many.writeVarU(kMaxInspect + 1);
        for (usize i = 0; i <= kMaxInspect; ++i) {
            many.writeVarU(i + 1);
        }
        CHECK_FALSE(decodeMessage(many.bytes()).has_value());
        CHECK(kProtocolVersion >= 3);
    }

    TEST_CASE("command codec: format limits — counts, NaN, unknown tag, bad JSON, shape, radius") {
        auto decode = [](const BitWriter& w) {
            BitReader r(w.bytes());
            cmd::CommandPayload p;
            return readPayload(r, p);
        };
        {
            BitWriter w;
            w.writeU8(static_cast<u8>(PayloadTag::DeleteEntity));
            w.writeVarU(5000); // > kMaxCommandTargets
            CHECK_FALSE(decode(w));
        }
        {
            BitWriter w;
            w.writeU8(static_cast<u8>(PayloadTag::SetSimulationSpeed));
            w.writeF32(std::numeric_limits<f32>::infinity());
            CHECK_FALSE(decode(w));
        }
        {
            BitWriter w;
            w.writeU8(99);
            CHECK_FALSE(decode(w));
        }
        {
            BitWriter w;
            w.writeU8(static_cast<u8>(PayloadTag::ChangeComponent));
            w.writeVarU(1);
            w.writeVarU(2);
            w.writeString("{not json");
            CHECK_FALSE(decode(w));
        }
        {
            cmd::PaintTerrain paint;
            paint.materialId = "core.water";
            BitWriter w;
            writePayload(w, paint);
            CHECK(decode(w));
            // 모양 2 는 없다
            BitWriter bad;
            bad.writeU8(static_cast<u8>(PayloadTag::PaintTerrain));
            bad.writeString("core.water");
            bad.writeVarU(0);
            bad.writeVarI(0);
            bad.writeVarI(0);
            bad.writeU8(2);
            bad.writeVarU(0);
            CHECK_FALSE(decode(bad));
        }
        {
            // EditContent (12C): 종류는 0~3 (3 = trigger, v0.2 M4-T3), 값은 객체 또는 null
            BitWriter badKind;
            badKind.writeU8(static_cast<u8>(PayloadTag::EditContent));
            badKind.writeU8(4);
            badKind.writeString("eco.x");
            badKind.writeString("null");
            CHECK_FALSE(decode(badKind));
            BitWriter badValue;
            badValue.writeU8(static_cast<u8>(PayloadTag::EditContent));
            badValue.writeU8(1);
            badValue.writeString("eco.x");
            badValue.writeString("[1, 2]");
            CHECK_FALSE(decode(badValue));
        }
        {
            // GameControl (프로토콜 9): op 는 1 · 2
            for (const u8 op : {u8{0}, u8{3}}) {
                BitWriter bad;
                bad.writeU8(static_cast<u8>(PayloadTag::GameControl));
                bad.writeU8(op);
                CHECK_FALSE(decode(bad));
            }
        }
        {
            BitWriter w;
            w.writeU8(static_cast<u8>(PayloadTag::MoveEntity));
            w.writeVarU(1);
            w.writeVarU(1);
            w.writeF32(std::numeric_limits<f32>::quiet_NaN());
            w.writeF32(0);
            w.writeBool(false);
            CHECK_FALSE(decode(w));
        }
    }

    TEST_CASE("messages: roles and display names") {
        CHECK(parseRole("admin").value() == Role::Admin);
        CHECK_FALSE(parseRole("root").has_value());
        CHECK(isValidDisplayName("오성식"));
        CHECK_FALSE(isValidDisplayName(""));
        CHECK_FALSE(isValidDisplayName("tab\tname"));
        CHECK_FALSE(isValidDisplayName(std::string(33, 'a')));
        CHECK(isValidDisplayName(std::string(32, 'a')));
    }

} // TEST_SUITE
