#include "network/transport/EnetTransport.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <enet/enet.h>

#include <chrono>
#include <cstdint>
#include <mutex>

namespace sbx::net {

namespace {

std::mutex g_initMutex;
int g_initCount = 0;

// 08 4장: 무응답 15 초
constexpr enet_uint32 kTimeoutLimit = 32;
constexpr enet_uint32 kTimeoutMinimumMs = 5000;
constexpr enet_uint32 kTimeoutMaximumMs = 15000;
constexpr size_t kClientPeers = 8;

ConnectionId peerId(const ENetPeer* peer) noexcept {
    return static_cast<ConnectionId>(reinterpret_cast<std::uintptr_t>(peer->data));
}

void setPeerId(ENetPeer* peer, ConnectionId id) noexcept {
    peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
}

DisconnectReason reasonFromData(enet_uint32 data) noexcept {
    // 0 = ENet 이 스스로 끊음 (시간 초과)
    if (data == 0 || data > static_cast<enet_uint32>(DisconnectReason::ConnectFailed)) {
        return DisconnectReason::Timeout;
    }
    return static_cast<DisconnectReason>(data);
}

Expected<ENetAddress> resolve(const Endpoint& ep, bool forListen) {
    ENetAddress addr{};
    addr.port = ep.port;
    if (forListen && (ep.host.empty() || ep.host == "*" || ep.host == "0.0.0.0")) {
        addr.host = ENET_HOST_ANY;
        return addr;
    }
    if (enet_address_set_host(&addr, ep.host.c_str()) != 0) {
        return makeError(ErrorCode::NotFound, "주소를 찾지 못했습니다", ep.host);
    }
    return addr;
}

} // namespace

struct EnetTransport::Impl {
    struct Conn {
        ENetPeer* peer = nullptr;
        bool connected = false;
        bool closing = false; // 이쪽이 끊는 중 — 이벤트를 위로 올리지 않는다
        TransportStats stats;
    };

    ENetHost* host = nullptr;
    ConnectionId next = 0;
    std::map<ConnectionId, Conn> conns;
    std::vector<TransportEvent> queued;

    void handle(const ENetEvent& ev) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            ConnectionId id = peerId(ev.peer);
            if (id == kInvalidConnection) { // 들어온 연결
                id = ++next;
                setPeerId(ev.peer, id);
                conns[id] = Conn{ev.peer, true, false, {}};
            } else if (auto it = conns.find(id); it != conns.end()) {
                it->second.connected = true;
            } else {
                break;
            }
            enet_peer_timeout(ev.peer, kTimeoutLimit, kTimeoutMinimumMs, kTimeoutMaximumMs);
            TransportEvent e;
            e.type = TransportEvent::Type::Connected;
            e.connection = id;
            queued.push_back(std::move(e));
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            const ConnectionId id = peerId(ev.peer);
            const auto it = conns.find(id);
            if (it != conns.end() && !it->second.closing && ev.channelID < kChannelCount) {
                TransportEvent e;
                e.type = TransportEvent::Type::Received;
                e.connection = id;
                e.channel = static_cast<Channel>(ev.channelID);
                const auto* begin = reinterpret_cast<const std::byte*>(ev.packet->data);
                e.data.assign(begin, begin + ev.packet->dataLength);
                it->second.stats.bytesReceived += ev.packet->dataLength;
                ++it->second.stats.packetsReceived;
                queued.push_back(std::move(e));
            }
            enet_packet_destroy(ev.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            const ConnectionId id = peerId(ev.peer);
            const auto it = conns.find(id);
            if (it != conns.end()) {
                if (!it->second.closing) {
                    TransportEvent e;
                    e.type = TransportEvent::Type::Disconnected;
                    e.connection = id;
                    e.reason = it->second.connected ? reasonFromData(ev.data) : DisconnectReason::ConnectFailed;
                    queued.push_back(std::move(e));
                }
                conns.erase(it);
            }
            ev.peer->data = nullptr;
            break;
        }
        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }

    void service(enet_uint32 timeoutMs) {
        if (host == nullptr) {
            return;
        }
        ENetEvent ev;
        // 첫 호출만 기다리고, 그 뒤는 쌓인 것만
        int r = enet_host_service(host, &ev, timeoutMs);
        while (r > 0) {
            handle(ev);
            r = enet_host_service(host, &ev, 0);
        }
    }
};

EnetTransport::EnetTransport() : m_impl(std::make_unique<Impl>()) {}

Expected<std::unique_ptr<EnetTransport>> EnetTransport::create() {
    {
        const std::lock_guard lock(g_initMutex);
        if (g_initCount == 0 && enet_initialize() != 0) {
            return makeError(ErrorCode::IoError, "ENet 을 초기화하지 못했습니다 (소켓 라이브러리)");
        }
        ++g_initCount;
    }
    return std::unique_ptr<EnetTransport>(new EnetTransport());
}

EnetTransport::~EnetTransport() {
    Impl& im = *m_impl;
    if (im.host != nullptr) {
        drain(300);                 // 끊는 중인 연결이 disconnect 를 실제로 보낼 시간
        enet_host_destroy(im.host); // 남은 peer 는 reset (상대는 시간 초과로 알게 된다)
        im.host = nullptr;
    }
    m_impl.reset();
    const std::lock_guard lock(g_initMutex);
    if (--g_initCount == 0) {
        enet_deinitialize();
    }
}

