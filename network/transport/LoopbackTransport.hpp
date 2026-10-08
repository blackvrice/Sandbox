#pragma once
// 같은 프로세스 안의 Transport. 싱글플레이(LocalServerHost, Phase 10)와 테스트가 쓴다 — 바이트는 그대로 복사되므로
// 직렬화 경로는 실제 네트워크와 같다 (08 2장). docs/08-NETWORK.md, ADR-0024.
//
//   LoopbackNetwork hub;                       // 이름 → 받는 쪽. 모든 Loopback 보다 오래 살아야 한다
//   LoopbackTransport server(hub), client(hub);
//   server.listen({"world", 0}, 16);
//   auto c = client.connect({"world", 0});     // 바로 양쪽 받은 편지함에 Connected
//
// 스레드: 서로 다른 스레드에서 각자의 Transport 를 써도 된다 (허브의 뮤텍스 하나로 잇는다). 한 Transport 를 두
// 스레드가 쓰는 것은 안 된다 (Transport.hpp 계약). 모든 채널이 신뢰 + 순서다 (Snapshot 도 잃지 않는다).

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>

#include "network/transport/Transport.hpp"

namespace sbx::net {

class LoopbackTransport;

class LoopbackNetwork {
public:
    LoopbackNetwork() = default;
    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;
    LoopbackNetwork(LoopbackNetwork&&) = delete;
    LoopbackNetwork& operator=(LoopbackNetwork&&) = delete;
    ~LoopbackNetwork(); // 남은 Transport 가 있으면 단언

private:
    friend class LoopbackTransport;
    std::mutex m_mutex;
    std::map<std::string, LoopbackTransport*, std::less<>> m_listeners;
    usize m_transports = 0;
};

class LoopbackTransport final : public INetworkTransport {
public:
    explicit LoopbackTransport(LoopbackNetwork& network);
    ~LoopbackTransport() override; // 열린 연결의 상대에게 Disconnected{Timeout} (프로세스가 사라진 것처럼)

    [[nodiscard]] Expected<void> listen(const Endpoint& at, u32 maxConnections) override;
    [[nodiscard]] u16 boundPort() const noexcept override { return m_listening ? m_port : 0; }
    [[nodiscard]] Expected<ConnectionId> connect(const Endpoint& to) override;
    void send(ConnectionId to, Channel channel, std::span<const std::byte> data) override;
    void flush() override {}
    void poll(std::vector<TransportEvent>& out) override;
    void wait(u32 maxMs) override;
    void disconnect(ConnectionId connection, DisconnectReason reason) override;
    [[nodiscard]] TransportStats stats(ConnectionId connection) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Loopback"; }

    // 열린 연결 수 (테스트)
    [[nodiscard]] usize openConnections() const;

private:
    struct Link {
        LoopbackTransport* peer = nullptr;
        ConnectionId peerConnection = kInvalidConnection;
        TransportStats stats;
    };

    // 허브 뮤텍스를 잡은 채로 부른다
    void pushLocked(TransportEvent event);
    ConnectionId allocateLocked() { return ++m_nextConnection; }

    LoopbackNetwork& m_network;
    std::string m_listenName;
    bool m_listening = false;
    u16 m_port = 0; // 이름이 주소다 — 포트는 받은 값을 돌려줄 뿐
    u32 m_maxConnections = 0;
    ConnectionId m_nextConnection = 0;
    std::map<ConnectionId, Link> m_links; // 허브 뮤텍스로 보호
    std::vector<TransportEvent> m_inbox;  // 허브 뮤텍스로 보호
    std::condition_variable m_inboxReady;
};

} // namespace sbx::net
