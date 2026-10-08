#include "network/server/ServerHost.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <format>

#include "foundation/assert/Assert.hpp"
#include "foundation/log/Log.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::net {

namespace {

constexpr usize kMaxCreatedInResult = 4096;

f64 steadySeconds() {
    using namespace std::chrono;
    return duration<f64>(steady_clock::now().time_since_epoch()).count();
}

// 바이트 상한 안에서 코드 포인트 경계로 자른다
std::string clampUtf8(std::string_view s, usize maxBytes) {
    if (s.size() <= maxBytes) {
        return std::string(s);
    }
    usize pos = 0;
    usize keep = 0;
    while (pos < s.size()) {
        usize next = pos;
        (void)utf8::decodeNext(s, next);
        if (next > maxBytes) {
            break;
        }
        keep = next;
        pos = next;
    }
    return std::string(s.substr(0, keep));
}

} // namespace

ServerHost::ServerHost(INetworkTransport& transport, std::unique_ptr<scenario::ScenarioRunner> runner,
                       ServerHostDesc desc)
    : m_transport(transport), m_runner(std::move(runner)), m_desc(std::move(desc)), m_validator(m_desc.validator),
      m_random(m_desc.randomSeed != 0 ? m_desc.randomSeed : std::random_device{}()) {
    SBX_ASSERT(m_runner != nullptr, "ServerHost: 월드가 없습니다");
    const auto& world = m_runner->world();
    m_meta.name = clampUtf8(m_desc.worldName, kMaxWorldNameBytes);
    m_meta.minChunkX = world.desc().bounds.minChunk.x;
    m_meta.minChunkY = world.desc().bounds.minChunk.y;
    m_meta.maxChunkX = world.desc().bounds.maxChunk.x;
    m_meta.maxChunkY = world.desc().bounds.maxChunk.y;
    m_contentHash = world.content().contentHash();
    if (m_desc.replicate) {
        ReplicationDesc rd;
        const u32 interval = std::max<u32>(1, m_desc.snapshotIntervalSteps);
        // 초당 예산 → 스냅숏당 (스냅숏 빈도 = 30 TPS ÷ 간격, 속도 배율과 무관하게 셈)
        rd.bytesPerSnapshot = m_desc.snapshotBytesPerSecond * interval / sim::kTickRate;
        m_replication = std::make_unique<ReplicationWriter>(world.catalog(), rd);
    }
    publishStatus();
}

ServerHost::~ServerHost() {
    stop();
}

Expected<void> ServerHost::start(const Endpoint& at) {
    SBX_ASSERT(!m_started, "ServerHost::start 를 두 번 불렀습니다");
    // Transport 상한은 조금 넉넉히 — 넘친 접속에 Reject{ServerFull} 을 말로 알려 주려면 일단 받아야 한다
    if (auto r = m_transport.listen(at, m_desc.maxClients + 4); !r) {
        return r;
    }
    m_started = true;
    const f64 interval = static_cast<f64>(m_runner->world().clock().tickIntervalNanos()) / 1e9;
    log::info("net", "서버 시작: {} · {} {} 포트 {} · 최대 {} 명 · 기본 역할 {}", m_meta.name, m_transport.name(),
              at.host.empty() ? "*" : at.host, m_transport.boundPort(), m_desc.maxClients,
              roleName(m_desc.defaultRole));
    if (m_desc.mode == ServerMode::Threaded) {
        m_simThread = std::thread([this] { simThreadMain(); });
        m_netThread = std::thread([this] { netThreadMain(); });
    } else {
        m_nextTickAt = interval;
        m_statsWindowStart = 0;
    }
    return {};
}

