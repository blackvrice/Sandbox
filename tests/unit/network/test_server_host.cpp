#include <doctest/doctest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/scenarios/Scenario.hpp"
#include "network/client/ClientSession.hpp"
#include "network/protocol/BitStream.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/LoopbackTransport.hpp"
#include "network/transport/SimulatedTransport.hpp"

using namespace sbx;
using namespace sbx::net;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

u64 contentHash() {
    return content::ContentDatabase::builtin().contentHash();
}

constexpr f64 kFrame = 1.0 / 30.0;

// 엔티티 50 개를 만들고 그 뒤로는 아무것도 하지 않는 월드 (random_walk 는 스스로 일시정지 · 편집 명령을 넣는다)
class QuietScenario final : public scenario::IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "quiet"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "test"; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 100; }
    void setup(sim::SimulationWorld& world) override {
        for (u32 i = 0; i < 50; ++i) {
            cmd::CreateEntity c;
            c.position = {static_cast<f32>(i), 0.f};
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{1, cmd::kServerIssuer, i + 1}, c});
        }
    }
    void beforeTick(sim::SimulationWorld&) override {}
};

std::unique_ptr<scenario::ScenarioRunner> quietWorld() {
    return std::make_unique<scenario::ScenarioRunner>(catalog(), content::ContentDatabase::builtin(),
                                                      std::make_unique<QuietScenario>(), 1);
}

// 서버 하나 (Inline) + Loopback 허브. 시간은 손으로
struct Rig {
    LoopbackNetwork hub;
    LoopbackTransport serverTransport{hub};
    std::unique_ptr<ServerHost> host;
    f64 now = 0;

    explicit Rig(ServerHostDesc desc = {}) {
        desc.randomSeed = 5;
        desc.worldName = "test_world";
        desc.packs = {"eco"};
        host = std::make_unique<ServerHost>(serverTransport, quietWorld(), desc);
        REQUIRE(host->start({"srv", 7777}).has_value());
    }
    void step(f64 dt = kFrame) {
        now += dt;
        host->update(dt);
    }
};

struct Peer {
    LoopbackTransport transport;
    ClientSession session;
    Peer(Rig& rig, ClientSessionDesc desc) : transport(rig.hub), session(transport, std::move(desc)) {
        REQUIRE(session.connect({"srv", 7777}, rig.now).has_value());
    }
};

ClientSessionDesc goodDesc(std::string name = "tester") {
    ClientSessionDesc d;
    d.displayName = std::move(name);
    d.contentHash = contentHash();
    return d;
}

// 클라이언트 · 서버를 프레임 단위로 번갈아 돌린다
void pump(Rig& rig, std::initializer_list<ClientSession*> sessions, int frames) {
    for (int i = 0; i < frames; ++i) {
        for (auto* s : sessions) {
            s->update(rig.now);
        }
        rig.step();
        for (auto* s : sessions) {
            s->update(rig.now);
        }
    }
}

// 메시지를 손으로 보내는 클라이언트 (잘못된 순서 · 쓰레기 바이트)
struct RawPeer {
    LoopbackTransport transport;
    ConnectionId conn = kInvalidConnection;
    std::vector<Message> received;
    bool disconnected = false;
    DisconnectReason reason = DisconnectReason::None;

    explicit RawPeer(Rig& rig) : transport(rig.hub) { conn = transport.connect({"srv", 0}).value(); }
    void send(const Message& m) { transport.send(conn, Channel::Control, encodeMessage(m)); }
    void sendRaw(std::span<const std::byte> b) { transport.send(conn, Channel::Control, b); }
    void poll() {
        std::vector<TransportEvent> ev;
        transport.poll(ev);
        for (auto& e : ev) {
            if (e.type == TransportEvent::Type::Received) {
                auto m = decodeMessage(e.data);
                REQUIRE(m.has_value());
                received.push_back(std::move(*m));
            } else if (e.type == TransportEvent::Type::Disconnected) {
                disconnected = true;
                reason = e.reason;
            }
        }
    }
    [[nodiscard]] const Reject* lastReject() const {
        for (auto it = received.rbegin(); it != received.rend(); ++it) {
            if (const auto* r = std::get_if<Reject>(&*it)) {
                return r;
            }
        }
        return nullptr;
    }
};

