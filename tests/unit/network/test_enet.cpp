#include <doctest/doctest.h>

#include <chrono>
#include <functional>
#include <memory>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/scenarios/Scenario.hpp"
#include "network/client/ClientSession.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/EnetTransport.hpp"

// 실제 UDP 소켓 (127.0.0.1, 빈 포트). 같은 프로세스 안에서 서버 · 클라이언트를 함께 돌린다.

using namespace sbx;
using namespace sbx::net;

namespace {

using Type = TransportEvent::Type;

f64 secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
}

// cond 가 참이 될 때까지 둘 다 돌린다 (최대 5 초)
bool spin(EnetTransport& a, std::vector<TransportEvent>& ea, EnetTransport& b, std::vector<TransportEvent>& eb,
          const std::function<bool()>& cond) {
    const auto t0 = std::chrono::steady_clock::now();
    while (secondsSince(t0) < 5) {
        a.wait(2);
        a.poll(ea);
        b.wait(2);
        b.poll(eb);
        if (cond()) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_SUITE("network") {

    TEST_CASE("enet: connect over UDP loopback, three channels both ways, disconnect carries the reason") {
        auto server = EnetTransport::create();
        auto client = EnetTransport::create();
        REQUIRE(server.has_value());
        REQUIRE(client.has_value());
        REQUIRE((*server)->listen({"127.0.0.1", 0}, 4).has_value());
        const u16 port = (*server)->boundPort();
        REQUIRE(port != 0);
        const auto c = (*client)->connect({"127.0.0.1", port});
        REQUIRE(c.has_value());

        std::vector<TransportEvent> se;
        std::vector<TransportEvent> ce;
        REQUIRE(spin(**server, se, **client, ce, [&] { return !se.empty() && !ce.empty(); }));
        CHECK(se[0].type == Type::Connected);
        CHECK(ce[0].type == Type::Connected);
        CHECK(ce[0].connection == *c);
        const ConnectionId s = se[0].connection;
        se.clear();
        ce.clear();

        const std::vector<std::byte> big(3000, std::byte{7}); // MTU 보다 커서 조각난다
        (*client)->send(*c, Channel::Control, std::vector<std::byte>{std::byte{1}});
        (*client)->send(*c, Channel::Snapshot, std::vector<std::byte>{std::byte{2}});
        (*client)->send(*c, Channel::Bulk, big);
        (*server)->send(s, Channel::Control, std::vector<std::byte>{std::byte{9}});
        (*client)->flush();
        (*server)->flush();
        REQUIRE(spin(**server, se, **client, ce, [&] { return se.size() >= 3 && !ce.empty(); }));
        bool sawControl = false;
        bool sawSnapshot = false;
        bool sawBulk = false;
        for (const auto& e : se) {
            REQUIRE(e.type == Type::Received);
            sawControl = sawControl || (e.channel == Channel::Control && e.data[0] == std::byte{1});
            sawSnapshot = sawSnapshot || (e.channel == Channel::Snapshot && e.data[0] == std::byte{2});
            sawBulk = sawBulk || (e.channel == Channel::Bulk && e.data == big);
        }
        CHECK(sawControl);
        CHECK(sawSnapshot);
        CHECK(sawBulk);
        CHECK(ce[0].data[0] == std::byte{9});
        CHECK((*client)->stats(*c).bytesSent == 3002);

        se.clear();
        (*client)->disconnect(*c, DisconnectReason::ClientQuit);
        REQUIRE(spin(**server, se, **client, ce, [&] { return !se.empty(); }));
        CHECK(se[0].type == Type::Disconnected);
        CHECK(se[0].reason == DisconnectReason::ClientQuit);
    }

    TEST_CASE("enet: a threaded ServerHost serves a ClientSession over real UDP") {
        static const ecs::ComponentCatalog catalog = [] {
            ecs::ComponentCatalog c;
            (void)comp::registerCoreComponents(c);
            return c;
        }();
        auto serverT = EnetTransport::create();
        REQUIRE(serverT.has_value());
        ServerHostDesc desc;
        desc.mode = ServerMode::Threaded;
        desc.defaultRole = Role::Admin;
        auto runner = std::make_unique<scenario::ScenarioRunner>(catalog, content::ContentDatabase::builtin(),
                                                                 scenario::makeScenario("random_walk_1k"), 1);
        ServerHost host(**serverT, std::move(runner), desc);
        REQUIRE(host.start({"127.0.0.1", 0}).has_value());
        const u16 port = host.boundPort();
        REQUIRE(port != 0);

        auto clientT = EnetTransport::create();
        REQUIRE(clientT.has_value());
        ClientSessionDesc cd;
        cd.displayName = "udp";
        cd.contentHash = content::ContentDatabase::builtin().contentHash();
        ClientSession session(**clientT, cd);
        const auto t0 = std::chrono::steady_clock::now();
        REQUIRE(session.connect({"127.0.0.1", port}, 0).has_value());
        std::vector<CommandResultMsg> results;
        bool sent = false;
        while (secondsSince(t0) < 5 && (results.empty() || !session.lastStats())) {
            (*clientT)->wait(5);
            session.update(secondsSince(t0));
            if (session.state() == ClientState::Connected && !sent) {
                session.sendCommand(cmd::SetSimulationSpeed{2.0f});
                sent = true;
            }
            for (auto& r : session.takeResults()) {
                results.push_back(std::move(r));
            }
        }
        REQUIRE(session.state() == ClientState::Connected);
        REQUIRE(results.size() == 1);
        CHECK(results[0].accepted);
        REQUIRE(session.lastStats().has_value());
        CHECK(session.lastStats()->clients == 1);

        host.stop();
        const auto t1 = std::chrono::steady_clock::now();
        while (secondsSince(t1) < 3 && session.state() == ClientState::Connected) {
            // 서버의 disconnect_later 는 클라이언트의 확인(ack)을 받아야 끝난다 — 같은 스레드라 서버 쪽도 돌려 준다
            (*serverT)->drain(5);
            (*clientT)->wait(5);
            session.update(secondsSince(t0));
        }
        CHECK(session.state() == ClientState::Disconnected);
        CHECK(session.disconnectReason() == DisconnectReason::ServerShutdown);
        CHECK(host.world().clock().speed() == 2.0f);
    }

} // TEST_SUITE