void ServerHost::update(f64 dtSeconds) {
    SBX_ASSERT(m_desc.mode == ServerMode::Inline, "update 는 Inline 전용입니다");
    if (!running()) {
        return;
    }
    m_inlineTime += dtSeconds;
    const f64 now = m_inlineTime;
    netStep(now);
    int ran = 0;
    while (m_nextTickAt <= now && ran < kMaxCatchUpTicks) {
        simStep(now);
        m_nextTickAt += static_cast<f64>(m_runner->world().clock().tickIntervalNanos()) / 1e9;
        ++ran;
    }
    if (m_nextTickAt <= now) { // 너무 밀렸다 — 몰아 돌지 않고 기준점을 다시 잡는다
        m_nextTickAt = now;
        const std::lock_guard lock(m_mutex);
        ++m_stats.overruns;
    }
    netStep(now); // 이번 틱의 결과를 바로 보낸다
}

void ServerHost::stop() {
    if (!m_started || m_stopped) {
        return;
    }
    if (m_desc.mode == ServerMode::Threaded) {
        m_quitSim = true;
        if (m_simThread.joinable()) {
            m_simThread.join();
        }
        m_quitNet = true;
        if (m_netThread.joinable()) {
            m_netThread.join();
        }
    }
    // Net IO 스레드가 끝났으니 이 스레드가 Transport 를 이어받는다 (한 번에 한 스레드)
    netShutdown();
    m_stopped = true;
    const auto s = stats();
    log::info("net", "서버 멈춤: tick {} · 수락한 명령 {} · 거절 {}", s.tick, s.commandsAccepted, s.commandsRejected);
}

ServerHostStats ServerHost::stats() const {
    const std::lock_guard lock(m_mutex);
    return m_stats;
}

sim::SimulationWorld& ServerHost::world() {
    SBX_ASSERT(m_desc.mode == ServerMode::Inline || !running(),
               "Threaded 서버의 월드는 멈춘 뒤에만 볼 수 있습니다 (T1)");
    return m_runner->world();
}

// ---------------------------------------------------------------------------------------------------------------
// Net 절반
// ---------------------------------------------------------------------------------------------------------------

void ServerHost::netStep(f64 now) {
    m_events.clear();
    m_transport.poll(m_events);
    for (auto& ev : m_events) {
        switch (ev.type) {
        case TransportEvent::Type::Connected: {
            Client c;
            c.connectedAt = now;
            auto [it, inserted] = m_clients.emplace(ev.connection, std::move(c));
            if (m_clients.size() > m_desc.maxClients) {
                reject(ev.connection, it->second, RejectReason::ServerFull,
                       std::format("최대 {} 명", m_desc.maxClients));
            }
            break;
        }
        case TransportEvent::Type::Received: {
            const auto it = m_clients.find(ev.connection);
            if (it != m_clients.end()) {
                onReceived(ev.connection, it->second, ev.data, now);
            }
            break;
        }
        case TransportEvent::Type::Disconnected: {
            const auto it = m_clients.find(ev.connection);
            if (it == m_clients.end()) {
                break;
            }
            if (it->second.state == Client::State::Active) {
                log::info("net", "나감: {} (#{}) — {}", it->second.name, it->second.clientId,
                          disconnectReasonName(ev.reason));
                m_byClientId.erase(it->second.clientId);
                const std::lock_guard lock(m_mutex);
                m_roster.push_back(RosterEvent{it->second.clientId, false});
            }
            m_clients.erase(it);
            break;
        }
        }
    }
    // 핸드셰이크 시간 초과
    std::vector<ConnectionId> late;
    for (const auto& [conn, c] : m_clients) {
        if (c.state != Client::State::Active && now - c.connectedAt > m_desc.handshakeTimeoutSeconds) {
            late.push_back(conn);
        }
    }
    for (const ConnectionId conn : late) {
        reject(conn, m_clients.at(conn), RejectReason::BadHandshake, "핸드셰이크 시간 초과");
    }
    flushOutbox();
    {
        const std::lock_guard lock(m_mutex);
        m_stats.clients = m_byClientId.size();
        m_stats.connections = m_clients.size();
    }
    m_transport.flush();
}

