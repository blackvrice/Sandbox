#pragma once
// 바이트를 실어 나르는 층. 위(ServerHost · ClientSession)는 메시지 의미를, 이 층은 연결 · 채널 · 전달 보장만 안다.
// docs/08-NETWORK.md 2 · 3장, ADR-0010 · ADR-0024.
//
// 계약
//   - 한 Transport 객체는 한 스레드(Net IO)가 쓴다. 스레드 안전하지 않다 (LoopbackNetwork 허브만 스레드 사이를 잇는다).
//   - poll 은 쌓인 이벤트를 out 끝에 붙인다. Connected → (Received …) → Disconnected 순서. Disconnected 뒤 그 id 는
//   다시
//     쓰이지 않는다 (ConnectionId 재사용 없음 — 늦은 이벤트가 새 연결로 잘못 가지 않게).
//   - send 는 복사한다. 연결이 없거나 끊긴 id 면 조용히 버린다 (끊김은 poll 의 Disconnected 로 알게 된다).
//   - 채널 보장 (3장): Control · Bulk = 신뢰 + 순서. Snapshot = 비신뢰 + 순차 (늦게 온 옛 패킷은 버린다).
//   - flush 는 쌓인 송신을 지금 내보낸다 (ENet 은 다음 service 까지 모은다).

#include <span>
#include <string>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::net {

enum class Channel : u8 { Control = 0, Snapshot = 1, Bulk = 2 };
inline constexpr u8 kChannelCount = 3;

[[nodiscard]] constexpr bool isReliable(Channel c) noexcept {
    return c != Channel::Snapshot;
}

using ConnectionId = u32;
inline constexpr ConnectionId kInvalidConnection = 0;

// host = 이름 또는 IPv4 주소. Loopback 은 이름만 본다. port 0 = (listen) 아무 빈 포트
struct Endpoint {
    std::string host;
    u16 port = 0;
    [[nodiscard]] std::string describe() const;
};
// "host:port" → Endpoint. 포트가 없으면 defaultPort
[[nodiscard]] Expected<Endpoint> parseEndpoint(std::string_view text, u16 defaultPort);

// 연결이 끊긴 이유. 와이어(Disconnect 메시지 · ENet disconnect data)에도 이 값이 실린다 — 값을 바꾸지 않는다
enum class DisconnectReason : u8 {
    None = 0,
    ClientQuit = 1, // 상대가 스스로 끊음
    ServerShutdown = 2,
    Timeout = 3,       // 응답 없음 (Transport) 또는 핸드셰이크 시간 초과
    Rejected = 4,      // 핸드셰이크 거절 (Reject 메시지 뒤)
    ProtocolError = 5, // 해석할 수 없는 메시지 · 상한 초과
    ServerFull = 6,
    Kicked = 7,
    ConnectFailed = 8, // 접속 시도 실패 (상대 없음)
};
[[nodiscard]] std::string_view disconnectReasonName(DisconnectReason r) noexcept;

struct TransportEvent {
    enum class Type : u8 { Connected, Disconnected, Received };
    Type type = Type::Received;
    ConnectionId connection = kInvalidConnection;
    Channel channel = Channel::Control;               // Received
    DisconnectReason reason = DisconnectReason::None; // Disconnected
    std::vector<std::byte> data;                      // Received
};

struct TransportStats {
    f32 rttMs = 0;
    f32 rttVarianceMs = 0;
    f32 packetLoss = 0; // 0 ~ 1 (ENet 추정)
    u64 bytesSent = 0;
    u64 bytesReceived = 0;
    u64 packetsSent = 0;
    u64 packetsReceived = 0;
};

class INetworkTransport {
public:
    INetworkTransport() = default;
    INetworkTransport(const INetworkTransport&) = delete;
    INetworkTransport& operator=(const INetworkTransport&) = delete;
    INetworkTransport(INetworkTransport&&) = delete;
    INetworkTransport& operator=(INetworkTransport&&) = delete;
    virtual ~INetworkTransport() = default;

    // 서버: 들어오는 연결을 받는다. maxConnections 를 넘는 접속은 Transport 가 거절한다
    [[nodiscard]] virtual Expected<void> listen(const Endpoint& at, u32 maxConnections) = 0;
    // 실제로 받고 있는 포트 (listen 에서 port 0 을 줬을 때 확인용). listen 전이면 0
    [[nodiscard]] virtual u16 boundPort() const noexcept = 0;
    // 클라이언트: 접속을 시작한다. 성공하면 poll 이 Connected, 실패하면 Disconnected{ConnectFailed}
    [[nodiscard]] virtual Expected<ConnectionId> connect(const Endpoint& to) = 0;
    virtual void send(ConnectionId to, Channel channel, std::span<const std::byte> data) = 0;
    virtual void flush() = 0;
    virtual void poll(std::vector<TransportEvent>& out) = 0;
    // 이벤트가 생기거나 maxMs 가 지날 때까지 기다린다 (Net IO 스레드의 대기). 기본은 잠깐 잔다
    virtual void wait(u32 maxMs);
    // 보낸 것을 먼저 다 보내고 끊는다 (그 뒤 상대는 Disconnected{reason}). 이쪽에는 Disconnected 가 오지 않는다
    virtual void disconnect(ConnectionId connection, DisconnectReason reason) = 0;
    // 끊는 중인 연결이 상대에게 실제로 닿을 시간을 준다 (최대 maxMs). 서버를 멈출 때 · 프로세스를 끝내기 전에.
    // 쌓인 이벤트는 버린다. 즉시 전달하는 Transport(Loopback)는 아무것도 하지 않는다
    virtual void drain(u32 maxMs) { (void)maxMs; }
    [[nodiscard]] virtual TransportStats stats(ConnectionId connection) const = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

} // namespace sbx::net
