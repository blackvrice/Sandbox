#include "network/server/ServerHost.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <format>
#include <utility>

#include "core/game/GameRules.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/assert/Assert.hpp"
#include "foundation/log/Log.hpp"
#include "foundation/text/Utf8.hpp"
#include "network/replication/Inspect.hpp"

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

ContentOverlayItem overlayItem(content::ContentKind kind, const std::string& id, const nlohmann::json* value) {
    return ContentOverlayItem{kind, id, value != nullptr ? value->dump() : std::string{}};
}

// 들어온 클라이언트에 보낼 전체 (reset)
ContentOverlayMsg fullOverlay(const content::ContentOverlay& overlay) {
    ContentOverlayMsg m;
    m.reset = true;
    for (u8 k = 0; k < content::kContentKindCount; ++k) {
        const auto kind = static_cast<content::ContentKind>(k);
        for (const auto& [id, value] : overlay.items(kind)) {
            m.items.push_back(overlayItem(kind, id, &value));
        }
    }
    return m;
}

// before → after 에서 바뀐 항목 (지운 것은 json "")
ContentOverlayMsg overlayDiff(const content::ContentOverlay& before, const content::ContentOverlay& after) {
    ContentOverlayMsg m;
    for (u8 k = 0; k < content::kContentKindCount; ++k) {
        const auto kind = static_cast<content::ContentKind>(k);
        for (const auto& [id, value] : after.items(kind)) {
            const nlohmann::json* old = before.find(kind, id);
            if (old == nullptr || *old != value) {
                m.items.push_back(overlayItem(kind, id, &value));
            }
        }
        for (const auto& [id, value] : before.items(kind)) {
            (void)value;
            if (after.find(kind, id) == nullptr) {
                m.items.push_back(overlayItem(kind, id, nullptr));
            }
        }
    }
    return m;
}

} // namespace