void ServerHost::onReceived(ConnectionId conn, Client& client, std::span<const std::byte> data, f64 now) {
    auto decoded = decodeMessage(data);
    if (!decoded) {
        log::warn("net", "연결 {} 의 메시지를 읽지 못해 끊습니다: {}", conn, decoded.error().describe());
        dropConnection(conn, DisconnectReason::ProtocolError);
        return;
    }
    Message& msg = *decoded;
    switch (client.state) {
    case Client::State::AwaitHello: {
        const auto* hello = std::get_if<Hello>(&msg);
        if (hello == nullptr) {
            reject(conn, client, RejectReason::BadHandshake, "Hello 를 먼저 보내야 합니다");
            return;
        }
        if (hello->protocolVersion != kProtocolVersion) {
            reject(conn, client, RejectReason::VersionMismatch,
                   std::format("서버 프로토콜 {}, 클라이언트 {}", kProtocolVersion, hello->protocolVersion));
            return;
        }
        if (hello->buildId != m_desc.buildId) {
            log::warn("net", "연결 {}: 빌드가 다릅니다 (서버 {:016x}, 클라이언트 {:016x}) — 프로토콜이 같아 계속", conn,
                      m_desc.buildId, hello->buildId);
        }
        client.nonce = m_random();
        client.state = Client::State::AwaitAuth;
        sendTo(conn, Challenge{client.nonce});
        return;
    }
    case Client::State::AwaitAuth: {
        auto* auth = std::get_if<Auth>(&msg);
        if (auth == nullptr || auth->nonce != client.nonce) {
            reject(conn, client, RejectReason::BadHandshake,
                   auth == nullptr ? "Auth 를 기다립니다" : "nonce 가 다릅니다");
            return;
        }
        if (!isValidDisplayName(auth->displayName)) {
            reject(conn, client, RejectReason::InvalidName, "이름은 1 ~ 32 글자, 제어 문자 없이");
            return;
        }
        if (auth->contentHash != m_contentHash) {
            reject(conn, client, RejectReason::ContentMismatch,
                   std::format("서버 콘텐츠 해시 {:016x}, 클라이언트 {:016x}", m_contentHash, auth->contentHash));
            return;
        }
        if (m_nextClientId == 0) { // u16 을 다 썼다 (세션 안 재사용 없음)
            reject(conn, client, RejectReason::ServerFull, "클라이언트 번호를 다 썼습니다");
            return;
        }
        client.clientId = m_nextClientId++;
        client.role = m_desc.defaultRole;
        client.name = std::move(auth->displayName);
        client.state = Client::State::Active;
        m_byClientId[client.clientId] = conn;

        Welcome w;
        w.clientId = client.clientId;
        w.role = client.role;
        w.world = m_meta;
        {
            const std::lock_guard lock(m_mutex);
            w.serverTick = m_status.tick;
            w.world.paused = m_status.paused;
            w.world.speed = m_status.speed;
        }
        w.tickRate = static_cast<u8>(sim::kTickRate);
        w.sessionToken.resize(kSessionTokenBytes);
        for (auto& b : w.sessionToken) {
            b = static_cast<std::byte>(m_random() & 0xFF);
        }
        if (m_replication) {
            w.replicated = m_replication->table(); // 만든 뒤 바뀌지 않는다 (스레드 사이 읽기 안전)
            w.snapshotRate = static_cast<u8>(sim::kTickRate / std::max<u32>(1, m_desc.snapshotIntervalSteps));
        } else {
            w.snapshotRate = 0;
        }
        sendTo(conn, w);
        {
            const std::lock_guard lock(m_mutex);
            m_roster.push_back(RosterEvent{client.clientId, true});
        }
        log::info("net", "들어옴: {} (#{}, {}) — 연결 {}", client.name, client.clientId, roleName(client.role), conn);
        return;
    }
    case Client::State::Active:
        break;
    }

    if (auto* command = std::get_if<CommandMsg>(&msg)) {
        if (auto ok = m_validator.check(client.commands, client.role, command->sequence, command->payload, now); !ok) {
            CommandResultMsg r;
            r.sequence = command->sequence;
            r.accepted = false;
            r.reason = ok.error().code;
            r.detail = clampUtf8(ok.error().message, kMaxDetailBytes);
            sendTo(conn, r);
            const std::lock_guard lock(m_mutex);
            ++m_stats.commandsRejected;
            return;
        }
        const std::lock_guard lock(m_mutex);
        m_inbox.push_back(ValidatedCommand{client.clientId, command->sequence, std::move(command->payload)});
        return;
    }
    if (const auto* ack = std::get_if<SnapshotAck>(&msg)) {
        const std::lock_guard lock(m_mutex);
        m_acks.emplace_back(client.clientId, *ack);
        return;
    }
    if (std::holds_alternative<DisconnectMsg>(msg)) {
        // 클라이언트가 곧 끊는다 — Transport 의 Disconnected 로 정리된다
        return;
    }
    log::warn("net", "{} (#{}) 이 보낼 수 없는 메시지 {} — 끊습니다", client.name, client.clientId,
              messageName(messageId(msg)));
    dropConnection(conn, DisconnectReason::ProtocolError);
}

