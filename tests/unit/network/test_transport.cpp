#include <doctest/doctest.h>

#include <memory>
#include <thread>

#include "network/transport/LoopbackTransport.hpp"
#include "network/transport/SimulatedTransport.hpp"

using namespace sbx;
using namespace sbx::net;

namespace {

std::vector<std::byte> bytesOf(u8 v, usize n = 1) {
    return std::vector<std::byte>(n, std::byte{v});
}

std::vector<TransportEvent> drain(INetworkTransport& t) {
    std::vector<TransportEvent> out;
    t.poll(out);
    return out;
}

using Type = TransportEvent::Type;

} // namespace

TEST_SUITE("network") {

    TEST_CASE("loopback: connect, send on every channel, disconnect with a reason") {
        LoopbackNetwork hub;
        LoopbackTransport server(hub);
        LoopbackTransport client(hub);
        REQUIRE(server.listen({"w", 7777}, 4).has_value());
        CHECK(server.boundPort() == 7777);
        CHECK_FALSE(server.listen({"w", 1}, 4).has_value()); // 두 번

        const auto c = client.connect({"w", 0});
        REQUIRE(c.has_value());
        auto ce = drain(client);
        REQUIRE(ce.size() == 1);
        CHECK(ce[0].type == Type::Connected);
        CHECK(ce[0].connection == *c);
        auto se = drain(server);
        REQUIRE(se.size() == 1);
        const ConnectionId s = se[0].connection;

        client.send(*c, Channel::Control, bytesOf(1));
        client.send(*c, Channel::Snapshot, bytesOf(2, 3));
        client.send(*c, Channel::Bulk, bytesOf(3));
        se = drain(server);
        REQUIRE(se.size() == 3);
        CHECK(se[0].channel == Channel::Control);
        CHECK(se[1].channel == Channel::Snapshot);
        CHECK(se[1].data.size() == 3);
        CHECK(se[2].data[0] == std::byte{3});
        CHECK(client.stats(*c).bytesSent == 5);
        CHECK(server.stats(s).packetsReceived == 3);

        server.send(s, Channel::Control, bytesOf(9));
        server.disconnect(s, DisconnectReason::Kicked);
        CHECK(drain(server).empty()); // 끊은 쪽에는 이벤트가 없다
        ce = drain(client);
        REQUIRE(ce.size() == 2);
        CHECK(ce[0].type == Type::Received); // 끊기 전에 보낸 것이 먼저
        CHECK(ce[1].type == Type::Disconnected);
        CHECK(ce[1].reason == DisconnectReason::Kicked);
        client.send(*c, Channel::Control, bytesOf(1)); // 끊긴 연결: 조용히 버린다
        CHECK(drain(server).empty());
        CHECK(server.openConnections() == 0);
    }

    TEST_CASE("loopback: no listener, full server, and a transport that goes away") {
        LoopbackNetwork hub;
        LoopbackTransport client(hub);
        const auto c = client.connect({"nobody", 0});
        REQUIRE(c.has_value());
        auto ev = drain(client);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].reason == DisconnectReason::ConnectFailed);

        LoopbackTransport server(hub);
        REQUIRE(server.listen({"w", 0}, 1).has_value());
        const auto first = client.connect({"w", 0});
        const auto second = client.connect({"w", 0});
        ev = drain(client);
        REQUIRE(ev.size() == 2);
        CHECK(ev[0].type == Type::Connected);
        CHECK(ev[1].type == Type::Disconnected);
        CHECK(ev[1].connection == *second);
        CHECK(ev[1].reason == DisconnectReason::ServerFull);
        CHECK(*first != *second); // id 를 다시 쓰지 않는다

        {
            LoopbackTransport other(hub);
            REQUIRE(other.connect({"w", 0}).has_value()); // 가득 참
        }
        {
            auto temp = std::make_unique<LoopbackTransport>(hub);
            REQUIRE(temp->listen({"tmp", 0}, 2).has_value());
            const auto t = client.connect({"tmp", 0});
            (void)drain(client);
            temp.reset();
            ev = drain(client);
            REQUIRE(ev.size() == 1);
            CHECK(ev[0].connection == *t);
            CHECK(ev[0].reason == DisconnectReason::Timeout);
        }
    }

    TEST_CASE("loopback: wait wakes up when another thread sends") {
        LoopbackNetwork hub;
        LoopbackTransport server(hub);
        LoopbackTransport client(hub);
        REQUIRE(server.listen({"w", 0}, 4).has_value());
        const auto c = client.connect({"w", 0});
        (void)drain(server);
        std::thread sender([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            client.send(*c, Channel::Control, bytesOf(1));
        });
        std::vector<TransportEvent> got;
        for (int i = 0; i < 100 && got.empty(); ++i) {
            server.wait(100);
            server.poll(got);
        }
        sender.join();
        REQUIRE(got.size() == 1);
        CHECK(got[0].type == Type::Received);
    }

    TEST_CASE("simulated: latency holds packets until their time, reliable channels keep order") {
        LoopbackNetwork hub;
        LoopbackTransport server(hub);
        LoopbackTransport inner(hub);
        f64 now = 0;
        SimulatedTransport client(inner, {.latencyMs = 100, .jitterMs = 30, .seed = 7}, [&] { return now; });
        REQUIRE(server.listen({"w", 0}, 4).has_value());
        const auto c = client.connect({"w", 0});
        (void)drain(server);
        for (u8 i = 0; i < 20; ++i) {
            client.send(*c, Channel::Control, bytesOf(i));
        }
        client.flush();
        CHECK(drain(server).empty());
        now = 69;
        client.flush();
        CHECK(drain(server).empty()); // 최소 70 ms
        now = 131;
        client.flush();
        const auto got = drain(server);
        REQUIRE(got.size() == 20);
        for (u8 i = 0; i < 20; ++i) {
            CHECK(got[i].data[0] == std::byte{i});
        }
    }

    TEST_CASE("simulated: snapshot loss and stale reordering are dropped, reliable loss only delays; seed decides") {
        auto run = [](u64 seed) {
            LoopbackNetwork hub;
            LoopbackTransport server(hub);
            LoopbackTransport inner(hub);
            f64 now = 0;
            SimulatedTransport client(inner, {.latencyMs = 50, .jitterMs = 40, .lossRate = 0.2, .seed = seed},
                                      [&] { return now; });
            REQUIRE(server.listen({"w", 0}, 4).has_value());
            const auto c = client.connect({"w", 0});
            (void)drain(server);
            for (u8 i = 0; i < 200; ++i) {
                client.send(*c, Channel::Snapshot, bytesOf(i));
                client.send(*c, Channel::Control, bytesOf(i));
                now += 5;
                client.flush();
            }
            now += 1000;
            client.flush();
            std::vector<u8> snaps;
            std::vector<u8> control;
            for (const auto& e : drain(server)) {
                (e.channel == Channel::Snapshot ? snaps : control).push_back(static_cast<u8>(e.data[0]));
            }
            REQUIRE(control.size() == 200); // 신뢰 채널은 하나도 잃지 않는다
            for (usize i = 0; i < control.size(); ++i) {
                CHECK(control[i] == i);
            }
            for (usize i = 1; i < snaps.size(); ++i) {
                CHECK(snaps[i] > snaps[i - 1]); // 순차: 옛것은 버려졌다
            }
            const auto& st = client.simulatedStats();
            CHECK(st.dropped > 0);
            CHECK(st.retransmitted > 0);
            CHECK(snaps.size() + st.dropped + st.droppedStale == 200);
            return snaps;
        };
        const auto a = run(11);
        CHECK(run(11) == a); // 같은 seed → 같은 결과
        CHECK(run(12) != a);
    }

    TEST_CASE("simulated: disconnect waits for queued packets, endpoints parse") {
        LoopbackNetwork hub;
        LoopbackTransport server(hub);
        LoopbackTransport inner(hub);
        f64 now = 0;
        SimulatedTransport client(inner, {.latencyMs = 20}, [&] { return now; });
        REQUIRE(server.listen({"w", 0}, 4).has_value());
        const auto c = client.connect({"w", 0});
        (void)drain(server);
        client.send(*c, Channel::Control, bytesOf(1));
        client.disconnect(*c, DisconnectReason::ClientQuit);
        CHECK(drain(server).empty());
        now = 25;
        client.flush();
        const auto ev = drain(server);
        REQUIRE(ev.size() == 2);
        CHECK(ev[0].type == Type::Received);
        CHECK(ev[1].reason == DisconnectReason::ClientQuit);

        CHECK(parseEndpoint("127.0.0.1:7777", 1).value().port == 7777);
        CHECK(parseEndpoint("localhost", 7777).value().port == 7777);
        CHECK_FALSE(parseEndpoint("host:99999", 1).has_value());
        CHECK_FALSE(parseEndpoint(":80", 1).has_value());
    }

} // TEST_SUITE
