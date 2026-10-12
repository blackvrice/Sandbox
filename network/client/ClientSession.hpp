#pragma once
// 클라이언트 쪽 연결: 핸드셰이크 · 명령 보내기 · 결과 · 서버 통계. docs/08-NETWORK.md 4장, ADR-0024.
// Phase 10: Welcome 뒤 ClientWorld(복제 월드)에 Snapshot · TerrainChunk 를 적용하고 SnapshotAck 를 보낸다.
// Phase 10B: 선택 상세 (setInspect → InspectResult). Phase 11: 관심 영역 (subscribe), 다시 접속 토큰.
// Phase 12C: 접속자 표 (PlayerList · 지금 내 역할), 역할 바꾸기 요청 (requestRole → SetRoleResult), 월드 오버레이
// (ContentOverlay 를 모아 overlay() — 바뀔 때마다 overlayRevision 이 오른다).
// SandboxClient 의 NetworkSession 이 쓴다.
//
// 한 스레드에서 쓴다 (Transport 계약). 시간은 호출자가 준다 (초) — 핸드셰이크 시간 초과에만 쓴다.

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "network/client/ClientWorld.hpp"
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
    // 다시 접속 (11.4): 지난 Welcome.sessionToken. 서버가 알아보면 같은 clientId · 역할 (모르면 새 클라이언트로)
    std::vector<std::byte> token;
    std::string ownerKey; // 서버의 --owner-key (12C) — 맞으면 Owner
    // 둘 다 있으면 Welcome 뒤 ClientWorld 를 만들어 Snapshot · TerrainChunk 를 적용한다 (Phase 10). 없으면 스냅숏은 ack
    // 만
    const ecs::ComponentCatalog* catalog = nullptr;
    const content::ContentDatabase* content = nullptr;
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
    // 복제 월드 (Welcome 전 · catalog/content 없음 · 만들기 실패면 nullptr)
    [[nodiscard]] const ClientWorld* world() const noexcept { return m_world.get(); }
    [[nodiscard]] u64 snapshotBytes() const noexcept { return m_snapshotBytes; }
    // 선택 상세 (10B): 볼 개체 (kMaxInspect 까지, 빈 목록 = 그만). 바뀌었을 때만 보낸다. 접속 전이면 무시
    void setInspect(std::span<const NetEntityId> ids);
    // 관심 영역 (Phase 11): 바뀌었을 때만 보낸다. 접속 전이면 무시 (보내지 않은 것으로 남아 다음에 다시)
    void subscribe(const Subscribe& interest);
    [[nodiscard]] const std::optional<Subscribe>& subscribed() const noexcept { return m_subscribed; }
    // 가장 최근 InspectResult (요청이 없거나 아직 안 왔으면 비어 있다)
    [[nodiscard]] const std::optional<InspectResult>& inspect() const noexcept { return m_inspect; }

    // --- 12C ---
    // 지금 내 역할 (Welcome 뒤 PlayerList 로 바뀐다). 접속 전이면 Observer
    [[nodiscard]] Role role() const noexcept { return m_role; }
    // 접속자 표 (clientId 오름차순) · 바뀔 때마다 오르는 번호
    [[nodiscard]] const std::vector<PlayerEntry>& players() const noexcept { return m_players; }
    [[nodiscard]] u64 playersRevision() const noexcept { return m_playersRevision; }
    // 역할 바꾸기 요청 (서버가 판단한다 — 결과는 takeRoleResults). 접속 전이면 false
    bool requestRole(u16 clientId, Role role);
    [[nodiscard]] std::vector<SetRoleResultMsg> takeRoleResults();
    // 서버 월드의 콘텐츠 오버레이 (받은 것을 모은 것) · 바뀔 때마다 오르는 번호. 잘못된 JSON 항목은 건너뛴다 (경고)
    [[nodiscard]] const content::ContentOverlay& overlay() const noexcept { return m_overlay; }
    [[nodiscard]] u64 overlayRevision() const noexcept { return m_overlayRevision; }

private:
    void send(const Message& m);
    void onMessage(Message& m);
    void fail(DisconnectReason reason);
    void applyOverlay(const ContentOverlayMsg& m);

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
    std::unique_ptr<ClientWorld> m_world;
    std::vector<TerrainChunk>
        m_earlyTerrain; // Welcome 보다 먼저 온 지형 (채널이 달라 순서가 바뀔 수 있다 — 신뢰 채널이라 버릴 수 없다)
    u64 m_snapshotBytes = 0;
    std::vector<NetEntityId> m_inspectIds;
    std::optional<InspectResult> m_inspect;
    std::optional<Subscribe> m_subscribed; // 마지막으로 보낸 관심 영역
    Role m_role = Role::Observer;
    std::vector<PlayerEntry> m_players;
    u64 m_playersRevision = 0;
    std::vector<SetRoleResultMsg> m_roleResults;
    content::ContentOverlay m_overlay;
    u64 m_overlayRevision = 0;
    std::vector<TransportEvent> m_events;
};

} // namespace sbx::net
