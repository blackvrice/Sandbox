#pragma once
// 클라이언트 쪽 연결: 핸드셰이크 · 명령 보내기 · 결과 · 서버 통계. docs/08-NETWORK.md 4장, ADR-0024.
// Phase 9 는 스냅숏 없이 여기까지 (sbx_net_probe · 테스트). [계획 Phase 10] Subscribe · Bulk · Ready · Snapshot 받기,
// SandboxClient 의 IWorldSession 구현(NetworkSession) 과 LocalServerHost.
//
// 한 스레드에서 쓴다 (Transport 계약). 시간은 호출자가 준다 (초) — 핸드셰이크 시간 초과에만 쓴다.

#include <optional>
#include <vector>

#include "network/protocol/Messages.hpp"
#include "network/transport/Transport.hpp"

namespace sbx::net {

enum class ClientState : u8 {
    Idle = 0,     // connect 전
    Connecting,   // Transport 연결 중
    Handshaking,  // Hello 보냄 → Challenge → Auth
    Connected,    // Welcome 받음
    Rejected,     // Reject 받음 (reject() 에 사유)
    Disconnected, // 끊김 (disconnectReason())
};
[[nodiscard]] std::string_view clientStateName(ClientState s) noexcept;

struct ClientSessionDesc {
    std::string displayName = "player";
    u64 contentHash = 0;
    u64 buildId = 0;
    f64 handshakeTimeoutSeconds = 10;
};

class ClientSession {
public:
    ClientSession(INetworkTransport& transport, ClientSessionDesc desc);
    ~ClientSession(); // 연결돼 있으면 disconnect()

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;
    ClientSession(ClientSession&&) = delete;
    ClientSession& operator=(ClientSession&&) = delete;

    [[nodiscard]] Expected<void> connect(const Endpoint& to, f64 nowSeconds);
    // 이벤트를 받아 처리하고 보낼 것을 내보낸다
    void update(f64 nowSeconds);
    // 연결돼 있지 않으면 0 (보내지 않는다). 결과는 takeResults() 로 — 같은 sequence
    u32 sendCommand(cmd::CommandPayload payload);
    // Disconnect{ClientQuit} 을 보내고 끊는다
    void disconnect();

    [[nodiscard]] ClientState state() const noexcept { return m_state; }
    [[nodiscard]] const std::optional<Welcome>& welcome() const noexcept { return m_welcome; }
    [[nodiscard]] const std::optional<Reject>& reject() const noexcept { return m_reject; }
    [[nodiscard]] DisconnectReason disconnectReason() const noexcept { return m_disconnectReason; }
    [[nodiscard]] const std::optional<ServerStats>& lastStats() const noexcept { return m_stats; }
    [[nodiscard]] std::vector<CommandResultMsg> takeResults();
    [[nodiscard]] TransportStats transportStats() const;

private:
    void send(const Message& m);
    void onMessage(Message& m);
    void fail(DisconnectReason reason);

    INetworkTransport& m_transport;
    ClientSessionDesc m_desc;
    ClientState m_state = ClientState::Idle;
    ConnectionId m_connection = kInvalidConnection;
    f64 m_startedAt = 0;
    u32 m_nextSequence = 1;
    std::optional<Welcome> m_welcome;
    std::optional<Reject> m_reject;
    std::optional<ServerStats> m_stats;
    DisconnectReason m_disconnectReason = DisconnectReason::None;
    std::vector<CommandResultMsg> m_results;
    std::vector<TransportEvent> m_events;
};

} // namespace sbx::net
