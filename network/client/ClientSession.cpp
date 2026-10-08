#include "network/client/ClientSession.hpp"

#include <algorithm>

#include "foundation/log/Log.hpp"

namespace sbx::net {

std::string_view clientStateName(ClientState s) noexcept {
    switch (s) {
    case ClientState::Idle:
        return "대기";
    case ClientState::Connecting:
        return "연결 중";
    case ClientState::Handshaking:
        return "핸드셰이크";
    case ClientState::Connected:
        return "접속됨";
    case ClientState::Rejected:
        return "거절됨";
    case ClientState::Disconnected:
        return "끊김";
    }
    return "?";
}

ClientSession::ClientSession(INetworkTransport& transport, ClientSessionDesc desc)
    : m_transport(transport), m_desc(std::move(desc)) {}

ClientSession::~ClientSession() {
    if (m_state == ClientState::Connecting || m_state == ClientState::Handshaking ||
        m_state == ClientState::Connected) {
        disconnect();
    }
}

Expected<void> ClientSession::connect(const Endpoint& to, f64 nowSeconds) {
    if (m_state != ClientState::Idle) {
        return makeError(ErrorCode::InvalidArgument, "ClientSession 은 한 번만 connect 한다 (새로 만드십시오)");
    }
    auto c = m_transport.connect(to);
    if (!c) {
        return std::unexpected(c.error());
    }
    m_connection = *c;
    m_state = ClientState::Connecting;
    m_startedAt = nowSeconds;
    return {};
}

void ClientSession::update(f64 nowSeconds) {
    m_events.clear();
    m_transport.poll(m_events);
    for (auto& ev : m_events) {
        if (ev.connection != m_connection) {
            continue;
        }
        switch (ev.type) {
        case TransportEvent::Type::Connected:
            if (m_state == ClientState::Connecting) {
                m_state = ClientState::Handshaking;
                Hello h;
                h.buildId = m_desc.buildId;
                send(h);
            }
            break;
        case TransportEvent::Type::Received: {
            if (ev.channel != Channel::Control) {
                m_snapshotBytes += ev.data.size();
            }
            auto m = decodeMessage(ev.data);
            if (!m) {
                log::warn("net", "서버 메시지를 읽지 못했습니다: {}", m.error().describe());
                fail(DisconnectReason::ProtocolError);
                break;
            }
            onMessage(*m);
            break;
        }
        case TransportEvent::Type::Disconnected:
            if (m_state != ClientState::Rejected) {
                m_state = ClientState::Disconnected;
                if (m_disconnectReason == DisconnectReason::None) {
                    m_disconnectReason = ev.reason;
                }
            }
            m_connection = kInvalidConnection;
            break;
        }
    }
    if ((m_state == ClientState::Connecting || m_state == ClientState::Handshaking) &&
        nowSeconds - m_startedAt > m_desc.handshakeTimeoutSeconds) {
        fail(DisconnectReason::Timeout);
    }
    m_transport.flush();
}

void ClientSession::onMessage(Message& m) {
    if (auto* r = std::get_if<Reject>(&m)) {
        m_reject = std::move(*r);
        m_state = ClientState::Rejected;
        return; // 서버가 곧 끊는다
    }
    if (auto* d = std::get_if<DisconnectMsg>(&m)) {
        m_disconnectReason = d->reason;
        return;
    }
    switch (m_state) {
    case ClientState::Handshaking:
        if (const auto* ch = std::get_if<Challenge>(&m)) {
            Auth a;
            a.displayName = m_desc.displayName;
            a.contentHash = m_desc.contentHash;
            a.nonce = ch->nonce;
            send(a);
            return;
        }
        if (auto* w = std::get_if<Welcome>(&m)) {
            m_welcome = std::move(*w);
            m_state = ClientState::Connected;
            if (m_desc.catalog != nullptr && m_desc.content != nullptr) {
                auto world = ClientWorld::create(*m_desc.catalog, *m_desc.content, *m_welcome);
                if (world) {
                    m_world = std::move(*world);
                    for (const TerrainChunk& c : m_earlyTerrain) {
                        m_world->apply(c);
                    }
                } else {
                    log::warn("net", "복제 월드를 만들지 못했습니다: {}", world.error().describe());
                }
            }
            m_earlyTerrain.clear();
            return;
        }
        if (std::holds_alternative<Snapshot>(m)) {
            return; // Welcome 보다 먼저 왔다 — 버려도 된다 (ack 하지 않으면 서버가 다음 차분에 담는다)
        }
        if (auto* tc = std::get_if<TerrainChunk>(&m)) {
            m_earlyTerrain.push_back(std::move(*tc));
            return;
        }
        break;
    case ClientState::Connected:
        if (auto* r = std::get_if<CommandResultMsg>(&m)) {
            m_results.push_back(std::move(*r));
            return;
        }
        if (const auto* s = std::get_if<ServerStats>(&m)) {
            m_stats = *s;
            return;
        }
        if (const auto* snap = std::get_if<Snapshot>(&m)) {
            // 복제 월드가 없어도 ack 한다 — 서버가 같은 상태를 계속 다시 보내지 않게
            const bool applied = m_world == nullptr || m_world->apply(*snap);
            if (applied) {
                send(SnapshotAck{snap->epoch, snap->snapshotId});
            }
            return;
        }
        if (const auto* tc = std::get_if<TerrainChunk>(&m)) {
            if (m_world != nullptr) {
                m_world->apply(*tc);
            }
            return;
        }
        if (auto* ir = std::get_if<InspectResult>(&m)) {
            // 요청을 거둔 뒤 늦게 온 것 · 옛것은 버린다
            if (!m_inspectIds.empty() && (!m_inspect || ir->serverTick >= m_inspect->serverTick)) {
                m_inspect = std::move(*ir);
            }
            return;
        }
        break;
    default:
        break;
    }
    log::warn("net", "지금({}) 받을 수 없는 메시지 {} — 끊습니다", clientStateName(m_state), messageName(messageId(m)));
    fail(DisconnectReason::ProtocolError);
}

u32 ClientSession::sendCommand(cmd::CommandPayload payload) {
    if (m_state != ClientState::Connected) {
        return 0;
    }
    const u32 seq = m_nextSequence++;
    send(CommandMsg{seq, std::move(payload)});
    return seq;
}

void ClientSession::setInspect(std::span<const NetEntityId> ids) {
    if (m_state != ClientState::Connected) {
        return;
    }
    const std::span<const NetEntityId> clipped = ids.first(std::min(ids.size(), kMaxInspect));
    if (std::ranges::equal(clipped, m_inspectIds)) {
        return;
    }
    m_inspectIds.assign(clipped.begin(), clipped.end());
    if (m_inspectIds.empty()) {
        m_inspect.reset();
    }
    send(InspectRequest{m_inspectIds});
}

void ClientSession::disconnect() {
    if (m_connection == kInvalidConnection) {
        return;
    }
    send(DisconnectMsg{DisconnectReason::ClientQuit});
    m_transport.disconnect(m_connection, DisconnectReason::ClientQuit);
    m_transport.flush();
    m_connection = kInvalidConnection;
    if (m_state != ClientState::Rejected) {
        m_state = ClientState::Disconnected;
        m_disconnectReason = DisconnectReason::ClientQuit;
    }
}

std::vector<CommandResultMsg> ClientSession::takeResults() {
    std::vector<CommandResultMsg> out;
    out.swap(m_results);
    return out;
}

TransportStats ClientSession::transportStats() const {
    return m_connection == kInvalidConnection ? TransportStats{} : m_transport.stats(m_connection);
}

void ClientSession::send(const Message& m) {
    if (m_connection == kInvalidConnection) {
        return;
    }
    const auto bytes = encodeMessage(m);
    m_transport.send(m_connection, channelOf(m), bytes);
}

void ClientSession::fail(DisconnectReason reason) {
    if (m_connection != kInvalidConnection) {
        m_transport.disconnect(m_connection, reason);
        m_connection = kInvalidConnection;
    }
    if (m_state != ClientState::Rejected) {
        m_state = ClientState::Disconnected;
        m_disconnectReason = reason;
    }
}

} // namespace sbx::net