void ServerHost::sendTo(ConnectionId conn, const Message& m) {
    const auto bytes = encodeMessage(m);
    m_transport.send(conn, channelOf(m), bytes);
}

void ServerHost::reject(ConnectionId conn, Client& client, RejectReason reason, std::string detail) {
    (void)client;
    Reject r;
    r.reason = reason;
    r.detail = clampUtf8(detail, kMaxDetailBytes);
    r.contentHash = m_contentHash;
    for (const auto& p : m_desc.packs) {
        if (r.packs.size() < kMaxPacks) {
            r.packs.push_back(clampUtf8(p, kMaxPackIdBytes));
        }
    }
    sendTo(conn, r);
    log::info("net", "연결 {} 거절: {} — {}", conn, rejectReasonName(reason), detail);
    {
        const std::lock_guard lock(m_mutex);
        ++m_stats.handshakesRejected;
    }
    dropConnection(conn, DisconnectReason::Rejected);
}

void ServerHost::dropConnection(ConnectionId conn, DisconnectReason reason) {
    m_transport.disconnect(conn, reason);
    const auto it = m_clients.find(conn);
    if (it != m_clients.end()) {
        if (it->second.state == Client::State::Active) {
            m_byClientId.erase(it->second.clientId);
            const std::lock_guard lock(m_mutex);
            m_roster.push_back(RosterEvent{it->second.clientId, false});
        }
        m_clients.erase(it);
    }
}

void ServerHost::flushOutbox() {
    std::vector<Outgoing> out;
    {
        const std::lock_guard lock(m_mutex);
        out.swap(m_outbox);
    }
    for (auto& o : out) {
        if (auto* st = std::get_if<ServerStats>(&o.message)) {
            st->clients = static_cast<u8>(std::min<usize>(m_byClientId.size(), 255));
        }
        if (o.clientId == 0) {
            const auto bytes = encodeMessage(o.message);
            for (const auto& [id, conn] : m_byClientId) {
                m_transport.send(conn, channelOf(o.message), bytes);
            }
        } else if (const auto it = m_byClientId.find(o.clientId); it != m_byClientId.end()) {
            sendTo(it->second, o.message);
        }
    }
}

void ServerHost::netShutdown() {
    flushOutbox(); // 마지막 결과까지
    const auto bytes = encodeMessage(DisconnectMsg{DisconnectReason::ServerShutdown});
    for (const auto& [conn, c] : m_clients) {
        m_transport.send(conn, Channel::Control, bytes);
        m_transport.disconnect(conn, DisconnectReason::ServerShutdown);
    }
    m_clients.clear();
    m_byClientId.clear();
    m_transport.flush();
    m_transport.drain(300); // 끊기가 상대에게 닿게 (ENet 은 상대의 확인을 받아야 끝난다)
    const std::lock_guard lock(m_mutex);
    m_stats.clients = 0;
    m_stats.connections = 0;
}