ServerHost::ServerHost(INetworkTransport& transport, std::unique_ptr<scenario::ScenarioRunner> runner,
                       ServerHostDesc desc)
    : m_transport(transport), m_runner(std::move(runner)), m_desc(std::move(desc)), m_validator(m_desc.validator),
      m_random(m_desc.randomSeed != 0 ? m_desc.randomSeed : std::random_device{}()) {
    SBX_ASSERT(m_runner != nullptr, "ServerHost: 월드가 없습니다");
    const auto& world = m_runner->world();
    m_meta.name = clampUtf8(m_desc.worldName, kMaxWorldNameBytes);
    // 와이어 상한 안으로 (매니페스트 검사가 이미 지키지만, 다른 호출자를 위해). gameId 가 없으면 게임이 아니다
    if (!m_desc.game.empty()) {
        m_desc.game.gameId = clampUtf8(m_desc.game.gameId, kMaxGameIdBytes);
        m_desc.game.name = clampUtf8(m_desc.game.name, kMaxGameNameBytes);
        m_desc.game.version = clampUtf8(m_desc.game.version, kMaxGameVersionBytes);
    } else {
        m_desc.game = GameIdentity{};
    }
    m_meta.minChunkX = world.desc().bounds.minChunk.x;
    m_meta.minChunkY = world.desc().bounds.minChunk.y;
    m_meta.maxChunkX = world.desc().bounds.maxChunk.x;
    m_meta.maxChunkY = world.desc().bounds.maxChunk.y;
    m_contentHash = world.content().contentHash();
    m_sentOverlay = world.contentOverlay();
    m_sentContentRevision = world.contentRevision();
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
    log::info("net", "서버 시작: {} · {} {} 포트 {} · 최대 {} 명 · 기본 역할 {}{}", m_meta.name, m_transport.name(),
              at.host.empty() ? "*" : at.host, m_transport.boundPort(), m_desc.maxClients, roleName(m_desc.defaultRole),
              m_desc.ownerKey.empty() ? "" : " · 주인 열쇠 있음");
    if (m_desc.mode == ServerMode::Threaded) {
        m_simThread = std::thread([this] { simThreadMain(); });
        m_netThread = std::thread([this] { netThreadMain(); });
    } else {
        m_nextTickAt = interval;
        m_statsWindowStart = 0;
        m_nextSnapshotAt = snapshotIntervalSeconds();
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
        m_nextTickAt += stepIntervalSeconds();
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
    // 기록 중이던 리플레이는 쓴다 (Simulation 스레드가 끝났다)
    if (m_recording) {
        if (auto r = stopRecording(); r) {
            log::info("net", "서버를 멈추며 리플레이를 썼습니다: {}", *r);
        } else {
            log::warn("net", "리플레이를 쓰지 못했습니다: {}", r.error().describe());
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

void ServerHost::releaseTicket(const Client& client, DisconnectReason reason) {
    const auto it = m_tickets.find(client.token);
    if (it == m_tickets.end() || it->second.clientId != client.clientId) {
        return;
    }
    if (reason == DisconnectReason::ClientQuit) {
        m_tickets.erase(it); // 스스로 나갔다 — 다시 접속할 일이 없다
    } else {
        it->second.active = false;
        it->second.expiresAt = m_netNow + m_desc.reconnectSeconds;
    }
}

void ServerHost::netStep(f64 now) {
    m_netNow = now;
    std::erase_if(m_tickets, [&](const auto& kv) { return !kv.second.active && kv.second.expiresAt < now; });
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
                releaseTicket(it->second, ev.reason);
                m_byClientId.erase(it->second.clientId);
                {
                    const std::lock_guard lock(m_mutex);
                    m_roster.push_back(RosterEvent{it->second.clientId, false});
                }
                m_clients.erase(it);
                broadcastPlayers();
                break;
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
        // 주인 열쇠 (12C): 맞으면 Owner. 틀린 열쇠는 조용히 무시하지 않고 거절한다 (잘못 친 것을 알게)
        const bool owner = !auth->ownerKey.empty();
        if (owner && (m_desc.ownerKey.empty() || auth->ownerKey != m_desc.ownerKey)) {
            reject(conn, client, RejectReason::BadHandshake,
                   m_desc.ownerKey.empty() ? "이 서버에는 주인 열쇠가 없습니다 (--owner-key)" : "주인 열쇠가 다릅니다");
            return;
        }
        // 다시 접속 (11.4): 유효한 토큰이면 같은 번호 · 역할. 살아 있는 연결의 토큰이면 옛 연결을 끊고 이어 받는다
        bool resumed = false;
        if (!auth->token.empty()) {
            const std::string key(reinterpret_cast<const char*>(auth->token.data()), auth->token.size());
            if (const auto t = m_tickets.find(key); t != m_tickets.end()) {
                const Ticket ticket = t->second;
                if (ticket.active) {
                    if (const auto old = m_byClientId.find(ticket.clientId); old != m_byClientId.end()) {
                        log::info("net", "#{} 의 옛 연결 {} 을 끊고 새 연결이 이어 받습니다", ticket.clientId,
                                  old->second);
                        dropConnection(old->second, DisconnectReason::Kicked); // 표를 비활성으로 돌린다
                    }
                }
                client.clientId = ticket.clientId;
                client.role = ticket.role;
                client.token = key;
                resumed = true;
            }
        }
        if (!resumed) {
            if (m_nextClientId == 0) { // u16 을 다 썼다 (세션 안 재사용 없음)
                reject(conn, client, RejectReason::ServerFull, "클라이언트 번호를 다 썼습니다");
                return;
            }
            client.clientId = m_nextClientId++;
            client.role = m_desc.defaultRole;
            client.token.resize(kSessionTokenBytes);
            for (char& b : client.token) {
                b = static_cast<char>(m_random() & 0xFF);
            }
        }
        if (owner) {
            client.role = Role::Owner;
        }
        m_tickets[client.token] = Ticket{client.clientId, client.role, true, 0};
        client.name = std::move(auth->displayName);
        client.state = Client::State::Active;
        m_byClientId[client.clientId] = conn;

        Welcome w;
        w.clientId = client.clientId;
        w.role = client.role;
        {
            const std::lock_guard lock(m_mutex);
            w.world = m_status.meta;
            w.generation = m_status.generation;
            w.serverTick = m_status.tick;
            w.world.paused = m_status.paused;
            w.world.speed = m_status.speed;
        }
        w.tickRate = static_cast<u8>(sim::kTickRate);
        w.sessionToken.resize(client.token.size());
        std::memcpy(w.sessionToken.data(), client.token.data(), client.token.size());
        if (m_replication) {
            w.replicated = m_replication->table(); // 만든 뒤 바뀌지 않는다 (스레드 사이 읽기 안전)
            w.snapshotRate = static_cast<u8>(sim::kTickRate / std::max<u32>(1, m_desc.snapshotIntervalSteps));
        } else {
            w.snapshotRate = 0;
        }
        w.game = m_desc.game;
        sendTo(conn, w);
        {
            const std::lock_guard lock(m_mutex);
            m_roster.push_back(RosterEvent{client.clientId, true});
        }
        log::info("net", "{}: {} (#{}, {}) — 연결 {}", resumed ? "다시 들어옴" : "들어옴", client.name, client.clientId,
                  roleName(client.role), conn);
        broadcastPlayers();
        return;
    }
    case Client::State::Active:
        break;
    }

    if (auto* command = std::get_if<CommandMsg>(&msg)) {
        auto ok = m_validator.check(client.commands, client.role, command->sequence, command->payload, now);
        // 게임 시작은 참가자가 모였을 때만 (GameMode.players.min — v0.2 M2-T3). 상태 검사는 시뮬레이션이 한다
        if (const auto* gc = std::get_if<cmd::GameControl>(&command->payload);
            ok && gc != nullptr && gc->op == cmd::GameControlOp::Start && playerCount() < m_desc.minPlayers) {
            ok = makeError(
                ErrorCode::InvalidArgument,
                std::format("참가자가 모자랍니다 ({} / 최소 {} — 역할 player 이상)", playerCount(), m_desc.minPlayers));
        }
        if (!ok) {
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
    if (const auto* sub = std::get_if<Subscribe>(&msg)) {
        const std::lock_guard lock(m_mutex);
        m_subscribes.emplace_back(client.clientId, *sub);
        return;
    }
    if (auto* inspect = std::get_if<InspectRequest>(&msg)) {
        // 읽기 전용 — 모든 역할 (개수 상한은 decode 가 본다)
        const std::lock_guard lock(m_mutex);
        m_inspectRequests.emplace_back(client.clientId, std::move(*inspect));
        return;
    }
    if (const auto* setRole = std::get_if<SetRoleRequest>(&msg)) {
        onSetRole(conn, client, *setRole);
        return;
    }
    if (auto* op = std::get_if<WorldOpRequest>(&msg)) {
        // 목록은 모두, 저장 · 불러오기 · 기록 · 재생은 실행 제어와 같은 열 (admin 부터 — 10-EDITOR 7장)
        if (op->op != WorldOpKind::List && !roleAllows(client.role, CommandClass::SimulationControl)) {
            sendTo(conn, WorldOpResultMsg{op->op, op->name, false,
                                          std::format("{} 역할로는 {} 요청을 할 수 없습니다 (admin 부터)",
                                                      roleName(client.role), worldOpName(op->op))});
            return;
        }
        const std::lock_guard lock(m_mutex);
        m_worldOps.push_back(WorldOpTask{client.clientId, std::move(*op)});
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
    r.game = m_desc.game;
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
        const bool active = it->second.state == Client::State::Active;
        if (active) {
            releaseTicket(it->second, reason);
            m_byClientId.erase(it->second.clientId);
            const std::lock_guard lock(m_mutex);
            m_roster.push_back(RosterEvent{it->second.clientId, false});
        }
        m_clients.erase(it);
        if (active) {
            broadcastPlayers();
        }
    }
}

PlayerList ServerHost::players() const {
    PlayerList list;
    for (const auto& [id, conn] : m_byClientId) { // clientId 오름차순
        const Client& c = m_clients.at(conn);
        if (list.players.size() < kMaxPlayers) {
            list.players.push_back(PlayerEntry{c.clientId, c.role, c.name});
        }
    }
    return list;
}

void ServerHost::broadcastPlayers() {
    if (m_byClientId.empty()) {
        return;
    }
    const PlayerList list = players();
    const auto bytes = encodeMessage(list);
    for (const auto& [id, conn] : m_byClientId) {
        m_transport.send(conn, Channel::Control, bytes);
    }
}

void ServerHost::onSetRole(ConnectionId conn, Client& client, const SetRoleRequest& request) {
    SetRoleResultMsg result{request.clientId, request.role, false, {}};
    const auto target = m_byClientId.find(request.clientId);
    if (target == m_byClientId.end()) {
        result.detail = std::format("#{} 접속자가 없습니다", request.clientId);
    } else if (request.clientId == client.clientId) {
        result.detail = "자기 역할은 바꿀 수 없습니다";
    } else {
        Client& who = m_clients.at(target->second);
        // 10-EDITOR 7장 "역할 부여": Owner 전부, Admin 은 Editor 이하 (Admin 이상인 사람은 건드리지 못한다)
        if (!roleCanAssign(client.role, who.role, request.role)) {
            result.detail =
                client.role == Role::Admin
                    ? "Admin 은 Admin 아래인 사람을 Editor 이하로만 바꿀 수 있습니다"
                    : std::format("{} 역할은 역할을 바꿀 수 없습니다 (10-EDITOR 7장)", roleName(client.role));
        } else {
            result.accepted = true;
            if (who.role != request.role) {
                log::info("net", "역할: {} (#{}) {} → {} (바꾼 사람 {} #{})", who.name, who.clientId,
                          roleName(who.role), roleName(request.role), client.name, client.clientId);
                who.role = request.role;
                if (const auto t = m_tickets.find(who.token); t != m_tickets.end()) {
                    t->second.role = who.role; // 다시 접속해도 바뀐 역할
                }
                broadcastPlayers();
            }
        }
    }
    sendTo(conn, result);
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
    std::vector<std::pair<u16, InspectRequest>> inspects;
    std::vector<std::pair<u16, Subscribe>> subscribes;
    std::vector<WorldOpTask> worldOps;
    {
        const std::lock_guard lock(m_mutex);
        commands.swap(m_inbox);
        worldOps.swap(m_worldOps);
        roster.swap(m_roster);
        acks.swap(m_acks);
        inspects.swap(m_inspectRequests);
        subscribes.swap(m_subscribes);
    }
    std::vector<Outgoing> overlayOut; // 월드 오버레이 (12C) — 스냅숏보다 먼저 내보낸다
    for (const RosterEvent& r : roster) {
        if (!r.joined) {
            m_inspect.erase(r.clientId);
        } else {
            overlayOut.push_back(Outgoing{r.clientId, fullOverlay(m_sentOverlay)});
        }
    }
    for (auto& [id, req] : inspects) {
        if (req.ids.empty()) {
            m_inspect.erase(id);
        } else {
            m_inspect[id] = std::move(req.ids);
        }
    }
    if (m_replication) {
        for (const RosterEvent& r : roster) {
            r.joined ? m_replication->addClient(r.clientId) : m_replication->removeClient(r.clientId);
        }
        for (const auto& [id, a] : acks) {
            m_replication->onAck(id, a.epoch, a.snapshotId);
        }
        // 관심 (Phase 11): 구독 사각형, 선택한 개체는 관심과 무관하게 늘 (roster 다음 — 같은 배치에 들어온
        // 클라이언트도)
        for (const auto& [id, sub] : subscribes) {
            m_replication->setInterest(id, sub);
        }
        for (const auto& [id, req] : inspects) {
            (void)req;
            const auto it = m_inspect.find(id);
            m_replication->setAlwaysRelevant(id, it != m_inspect.end() ? std::span<const NetEntityId>(it->second)
                                                                       : std::span<const NetEntityId>());
        }
    }
    // 세이브 · 로드 · 리플레이 (12D) — 틱 사이에. 월드가 바뀌면 이 배치의 명령은 옛 월드의 것이라 적용하지 않는다
    std::vector<Outgoing> worldOut;
    std::vector<Outgoing> results;
    bool replaced = false;
    if (autoRestartDue()) { // GameMode.restart auto (M2-T3) — 결과는 모두에게
        log::info("net", "게임 자동 재시작");
        worldOps.push_back(WorldOpTask{0, WorldOpRequest{WorldOpKind::Restart, {}}});
    }
    for (const WorldOpTask& t : worldOps) {
        runWorldOp(t, worldOut, replaced);
    }
    if (replaced) {
        for (auto& c : commands) {
            CommandResultMsg r;
            r.sequence = c.sequence;
            r.reason = ErrorCode::Unsupported;
            r.detail = "월드가 바뀌어 적용하지 않았습니다";
            results.push_back(Outgoing{c.issuer, std::move(r)});
        }
        commands.clear();
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
        for (auto& o : overlayOut) {
            m_outbox.push_back(std::move(o));
        }
        for (auto& o : worldOut) {
            m_outbox.push_back(std::move(o));
        }
        for (auto& o : results) {
            m_outbox.push_back(std::move(o));
        }
        m_stats.reachedStopTick = true;
        return;
    }
    const sim::Tick before = world.currentTick();
    const f64 t0 = steadySeconds();
    if (m_playback) {
        // 리플레이 재생: 편집은 거절, 실행 제어는 재생을 다룬다. 기록된 call 하나 (멈췄으면 없음)
        for (const auto& c : commands) {
            handlePlaybackCommand(c, results);
        }
        Playback& pb = *m_playback;
        if (!pb.paused || pb.steps > 0) {
            if (auto r = pb.player->step(); !r) {
                log::warn("net", "리플레이 재생 오류 — 재생을 끝냅니다: {}", r.error().describe());
                worldOut.push_back(Outgoing{0, WorldOpResultMsg{WorldOpKind::Play, pb.name, false,
                                                                clampUtf8(r.error().message, kMaxDetailBytes)}});
                m_playback.reset();
            } else if (pb.paused) {
                --pb.steps;
            }
        }
        if (m_playback && m_playback->player->done()) {
            const replay::PlaybackResult& res = m_playback->player->result();
            std::string detail =
                res.mismatch
                    ? std::format("재생 끝 — 기록과 다르다: call {} tick {}", res.mismatch->call, res.mismatch->tick)
                    : std::format("재생 끝 — {} call, 해시 {}개 일치", res.calls, res.hashesCompared);
            log::info("net", "리플레이 {}: {}", m_playback->name, detail);
            worldOut.push_back(
                Outgoing{0, WorldOpResultMsg{WorldOpKind::Play, m_playback->name, !res.mismatch.has_value(), detail}});
            m_playback.reset();
            // 보통 월드로 돌아와 멈춰 둔다 (살펴보고 이어서 편집)
            world.enqueue(
                cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, cmd::kServerIssuer, ++m_serverSequence},
                                cmd::PauseSimulation{}});
        }
    } else {
        // M4: executeTick 은 서버가 정한다 — 다음 tick() 이 적용할 틱 (일시정지 편집 단계도 "다음 틱" 몫을 적용한다)
        for (auto& c : commands) {
            m_awaitingResult.emplace(c.issuer, c.sequence);
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, c.issuer, c.sequence},
                                          std::move(c.payload)});
        }
        m_runner->step();
        if (m_recording) {
            if (auto r = m_recording->recorder->afterTick(); !r) {
                log::warn("net", "리플레이 기록 오류 — 기록을 끝냅니다: {}", r.error().describe());
                (void)stopRecording();
            }
        }
    }
    const f64 ms = (steadySeconds() - t0) * 1000.0;
    const bool advanced = world.currentTick() != before;

    if (world.contentRevision() != m_sentContentRevision) {
        ContentOverlayMsg diff = overlayDiff(m_sentOverlay, world.contentOverlay());
        if (!diff.items.empty()) {
            overlayOut.push_back(Outgoing{0, std::move(diff)});
        }
        m_sentOverlay = world.contentOverlay();
        m_sentContentRevision = world.contentRevision();
    }

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
        if (m_playback) {
            s.mode = 2;
            s.modeName = m_playback->name;
            s.replayCall = m_playback->player->call();
            s.replayCalls = m_playback->player->calls();
            s.hashesChecked = static_cast<u32>(m_playback->player->result().hashesCompared);
            s.mismatch = m_playback->player->result().mismatch.has_value();
            s.replayPaused = m_playback->paused;
            s.replaySpeed = m_playback->speed;
        } else if (m_recording) {
            s.mode = 1;
            s.modeName = m_recording->name;
            s.replayCall = m_recording->recorder->replay().calls;
        } else if (m_editRunner) {
            s.mode = 3; // 테스트 플레이 (프로토콜 12)
        }
        statsMsg = s;
        m_statsWindowStart = now;
        m_tickMsSum = 0;
        m_tickMsMax = 0;
        m_ticksInWindow = 0;
    }

    // 복제: 스냅숏 간격(실제 시간)마다 — 일시정지 편집 단계에서도 (멈춘 월드의 편집도 보이게). 속도 배율이 커도
    // 초당 횟수는 그대로라 예산 · 클라이언트 적용 비용이 속도에 비례해 늘지 않는다 (ADR-0026). 단계 시각이 조금
    // 흔들려도 간격의 1/4 까지는 당겨 보낸다
    m_replicationOut.clear();
    ++m_steps;
    f64 replicationMs = -1;
    const f64 snapInterval = snapshotIntervalSeconds();
    if (m_replication && now >= m_nextSnapshotAt - snapInterval * 0.25) {
        const f64 r0 = steadySeconds();
        m_replication->build(world, m_replicationOut);
        for (auto& [id, m] : m_replicationOut) {
            (void)id;
            if (auto* snap = std::get_if<Snapshot>(&m)) {
                snap->generation = m_generation;
            }
        }
        for (const auto& [id, ids] : m_inspect) {
            m_replicationOut.emplace_back(id, buildInspect(world, ids));
        }
        replicationMs = (steadySeconds() - r0) * 1000.0;
        m_nextSnapshotAt += snapInterval;
        if (m_nextSnapshotAt < now) { // 많이 밀렸다 (느린 틱) — 쌓아 두지 않는다
            m_nextSnapshotAt = now + snapInterval * 0.5;
        }
    }

    const std::lock_guard lock(m_mutex);
    for (auto& o : overlayOut) {
        m_outbox.push_back(std::move(o));
    }
    for (auto& o : worldOut) {
        m_outbox.push_back(std::move(o));
    }
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
    writeModeStats();
    if (m_desc.stopAtTick != 0 && world.currentTick() >= m_desc.stopAtTick) {
        m_stats.reachedStopTick = true;
    }
}

