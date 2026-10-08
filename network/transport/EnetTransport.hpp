#pragma once
// ENet(UDP) 위의 Transport — 실제 네트워크. docs/08-NETWORK.md 2 · 3장, ADR-0010 · ADR-0024.
//
//   채널    ENet 채널 3개 = Control(0) · Snapshot(1) · Bulk(2). Control · Bulk = RELIABLE, Snapshot = 플래그 없음
//           (ENet 의 unreliable sequenced — 옛 패킷은 버린다)
//   끊기    enet_peer_disconnect_later — 쌓인 송신을 다 보낸 뒤 끊는다. 이유(DisconnectReason)는 disconnect data 로
//   시간    무응답 15 초에 끊김 (enet_peer_timeout 32 · 5 s · 15 s — 08 4장)
//   주소    host 는 IPv4 주소 또는 이름 (enet_address_set_host). listen 의 빈 host = 모든 인터페이스
// enet.h(와 Windows 의 winsock2.h)는 .cpp 에만 — 이 헤더를 쓰는 쪽은 ENet 을 모른다.
// 암호화가 없다 (ENet) — LAN · 신뢰하는 환경 전용 (16 위험 R6).

#include <map>
#include <memory>

#include "network/transport/Transport.hpp"

namespace sbx::net {

class EnetTransport final : public INetworkTransport {
public:
    // ENet 라이브러리 초기화 (프로세스에 한 번, 참조 계수). 실패하면 IoError
    static Expected<std::unique_ptr<EnetTransport>> create();
    ~EnetTransport() override; // 끊는 중인 연결을 잠깐(최대 0.3 초) 마무리한 뒤 호스트를 닫는다

    [[nodiscard]] Expected<void> listen(const Endpoint& at, u32 maxConnections) override;
    [[nodiscard]] u16 boundPort() const noexcept override { return m_boundPort; }
    [[nodiscard]] Expected<ConnectionId> connect(const Endpoint& to) override;
    void send(ConnectionId to, Channel channel, std::span<const std::byte> data) override;
    void flush() override;
    void poll(std::vector<TransportEvent>& out) override;
    void wait(u32 maxMs) override;
    void disconnect(ConnectionId connection, DisconnectReason reason) override;
    void drain(u32 maxMs) override;
    [[nodiscard]] TransportStats stats(ConnectionId connection) const override;
    [[nodiscard]] std::string_view name() const noexcept override { return "ENet"; }

    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;
    EnetTransport(EnetTransport&&) = delete;
    EnetTransport& operator=(EnetTransport&&) = delete;

private:
    struct Impl;
    EnetTransport();
    std::unique_ptr<Impl> m_impl;
    u16 m_boundPort = 0;
};

} // namespace sbx::net