void ServerHost::netThreadMain() {
    while (!m_quitNet.load(std::memory_order_acquire)) {
        m_transport.wait(2);
        netStep(steadySeconds());
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Sim 절반
// ---------------------------------------------------------------------------------------------------------------

void ServerHost::simStep(f64 now) {
    std::vector<ValidatedCommand> commands;
    std::vector<RosterEvent> roster;
    std::vector<std::pair<u16, SnapshotAck>> acks;
    {
        const std::lock_guard lock(m_mutex);
        commands.swap(m_inbox);
        roster.swap(m_roster);
        acks.swap(m_acks);
    }
    if (m_replication) {
        for (const RosterEvent& r : roster) {
            r.joined ? m_replication->addClient(r.clientId) : m_replication->removeClient(r.clientId);
        }
        for (const auto& [id, a] : acks) {
            m_replication->onAck(id, a.epoch, a.snapshotId);
        }
    }
    auto& world = m_runner->world();
    if (m_desc.stopAtTick != 0 && world.currentTick() >= m_desc.stopAtTick) {
        // 멈춘 뒤 온 명령은 적용하지 않는다 — 말없이 버리지 않고 거절을 알린다 (M6)
        for (auto& c : commands) {
            CommandResultMsg r;
            r.sequence = c.sequence;
            r.reason = ErrorCode::Unsupported;
            r.detail = "서버가 종료 틱에 도달했습니다";
            const std::lock_guard lock(m_mutex);
            m_outbox.push_back(Outgoing{c.issuer, r});
            ++m_stats.commandsRejected;
        }
        const std::lock_guard lock(m_mutex);
        m_stats.reachedStopTick = true;
        return;
    }
    // M4: executeTick 은 서버가 정한다 — 다음 tick() 이 적용할 틱 (일시정지 편집 단계도 "다음 틱" 몫을 적용한다)
    for (auto& c : commands) {
        m_awaitingResult.emplace(c.issuer, c.sequence);
        world.enqueue(
            cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, c.issuer, c.sequence}, std::move(c.payload)});
    }
    const sim::Tick before = world.currentTick();
    const f64 t0 = steadySeconds();
    m_runner->step();
    const f64 ms = (steadySeconds() - t0) * 1000.0;
    const bool advanced = world.currentTick() != before;

    std::vector<Outgoing> results;
    u64 accepted = 0;
    u64 rejected = 0;
    for (const auto& res : world.lastResults()) {
        if (m_awaitingResult.erase({res.issuer, res.sequence}) == 0) {
            continue; // 시나리오가 넣은 명령
        }
        CommandResultMsg r;
        r.sequence = res.sequence;
        r.accepted = res.accepted;
        r.reason = res.accepted ? ErrorCode::Unknown : res.error.code;
        r.detail = res.accepted ? std::string{} : clampUtf8(res.error.message, kMaxDetailBytes);
        r.appliedTick = res.appliedTick;
        r.created.assign(res.created.begin(), res.created.begin() + static_cast<std::ptrdiff_t>(std::min(
                                                                        res.created.size(), kMaxCreatedInResult)));
        (res.accepted ? accepted : rejected) += 1;
        results.push_back(Outgoing{res.issuer, std::move(r)});
    }

    if (advanced) {
        m_tickMsSum += ms;
        m_tickMsMax = std::max(m_tickMsMax, ms);
        ++m_ticksInWindow;
    }
    std::optional<ServerStats> statsMsg;
    if (now - m_statsWindowStart >= m_desc.statsIntervalSeconds) {
        ServerStats s;
        s.serverTick = world.currentTick();
        s.entities = static_cast<u32>(world.registry().aliveCount());
        s.tickMsAvg = m_ticksInWindow > 0 ? static_cast<f32>(m_tickMsSum / m_ticksInWindow) : 0.0f;
        s.tickMsMax = static_cast<f32>(m_tickMsMax);
        const f64 span = now - m_statsWindowStart;
        s.ticksPerSecond = span > 0 ? static_cast<f32>(m_ticksInWindow / span) : 0.0f;
        s.paused = world.clock().paused();
        s.speed = world.clock().speed();
        statsMsg = s;
        m_statsWindowStart = now;
        m_tickMsSum = 0;
        m_tickMsMax = 0;
        m_ticksInWindow = 0;
    }

    // 복제: snapshotIntervalSteps 단계마다 (일시정지 편집 단계도 센다 — 멈춘 월드의 편집도 보이게)
    m_replicationOut.clear();
    ++m_steps;
    f64 replicationMs = -1;
    if (m_replication && m_steps % std::max<u32>(1, m_desc.snapshotIntervalSteps) == 0) {
        const f64 r0 = steadySeconds();
        m_replication->build(world, m_replicationOut);
        replicationMs = (steadySeconds() - r0) * 1000.0;
    }

    const std::lock_guard lock(m_mutex);
    for (auto& r : results) {
        m_outbox.push_back(std::move(r));
    }
    for (auto& [id, m] : m_replicationOut) {
        m_outbox.push_back(Outgoing{id, std::move(m)});
    }
    if (statsMsg) {
        m_outbox.push_back(Outgoing{0, *statsMsg});
    }
    m_stats.commandsAccepted += accepted;
    m_stats.commandsRejected += rejected;
    if (replicationMs >= 0 && !m_replicationOut.empty()) {
        m_stats.replicationMs =
            m_stats.snapshotsBuilt == 0 ? replicationMs : m_stats.replicationMs * 0.9 + replicationMs * 0.1;
        ++m_stats.snapshotsBuilt;
    }
    if (advanced) {
        ++m_stats.ticksRun;
    }
    m_status.tick = world.currentTick();
    m_status.paused = world.clock().paused();
    m_status.speed = world.clock().speed();
    m_stats.tick = m_status.tick;
    m_stats.paused = m_status.paused;
    m_stats.speed = m_status.speed;
    if (m_desc.stopAtTick != 0 && world.currentTick() >= m_desc.stopAtTick) {
        m_stats.reachedStopTick = true;
    }
}

