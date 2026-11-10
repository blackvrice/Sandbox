#include "network/transport/SimulatedTransport.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>

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

void SimulatedTransport::drain(u32 maxMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
    pump();
    while (!m_queue.empty() && std::chrono::steady_clock::now() < deadline) {
        m_inner.wait(1); // 안쪽을 돌리며 (ENet 서비스) 1 ms 씩
        pump();
    }
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    m_inner.drain(static_cast<u32>(std::max<i64>(left.count(), 0)));
}

TransportStats SimulatedTransport::stats(ConnectionId connection) const {
    TransportStats s = m_inner.stats(connection);
    s.rttMs += static_cast<f32>(m_desc.latencyMs); // 이쪽 방향만 흉내 내므로 한 번만 더한다
    return s;
}

// ---- --net-sim (v0.8 ST-06-9) ----

Expected<SimulatedLinkDesc> parseSimulatedLink(std::string_view spec) {
    if (spec.empty()) {
        return makeError(ErrorCode::InvalidArgument, "--net-sim 이 비었다 (예: latency=80,jitter=10,loss=0.05)");
    }
    SimulatedLinkDesc d;
    usize begin = 0;
    while (begin <= spec.size()) {
        usize end = spec.find(',', begin);
        if (end == std::string_view::npos) {
            end = spec.size();
        }
        const std::string_view item = spec.substr(begin, end - begin);
        const usize eq = item.find('=');
        const std::string_view key = item.substr(0, eq);
        const std::string_view text = eq == std::string_view::npos ? std::string_view() : item.substr(eq + 1);
        f64 v = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
        if (text.empty() || ec != std::errc() || ptr != text.data() + text.size()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("--net-sim '{}' — <키>=<수> (latency · jitter · loss · kbps)", item));
        }
        const auto range = [&](f64 lo, f64 hi) -> Expected<void> {
            if (!(v >= lo && v <= hi)) {
                return makeError(ErrorCode::InvalidArgument, std::format("--net-sim {} 은 {} ~ {}", key, lo, hi));
            }
            return {};
        };
        Expected<void> ok;
        if (key == "latency") {
            ok = range(0, 2000);
            d.latencyMs = v;
        } else if (key == "jitter") {
            ok = range(0, 2000);
            d.jitterMs = v;
        } else if (key == "loss") {
            ok = range(0, 0.5);
            d.lossRate = v;
        } else if (key == "kbps") {
            ok = range(0, 1000000);
            d.bandwidthBytesPerSec = v * 1000.0 / 8.0;
        } else {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("--net-sim 의 모르는 키 '{}' (latency · jitter · loss · kbps)", key));
        }
        if (!ok) {
            return std::unexpected(ok.error());
        }
        begin = end + 1;
    }
    return d;
}

std::string describeSimulatedLink(const SimulatedLinkDesc& d) {
    std::string s = std::format("지연 {:.0f}±{:.0f} ms · 손실 {:.1f}%", d.latencyMs, d.jitterMs, d.lossRate * 100.0);
    if (d.bandwidthBytesPerSec > 0) {
        s += std::format(" · {:.0f} kbps", d.bandwidthBytesPerSec * 8.0 / 1000.0);
    }
    return s;
}

namespace {

// 안쪽을 갖고 SimulatedTransport 에 넘긴다 — 선언 순서가 소멸 순서를 정한다 (흉내 층이 먼저 없어진다)
class OwnedSimulatedTransport final : public INetworkTransport {
public:
    OwnedSimulatedTransport(std::unique_ptr<INetworkTransport> inner, SimulatedLinkDesc desc)
        : m_inner(std::move(inner)), m_sim(*m_inner, desc) {}
    [[nodiscard]] Expected<void> listen(const Endpoint& at, u32 maxConnections) override {
        return m_sim.listen(at, maxConnections);
    }
    [[nodiscard]] u16 boundPort() const noexcept override { return m_sim.boundPort(); }
    [[nodiscard]] Expected<ConnectionId> connect(const Endpoint& to) override { return m_sim.connect(to); }
    void send(ConnectionId to, Channel channel, std::span<const std::byte> data) override {
        m_sim.send(to, channel, data);
    }
    void flush() override { m_sim.flush(); }
    void poll(std::vector<TransportEvent>& out) override { m_sim.poll(out); }
    void wait(u32 maxMs) override { m_sim.wait(maxMs); }
    void disconnect(ConnectionId connection, DisconnectReason reason) override { m_sim.disconnect(connection, reason); }
    void drain(u32 maxMs) override { m_sim.drain(maxMs); }
    [[nodiscard]] TransportStats stats(ConnectionId connection) const override { return m_sim.stats(connection); }
    [[nodiscard]] std::string_view name() const noexcept override { return m_inner->name(); }

private:
    std::unique_ptr<INetworkTransport> m_inner;
    SimulatedTransport m_sim;
};

} // namespace

std::unique_ptr<INetworkTransport> wrapSimulated(std::unique_ptr<INetworkTransport> inner, SimulatedLinkDesc desc) {
    return std::make_unique<OwnedSimulatedTransport>(std::move(inner), desc);
}

} // namespace sbx::net