f64 ServerHost::snapshotIntervalSeconds() const noexcept {
    return static_cast<f64>(std::max<u32>(1, m_desc.snapshotIntervalSteps)) / static_cast<f64>(sim::kTickRate);
}

f64 ServerHost::stepIntervalSeconds() const noexcept {
    const f64 interval = static_cast<f64>(m_runner->world().clock().tickIntervalNanos()) / 1e9;
    return m_playback ? interval / static_cast<f64>(m_playback->speed) : interval;
}

void ServerHost::publishStatus() {
    const auto& world = m_runner->world();
    const std::lock_guard lock(m_mutex);
    m_status.tick = world.currentTick();
    m_status.paused = world.clock().paused();
    m_status.speed = world.clock().speed();
    m_status.meta = m_meta;
    m_status.generation = m_generation;
    m_stats.tick = m_status.tick;
    m_stats.paused = m_status.paused;
    m_stats.speed = m_status.speed;
    writeModeStats();
}

void ServerHost::writeModeStats() {
    m_stats.generation = m_generation;
    m_stats.mode = m_playback ? 2 : (m_recording ? 1 : (m_editRunner ? 3 : 0));
    m_stats.replayCall =
        m_playback ? m_playback->player->call() : (m_recording ? m_recording->recorder->replay().calls : 0);
    m_stats.replayCalls = m_playback ? m_playback->player->calls() : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// 세이브 · 로드 · 리플레이 (12D, ADR-0031) — Sim 절반, 틱 사이
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr const char* kReplayFile = "replay.sbxr";
constexpr const char* kReplayStart = "start";

bool isReplayFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(dir / kReplayFile, ec);
}

