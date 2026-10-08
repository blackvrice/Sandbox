#include "network/transport/SimulatedTransport.hpp"

#include <algorithm>
#include <chrono>

namespace sbx::net {

namespace {

u64 splitmix64(u64& state) noexcept {
    u64 z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

f64 steadyMs() {
    using namespace std::chrono;
    return duration<f64, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

SimulatedTransport::SimulatedTransport(INetworkTransport& inner, SimulatedLinkDesc desc, Clock now)
    : m_inner(inner), m_desc(desc), m_now(now ? std::move(now) : Clock(steadyMs)), m_rng(desc.seed) {}

f64 SimulatedTransport::random01() {
    return static_cast<f64>(splitmix64(m_rng) >> 11) * (1.0 / 9007199254740992.0);
}

void SimulatedTransport::send(ConnectionId to, Channel channel, std::span<const std::byte> data) {
    const f64 now = m_now();
    LinkState& link = m_links[to];
    Pending p;
    p.order = ++m_order;
    p.connection = to;
    p.channel = channel;
    p.data.assign(data.begin(), data.end());

    f64 at = now + m_desc.latencyMs + (m_desc.jitterMs > 0 ? (random01() * 2 - 1) * m_desc.jitterMs : 0.0);
    if (m_desc.bandwidthBytesPerSec > 0) {
        // 한 링크로 보낸다고 보고 직렬화 시간을 줄 세운다
        m_linkBusyUntil =
            std::max(m_linkBusyUntil, now) + static_cast<f64>(data.size()) * 1000.0 / m_desc.bandwidthBytesPerSec;
        at += m_linkBusyUntil - now;
    }
    const bool lost = m_desc.lossRate > 0 && random01() < m_desc.lossRate;
    if (isReliable(channel)) {
        if (lost) {
            at += std::max(m_desc.latencyMs * 2, 1.0); // 재전송
            ++m_stats.retransmitted;
        }
        auto& last = link.lastReliableAt[static_cast<u8>(channel)];
        at = std::max(at, last); // 신뢰 채널은 순서를 지킨다
        last = at;
    } else {
        p.snapshotSequence = link.nextSnapshotSequence++;
        if (lost) {
            ++m_stats.dropped;
            return;
        }
    }
    p.deliverAt = std::max(at, now);
    link.lastAnyAt = std::max(link.lastAnyAt, p.deliverAt);
    if (p.deliverAt > now) {
        ++m_stats.delayed;
    }
    const auto pos = std::upper_bound(m_queue.begin(), m_queue.end(), p, [](const Pending& a, const Pending& b) {
        return a.deliverAt != b.deliverAt ? a.deliverAt < b.deliverAt : a.order < b.order;
    });
    m_queue.insert(pos, std::move(p));
}

void SimulatedTransport::disconnect(ConnectionId connection, DisconnectReason reason) {
    // 쌓인 것을 다 보낸 뒤에 끊는다
    Pending p;
    p.order = ++m_order;
    p.connection = connection;
    p.disconnect = true;
    p.reason = reason;
    const auto it = m_links.find(connection);
    p.deliverAt = std::max(m_now(), it == m_links.end() ? 0.0 : it->second.lastAnyAt);
    const auto pos = std::upper_bound(m_queue.begin(), m_queue.end(), p, [](const Pending& a, const Pending& b) {
        return a.deliverAt != b.deliverAt ? a.deliverAt < b.deliverAt : a.order < b.order;
    });
    m_queue.insert(pos, std::move(p));
    pump();
}

void SimulatedTransport::pump() {
    const f64 now = m_now();
    while (!m_queue.empty() && m_queue.front().deliverAt <= now) {
        Pending p = std::move(m_queue.front());
        m_queue.pop_front();
        if (p.disconnect) {
            m_inner.disconnect(p.connection, p.reason);
            m_links.erase(p.connection);
            continue;
        }
        if (!isReliable(p.channel)) {
            LinkState& link = m_links[p.connection];
            if (p.snapshotSequence < link.lastForwardedSnapshot) {
                ++m_stats.droppedStale;
                continue;
            }
            link.lastForwardedSnapshot = p.snapshotSequence;
        }
        m_inner.send(p.connection, p.channel, p.data);
    }
}

void SimulatedTransport::flush() {
    pump();
    m_inner.flush();
}

void SimulatedTransport::poll(std::vector<TransportEvent>& out) {
    pump();
    m_inner.flush();
    const usize before = out.size();
    m_inner.poll(out);
    for (usize i = before; i < out.size(); ++i) {
        if (out[i].type == TransportEvent::Type::Disconnected) {
            // 상대가 끊었다 — 이 연결로 보낼 것은 버린다
            const ConnectionId c = out[i].connection;
            std::erase_if(m_queue, [c](const Pending& p) { return p.connection == c; });
            m_links.erase(c);
        }
    }
}

void SimulatedTransport::wait(u32 maxMs) {
    m_inner.wait(m_queue.empty() ? maxMs : std::min<u32>(maxMs, 1));
}

TransportStats SimulatedTransport::stats(ConnectionId connection) const {
    TransportStats s = m_inner.stats(connection);
    s.rttMs += static_cast<f32>(m_desc.latencyMs); // 이쪽 방향만 흉내 내므로 한 번만 더한다
    return s;
}

} // namespace sbx::net
