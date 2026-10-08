#pragma once
// 서버 한 대: 월드 하나 + Transport 하나 + 접속한 클라이언트들. docs/01-ARCHITECTURE.md 4 · 5장, docs/08-NETWORK.md 4 ·
// 10장, ADR-0024.
//
// 두 절반 (T1 · T2 — 스레드 사이에는 복사한 값만 오간다)
//   Net 절반   Transport · 핸드셰이크 · 클라이언트 표 · CommandValidator · 메시지 인코딩. 월드를 보지 않는다.
//   Sim 절반   ScenarioRunner(월드)를 30 TPS × speed 로 돌린다. 받은 명령에 executeTick = 다음 틱을 찍어 넣고, 적용
//              결과(CommandResult)와 1 초마다 ServerStats 를 Net 절반에 넘긴다.
//   잇는 것     inbox (Net → Sim, 검사를 통과한 명령) · outbox (Sim → Net, 보낼 메시지) · status (Sim 이 쓰는 월드 요약
//   —
//              Welcome 에 싣는다). 모두 뮤텍스 + vector 교환 (배치마다 락 한 번, 01 5.3).
//
// 진행 방식 (ADR-0021 의 DirectSim 과 같은 두 가지)
//   Threaded  start() 가 Simulation 스레드와 Net IO 스레드를 띄운다. SandboxServer.
//   Inline    update(dt) 가 그 자리에서 Net → 틱(밀린 만큼, 최대 kMaxCatchUpTicks) → Net 을 돈다. 단위 테스트 — 시간을
//             손으로 넣으므로 결정적이다.
//
// 핸드셰이크 (08 4장): Connected → Hello → Challenge{nonce} → Auth → Welcome | Reject + 끊기.
//   Reject: 버전이 다름 · 콘텐츠 해시가 다름(팩 목록과 해시를 실어 준다) · 가득 참 · 순서 틀림 · nonce 틀림 ·
//           이름 규칙 · 시간 초과(handshakeTimeout). 해석할 수 없는 메시지 → ProtocolError 로 끊는다.
//   [계획] Subscribe · Bulk · Ready · Snapshot (Phase 10 · 11), 재접속 토큰 (11.4), 역할 바꾸기 (12).

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "core/scenarios/Scenario.hpp"
#include "network/protocol/Messages.hpp"
#include "network/replication/ReplicationWriter.hpp"
#include "network/server/CommandValidator.hpp"
#include "network/transport/Transport.hpp"

namespace sbx::net {

enum class ServerMode : u8 { Inline = 0, Threaded };

struct ServerHostDesc {
    std::string worldName = "world";
    u32 maxClients = 16;
    Role defaultRole = Role::Editor; // 10 7장: 기본 역할 (--default-role)
    u64 buildId = 0;
    std::vector<std::string> packs; // 콘텐츠 팩 id (Reject{ContentMismatch} 로 알려 준다)
    f64 handshakeTimeoutSeconds = 10;
    f64 statsIntervalSeconds = 1;
    CommandValidatorDesc validator;
    u64 randomSeed = 0; // nonce · 세션 토큰. 0 = std::random_device (테스트는 고정)
    ServerMode mode = ServerMode::Inline;
    sim::Tick stopAtTick = 0; // 0 이 아니면 그 틱에서 진행을 멈춘다 (--ticks N --exit)
    // 복제 (Phase 10, ADR-0025): 스냅숏 간격(Simulation 단계 수 — 2 = 30 TPS 에서 15 Hz) · 클라이언트당 초당 바이트
    // 예산 (0 = 제한 없음 — Loopback 싱글플레이). 08 6.3: 원격 기본 256 KB/s
    bool replicate = true;
    u32 snapshotIntervalSteps = 2;
    usize snapshotBytesPerSecond = 256 * 1024;
};

// 서버 상태 요약 (아무 스레드에서나 읽는다 — 복사본)
struct ServerHostStats {
    sim::Tick tick = 0;
    u64 ticksRun = 0;
    usize clients = 0;     // Welcome 까지 마친 클라이언트
    usize connections = 0; // 핸드셰이크 중 포함
    u64 commandsAccepted = 0;
    u64 commandsRejected = 0; // 검사 · 적용 거절 모두
    u64 handshakesRejected = 0;
    u64 overruns = 0; // 너무 밀려 기준점을 다시 잡은 횟수 (Threaded)
    bool paused = false;
    f32 speed = 1.0f;
    bool reachedStopTick = false;
    f64 replicationMs = 0; // 스냅숏 만들기 (모든 클라이언트) — 최근 값의 지수 평균, Simulation 스레드 시간
    u64 snapshotsBuilt = 0;
};

class ServerHost {
public:
    static constexpr int kMaxCatchUpTicks = 3;