bool isSaveFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(dir / "world.json", ec);
}

} // namespace

void ServerHost::runWorldOp(const WorldOpTask& task, std::vector<Outgoing>& out, bool& replaced) {
    const WorldOpRequest& req = task.request;
    WorldOpResultMsg result{req.op, req.name, false, {}};
    const auto reply = [&](std::string detail, bool accepted) {
        result.accepted = accepted;
        result.detail = clampUtf8(detail, kMaxDetailBytes);
        if (accepted) {
            log::info("net", "월드 {} '{}' (#{}): {}", worldOpName(req.op), req.name, task.clientId, detail);
        }
        out.push_back(Outgoing{task.clientId, result});
    };
    if (req.op == WorldOpKind::List) {
        out.push_back(Outgoing{task.clientId, listWorlds()});
        return;
    }
    // 테스트 플레이 (v0.2 M5-T1): 시작 · 멈춤은 세이브 폴더가 없어도 된다. 그 동안 월드를 바꾸는 다른 요청은 거절
    // (저장은 된다 — 플레이 결과를 남기려면 저장한다)
    if (req.op == WorldOpKind::TestPlay || req.op == WorldOpKind::TestStop) {
        auto r = req.op == WorldOpKind::TestPlay ? startTestPlay(out) : stopTestPlay(out);
        if (r) {
            replaced = true;
        }
        r ? reply(*r, true) : reply(r.error().message, false);
        return;
    }
    if (m_editRunner && (req.op == WorldOpKind::Load || req.op == WorldOpKind::Play || req.op == WorldOpKind::Restart ||
                         req.op == WorldOpKind::RecordStart)) {
        reply("테스트 플레이 중입니다 — 먼저 멈추십시오", false);
        return;
    }
    if (req.op == WorldOpKind::Restart) {
        auto r = restartGame(out);
        if (r) {
            replaced = true;
        }
        r ? reply(*r, true) : reply(r.error().describe(), false);
        return;
    }
    if (req.op == WorldOpKind::RecordStop) {
        if (!m_recording) {
            reply("기록 중이 아닙니다", false);
            return;
        }
        result.name = m_recording->name;
        auto r = stopRecording();
        r ? reply(*r, true) : reply(r.error().message, false);
        return;
    }
    if (m_desc.savesRoot.empty()) {
        reply("이 서버에는 세이브 폴더가 없습니다 (--saves)", false);
        return;
    }
    if (!isValidSaveName(req.name)) {
        reply("이름은 영문 · 숫자 · _ · - 로 1 ~ 48 글자", false);
        return;
    }
    const std::filesystem::path dir = m_desc.savesRoot / req.name;
    auto& world = m_runner->world();
    switch (req.op) {
    case WorldOpKind::Save: {
        if (isReplayFolder(dir)) {
            reply("같은 이름의 리플레이가 있습니다", false);
            return;
        }
        auto saved = persist::saveWorld(world, dir, persist::SaveOptions{req.name});
        if (!saved) {
            reply(saved.error().describe(), false);
            return;
        }
        reply(std::format("tick {} · 엔티티 {}", world.currentTick(), saved->entities), true);
        return;
    }
    case WorldOpKind::Load: {
        if (isReplayFolder(dir)) {
            reply("리플레이입니다 — 재생으로 여세요", false);
            return;
        }
        if (!isSaveFolder(dir)) {
            reply("그런 세이브가 없습니다", false);
            return;
        }
        auto opened = scenario::openSave(dir, world.baseContent(), world.catalog());
        if (!opened) {
            reply(opened.error().describe(), false);
            return;
        }
        for (const auto& w : opened->warnings) {
            log::warn("net", "세이브 {}: {}", req.name, w);
        }
        std::string recorded;
        if (m_recording) { // 기록하던 월드가 사라진다 — 거기까지 쓴다
            auto r = stopRecording();
            recorded =
                r ? std::format(" (기록을 끝냈습니다: {})", *r) : std::format(" (기록 실패: {})", r.error().message);
        }
        const sim::Tick tick = opened->runner->world().currentTick();
        replaceWorld(std::move(opened->runner), WorldChangeReason::Load, req.name, out);
        replaced = true;
        reply(std::format("tick {}{}", tick, recorded), true);
        return;
    }
    case WorldOpKind::RecordStart: {
        if (m_recording) {
            reply(std::format("이미 '{}' 를 기록하고 있습니다", m_recording->name), false);
            return;
        }
        if (m_playback) {
            reply("리플레이를 재생하는 중에는 기록할 수 없습니다", false);
            return;
        }
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            reply("같은 이름의 세이브 · 리플레이가 있습니다", false);
            return;
        }
        auto saved = persist::saveWorld(world, dir / kReplayStart, persist::SaveOptions{req.name});
        if (!saved) {
            reply(saved.error().describe(), false);
            return;
        }
        auto startHash = world.worldHash();
        if (!startHash) {
            reply(startHash.error().describe(), false);
            return;
        }
        replay::ReplayHeader header;
        header.startWorld = kReplayStart;
        header.startWorldHash = *startHash;
        Recording rec;
        rec.name = req.name;
        rec.dir = dir;
        rec.recorder = std::make_unique<replay::ReplayRecorder>(world, std::move(header));
        m_recording = std::move(rec);
        reply(std::format("tick {} 부터 기록", world.currentTick()), true);
        publishStatus();
        return;
    }
    case WorldOpKind::Play: {
        if (!isReplayFolder(dir)) {
            reply("그런 리플레이가 없습니다", false);
            return;
        }
        auto pb = std::make_unique<Playback>();
        pb->name = req.name;
        auto rep = replay::readReplay(dir / kReplayFile);
        if (!rep) {
            reply(rep.error().describe(), false);
            return;
        }
        pb->replay = std::move(*rep);
        if (pb->replay.header.contentHash != m_contentHash) {
            reply(std::format("다른 콘텐츠로 기록한 리플레이입니다 (리플레이 {}, 서버 {})",
                              replay::formatHash(pb->replay.header.contentHash), replay::formatHash(m_contentHash)),
                  false);
            return;
        }
        auto opened = scenario::openSave(dir / pb->replay.header.startWorld, world.baseContent(), world.catalog());
        if (!opened) {
            reply(opened.error().describe(), false);
            return;
        }
        auto player = replay::ReplayPlayer::start(opened->runner->world(), pb->replay);
        if (!player) {
            reply(player.error().describe(), false);
            return;
        }
        pb->player = std::move(*player);
        std::string recorded;
        if (m_recording) {
            auto r = stopRecording();
            recorded =
                r ? std::format(" (기록을 끝냈습니다: {})", *r) : std::format(" (기록 실패: {})", r.error().message);
        }
        const u64 calls = pb->player->calls();
        replaceWorld(std::move(opened->runner), WorldChangeReason::Replay, req.name, out);
        m_playback = std::move(pb);
        replaced = true;
        reply(std::format("{} call 재생 시작{}", calls, recorded), true);
        publishStatus();
        return;
    }
    case WorldOpKind::List:
    case WorldOpKind::RecordStop:
    case WorldOpKind::Restart:
    case WorldOpKind::TestPlay:
    case WorldOpKind::TestStop:
        break;
    }
}