Expected<void> EnetTransport::listen(const Endpoint& at, u32 maxConnections) {
    Impl& im = *m_impl;
    if (im.host != nullptr) {
        return makeError(ErrorCode::AlreadyExists, "이 ENet Transport 는 이미 호스트가 있습니다");
    }
    auto addr = resolve(at, true);
    if (!addr) {
        return std::unexpected(addr.error());
    }
    im.host = enet_host_create(&*addr, maxConnections, kChannelCount, 0, 0);
    if (im.host == nullptr) {
        return makeError(ErrorCode::IoError, "UDP 포트를 열지 못했습니다 (이미 쓰는 중?)", at.describe());
    }
    m_boundPort = im.host->address.port;
    return {};
}

Expected<ConnectionId> EnetTransport::connect(const Endpoint& to) {
    Impl& im = *m_impl;
    auto addr = resolve(to, false);
    if (!addr) {
        return std::unexpected(addr.error());
    }
    if (addr->port == 0) {
        return makeError(ErrorCode::InvalidArgument, "접속할 포트가 0 입니다", to.describe());
    }
    if (im.host == nullptr) {
        im.host = enet_host_create(nullptr, kClientPeers, kChannelCount, 0, 0);
        if (im.host == nullptr) {
            return makeError(ErrorCode::IoError, "클라이언트 UDP 소켓을 만들지 못했습니다");
        }
    }
    ENetPeer* peer = enet_host_connect(im.host, &*addr, kChannelCount, 0);
    if (peer == nullptr) {
        return makeError(ErrorCode::IoError, "더 접속할 수 없습니다 (peer 슬롯 없음)", to.describe());
    }
    const ConnectionId id = ++im.next;
    setPeerId(peer, id);
    im.conns[id] = Impl::Conn{peer, false, false, {}};
    enet_host_flush(im.host);
    return id;
}

void EnetTransport::send(ConnectionId to, Channel channel, std::span<const std::byte> data) {
    Impl& im = *m_impl;
    const auto it = im.conns.find(to);
    if (it == im.conns.end() || it->second.closing || !it->second.connected) {
        return;
    }
    const enet_uint32 flags = isReliable(channel) ? static_cast<enet_uint32>(ENET_PACKET_FLAG_RELIABLE) : 0u;
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
    if (packet == nullptr) {
        return;
    }
    if (enet_peer_send(it->second.peer, static_cast<enet_uint8>(channel), packet) < 0) {
        enet_packet_destroy(packet);
        return;
    }
    it->second.stats.bytesSent += data.size();
    ++it->second.stats.packetsSent;
}

void EnetTransport::drain(u32 maxMs) {
    Impl& im = *m_impl;
    if (im.host == nullptr) {
        return;
    }
    enet_host_flush(im.host);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
    while (std::chrono::steady_clock::now() < deadline) {
        bool anyClosing = false;
        for (const auto& [id, c] : im.conns) {
            anyClosing = anyClosing || c.closing;
        }
        if (!anyClosing) {
            break;
        }
        im.service(10);
    }
    // 끊는 중 받은 이벤트는 의미가 없다 (끊기가 끝난 연결 · 마지막 패킷)
    std::erase_if(im.queued, [&](const TransportEvent& e) { return !im.conns.contains(e.connection); });
}

void EnetTransport::flush() {
    if (m_impl->host != nullptr) {
        enet_host_flush(m_impl->host);
    }
}

void EnetTransport::poll(std::vector<TransportEvent>& out) {
    Impl& im = *m_impl;
    im.service(0);
    for (auto& e : im.queued) {
        out.push_back(std::move(e));
    }
    im.queued.clear();
}

void EnetTransport::wait(u32 maxMs) {
    Impl& im = *m_impl;
    if (!im.queued.empty()) {
        return;
    }
    if (im.host == nullptr) {
        INetworkTransport::wait(maxMs);
        return;
    }
    im.service(maxMs);
}

void EnetTransport::disconnect(ConnectionId connection, DisconnectReason reason) {
    Impl& im = *m_impl;
    const auto it = im.conns.find(connection);
    if (it == im.conns.end() || it->second.closing) {
        return;
    }
    it->second.closing = true;
    ENetPeer* peer = it->second.peer;
    enet_peer_disconnect_later(peer, static_cast<enet_uint32>(reason));
    if (peer->state == ENET_PEER_STATE_DISCONNECTED) {
        // 아직 연결 전이었다 — ENet 이 바로 reset 했고 이벤트는 오지 않는다
        peer->data = nullptr;
        im.conns.erase(it);
    }
    enet_host_flush(im.host);
}

TransportStats EnetTransport::stats(ConnectionId connection) const {
    const Impl& im = *m_impl;
    const auto it = im.conns.find(connection);
    if (it == im.conns.end()) {
        return {};
    }
    TransportStats s = it->second.stats;
    const ENetPeer* peer = it->second.peer;
    s.rttMs = static_cast<f32>(peer->roundTripTime);
    s.rttVarianceMs = static_cast<f32>(peer->roundTripTimeVariance);
    s.packetLoss = static_cast<f32>(peer->packetLoss) / static_cast<f32>(ENET_PEER_PACKET_LOSS_SCALE);
    return s;
}

} // namespace sbx::net
