#include "apps/client/NetworkSession.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iterator>
#include <utility>

#include "apps/client/presentation/SelectionOverlay.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/content/ContentLoader.hpp"
#include "foundation/log/Log.hpp"
#include "network/server/CommandValidator.hpp"
#include "network/transport/EnetTransport.hpp"

namespace sbx::client {

namespace {
constexpr u16 kDefaultPort = 7777;

// "Arena — studio.arena 0.1.0" (게임이 아니면 빈 문자열)
std::string describeGame(const net::GameIdentity& g) {
    return g.empty() ? std::string{} : std::format("{} — {} {}", g.name, g.gameId, g.version);
}
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
        ld.savesRoot = desc.savesRoot;
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

Expected<std::unique_ptr<net::INetworkTransport>> NetworkSession::makeTransport() const {
    if (m_desc.transportFactory) {
        return m_desc.transportFactory();
    }
    auto e = net::EnetTransport::create();
    if (!e) {
        return std::unexpected(e.error());
    }
    return std::unique_ptr<net::INetworkTransport>(std::move(*e));
}

net::ClientSessionDesc NetworkSession::sessionDesc(const content::ContentDatabase& content) const {
    net::ClientSessionDesc cd;
    cd.displayName = m_desc.displayName;
    cd.contentHash = content.contentHash();
    cd.buildId = net::localBuildId();
    cd.handshakeTimeoutSeconds = m_desc.connectTimeoutSeconds;
    cd.catalog = m_catalog.get();
    cd.content = &content;
    cd.ownerKey = m_desc.ownerKey;
    return cd;
}

Expected<void> NetworkSession::connect() {
    m_session.reset();
    net::INetworkTransport* transport = nullptr;
    const content::ContentDatabase* content = nullptr;
    if (m_local) {
        transport = &m_local->clientTransport();
        content = &m_local->content();
    } else {
        auto t = makeTransport();
        if (!t) {
            return std::unexpected(t.error());
        }
        m_ownTransport = std::move(*t);
        transport = m_ownTransport.get();
        content = m_ownContent.get();
    }
    m_session = std::make_unique<net::ClientSession>(*transport, sessionDesc(*content));
    m_seenWorld = nullptr;
    m_seenSnapshots = 0;
    if (auto r = m_session->connect(m_target, m_now); !r) {
        return r;
    }
    log::info("client", "접속: {} ({})", m_target.describe(), m_local ? "로컬 서버" : "원격");
    return {};
}

void NetworkSession::updateReconnect() {
    if (m_retry) {
        m_retry->update(m_now);
        const net::ClientWorld* w = m_retry->world();
        if (m_retry->state() == net::ClientState::Connected && w != nullptr && w->stats().snapshotsApplied > 0) {
            // 새 연결이 그릴 수 있게 되면 바꿔 낀다 (그때까지 옛 복제본을 그렸다)
            m_session.reset();
            m_ownTransport = std::move(m_retryTransport);
            m_session = std::move(m_retry);
            m_lostAt = -1;
            ++m_reconnects;
            m_lastSubscribeAt = -1e9; // 관심 · 선택 상세를 새 연결에 다시 알린다
            selectionChanged();
            log::info("client", "다시 접속했습니다: {} (#{})", m_target.describe(), m_session->welcome()->clientId);
            return;
        }
        if (m_retry->state() == net::ClientState::Rejected || m_retry->state() == net::ClientState::Disconnected) {
            m_retry.reset();
            m_retryTransport.reset();
            m_nextRetryAt = m_now + kRetryInterval;
        }
    } else if (m_now >= m_nextRetryAt) {
        auto t = makeTransport();
        if (t) {
            m_retryTransport = std::move(*t);
            net::ClientSessionDesc cd = sessionDesc(*m_ownContent);
            cd.token = m_session->welcome()->sessionToken;
            m_retry = std::make_unique<net::ClientSession>(*m_retryTransport, cd);
            if (!m_retry->connect(m_target, m_now)) {
                m_retry.reset();
                m_retryTransport.reset();
            }
        }
        m_nextRetryAt = m_now + kRetryInterval;
    }
    if (m_lostAt >= 0 && m_now - m_lostAt > kReconnectSeconds) {
        m_failure = std::format("서버와 연결이 끊겼고 {:.0f} 초 동안 다시 접속하지 못했습니다 ({})", kReconnectSeconds,
                                m_target.describe());
    }
}

const net::ClientWorld* NetworkSession::clientWorld() const noexcept {
    return m_session ? m_session->world() : nullptr;
}

u64 NetworkSession::serverTick() const {
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
                if (!r.game.empty()) {
                    *m_failure += std::format("\n게임 {} 의 팩은 그 프로젝트의 content/ 에 있습니다 — "
                                              "--content <프로젝트 폴더>/content 로 다시 접속하십시오",
                                              describeGame(r.game));
                }
                return;
            }
            m_session.reset(); // 복제 월드가 옛 콘텐츠를 가리킨다 — 먼저 없앤다
            m_ownContent = std::make_unique<content::ContentDatabase>(std::move(*db));
        }
        if (m_ownContent->contentHash() != r.contentHash) {
            m_failure = std::format("콘텐츠가 다릅니다: 서버 {:016x}, 이 컴퓨터 {:016x} (팩 {} 개)", r.contentHash,
                                    m_ownContent->contentHash(), r.packs.size());
            if (!r.game.empty()) {
                // 게임 프로젝트 서버 — 팩은 그 프로젝트의 content/ 에 있다 (ADR-0041)
                *m_failure +=
                    std::format(" — 게임 {} 의 팩: --content <그 프로젝트 폴더>/content", describeGame(r.game));
            }
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
    case net::ClientState::Disconnected: {
        // 네트워크가 끊긴 것(시간 초과)이면 같은 토큰으로 다시 접속한다 (11.4) — 그동안 옛 복제본을 그린다
        const net::DisconnectReason reason = m_session->disconnectReason();
        if (!m_local && m_session->welcome() && reason == net::DisconnectReason::Timeout) {
            if (m_lostAt < 0) {
                m_lostAt = m_now;
                m_nextRetryAt = m_now;
                log::warn("client", "서버와 연결이 끊겼습니다 — 다시 접속합니다 ({})", m_target.describe());
            }
            updateReconnect();
            return;
        }
        m_failure =
            std::format("서버와 연결이 끊겼습니다 ({}): {}", m_target.describe(), net::disconnectReasonName(reason));
        return;
    }
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
        // 다른 연결(다시 접속)의 sequence 는 새로 시작한다 — 기다리던 에디터 명령은 결과가 오지 않는다. 같은 연결의
        // 월드가 바뀐 것(12D)이면 결과는 오지만 옛 월드의 것이라 에디터가 기록을 비운다
        for (const u32 seq : m_editorSequences) {
            m_outcomes.push_back({seq, false, 0, {}, "연결 · 월드가 바뀌어 결과를 받지 못했습니다"});
        }
        m_editorSequences.clear();
        m_preview.clear();
        selectionChanged();
    }
    if (world->stats().snapshotsApplied != m_seenSnapshots) {
        m_seenSnapshots = world->stats().snapshotsApplied;
        m_clock.onSnapshot(world->serverTick(), world->paused(), world->speed(), m_now);
    }