// --- 테스트 플레이 (v0.2 M5-T1, ADR-0053) ------------------------------------------------------------------------
// 복사본 = 편집 월드를 임시 폴더에 저장하고 다시 연 것 (세이브 · 로드와 같은 길 — 해시 검증, 오버레이 포함). 폴더는
// 바로 지운다. 원본 runner 는 m_editRunner 에 그대로 맡겨 두고 돌리지 않는다 — 멈추면 그 객체가 돌아오므로 틱 ·
// 일시정지 · 선택하지 않은 모든 상태가 시작 전과 같다. 게임 모드가 있으면 복사본의 세션을 Lobby 로 놓고 시작 명령을,
// 일시정지였으면 재개 명령을 서버가 넣는다.
Expected<std::string> ServerHost::startTestPlay(std::vector<Outgoing>& out) {
    if (m_editRunner) {
        return makeError(ErrorCode::InvalidArgument, "이미 테스트 플레이 중입니다");
    }
    if (m_playback) {
        return makeError(ErrorCode::InvalidArgument, "리플레이를 재생하는 중에는 테스트 플레이를 할 수 없습니다");
    }
    if (m_recording) {
        return makeError(ErrorCode::InvalidArgument, "기록 중에는 테스트 플레이를 할 수 없습니다 — 기록을 끝내십시오");
    }
    auto& world = m_runner->world();
    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path(ec) /
        std::format("sbx-testplay-{:x}-{}",
                    static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()), m_generation);
    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(dir, e);
        }
    } cleanup{dir};
    if (auto saved = persist::saveWorld(world, dir, persist::SaveOptions{"testplay"}); !saved) {
        return std::unexpected(saved.error());
    }
    auto opened = scenario::openSave(dir, world.baseContent(), world.catalog());
    if (!opened) {
        return std::unexpected(opened.error());
    }
    sim::SimulationWorld& copy = opened->runner->world();
    std::string what = "편집 월드의 복사본";
    if (!m_desc.gameMode.empty()) {
        if (auto s = game::resetSession(copy, m_desc.gameMode); !s) {
            return std::unexpected(s.error());
        }
        copy.enqueue(cmd::SimCommand{cmd::CommandHeader{copy.currentTick() + 1, cmd::kServerIssuer, ++m_serverSequence},
                                     cmd::GameControl{cmd::GameControlOp::Start}});
        what += std::format(" · 게임 {} 시작", m_desc.gameMode);
    }
    if (copy.clock().paused()) {
        copy.enqueue(cmd::SimCommand{cmd::CommandHeader{copy.currentTick() + 1, cmd::kServerIssuer, ++m_serverSequence},
                                     cmd::ResumeSimulation{}});
    }
    const sim::Tick tick = copy.currentTick();
    replaceWorld(std::move(opened->runner), WorldChangeReason::TestPlay, {}, out, &m_editRunner);
    m_finishedSteps = 0;
    return std::format("tick {} — {} (멈추면 편집 월드로 돌아갑니다)", tick, what);
}

