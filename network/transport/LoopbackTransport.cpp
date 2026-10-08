#include "network/transport/LoopbackTransport.hpp"

#include <chrono>

#include "foundation/assert/Assert.hpp"

namespace sbx::net {

LoopbackNetwork::~LoopbackNetwork() {
    SBX_ASSERT(m_transports == 0, "LoopbackNetwork 가 LoopbackTransport 보다 먼저 사라졌습니다");
}

LoopbackTransport::LoopbackTransport(LoopbackNetwork& network) : m_network(network) {
    const std::lock_guard lock(m_network.m_mutex);
    ++m_network.m_transports;
}

LoopbackTransport::~LoopbackTransport() {
    const std::lock_guard lock(m_network.m_mutex);
    if (m_listening) {
        m_network.m_listeners.erase(m_listenName);
    }
    for (auto& [id, link] : m_links) {
        link.peer->m_links.erase(link.peerConnection);
        TransportEvent ev;
        ev.type = TransportEvent::Type::Disconnected;
        ev.connection = link.peerConnection;
        ev.reason = DisconnectReason::Timeout;
        link.peer->pushLocked(std::move(ev));
    }
    m_links.clear();
    --m_network.m_transports;
}

Expected<void> LoopbackTransport::listen(const Endpoint& at, u32 maxConnections) {
    const std::lock_guard lock(m_network.m_mutex);
    if (m_listening) {
        return makeError(ErrorCode::AlreadyExists, "이미 listen 중입니다", m_listenName);
    }
    if (at.host.empty()) {
        return makeError(ErrorCode::InvalidArgument, "Loopback 주소 이름이 비었습니다");
    }
    if (!m_network.m_listeners.emplace(at.host, this).second) {
        return makeError(ErrorCode::AlreadyExists, "같은 이름으로 listen 중인 Loopback 이 있습니다", at.host);
    }
    m_listenName = at.host;
    m_port = at.port;
    m_listening = true;
    m_maxConnections = maxConnections;
    return {};
}

Expected<ConnectionId> LoopbackTransport::connect(const Endpoint& to) {
    const std::lock_guard lock(m_network.m_mutex);
    const ConnectionId mine = allocateLocked();
    const auto it = m_network.m_listeners.find(to.host);
    auto fail = [&](DisconnectReason reason) {
        TransportEvent ev;
        ev.type = TransportEvent::Type::Disconnected;
        ev.connection = mine;
        ev.reason = reason;
        pushLocked(std::move(ev));
    };
    if (it == m_network.m_listeners.end() || it->second == this) {
        fail(DisconnectReason::ConnectFailed);
        return mine;
    }
    LoopbackTransport& server = *it->second;
    if (server.m_links.size() >= server.m_maxConnections) {
        fail(DisconnectReason::ServerFull);
        return mine;
    }
    const ConnectionId theirs = server.allocateLocked();
    m_links[mine] = Link{&server, theirs, {}};
    server.m_links[theirs] = Link{this, mine, {}};
    TransportEvent a;
    a.type = TransportEvent::Type::Connected;
    a.connection = mine;
    pushLocked(std::move(a));
    TransportEvent b;
    b.type = TransportEvent::Type::Connected;
    b.connection = theirs;
    server.pushLocked(std::move(b));
    return mine;
}

void LoopbackTransport::send(ConnectionId to, Channel channel, std::span<const std::byte> data) {
    const std::lock_guard lock(m_network.m_mutex);
    const auto it = m_links.find(to);
    if (it == m_links.end()) {
        return;
    }
    Link& link = it->second;
    link.stats.bytesSent += data.size();
    ++link.stats.packetsSent;
    auto& peerLink = link.peer->m_links.at(link.peerConnection);
    peerLink.stats.bytesReceived += data.size();
    ++peerLink.stats.packetsReceived;
    TransportEvent ev;
    ev.type = TransportEvent::Type::Received;
    ev.connection = link.peerConnection;
    ev.channel = channel;
    ev.data.assign(data.begin(), data.end());
    link.peer->pushLocked(std::move(ev));
}

void LoopbackTransport::poll(std::vector<TransportEvent>& out) {
    const std::lock_guard lock(m_network.m_mutex);
    for (auto& ev : m_inbox) {
        out.push_back(std::move(ev));
    }
    m_inbox.clear();
}

void LoopbackTransport::wait(u32 maxMs) {
    std::unique_lock lock(m_network.m_mutex);
    m_inboxReady.wait_for(lock, std::chrono::milliseconds(maxMs), [&] { return !m_inbox.empty(); });
}

void LoopbackTransport::disconnect(ConnectionId connection, DisconnectReason reason) {
    const std::lock_guard lock(m_network.m_mutex);
    const auto it = m_links.find(connection);
    if (it == m_links.end()) {
        return;
    }
    const Link link = it->second;
    m_links.erase(it);
    link.peer->m_links.erase(link.peerConnection);
    TransportEvent ev;
    ev.type = TransportEvent::Type::Disconnected;
    ev.connection = link.peerConnection;
    ev.reason = reason;
    link.peer->pushLocked(std::move(ev));
}

TransportStats LoopbackTransport::stats(ConnectionId connection) const {
    const std::lock_guard lock(m_network.m_mutex);
    const auto it = m_links.find(connection);
    return it == m_links.end() ? TransportStats{} : it->second.stats;
}

usize LoopbackTransport::openConnections() const {
    const std::lock_guard lock(m_network.m_mutex);
    return m_links.size();
}

void LoopbackTransport::pushLocked(TransportEvent event) {
    m_inbox.push_back(std::move(event));
    m_inboxReady.notify_one();
}

} // namespace sbx::net