    refreshOverlay(*world);
    updateWorldOps();
    for (const net::SetRoleResultMsg& r : m_session->takeRoleResults()) {
        m_roleMessage = r.accepted ? std::format("#{} → {}", r.clientId, net::roleName(r.role))
                                   : std::format("역할 바꾸기 거절: {}", r.detail);
        if (!r.accepted) {
            log::warn("client", "{}", m_roleMessage);
        }
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
        if (const auto e = std::ranges::find(m_editorSequences, r.sequence); e != m_editorSequences.end()) {
            m_editorSequences.erase(e);
            m_outcomes.push_back(
                {r.sequence, r.accepted, r.appliedTick, r.created, r.accepted ? std::string() : m_lastRejection});
        }
    }

    // 관심 영역 (Phase 11): 화면이 바뀌면 (초당 2 번까지) 서버에 알린다
    if (m_view && m_now - m_lastSubscribeAt >= kSubscribeInterval && m_session->welcome()) {
        const net::Subscribe want = interestFor(*m_view, m_session->welcome()->world);
        if (m_session->subscribed() != want) {
            m_session->subscribe(want);
            m_lastSubscribeAt = m_now;
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
    applyPreview();
    SpriteExtraction::emit(m_snapshot, out);
    if (!m_selection.empty()) {
        (void)drawSelectionOutlines(m_snapshot, m_selection, out.selection);
        if (m_detailOverlay) {
            drawSelectedDetails(m_snapshot, out.debug);
        }
    }
}

net::Subscribe NetworkSession::interestFor(render::WorldRect visible, const net::WorldMeta& world) {
    // 타일 좌표 → 청크 번호 (아주 먼 곳 · NaN 도 i32 범위 안으로)
    auto chunkOf = [](f32 tile) -> i64 {
        const f64 c = std::floor(static_cast<f64>(tile) / static_cast<f64>(world::kChunkSize));
        return std::isfinite(c) ? static_cast<i64>(std::clamp(c, -1e9, 1e9)) : 0;
    };
    auto clampX = [&](i64 v) { return static_cast<i32>(std::clamp<i64>(v, world.minChunkX, world.maxChunkX)); };
    auto clampY = [&](i64 v) { return static_cast<i32>(std::clamp<i64>(v, world.minChunkY, world.maxChunkY)); };
    net::Subscribe s;
    s.all = false;
    s.minChunkX = clampX(chunkOf(std::min(visible.min.x, visible.max.x)) - 1);
    s.maxChunkX = clampX(chunkOf(std::max(visible.min.x, visible.max.x)) + 1);
    s.minChunkY = clampY(chunkOf(std::min(visible.min.y, visible.max.y)) - 1);
    s.maxChunkY = clampY(chunkOf(std::max(visible.min.y, visible.max.y)) + 1);
    if (s.minChunkX == world.minChunkX && s.minChunkY == world.minChunkY && s.maxChunkX == world.maxChunkX &&
        s.maxChunkY == world.maxChunkY) {
        return net::Subscribe{}; // 월드 전체
    }
    return s;
}

render::WorldRect NetworkSession::bounds() const {
    const net::ClientWorld* w = clientWorld();
    return w != nullptr ? SpriteExtraction::worldBounds(*w) : render::WorldRect{};
}

u32 NetworkSession::send(cmd::CommandPayload payload) {
    if (!m_session) {
        return 0;
    }
    const std::string name(cmd::commandName(payload));
    const bool isPause = std::holds_alternative<cmd::PauseSimulation>(payload);
    const bool isResume = std::holds_alternative<cmd::ResumeSimulation>(payload);
    const auto* speed = std::get_if<cmd::SetSimulationSpeed>(&payload);
    const f32 speedValue = speed != nullptr ? speed->speed : 0.0f;
    const u32 seq = m_session->sendCommand(std::move(payload));
    if (seq == 0) {
        return 0; // 접속 전
    }
    m_pendingNames[seq] = name;
    if (isPause || isResume) {
        m_pendingPause = std::pair{seq, isPause};
    }
    if (speed != nullptr) {
        m_pendingSpeed = std::pair{seq, speedValue};
    }
    return seq;
}

bool NetworkSession::playingReplay() const noexcept {
    const auto& st = m_session ? m_session->lastStats() : std::optional<net::ServerStats>{};
    return st && st->mode == 2;
}

bool NetworkSession::controlPaused() const noexcept {
    if (m_pendingPause) {
        return m_pendingPause->second;
    }
    return playingReplay() ? m_session->lastStats()->replayPaused : paused();
}

f32 NetworkSession::controlSpeed() const noexcept {
    if (m_pendingSpeed) {
        return m_pendingSpeed->second;
    }
    return playingReplay() ? m_session->lastStats()->replaySpeed : speed();
}

void NetworkSession::togglePause() {
    (void)send(controlPaused() ? cmd::CommandPayload{cmd::ResumeSimulation{}}
                               : cmd::CommandPayload{cmd::PauseSimulation{}});
}

void NetworkSession::stepOnce() {
    if (controlPaused()) {
        (void)send(cmd::StepSimulation{1});
    }
}

void NetworkSession::changeSpeed(int dir) {
    const f32 current = controlSpeed();
    // 지금 값에 가장 가까운 칸에서 한 칸
    usize idx = 0;
    for (usize i = 0; i < std::size(kSpeeds); ++i) {
        if (std::abs(kSpeeds[i] - current) < std::abs(kSpeeds[idx] - current)) {
            idx = i;
        }
    }
    const usize next = dir > 0 ? std::min(idx + 1, std::size(kSpeeds) - 1) : (idx > 0 ? idx - 1 : 0);
    if (kSpeeds[next] != current) {
        (void)send(cmd::SetSimulationSpeed{kSpeeds[next]});
    }
}

std::string NetworkSession::status() const {
    const net::ClientWorld* w = clientWorld();
    if (w == nullptr || !m_session) {
        return std::format("{} 접속 중", m_name);
    }
    std::string s = std::format("{} tick {} · 개체 {}", m_name, w->serverTick(), w->entityCount());
    if (m_lostAt >= 0) {
        s += std::format(" · 다시 접속 중 {:.0f} 초", m_now - m_lostAt);
    }
    if (const auto& st = m_session->lastStats()) {
        s += std::format(" · {:.1f} TPS · 틱 {:.1f} ms", st->ticksPerSecond, st->tickMsAvg);
    }
    s += w->paused() ? " · 일시정지" : std::format(" · ×{}", w->speed());
    if (const auto& st = m_session->lastStats(); st && st->mode == 1) {
        s += std::format(" · 기록 중 {}", st->modeName);
    } else if (st && st->mode == 2) {
        s += std::format(" · 재생 {} {}/{}{}", st->modeName, st->replayCall, st->replayCalls,
                         st->replayPaused ? " 멈춤" : "");
    }
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
    const auto id = client::pickAt(m_snapshot, world);
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
    auto ids = client::pickBox(m_snapshot, area);
    if (additive) {
        mergeSelection(m_selection, ids);
    } else {
        m_selection = std::move(ids);
    }
    selectionChanged();
}

// ---- IEditorHost (12A) ----

u32 NetworkSession::submit(cmd::CommandPayload payload) {
    const u32 seq = send(std::move(payload));
    if (seq != 0) {
        m_editorSequences.push_back(seq);
    }
    return seq;
}

std::vector<editor::CommandOutcome> NetworkSession::takeOutcomes() {
    return std::exchange(m_outcomes, {});
}

const content::ContentDatabase* NetworkSession::content() const {
    const net::ClientWorld* w = clientWorld();
    if (w == nullptr) {
        return nullptr;
    }
    return m_overlaid ? m_overlaid.get() : &w->content();
}

void NetworkSession::refreshOverlay(const net::ClientWorld& world) {
    if (m_overlaySession == m_session.get() && m_overlayRevision == m_session->overlayRevision()) {
        return;
    }
    m_overlaySession = m_session.get();
    m_overlayRevision = m_session->overlayRevision();
    const content::ContentOverlay& overlay = m_session->overlay();
    if (overlay.empty()) {
        m_overlaid.reset();
        m_overlayError.clear();
        return;
    }
    // 서버가 받아들인 오버레이다 — 같은 팩 · 같은 카탈로그면 여기서도 검증을 통과한다
    content::ContentLoadResult r = content::ContentLoader::applyOverlay(world.content(), overlay, *m_catalog);
    if (!r.ok()) {
        m_overlaid.reset();
        m_overlayError = std::format("서버의 콘텐츠 오버레이를 적용하지 못했습니다 (오류 {}개)", r.errorCount());
        for (const content::ContentIssue& i : r.issues) {
            if (i.severity == content::ContentIssue::Severity::Error) {
                m_overlayError += "\n  " + i.describe();
                break;
            }
        }
        log::warn("client", "{}", m_overlayError);
        return;
    }
    m_overlayError.clear();
    m_overlaid = std::make_shared<const content::ContentDatabase>(std::move(r.db));
}

editor::EditPermissions NetworkSession::permissions() const {
    const net::Role role = m_session ? m_session->role() : net::Role::Observer;
    editor::EditPermissions p;
    p.entities = net::roleAllows(role, net::CommandClass::EntityEdit);
    p.terrain = net::roleAllows(role, net::CommandClass::TerrainEdit);
    p.content = net::roleAllows(role, net::CommandClass::ContentEdit);
    p.simulation = net::roleAllows(role, net::CommandClass::SimulationControl);
    return p;
}

std::optional<PlayersInfo> NetworkSession::players() const {
    if (!m_session || !m_session->welcome()) {
        return std::nullopt;
    }
    PlayersInfo info;
    info.myRole = static_cast<u8>(m_session->role());
    info.message = m_roleMessage;
    const u16 me = m_session->welcome()->clientId;
    for (const net::PlayerEntry& p : m_session->players()) {
        info.players.push_back({p.clientId, static_cast<u8>(p.role), p.name, p.clientId == me});
    }
    return info;
}

void NetworkSession::requestRole(u16 clientId, u8 role) {
    if (role > static_cast<u8>(net::Role::Owner) || !m_session) {
        return;
    }
    if (!m_session->requestRole(clientId, static_cast<net::Role>(role))) {
        m_roleMessage = "접속 전이라 역할을 바꿀 수 없습니다";
    }
}

void NetworkSession::updateWorldOps() {
    // 접속하면 (다시 접속해도) 목록을 한 번
    if (m_listSession != m_session.get() && m_session->welcome()) {
        if (m_session->requestWorldOp(net::WorldOpKind::List)) {
            m_listSession = m_session.get();
        }
    }
    bool relist = false;
    for (const net::WorldOpResultMsg& r : m_session->takeWorldOpResults()) {
        const std::string what = r.name.empty() ? std::string(net::worldOpName(r.op))
                                                : std::format("{} '{}'", net::worldOpName(r.op), r.name);
        m_worldMessage =
            r.accepted ? std::format("{}: {}", what, r.detail) : std::format("{} 거절: {}", what, r.detail);
        if (r.accepted) {
            log::info("client", "{}", m_worldMessage);
            relist = true; // 폴더가 바뀌었을 수 있다
        } else {
            log::warn("client", "{}", m_worldMessage);
        }
    }
    if (m_session->worldChanges() != m_seenWorldChanges) {
        m_seenWorldChanges = m_session->worldChanges();
        if (const auto& wc = m_session->lastWorldChange()) {
            log::info("client", "서버 월드가 바뀌었습니다 ({} '{}', generation {})",
                      wc->reason == net::WorldChangeReason::Load ? "불러오기" : "리플레이", wc->name, wc->generation);
        }
    }
    if (relist) {
        (void)m_session->requestWorldOp(net::WorldOpKind::List);
    }
}

std::optional<WorldOpsInfo> NetworkSession::worldOps() const {
    if (!m_session || !m_session->welcome()) {
        return std::nullopt;
    }
    WorldOpsInfo info;
    info.canChange = permissions().simulation; // 실행 제어와 같은 열 (admin 부터)
    if (const auto& list = m_session->worldList()) {
        info.listed = true;
        for (const net::SavedWorld& w : list->worlds) {
            info.saved.push_back({w.name, w.replay, w.tick, w.entities});
        }
    }
    if (const auto& st = m_session->lastStats()) {
        info.mode = st->mode;
        info.modeName = st->modeName;
        info.replayCall = st->replayCall;
        info.replayCalls = st->replayCalls;
        info.hashesChecked = st->hashesChecked;
        info.mismatch = st->mismatch;
        info.replayPaused = st->replayPaused;
        info.replaySpeed = st->replaySpeed;
    }
    info.generation = m_session->generation();
    info.message = m_worldMessage;
    return info;
}

void NetworkSession::worldOp(WorldOp op, std::string name) {
    if (!m_session || !m_session->requestWorldOp(static_cast<net::WorldOpKind>(op), std::move(name))) {
        m_worldMessage = "접속 전이라 요청할 수 없습니다";
    }
}

const world::WorldGrid* NetworkSession::grid() const {
    const net::ClientWorld* w = clientWorld();
    return w != nullptr ? &w->grid() : nullptr;
}

std::optional<NetEntityId> NetworkSession::pickAt(Vec2 world) const {
    return client::pickAt(m_snapshot, world);
}

std::vector<NetEntityId> NetworkSession::pickBox(render::WorldRect area) const {
    return client::pickBox(m_snapshot, area);
}

void NetworkSession::setSelection(std::vector<NetEntityId> ids) {
    std::ranges::sort(ids);
    const auto [first, last] = std::ranges::unique(ids);
    ids.erase(first, last);
    if (ids != m_selection) {
        m_selection = std::move(ids);
        selectionChanged();
    }
}

void NetworkSession::setPreview(std::span<const editor::PreviewOffset> offsets) {
    m_preview.assign(offsets.begin(), offsets.end());
    std::ranges::sort(m_preview, {}, &editor::PreviewOffset::id);
}

void NetworkSession::applyPreview() {
    if (m_preview.empty()) {
        return;
    }
    const auto offsetOf = [&](NetEntityId id) -> const Vec2* {
        const auto it = std::ranges::lower_bound(m_preview, id, {}, &editor::PreviewOffset::id);
        return it != m_preview.end() && it->id == id ? &it->offset : nullptr;
    };
    for (SnapshotSprite& s : m_snapshot.sprites) {
        if (const Vec2* o = offsetOf(s.id)) {
            s.position += *o;
        }
    }
    for (SelectedDetail& d : m_snapshot.selected) {
        if (const Vec2* o = offsetOf(d.id)) {
            d.position += *o;
        }
    }
}

std::vector<editor::ComponentState> NetworkSession::components(NetEntityId id) const {
    std::vector<editor::ComponentState> out;
    const net::ClientWorld* w = clientWorld();
    const ecs::EntityId e = w != nullptr ? w->find(id) : ecs::kNullEntity;
    if (e == ecs::kNullEntity) {
        return out;
    }
    // 서버 전용 값: 선택한 개체면 선택 상세에 온다 (한 스냅숏 늦을 수 있다)
    const net::InspectEntry* inspect = nullptr;
    if (m_session && m_session->inspect()) {
        for (const net::InspectEntry& entry : m_session->inspect()->entries) {
            if (entry.netId == id) {
                inspect = &entry;
            }
        }
    }
    const ecs::Registry& reg = w->registry();
    for (const ecs::ComponentInfo& info : w->catalog().all()) {
        if (!ecs::hasFlag(info.flags, ecs::ComponentFlags::EditorVisible)) {
            continue;
        }
        if (ecs::hasFlag(info.flags, ecs::ComponentFlags::Replicated)) {
            const ecs::ComponentPoolBase* pool = reg.poolByStableId(info.stableId);
            if (const void* raw = pool != nullptr ? pool->getRaw(e) : nullptr) {
                ecs::Json j;
                info.writeJson(raw, j);
                out.push_back({info.stableId, info.name, std::move(j), true});
            }
        } else if (inspect != nullptr) {
            for (const net::InspectComponent& c : inspect->components) {
                if (c.stableId != info.stableId) {
                    continue;
                }
                ecs::Json j = ecs::Json::parse(c.json, nullptr, false);
                if (!j.is_discarded()) {
                    out.push_back({info.stableId, info.name, std::move(j), false});
                }
            }
        }
    }
    return out;
}

void NetworkSession::listEntities(std::vector<editor::EntityRow>& out) const {
    out.clear();
    const net::ClientWorld* w = clientWorld();
    if (w == nullptr) {
        return;
    }
    const ecs::Registry& reg = w->registry();
    reg.forEachEntityByIndex([&](ecs::EntityId e) {
        if (const auto* ni = reg.tryRead<comp::NetIdentity>(e)) {
            const auto* prefab = reg.tryRead<comp::PrefabSource>(e);
            out.push_back({ni->netId, prefab != nullptr ? prefab->prefab.view() : std::string_view()});
        }
    });
    std::ranges::sort(out, {}, &editor::EntityRow::id);
}

std::optional<Vec2> NetworkSession::positionOf(NetEntityId id) const {
    for (const SnapshotSprite& s : m_snapshot.sprites) {
        if (s.id == id) {
            return s.position;
        }
    }
    return std::nullopt;
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
    if (m_session && m_session->welcome()) {
        i.game = describeGame(m_session->welcome()->game);
    }
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
    n.role = std::string(net::roleName(m_session->role())); // 12C: 바뀐 역할까지
    i.canControl = permissions().simulation;
    n.rttMs = m_session->transportStats().rttMs;
    n.snapshots = w->stats().snapshotsApplied;
    n.resyncs = w->stats().resets;
    n.epoch = w->epoch();
    n.receivedKBps = m_receivedKBps;
    n.applyMs = m_applyMs;
    n.delayMs = m_clock.delayTicks() / std::max(1e-6, 30.0 * static_cast<f64>(w->speed())) * 1000.0;
    n.behindTicks = static_cast<f64>(w->serverTick()) - m_snapshot.renderTick;
    n.commandsRejected = m_commandsRejected;
    n.reconnecting = m_lostAt >= 0;
    n.reconnects = m_reconnects;
    if (const auto& sub = m_session->subscribed(); sub && !sub->all) {
        n.interest =
            std::format("청크 {}..{} × {}..{}", sub->minChunkX, sub->maxChunkX, sub->minChunkY, sub->maxChunkY);
    } else {
        n.interest = "월드 전체";
    }
    n.lastRejection = m_lastRejection;
    i.net = std::move(n);
    return i;
}

} // namespace sbx::client