Expected<std::string> ServerHost::stopTestPlay(std::vector<Outgoing>& out) {
    if (!m_editRunner) {
        return makeError(ErrorCode::InvalidArgument, "테스트 플레이 중이 아닙니다");
    }
    const sim::Tick tick = m_editRunner->world().currentTick();
    replaceWorld(std::move(m_editRunner), WorldChangeReason::TestStop, {}, out);
    m_finishedSteps = 0;
    return std::format("편집 월드로 돌아왔습니다 (tick {}) — 플레이한 것은 버렸습니다", tick);
}

void setGameProject(ServerHostDesc& desc, const project::GameManifest& manifest,
                    const std::filesystem::path& projectDir, const content::ContentDatabase& content) {
    desc.game = gameIdentityOf(manifest);
    if (manifest.gameMode.empty()) {
        return;
    }
    desc.entryWorld = projectDir / manifest.entryWorld;
    desc.gameMode = manifest.gameMode;
    const content::GameMode* mode = content.findMode(manifest.gameMode);
    desc.minPlayers = mode != nullptr ? mode->minPlayers : 0;
}

Expected<std::string> ServerHost::restartGame(std::vector<Outgoing>& out) {
    if (m_desc.entryWorld.empty() || m_desc.gameMode.empty()) {
        return makeError(ErrorCode::Unsupported, "게임 모드가 있는 프로젝트 서버만 재시작할 수 있습니다");
    }
    auto& world = m_runner->world();
    // 시작 월드는 프로젝트를 열 때처럼 콘텐츠 해시가 달라도 연다 (P5 경고 — 팩을 고친 뒤 월드를 다시 저장하지 않았다)
    auto opened = scenario::openSave(m_desc.entryWorld, world.baseContent(), world.catalog(), true);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    for (const auto& w : opened->warnings) {
        log::warn("net", "시작 월드: {}", w);
    }
    if (auto s = game::resetSession(opened->runner->world(), m_desc.gameMode); !s) {
        return std::unexpected(s.error());
    }
    std::string recorded;
    if (m_recording) {
        auto r = stopRecording();
        recorded = r ? std::format(" (기록을 끝냈습니다: {})", *r) : std::format(" (기록 실패: {})", r.error().message);
    }
    const sim::Tick tick = opened->runner->world().currentTick();
    replaceWorld(std::move(opened->runner), WorldChangeReason::Restart, {}, out);
    m_finishedSteps = 0;
    return std::format("시작 월드 tick {} · Lobby{}", tick, recorded);
}