    // transport 는 ServerHost 보다 오래 살아야 한다. runner 의 월드가 쓰는 catalog · content 도.
    ServerHost(INetworkTransport& transport, std::unique_ptr<scenario::ScenarioRunner> runner, ServerHostDesc desc);
    ~ServerHost(); // stop()
    ServerHost(const ServerHost&) = delete;
    ServerHost& operator=(const ServerHost&) = delete;
    ServerHost(ServerHost&&) = delete;
    ServerHost& operator=(ServerHost&&) = delete;

    // listen 하고 (Threaded) 스레드를 띄운다
    [[nodiscard]] Expected<void> start(const Endpoint& at);
    // Inline 전용: dt 초만큼 진행
    void update(f64 dtSeconds);
    // 모든 클라이언트에 Disconnect{ServerShutdown} 을 보내고 끊은 뒤 스레드를 멈춘다. 두 번 불러도 된다
    void stop();

    [[nodiscard]] ServerHostStats stats() const;
    [[nodiscard]] bool running() const noexcept { return m_started && !m_stopped; }
    // 월드: Inline 이거나 (Threaded) 시작 전 · 멈춘 뒤에만
    [[nodiscard]] sim::SimulationWorld& world();
    [[nodiscard]] u16 boundPort() const noexcept { return m_transport.boundPort(); }

private:
    struct Client {
        enum class State : u8 { AwaitHello, AwaitAuth, Active };
        State state = State::AwaitHello;
        f64 connectedAt = 0;
        u64 nonce = 0;
        u16 clientId = 0;
        Role role = Role::Observer;
        std::string name;
        ClientCommandState commands;
    };
    struct ValidatedCommand {
        cmd::ClientId issuer = 0;
        u32 sequence = 0;
        cmd::CommandPayload payload;
    };
    struct Outgoing {
        u16 clientId = 0; // 0 = Welcome 을 마친 모두
        Message message;
    };
    struct WorldStatus {
        sim::Tick tick = 0;
        bool paused = false;
        f32 speed = 1.0f;
    };

    // --- Net 절반 (Net IO 스레드 또는 Inline) ---
    void netStep(f64 now);
    void onReceived(ConnectionId c, Client& client, std::span<const std::byte> data, f64 now);
    void sendTo(ConnectionId c, const Message& m);
    void reject(ConnectionId c, Client& client, RejectReason reason, std::string detail);
    void dropConnection(ConnectionId c, DisconnectReason reason);
    void flushOutbox();
    void netShutdown();
    void netThreadMain();

    // --- Sim 절반 (Simulation 스레드 또는 Inline) ---
    void simStep(f64 now);
    void simThreadMain();
    void publishStatus();

    INetworkTransport& m_transport;
    std::unique_ptr<scenario::ScenarioRunner> m_runner;
    ServerHostDesc m_desc;
    CommandValidator m_validator;
    WorldMeta m_meta; // 바뀌지 않는 월드 정보 (이름 · 경계)
    u64 m_contentHash = 0;

    // Net 절반 소유
    std::map<ConnectionId, Client> m_clients;
    std::map<u16, ConnectionId> m_byClientId;
    u16 m_nextClientId = 1;
    std::mt19937_64 m_random;
    std::vector<TransportEvent> m_events;

    // Sim 절반 소유
    std::unique_ptr<ReplicationWriter>
        m_replication; // 표(table)는 만든 뒤 바뀌지 않는다 — Net 절반이 Welcome 에 읽는다
    u64 m_steps = 0;
    std::vector<std::pair<u16, Message>> m_replicationOut;
    // 네트워크에서 온 명령 (issuer, sequence) — 결과를 돌려줄 것. 시나리오가 넣은 명령도 issuer 가 0 이 아닐 수 있어
    // (random_walk 는 1) issuer 만으로는 가릴 수 없다
    std::set<std::pair<cmd::ClientId, u32>> m_awaitingResult;
    f64 m_nextTickAt = 0;
    f64 m_statsWindowStart = 0;
    f64 m_tickMsSum = 0;
    f64 m_tickMsMax = 0;
    u32 m_ticksInWindow = 0;

    // 스레드 사이
    mutable std::mutex m_mutex;
    std::vector<ValidatedCommand> m_inbox;
    std::vector<Outgoing> m_outbox;
    struct RosterEvent {
        u16 clientId = 0;
        bool joined = false;
    };
    std::vector<RosterEvent> m_roster;               // Net → Sim: 복제 대상 추가 · 제거
    std::vector<std::pair<u16, SnapshotAck>> m_acks; // Net → Sim
    WorldStatus m_status;
    ServerHostStats m_stats;

    f64 m_inlineTime = 0;
    bool m_started = false;
    bool m_stopped = false;
    std::atomic<bool> m_quitSim{false};
    std::atomic<bool> m_quitNet{false};
    std::thread m_simThread;
    std::thread m_netThread;
};

} // namespace sbx::net