std::vector<CommandResultMsg> waitResults(Rig& rig, ClientSession& s, usize count, int maxFrames = 30) {
    std::vector<CommandResultMsg> all;
    for (int i = 0; i < maxFrames && all.size() < count; ++i) {
        pump(rig, {&s}, 1);
        for (auto& r : s.takeResults()) {
            all.push_back(std::move(r));
        }
    }
    return all;
}

} // namespace

TEST_SUITE("network") {

    TEST_CASE("server host: handshake gives each client a new id, the default role and the world meta") {
        Rig rig;
        Peer a(rig, goodDesc("가"));
        Peer b(rig, goodDesc("나"));
        pump(rig, {&a.session, &b.session}, 3);
        REQUIRE(a.session.state() == ClientState::Connected);
        REQUIRE(b.session.state() == ClientState::Connected);
        const Welcome& w = *a.session.welcome();
        CHECK(w.clientId == 1);
        CHECK(b.session.welcome()->clientId == 2);
        CHECK(w.role == Role::Editor);
        CHECK(w.world.name == "test_world");
        CHECK(w.world.minChunkX <= w.world.maxChunkX);
        CHECK(w.tickRate == 30);
        CHECK(w.sessionToken.size() == kSessionTokenBytes);
        CHECK(rig.host->stats().clients == 2);
        CHECK(a.session.welcome()->sessionToken != b.session.welcome()->sessionToken);
    }

    TEST_CASE("server host: version mismatch, content mismatch, bad name, out of order and full server are rejected") {
        ServerHostDesc desc;
        desc.maxClients = 2;
        Rig rig(desc);
        {
            RawPeer raw(rig);
            Hello h;
            h.protocolVersion = kProtocolVersion + 1;
            raw.send(h);
            rig.step();
            raw.poll();
            REQUIRE(raw.lastReject() != nullptr);
            CHECK(raw.lastReject()->reason == RejectReason::VersionMismatch);
            CHECK(raw.lastReject()->serverProtocolVersion == kProtocolVersion);
            CHECK(raw.disconnected);
            CHECK(raw.reason == DisconnectReason::Rejected);
        }
        {
            ClientSessionDesc wrong = goodDesc();
            wrong.contentHash = contentHash() ^ 1;
            Peer p(rig, wrong);
            pump(rig, {&p.session}, 3);
            REQUIRE(p.session.state() == ClientState::Rejected);
            CHECK(p.session.reject()->reason == RejectReason::ContentMismatch);
            CHECK(p.session.reject()->contentHash == contentHash()); // 무엇을 읽어야 하는지 알려 준다
            CHECK(p.session.reject()->packs == std::vector<std::string>{"eco"});
        }
        {
            Peer p(rig, goodDesc("bad\nname"));
            pump(rig, {&p.session}, 3);
            REQUIRE(p.session.state() == ClientState::Rejected);
            CHECK(p.session.reject()->reason == RejectReason::InvalidName);
        }
        {
            RawPeer raw(rig);
            raw.send(Auth{{}, "x", contentHash(), 0}); // Hello 없이
            rig.step();
            raw.poll();
            REQUIRE(raw.lastReject() != nullptr);
            CHECK(raw.lastReject()->reason == RejectReason::BadHandshake);
        }
        {
            RawPeer raw(rig);
            raw.send(Hello{});
            rig.step();
            raw.send(Auth{{}, "x", contentHash(), 12345}); // nonce 틀림
            rig.step();
            raw.poll();
            REQUIRE(raw.lastReject() != nullptr);
            CHECK(raw.lastReject()->reason == RejectReason::BadHandshake);
        }
        Peer a(rig, goodDesc("a"));
        Peer b(rig, goodDesc("b"));
        pump(rig, {&a.session, &b.session}, 3);
        REQUIRE(a.session.state() == ClientState::Connected);
        Peer c(rig, goodDesc("c"));
        pump(rig, {&c.session}, 3);
        REQUIRE(c.session.state() == ClientState::Rejected);
        CHECK(c.session.reject()->reason == RejectReason::ServerFull);
        CHECK(rig.host->stats().handshakesRejected == 6);
    }

    TEST_CASE(
        "server host: a silent connection times out, garbage bytes and wrong-phase messages drop the connection") {
        Rig rig;
        RawPeer silent(rig);
        for (int i = 0; i < 30 * 9; ++i) {
            rig.step();
        }
        silent.poll();
        CHECK_FALSE(silent.disconnected);
        for (int i = 0; i < 30 * 2; ++i) {
            rig.step();
        }
        silent.poll();
        CHECK(silent.disconnected);
        REQUIRE(silent.lastReject() != nullptr);
        CHECK(silent.lastReject()->reason == RejectReason::BadHandshake);

        RawPeer junk(rig);
        const std::byte bytes[] = {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
        junk.sendRaw(bytes);
        rig.step();
        junk.poll();
        CHECK(junk.disconnected);
        CHECK(junk.reason == DisconnectReason::ProtocolError);

        // 핸드셰이크를 마친 뒤 클라이언트가 보낼 수 없는 메시지 (Welcome) → ProtocolError
        RawPeer rude(rig);
        rude.send(Hello{});
        rig.step();
        rude.poll();
        REQUIRE(rude.received.size() == 1);
        const u64 nonce = std::get<Challenge>(rude.received[0]).nonce;
        rude.send(Auth{{}, "rude", contentHash(), nonce});
        rig.step();
        rude.poll();
        REQUIRE(rude.received.size() == 2);
        CHECK(std::holds_alternative<Welcome>(rude.received[1]));
        CHECK(rig.host->stats().clients == 1);
        rude.send(Welcome{});
        rig.step();
        rude.poll();
        CHECK(rude.disconnected);
        CHECK(rude.reason == DisconnectReason::ProtocolError);
        CHECK(rig.host->stats().clients == 0);
    }

    TEST_CASE("server host: commands are stamped for the next tick, applied, and answered with the result") {
        ServerHostDesc desc;
        desc.defaultRole = Role::Admin;
        Rig rig(desc);
        Peer p(rig, goodDesc());
        pump(rig, {&p.session}, 3);
        REQUIRE(p.session.state() == ClientState::Connected);

        cmd::CreateEntity create;
        create.position = {3.f, 4.f};
        const u32 s1 = p.session.sendCommand(create);
        auto results = waitResults(rig, p.session, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].sequence == s1);
        CHECK(results[0].accepted);
        REQUIRE(results[0].created.size() == 1);
        const NetEntityId created = results[0].created[0];
        CHECK(rig.host->world().resolve(created) != ecs::kNullEntity);
        CHECK(results[0].appliedTick == rig.host->world().currentTick());

        // 없는 대상: 월드가 거절 → 같은 길로 결과
        const u32 s2 = p.session.sendCommand(cmd::DeleteEntity{{0xFFFFFF}});
        results = waitResults(rig, p.session, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].sequence == s2);
        CHECK_FALSE(results[0].accepted);
        CHECK(results[0].reason == ErrorCode::NotFound);
        CHECK_FALSE(results[0].detail.empty());

        const u32 s3 = p.session.sendCommand(cmd::PauseSimulation{});
        results = waitResults(rig, p.session, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].sequence == s3);
        CHECK(results[0].accepted);
        pump(rig, {&p.session}, 2);
        CHECK(rig.host->world().clock().paused());
        CHECK(rig.host->stats().paused);
        const sim::Tick pausedAt = rig.host->world().currentTick();
        // 일시정지 중에도 편집은 바로 적용된다 (편집 단계)
        p.session.sendCommand(cmd::MoveEntity{{created}, {1.f, 0.f}, false});
        results = waitResults(rig, p.session, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].accepted);
        CHECK(rig.host->world().currentTick() == pausedAt);
        CHECK(rig.host->stats().commandsAccepted == 3);
        CHECK(rig.host->stats().commandsRejected == 1);
    }

    TEST_CASE("server host: roles gate command kinds (10-EDITOR 7) and the rate limit rejects bursts") {
        ServerHostDesc desc;
        desc.defaultRole = Role::Editor;
        desc.validator.ratePerSecond = 10;
        desc.validator.burst = 5;
        Rig rig(desc);
        Peer p(rig, goodDesc());
        pump(rig, {&p.session}, 3);
        REQUIRE(p.session.state() == ClientState::Connected);

        p.session.sendCommand(cmd::PauseSimulation{}); // Editor 는 시뮬레이션 제어 불가
        auto results = waitResults(rig, p.session, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].reason == ErrorCode::PermissionDenied);
        CHECK(results[0].appliedTick == 0);

        for (int i = 0; i < 8; ++i) {
            p.session.sendCommand(cmd::CreateEntity{});
        }
        results = waitResults(rig, p.session, 8);
        REQUIRE(results.size() == 8);
        int limited = 0;
        int accepted = 0;
        for (const auto& r : results) {
            limited += r.reason == ErrorCode::RateLimited ? 1 : 0;
            accepted += r.accepted ? 1 : 0;
        }
        CHECK(limited == 4); // 버킷 5 중 1 은 위의 Pause 가 썼다
        CHECK(accepted == 4);
        CHECK_FALSE(rig.host->world().clock().paused());

        CHECK(roleAllows(Role::Observer, CommandClass::EntityEdit) == false);
        CHECK(roleAllows(Role::Player, CommandClass::PlayerAction));
        CHECK(roleAllows(Role::Owner, CommandClass::SimulationControl));
        CHECK(classifyCommand(cmd::PaintTerrain{}) == CommandClass::TerrainEdit);
    }

    TEST_CASE("server host: stats every second, Disconnect{ServerShutdown} on stop, stop tick holds the world") {
        ServerHostDesc desc;
        desc.stopAtTick = 40;
        desc.defaultRole = Role::Admin;
        Rig rig(desc);
        Peer p(rig, goodDesc());
        pump(rig, {&p.session}, 35);
        REQUIRE(p.session.lastStats().has_value());
        CHECK(p.session.lastStats()->entities == 50);
        CHECK(p.session.lastStats()->clients == 1);
        CHECK(p.session.lastStats()->ticksPerSecond > 20);

        pump(rig, {&p.session}, 20);
        CHECK(rig.host->world().currentTick() == 40);
        CHECK(rig.host->stats().reachedStopTick);
        p.session.sendCommand(cmd::PauseSimulation{});
        const auto results = waitResults(rig, p.session, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].reason == ErrorCode::Unsupported);

        rig.host->stop();
        p.session.update(rig.now);
        CHECK(p.session.state() == ClientState::Disconnected);
        CHECK(p.session.disconnectReason() == DisconnectReason::ServerShutdown);
        rig.host->stop(); // 두 번 불러도 된다
    }

    TEST_CASE("server host: handshake and commands survive a bad network (100 ms ± 20, 5 % loss, both ways)") {
        LoopbackNetwork hub;
        LoopbackTransport serverInner(hub);
        f64 now = 0;
        auto clock = [&] { return now * 1000.0; };
        SimulatedTransport serverT(serverInner, {.latencyMs = 100, .jitterMs = 20, .lossRate = 0.05, .seed = 3}, clock);
        ServerHostDesc desc;
        desc.defaultRole = Role::Admin;
        desc.randomSeed = 9;
        ServerHost host(serverT, quietWorld(), desc);
        REQUIRE(host.start({"srv", 0}).has_value());
        LoopbackTransport clientInner(hub);
        SimulatedTransport clientT(clientInner, {.latencyMs = 100, .jitterMs = 20, .lossRate = 0.05, .seed = 4}, clock);
        ClientSession session(clientT, goodDesc());
        REQUIRE(session.connect({"srv", 0}, now).has_value());
        std::vector<CommandResultMsg> results;
        int sent = 0;
        for (int frame = 0; frame < 300 && results.size() < 5; ++frame) {
            session.update(now);
            now += kFrame;
            host.update(kFrame);
            session.update(now);
            if (session.state() == ClientState::Connected && sent < 5) {
                session.sendCommand(cmd::CreateEntity{});
                ++sent;
            }
            for (auto& r : session.takeResults()) {
                results.push_back(std::move(r));
            }
        }
        REQUIRE(session.state() == ClientState::Connected);
        REQUIRE(results.size() == 5);
        for (u32 i = 0; i < 5; ++i) {
            CHECK(results[i].sequence == i + 1); // 신뢰 채널 — 순서대로
            CHECK(results[i].accepted);
        }
    }

    TEST_CASE("server host: a scenario's own commands (issuer 1 in random_walk) are never reported to client #1") {
        LoopbackNetwork hub;
        LoopbackTransport serverT(hub);
        ServerHostDesc desc;
        desc.defaultRole = Role::Admin;
        auto runner = std::make_unique<scenario::ScenarioRunner>(catalog(), content::ContentDatabase::builtin(),
                                                                 scenario::makeScenario("random_walk_1k"), 1);
        ServerHost host(serverT, std::move(runner), desc);
        REQUIRE(host.start({"srv", 0}).has_value());
        LoopbackTransport clientT(hub);
        ClientSession session(clientT, goodDesc());
        REQUIRE(session.connect({"srv", 0}, 0).has_value());
        std::vector<CommandResultMsg> results;
        f64 now = 0;
        for (int frame = 0; frame < 200; ++frame) {
            session.update(now);
            now += kFrame;
            host.update(kFrame);
            session.update(now);
            if (frame == 5) {
                REQUIRE(session.state() == ClientState::Connected);
                REQUIRE(session.welcome()->clientId == 1);
                session.sendCommand(cmd::CreateEntity{});
            }
            for (auto& r : session.takeResults()) {
                results.push_back(std::move(r));
            }
        }
        REQUIRE(results.size() == 1);
        CHECK(results[0].sequence == 1);
        CHECK(results[0].accepted);
    }

    TEST_CASE("server host: threaded mode serves a client in real time") {
        LoopbackNetwork hub;
        LoopbackTransport serverT(hub);
        ServerHostDesc desc;
        desc.mode = ServerMode::Threaded;
        desc.defaultRole = Role::Admin;
        ServerHost host(serverT, quietWorld(), desc);
        REQUIRE(host.start({"srv", 0}).has_value());
        LoopbackTransport clientT(hub);
        ClientSession session(clientT, goodDesc());
        const auto t0 = std::chrono::steady_clock::now();
        auto seconds = [&] { return std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count(); };
        REQUIRE(session.connect({"srv", 0}, seconds()).has_value());
        std::vector<CommandResultMsg> results;
        bool sent = false;
        while (seconds() < 10 && results.empty()) {
            clientT.wait(5);
            session.update(seconds());
            if (session.state() == ClientState::Connected && !sent) {
                session.sendCommand(cmd::PauseSimulation{});
                sent = true;
            }
            for (auto& r : session.takeResults()) {
                results.push_back(std::move(r));
            }
        }
        REQUIRE(results.size() == 1);
        CHECK(results[0].accepted);
        host.stop();
        CHECK(host.world().clock().paused()); // 멈춘 뒤에는 월드를 볼 수 있다
        CHECK(host.stats().ticksRun >= 1);
        session.update(seconds());
        CHECK(session.disconnectReason() == DisconnectReason::ServerShutdown);
    }

} // TEST_SUITE