bool ServerHost::autoRestartDue() {
    if (m_desc.entryWorld.empty() || m_playback || m_editRunner) { // 테스트 플레이는 멈출 때까지 그대로
        return false;
    }
    const auto& world = m_runner->world();
    const comp::GameSession* s = game::findSession(world.registry());
    if (s == nullptr || s->gameState() != comp::GameState::Finished) {
        m_finishedSteps = 0;
        return false;
    }
    const content::GameMode* mode = world.content().findMode(std::string_view(s->mode.view()));
    if (mode == nullptr || !mode->autoRestart) {
        return false;
    }
    return ++m_finishedSteps > mode->restartDelayTicks;
}

u32 ServerHost::playerCount() const {
    u32 n = 0;
    for (const auto& [id, conn] : m_byClientId) {
        (void)id;
        n += m_clients.at(conn).role >= Role::Player ? 1u : 0u;
    }
    return n;
}

void ServerHost::replaceWorld(std::unique_ptr<scenario::ScenarioRunner> runner, WorldChangeReason reason,
                              const std::string& name, std::vector<Outgoing>& out,
                              std::unique_ptr<scenario::ScenarioRunner>* keepOld) {
    SBX_ASSERT(!m_recording, "기록을 끝낸 뒤 월드를 바꿔야 합니다");
    runner->world().setJobSystem(m_runner->world().paths().jobSystem());
    m_playback.reset(); // 옛 월드를 가리킨다
    std::unique_ptr<scenario::ScenarioRunner> old = std::exchange(m_runner, std::move(runner));
    if (keepOld != nullptr) {
        *keepOld = std::move(old); // 테스트 플레이: 편집 월드를 맡긴다
    }
    auto& world = m_runner->world();
    ++m_generation;
    m_inspect.clear();
    m_awaitingResult.clear();
    m_meta.minChunkX = world.desc().bounds.minChunk.x;
    m_meta.minChunkY = world.desc().bounds.minChunk.y;
    m_meta.maxChunkX = world.desc().bounds.maxChunk.x;
    m_meta.maxChunkY = world.desc().bounds.maxChunk.y;
    if (m_replication) {
        m_replication->resetWorld();
    }
    WorldChanged changed;
    changed.generation = m_generation;
    changed.world = m_meta;
    changed.world.paused = world.clock().paused();
    changed.world.speed = world.clock().speed();
    changed.reason = reason;
    changed.name = clampUtf8(name, kMaxSaveNameBytes);
    out.push_back(Outgoing{0, std::move(changed)});
    m_sentOverlay = world.contentOverlay();
    m_sentContentRevision = world.contentRevision();
    out.push_back(Outgoing{0, fullOverlay(m_sentOverlay)});
    log::info("net", "월드를 바꿨습니다 ({} '{}'): tick {} · 엔티티 {} · generation {}",
              reason == WorldChangeReason::Load       ? "불러오기"
              : reason == WorldChangeReason::Restart  ? "게임 재시작"
              : reason == WorldChangeReason::TestPlay ? "테스트 플레이"
              : reason == WorldChangeReason::TestStop ? "테스트 플레이 멈춤"
                                                      : "리플레이",
              name, world.currentTick(), world.registry().aliveCount(), m_generation);
    publishStatus();
}

