#include "apps/client/NetworkSession.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iterator>

#include "apps/client/presentation/SelectionOverlay.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "foundation/log/Log.hpp"
#include "network/transport/EnetTransport.hpp"

namespace sbx::client {

namespace {
constexpr u16 kDefaultPort = 7777;
} // namespace

Expected<std::unique_ptr<NetworkSession>> NetworkSession::create(const NetworkSessionDesc& desc,
                                                                 render::MaterialLibrary& materials) {
    std::unique_ptr<NetworkSession> s(new NetworkSession(desc, materials));
    s->m_catalog = std::make_unique<ecs::ComponentCatalog>();
    if (auto r = comp::registerCoreComponents(*s->m_catalog); !r) {
        return std::unexpected(r.error());
    }
    if (desc.mode == NetworkSessionMode::Local) {
        net::LocalServerDesc ld;
        ld.world = desc.world;
        ld.contentRoot = desc.contentRoot;
        ld.seed = desc.seed;
        ld.workers = desc.workers;
        ld.mode = desc.inlineServer ? net::ServerMode::Inline : net::ServerMode::Threaded;
        auto local = net::LocalServerHost::create(*s->m_catalog, ld);
        if (!local) {
            return std::unexpected(local.error());
        }
        s->m_local = std::move(*local);
        for (const auto& w : s->m_local->warnings()) {
            log::warn("client", "세이브: {}", w);
        }
        s->m_name = s->m_local->name();
        s->m_target = s->m_local->endpoint();
    } else {
        auto target = net::parseEndpoint(desc.connect, kDefaultPort);
        if (!target) {
            return std::unexpected(target.error());
        }
        s->m_target = *target;
        s->m_name = target->describe();
        // 처음에는 내장 콘텐츠 — 서버가 다르다고 하면 서버의 팩을 읽어 다시 (handleRejected)
        s->m_ownContent = std::make_unique<content::ContentDatabase>(content::ContentDatabase::builtin());
    }
    if (auto r = s->connect(); !r) {
        return std::unexpected(r.error());
    }
    return s;
}

NetworkSession::NetworkSession(const NetworkSessionDesc& desc, render::MaterialLibrary& materials)
    : m_desc(desc), m_extraction(materials) {}

NetworkSession::~NetworkSession() {
    if (m_session) {
        m_session->disconnect();
    }
    m_session.reset();
    if (m_ownTransport) {
        m_ownTransport->drain(200); // 끊기 알림이 나가게
    }
}

Expected<void> NetworkSession::connect() {
    m_session.reset();
    net::INetworkTransport* transport = nullptr;
    const content::ContentDatabase* content = nullptr;
    if (m_local) {
        transport = &m_local->clientTransport();
        content = &m_local->content();
    } else {
        auto t = m_desc.transportFactory ? m_desc.transportFactory()
                                         : [&]() -> Expected<std::unique_ptr<net::INetworkTransport>> {
            auto e = net::EnetTransport::create();
            if (!e) {
                return std::unexpected(e.error());
            }
            return std::unique_ptr<net::INetworkTransport>(std::move(*e));
        }();
        if (!t) {
            return std::unexpected(t.error());
        }
        m_ownTransport = std::move(*t);
        transport = m_ownTransport.get();
        content = m_ownContent.get();
    }
    net::ClientSessionDesc cd;
    cd.displayName = m_desc.displayName;
    cd.contentHash = content->contentHash();
    cd.buildId = net::localBuildId();
    cd.handshakeTimeoutSeconds = m_desc.connectTimeoutSeconds;
    cd.catalog = m_catalog.get();
    cd.content = content;
    m_session = std::make_unique<net::ClientSession>(*transport, cd);
    m_seenWorld = nullptr;
    m_seenSnapshots = 0;
    if (auto r = m_session->connect(m_target, m_now); !r) {
        return r;
    }
    log::info("client", "접속: {} ({})", m_target.describe(), m_local ? "로컬 서버" : "원격");
    return {};
}

const net::ClientWorld* NetworkSession::clientWorld() const noexcept {
    return m_session ? m_session->world() : nullptr;
}

u64 NetworkSession::serverTick() const noexcept {
    const net::ClientWorld* w = clientWorld();
    return w != nullptr ? w->serverTick() : 0;
}

bool NetworkSession::paused() const noexcept {
    const net::ClientWorld* w = clientWorld();
    return w != nullptr && w->paused();
}

f32 NetworkSession::speed() const noexcept {
    const net::ClientWorld* w = clientWorld();
    return w != nullptr ? w->speed() : 1.0f;
}

bool NetworkSession::ready() const {
    const net::ClientWorld* w = clientWorld();
    return m_session && m_session->state() == net::ClientState::Connected && w != nullptr &&
           w->stats().snapshotsApplied > 0;
}

void NetworkSession::handleRejected() {
    const net::Reject r = *m_session->reject(); // 복사 — 아래에서 세션을 없앤다
    if (r.reason == net::RejectReason::ContentMismatch && !m_local && !m_retriedContent) {
        m_retriedContent = true;
        if (!r.packs.empty()) {
            auto db = content::loadContent(m_desc.contentRoot, r.packs, *m_catalog);
            if (!db) {
                m_failure = std::format("서버의 콘텐츠 팩을 읽지 못했습니다: {}", db.error().describe());
                return;
            }
            m_session.reset(); // 복제 월드가 옛 콘텐츠를 가리킨다 — 먼저 없앤다
            m_ownContent = std::make_unique<content::ContentDatabase>(std::move(*db));
        }
        if (m_ownContent->contentHash() != r.contentHash) {
            m_failure = std::format("콘텐츠가 다릅니다: 서버 {:016x}, 이 컴퓨터 {:016x} (팩 {} 개)", r.contentHash,
                                    m_ownContent->contentHash(), r.packs.size());
            return;
        }
        std::string packs;
        for (const auto& id : r.packs) {
            packs += (packs.empty() ? "" : ", ") + id;
        }
        log::info("client", "서버 콘텐츠({})를 읽어 다시 접속합니다", packs.empty() ? "내장" : packs);
        if (auto c = connect(); !c) {
            m_failure = c.error().describe();
        }
        return;
    }
    m_failure = std::format("서버가 거절했습니다: {} — {}", net::rejectReasonName(r.reason), r.detail);
}

void NetworkSession::update(f64 dtSeconds) {
    m_now += dtSeconds;
    if (m_local && m_desc.inlineServer) {
        m_local->update(dtSeconds);
    }
    if (!m_session || m_failure) {
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    m_session->update(m_now); // 받기 · 복제 월드에 적용 · ack
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_applyMs = m_applyMs == 0 ? ms : m_applyMs * 0.95 + ms * 0.05;
    switch (m_session->state()) {
    case net::ClientState::Rejected:
        handleRejected();
        return;
    case net::ClientState::Disconnected:
        m_failure = std::format("서버와 연결이 끊겼습니다 ({}): {}", m_target.describe(),
                                net::disconnectReasonName(m_session->disconnectReason()));
        return;
    case net::ClientState::Connected:
        break;
    default:
        return; // 접속 중
    }

    const net::ClientWorld* world = m_session->world();
    if (world == nullptr) {
        m_failure = "복제 월드를 만들지 못했습니다 (로그 참고)";
        return;
    }
    if (world != m_seenWorld) {
        m_seenWorld = world;
        m_extraction.reset();
        m_clock.reset();
        m_seenSnapshots = 0;
        m_selection.clear();
    }
    if (world->stats().snapshotsApplied != m_seenSnapshots) {
        m_seenSnapshots = world->stats().snapshotsApplied;
        m_clock.onSnapshot(world->serverTick(), world->paused(), world->speed(), m_now);
    }

    for (const net::CommandResultMsg& r : m_session->takeResults()) {
        const auto it = m_pendingNames.find(r.sequence);
        const std::string name = it != m_pendingNames.end() ? it->second : std::string("명령");
        if (it != m_pendingNames.end()) {
            m_pendingNames.erase(it);
        }
        if (m_pendingPause && m_pendingPause->first == r.sequence) {
            m_pendingPause.reset();
        }
        if (m_pendingSpeed && m_pendingSpeed->first == r.sequence) {
            m_pendingSpeed.reset();
        }
        if (!r.accepted) {
            ++m_commandsRejected;
            m_lastRejection = std::format("{} 거절: {}", name, r.detail.empty() ? errorCodeName(r.reason) : r.detail);
            log::warn("client", "{}", m_lastRejection);
        }
    }

    // 사라진 개체는 선택에서 뺀다
    if (!m_selection.empty()) {
        const auto before = m_selection.size();
        std::erase_if(m_selection, [&](NetEntityId id) { return world->find(id) == ecs::kNullEntity; });
        if (m_selection.size() != before) {
            selectionChanged();
        }
    }

    // 받은 바이트 (1 초 창)
    if (m_now - m_bytesWindowStart >= 1.0) {
        const u64 bytes = m_session->snapshotBytes();
        m_receivedKBps = static_cast<f64>(bytes - m_bytesAtWindowStart) / 1024.0 / (m_now - m_bytesWindowStart);
        m_bytesAtWindowStart = bytes;
        m_bytesWindowStart = m_now;
    }
}

void NetworkSession::extract(render::RenderWorld& out) {
    const net::ClientWorld* world = clientWorld();
    if (world == nullptr) {
        return;
    }
    const f64 rt = m_clock.renderTick(m_now);
    const auto& inspect = m_session->inspect();
    m_extraction.capture(*world, rt, m_selection, inspect ? &*inspect : nullptr, m_snapshot);
    SpriteExtraction::emit(m_snapshot, out);
    if (!m_selection.empty()) {
        (void)drawSelectionOutlines(m_snapshot, m_selection, out.selection);
        if (m_detailOverlay) {
            drawSelectedDetails(m_snapshot, out.debug);
        }
    }
}

render::WorldRect NetworkSession::bounds() const {
    const net::ClientWorld* w = clientWorld();
    return w != nullptr ? SpriteExtraction::worldBounds(*w) : render::WorldRect{};
}

void NetworkSession::send(cmd::CommandPayload payload) {
    if (!m_session) {
        return;
    }
    const std::string name(cmd::commandName(payload));
    const bool isPause = std::holds_alternative<cmd::PauseSimulation>(payload);
    const bool isResume = std::holds_alternative<cmd::ResumeSimulation>(payload);
    const auto* speed = std::get_if<cmd::SetSimulationSpeed>(&payload);
    const f32 speedValue = speed != nullptr ? speed->speed : 0.0f;
    const u32 seq = m_session->sendCommand(std::move(payload));
    if (seq == 0) {
        return; // 접속 전
    }
    m_pendingNames[seq] = name;
    if (isPause || isResume) {
        m_pendingPause = std::pair{seq, isPause};
    }
    if (speed != nullptr) {
        m_pendingSpeed = std::pair{seq, speedValue};
    }
}

void NetworkSession::togglePause() {
    const bool nowPaused = m_pendingPause ? m_pendingPause->second : paused();
    send(nowPaused ? cmd::CommandPayload{cmd::ResumeSimulation{}} : cmd::CommandPayload{cmd::PauseSimulation{}});
}

void NetworkSession::stepOnce() {
    const bool nowPaused = m_pendingPause ? m_pendingPause->second : paused();
    if (nowPaused) {
        send(cmd::StepSimulation{1});
    }
}

void NetworkSession::changeSpeed(int dir) {
    const f32 current = m_pendingSpeed ? m_pendingSpeed->second : speed();
    // 지금 값에 가장 가까운 칸에서 한 칸
    usize idx = 0;
    for (usize i = 0; i < std::size(kSpeeds); ++i) {
        if (std::abs(kSpeeds[i] - current) < std::abs(kSpeeds[idx] - current)) {
            idx = i;
        }
    }
    const usize next = dir > 0 ? std::min(idx + 1, std::size(kSpeeds) - 1) : (idx > 0 ? idx - 1 : 0);
    if (kSpeeds[next] != current) {
        send(cmd::SetSimulationSpeed{kSpeeds[next]});
    }
}

std::string NetworkSession::status() const {
    const net::ClientWorld* w = clientWorld();
    if (w == nullptr || !m_session) {
        return std::format("{} 접속 중", m_name);
    }
    std::string s = std::format("{} tick {} · 개체 {}", m_name, w->serverTick(), w->entityCount());
    if (const auto& st = m_session->lastStats()) {
        s += std::format(" · {:.1f} TPS · 틱 {:.1f} ms", st->ticksPerSecond, st->tickMsAvg);
    }
    s += w->paused() ? " · 일시정지" : std::format(" · ×{}", w->speed());
    if (!m_local) {
        s += std::format(" · RTT {:.0f} ms", m_session->transportStats().rttMs);
    }
    return s;
}

void NetworkSession::selectionChanged() {
    if (m_session) {
        m_session->setInspect(m_selection);
    }
}

void NetworkSession::selectAt(Vec2 world, bool additive) {
    const auto id = pickAt(m_snapshot, world);
    if (!additive) {
        m_selection.clear();
        if (id) {
            m_selection.push_back(*id);
        }
    } else if (id) {
        if (const auto it = std::ranges::lower_bound(m_selection, *id); it != m_selection.end() && *it == *id) {
            m_selection.erase(it);
        } else {
            m_selection.insert(it, *id);
        }
    }
    selectionChanged();
}

void NetworkSession::selectBox(render::WorldRect area, bool additive) {
    auto ids = pickBox(m_snapshot, area);
    if (additive) {
        mergeSelection(m_selection, ids);
    } else {
        m_selection = std::move(ids);
    }
    selectionChanged();
}

void NetworkSession::clearSelection() {
    m_selection.clear();
    selectionChanged();
}

std::string NetworkSession::selectionStatus() const {
    return describeSelection(m_snapshot, m_selection);
}

WorldInfo NetworkSession::info() const {
    WorldInfo i;
    i.name = m_name;
    i.selected = m_selection.size();
    const net::ClientWorld* w = clientWorld();
    if (w == nullptr || !m_session) {
        return i;
    }
    i.tick = w->serverTick();
    i.entities = static_cast<u32>(w->entityCount());
    i.speed = w->speed();
    i.paused = w->paused();
    i.targetTicksPerSecond = 30.0 * static_cast<f64>(w->speed());
    if (const auto& st = m_session->lastStats()) {
        i.ticksPerSecond = st->ticksPerSecond;
        i.tickMs = st->tickMsAvg;
    }
    NetInfo n;
    n.local = m_local != nullptr;
    n.server = m_local ? std::string("로컬") : m_target.describe();
    if (const auto& wel = m_session->welcome()) {
        n.role = std::string(net::roleName(wel->role));
    }
    n.rttMs = m_session->transportStats().rttMs;
    n.snapshots = w->stats().snapshotsApplied;
    n.resyncs = w->stats().resets;
    n.epoch = w->epoch();
    n.receivedKBps = m_receivedKBps;
    n.applyMs = m_applyMs;
    n.delayMs = m_clock.delayTicks() / std::max(1e-6, 30.0 * static_cast<f64>(w->speed())) * 1000.0;
    n.behindTicks = static_cast<f64>(w->serverTick()) - m_snapshot.renderTick;
    n.commandsRejected = m_commandsRejected;
    n.lastRejection = m_lastRejection;
    i.net = std::move(n);
    return i;
}

} // namespace sbx::client
