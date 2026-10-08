#pragma once
// 다른 Transport 를 감싸 나쁜 네트워크를 흉내 낸다 (테스트 전용). docs/08-NETWORK.md 2 · 12장, docs/13-TESTING.md L3′.
//
// 보내는 쪽에서만 흉내 낸다 — 양방향을 보려면 양쪽 Transport 를 각각 감싼다.
//   지연      latencyMs ± jitterMs (균등). 대역폭 제한이 있으면 직렬화 시간이 더해진다
//   손실      Snapshot(비신뢰) 채널만 lossRate 로 버린다. 신뢰 채널은 잃지 않고 재전송 시간(지연 × 2)만큼 늦어진다
//   순서      신뢰 채널은 연결 · 채널별 순서를 지킨다. Snapshot 은 지터로 순서가 바뀔 수 있고, 먼저 간 것보다 옛
//             패킷은 버린다 (ENet 의 unreliable sequenced 와 같은 규칙)
//   끊기      disconnect 는 그 연결에 쌓인 패킷이 다 나간 뒤에 안쪽 Transport 로 간다
// 시간은 now() 함수가 준다 (기본 steady_clock). 테스트는 손으로 움직이는 시계를 넘겨 결정적으로 돌린다.
// 난수는 seed 로 정해진다 (splitmix64 — 툴체인과 무관).

#include <deque>
#include <functional>
#include <map>

#include "network/transport/Transport.hpp"

namespace sbx::net {

struct SimulatedLinkDesc {
    f64 latencyMs = 0;
    f64 jitterMs = 0;
    f64 lossRate = 0;             // 0 ~ 1, Snapshot 채널만
    f64 bandwidthBytesPerSec = 0; // 0 = 제한 없음
    u64 seed = 1;
};

struct SimulatedStats {
    u64 delayed = 0;
    u64 dropped = 0;       // 손실로 버림
    u64 droppedStale = 0;  // 순서가 바뀌어 옛것이라 버림 (Snapshot)
    u64 retransmitted = 0; // 신뢰 채널 손실 → 늦게 보냄
};

class SimulatedTransport final : public INetworkTransport {
public:
    using Clock = std::function<f64()>; // 밀리초

    SimulatedTransport(INetworkTransport& inner, SimulatedLinkDesc desc, Clock now = {});

    [[nodiscard]] Expected<void> listen(const Endpoint& at, u32 maxConnections) override {
        return m_inner.listen(at, maxConnections);
    }
    [[nodiscard]] u16 boundPort() const noexcept override { return m_inner.boundPort(); }
    [[nodiscard]] Expected<ConnectionId> connect(const Endpoint& to) override { return m_inner.connect(to); }
    void send(ConnectionId to, Channel channel, std::span<const std::byte> data) override;
    void flush() override;
    void poll(std::vector<TransportEvent>& out) override;
    void wait(u32 maxMs) override;
    void disconnect(ConnectionId connection, DisconnectReason reason) override;
    void drain(u32 maxMs) override {
        pump();
        m_inner.drain(maxMs);
    }
    [[nodiscard]] TransportStats stats(ConnectionId connection) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Simulated"; }

    void setDesc(const SimulatedLinkDesc& desc) { m_desc = desc; }
    [[nodiscard]] const SimulatedStats& simulatedStats() const noexcept { return m_stats; }
    [[nodiscard]] usize inFlight() const noexcept { return m_queue.size(); }

private:
    struct Pending {
        f64 deliverAt = 0;
        u64 order = 0; // 같은 시각이면 보낸 순서
        ConnectionId connection = kInvalidConnection;
        Channel channel = Channel::Control;
        u64 snapshotSequence = 0;
        bool disconnect = false;
        DisconnectReason reason = DisconnectReason::None;
        std::vector<std::byte> data;
    };
    struct LinkState {
        f64 lastReliableAt[kChannelCount]{};
        f64 lastAnyAt = 0;
        u64 nextSnapshotSequence = 1;
        u64 lastForwardedSnapshot = 0;
    };

    void pump();
    [[nodiscard]] f64 random01();

    INetworkTransport& m_inner;
    SimulatedLinkDesc m_desc;
    Clock m_now;
    u64 m_rng;
    u64 m_order = 0;
    f64 m_linkBusyUntil = 0;
    std::deque<Pending> m_queue; // deliverAt · order 로 정렬된 채
    std::map<ConnectionId, LinkState> m_links;
    SimulatedStats m_stats;
};

} // namespace sbx::net