Expected<std::string> ServerHost::stopRecording() {
    SBX_ASSERT(m_recording.has_value(), "기록 중이 아닙니다");
    Recording rec = std::move(*m_recording);
    m_recording.reset();
    if (auto r = rec.recorder->finish(); !r) {
        return std::unexpected(r.error());
    }
    const replay::Replay& rep = rec.recorder->replay();
    if (auto w = replay::writeReplay(rep, rec.dir / kReplayFile); !w) {
        return std::unexpected(w.error());
    }
    std::string summary = std::format("{} call · 명령 {} · tick {} → {}", rep.calls, rep.commands.size(),
                                      rep.header.startTick, rep.endTick);
    rec.recorder.reset(); // 관찰자를 뗀다
    publishStatus();
    return summary;
}

WorldList ServerHost::listWorlds() const {
    WorldList list;
    if (m_desc.savesRoot.empty()) {
        return list;
    }
    std::error_code ec;
    std::filesystem::directory_iterator it(m_desc.savesRoot, ec);
    if (ec) {
        return list;
    }
    for (const auto& entry : it) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (!isValidSaveName(name)) {
            continue;
        }
        SavedWorld w;
        w.name = name;
        w.replay = isReplayFolder(entry.path());
        const std::filesystem::path summaryDir = w.replay ? entry.path() / kReplayStart : entry.path();
        if (!w.replay && !isSaveFolder(summaryDir)) {
            continue;
        }
        if (auto s = scenario::readSaveSummary(summaryDir)) {
            w.tick = s->tick;
            w.entities = s->entities;
        }
        list.worlds.push_back(std::move(w));
    }
    std::ranges::sort(list.worlds, {}, &SavedWorld::name);
    if (list.worlds.size() > kMaxSavedWorlds) {
        list.worlds.resize(kMaxSavedWorlds);
    }
    return list;
}

void ServerHost::handlePlaybackCommand(const ValidatedCommand& c, std::vector<Outgoing>& results) {
    Playback& pb = *m_playback;
    CommandResultMsg r;
    r.sequence = c.sequence;
    r.accepted = true;
    r.appliedTick = m_runner->world().currentTick();
    if (std::holds_alternative<cmd::PauseSimulation>(c.payload)) {
        pb.paused = true;
        pb.steps = 0;
    } else if (std::holds_alternative<cmd::ResumeSimulation>(c.payload)) {
        pb.paused = false;
        pb.steps = 0;
    } else if (const auto* step = std::get_if<cmd::StepSimulation>(&c.payload)) {
        pb.paused = true;
        pb.steps += std::max<u32>(1, step->ticks);
    } else if (const auto* speed = std::get_if<cmd::SetSimulationSpeed>(&c.payload)) {
        pb.speed = speed->speed; // 검사기가 범위를 봤다
    } else {
        r.accepted = false;
        r.appliedTick = 0;
        r.reason = ErrorCode::Unsupported;
        r.detail = "리플레이 재생 중에는 편집할 수 없습니다";
    }
    {
        const std::lock_guard lock(m_mutex);
        ++(r.accepted ? m_stats.commandsAccepted : m_stats.commandsRejected);
    }
    results.push_back(Outgoing{c.issuer, std::move(r)});
}

void ServerHost::simThreadMain() {
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    m_statsWindowStart = steadySeconds();
    m_nextSnapshotAt = m_statsWindowStart + snapshotIntervalSeconds();
    std::mutex sleepMutex;
    std::condition_variable never;
    while (!m_quitSim.load(std::memory_order_acquire)) {
        const auto interval = std::chrono::nanoseconds(static_cast<std::int64_t>(stepIntervalSeconds() * 1e9));
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