void ServerHost::publishStatus() {
    const auto& world = m_runner->world();
    const std::lock_guard lock(m_mutex);
    m_status.tick = world.currentTick();
    m_status.paused = world.clock().paused();
    m_status.speed = world.clock().speed();
    m_stats.tick = m_status.tick;
    m_stats.paused = m_status.paused;
    m_stats.speed = m_status.speed;
}

void ServerHost::simThreadMain() {
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    m_statsWindowStart = steadySeconds();
    std::mutex sleepMutex;
    std::condition_variable never;
    while (!m_quitSim.load(std::memory_order_acquire)) {
        const auto interval = std::chrono::nanoseconds(m_runner->world().clock().tickIntervalNanos());
        next += interval;
        auto now = Clock::now();
        // 잠깐씩 끊어 자면서 종료 요청을 본다
        while (now < next && !m_quitSim.load(std::memory_order_acquire)) {
            std::unique_lock lock(sleepMutex);
            never.wait_for(lock, std::min<Clock::duration>(next - now, std::chrono::milliseconds(5)));
            now = Clock::now();
        }
        if (m_quitSim.load(std::memory_order_acquire)) {
            break;
        }
        if (now - next > interval * kMaxCatchUpTicks) {
            next = now; // 너무 밀렸다 — 몰아 돌지 않는다 (03 3장)
            const std::lock_guard lock(m_mutex);
            ++m_stats.overruns;
        }
        simStep(steadySeconds());
    }
}

} // namespace sbx::net
