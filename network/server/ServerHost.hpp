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
// 진행 방식 (두 가지 — LocalServerHost 도 같다, ADR-0026)
//   Threaded  start() 가 Simulation 스레드와 Net IO 스레드를 띄운다. SandboxServer.
//   Inline    update(dt) 가 그 자리에서 Net → 틱(밀린 만큼, 최대 kMaxCatchUpTicks) → Net 을 돈다. 단위 테스트 — 시간을
//             손으로 넣으므로 결정적이다.
//
// 핸드셰이크 (08 4장): Connected → Hello → Challenge{nonce} → Auth → Welcome | Reject + 끊기.
//   Reject: 버전이 다름 · 콘텐츠 해시가 다름(팩 목록과 해시를 실어 준다) · 가득 참 · 순서 틀림 · nonce 틀림 ·
//           이름 규칙 · 시간 초과(handshakeTimeout). 해석할 수 없는 메시지 → ProtocolError 로 끊는다.
// 복제 (Phase 10, ADR-0025 · 0026): Sim 절반이 스냅숏 간격(실제 시간 — 속도 배율과 무관하게 초당 15 번)마다
//   ReplicationWriter::build, 선택 상세를 요청한 클라이언트에는 InspectResult 도.
// 관심 (Phase 11): Subscribe · 선택(Inspect) 을 ReplicationWriter 에 넘긴다.
// 다시 접속 (11.4): Welcome.sessionToken 을 Auth.token 으로 가져오면 reconnectSeconds 안이면 같은 clientId · 역할.
//   아직 살아 있는 연결의 토큰이면 옛 연결을 끊고(Kicked) 이어 받는다 (클라이언트 쪽 네트워크만 바뀐 경우). 스스로
//   나간(ClientQuit) 토큰은 지운다. 상태(복제)는 처음부터 — 클라이언트가 새 epoch 로 받는다.
// 접속자 · 역할 (12C, ADR-0030): 들어옴 · 나감 · 역할이 바뀔 때마다 모두에게 PlayerList. SetRole 은 Net 절반이
//   10-EDITOR 7장 표대로 판단한다 (roleCanAssign — Owner = 누구든 어떤 역할로, Admin = Admin 아래인 사람을 Editor
//   이하로, 자기 자신은 못 바꾼다). 바뀐 역할은 다시 접속 표에도. Auth.ownerKey 가 --owner-key 와 같으면 Owner.
// 월드 오버레이 (12C): 들어온 클라이언트에 ContentOverlay{reset} 로 전체, 월드의 오버레이가 바뀌면 (EditContent) 바뀐
//   항목만 모두에게 — Sim 절반이 contentRevision 으로 알아챈다.
// 세이브 · 로드 · 리플레이 (12D, ADR-0031): WorldOp 을 Net 절반이 권한(목록 = 모두, 나머지 = admin 부터)으로 거르고 Sim
//   절반이 틱 사이에 한다 — <savesRoot>/<이름>. 로드 · 재생은 월드를 통째로 바꾼다 (같은 콘텐츠의 세이브만): generation++,
//   복제를 새 epoch 로, 모두에게 WorldChanged · 오버레이 전체, 그 배치의 명령은 거절. 기록 = 시작 세이브 + ReplayRecorder
//   (tick() 마다), 재생 = ReplayPlayer 로 한 call 씩 (편집 명령 거절, 실행 제어는 재생을 멈추고 · 한 call 씩 · 빠르게),
//   끝나면 보통 월드로 돌아와 일시정지. 서버를 멈추면 기록 중인 리플레이를 쓴다.

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "core/persist/WorldSave.hpp"
#include "core/replay/Replay.hpp"
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
    std::vector<std::string> packs;  // 콘텐츠 팩 id (Reject{ContentMismatch} 로 알려 준다)
    std::string ownerKey;            // 비지 않으면 Auth.ownerKey 가 같은 사람이 Owner (--owner-key, 12C)
    std::filesystem::path savesRoot; // 세이브 · 리플레이 폴더 (12D, --saves). 비면 목록만 (빈 목록)
    f64 handshakeTimeoutSeconds = 10;
    f64 statsIntervalSeconds = 1;
    f64 reconnectSeconds = 60; // 다시 접속 토큰이 유효한 시간 (끊긴 뒤)
    CommandValidatorDesc validator;
    u64 randomSeed = 0; // nonce · 세션 토큰. 0 = std::random_device (테스트는 고정)
    ServerMode mode = ServerMode::Inline;
    // 게임 프로젝트면 그 정체성 (Welcome · Reject 에 — v0.2 M1-T3, ADR-0042). 서버가 사는 동안 같다
    GameIdentity game;
    // 게임 진행 (v0.2 M2-T3, ADR-0045): 시작 월드 폴더 (비면 재시작 없음) · 게임 모드 id · 시작에 필요한 참가자 수
    // (역할 player 이상으로 접속한 사람 — GameMode.players.min). 재시작 = 시작 월드를 다시 열고 세션을 Lobby 로,
    // GameMode.restart 가 auto 면 Finished 뒤 delay 만큼 서버 단계가 지나면 스스로
    std::filesystem::path entryWorld;
    std::string gameMode;
    std::string packageDigest; // 게임 패키지로 돌리면 그 digest (v0.2 M5-T2 — 세이브의 "game".package)
    u32 minPlayers = 0;
    sim::Tick stopAtTick = 0; // 0 이 아니면 그 틱에서 진행을 멈춘다 (--ticks N --exit)
    // 복제 (Phase 10, ADR-0025): 스냅숏 간격(30 TPS 틱 수로 센 실제 시간 — 2 = 15 Hz, 속도 배율과 무관 · 10B) ·
    // 클라이언트당 초당 바이트 예산 (0 = 제한 없음 — Loopback 싱글플레이). 08 6.3: 원격 기본 256 KB/s
    bool replicate = true;
    u32 snapshotIntervalSteps = 2;
    usize snapshotBytesPerSecond = 256 * 1024;
};

// 게임 프로젝트의 정체성 · 진행 설정을 desc 에 (game · entryWorld · gameMode · minPlayers — v0.2 M2-T3).
// projectDir = --world 에 준 프로젝트 폴더, content = 그 프로젝트 콘텐츠 (모드의 players.min)
void setGameProject(ServerHostDesc& desc, const project::GameManifest& manifest,
                    const std::filesystem::path& projectDir, const content::ContentDatabase& content);

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
    u32 generation = 0;  // 월드가 바뀐 횟수 (12D)
    u8 mode = 0;         // 0 보통 · 1 기록 중 · 2 재생 중 · 3 테스트 플레이 (ServerStats.mode)
    u64 replayCall = 0;  // 기록: 기록한 tick() 수 · 재생: 재생한 call
    u64 replayCalls = 0; // 재생: 전체 call
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
    // 다시 접속 토큰 수 (시험) — Net 절반 소유라 Inline 에서만
    [[nodiscard]] usize reconnectTickets() const noexcept { return m_tickets.size(); }
    // 접속자 표 (시험 — Inline 에서만, Net 절반 소유)
    [[nodiscard]] PlayerList players() const;

private:
    struct Client {
        enum class State : u8 { AwaitHello, AwaitAuth, Active };
        State state = State::AwaitHello;
        f64 connectedAt = 0;
        u64 nonce = 0;
        u16 clientId = 0;
        Role role = Role::Observer;
        std::string name;
        std::string token; // 세션 토큰 (바이트)
        ClientCommandState commands;
    };
    // 다시 접속 표 (11.4) — 토큰 → 누구였나
    struct Ticket {
        u16 clientId = 0;
        Role role = Role::Observer;
        bool active = true; // 지금 연결이 있다
        f64 expiresAt = 0;  // active 가 아니면 이 시각까지
    };
    void releaseTicket(const Client& client, DisconnectReason reason);
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
        WorldMeta meta;     // 로드하면 바뀐다 (Welcome)
        u32 generation = 0; // Welcome
    };
    struct WorldOpTask {
        u16 clientId = 0;
        WorldOpRequest request;
    };
    // 리플레이 기록 · 재생 (12D, Sim 절반 소유)
    struct Recording {
        std::string name;
        std::filesystem::path dir;
        std::unique_ptr<replay::ReplayRecorder> recorder;
    };
    struct Playback {
        std::string name;
        replay::Replay replay;                        // player 가 가리킨다 (Playback 은 힙에 — 주소 고정)
        std::unique_ptr<replay::ReplayPlayer> player; // 월드 = m_runner 의 것
        bool paused = false;
        u32 steps = 0; // 멈춘 동안 진행할 call
        f32 speed = 1.0f;
    };

    // --- Net 절반 (Net IO 스레드 또는 Inline) ---
    void netStep(f64 now);
    void onReceived(ConnectionId c, Client& client, std::span<const std::byte> data, f64 now);
    void sendTo(ConnectionId c, const Message& m);
    void reject(ConnectionId c, Client& client, RejectReason reason, std::string detail);
    void dropConnection(ConnectionId c, DisconnectReason reason);
    void broadcastPlayers();
    void onSetRole(ConnectionId c, Client& client, const SetRoleRequest& request);
    void flushOutbox();
    void netShutdown();
    void netThreadMain();

    // --- Sim 절반 (Simulation 스레드 또는 Inline) ---
    void simStep(f64 now);
    void simThreadMain();
    void publishStatus();
    void writeModeStats(); // m_stats 의 generation · 기록/재생 (m_mutex 를 잡고)
    [[nodiscard]] f64 snapshotIntervalSeconds() const noexcept;
    // 다음 단계까지 (월드의 틱 간격, 재생 중이면 재생 속도로 나눈다)
    [[nodiscard]] f64 stepIntervalSeconds() const noexcept;
    void runWorldOp(const WorldOpTask& task, std::vector<Outgoing>& out, bool& replaced);
    // keepOld 가 있으면 옛 월드를 버리지 않고 거기에 맡긴다 (테스트 플레이 — 편집 월드를 그대로 둔다)
    void replaceWorld(std::unique_ptr<scenario::ScenarioRunner> runner, WorldChangeReason reason,
                      const std::string& name, std::vector<Outgoing>& out,
                      std::unique_ptr<scenario::ScenarioRunner>* keepOld = nullptr);
    // 테스트 플레이 (v0.2 M5-T1, ADR-0053): 편집 월드의 복사본으로 바꾸고 원본은 m_editRunner 에 맡긴다 · 되돌린다.
    // 결과 요약
    [[nodiscard]] Expected<std::string> startTestPlay(std::vector<Outgoing>& out);
    [[nodiscard]] Expected<std::string> stopTestPlay(std::vector<Outgoing>& out);
    // 세이브에 쓸 게임 정체성 (게임 프로젝트 · 패키지 서버만, v0.2 M5-T2)
    [[nodiscard]] std::optional<persist::GameBinding> gameBinding() const;
    [[nodiscard]] Expected<std::string> stopRecording(); // 기록을 끝내 replay.sbxr 로 쓴다 — 요약
    // 게임 재시작 (WorldOp Restart · 자동, M2-T3): 시작 월드를 다시 열어 바꾸고 세션을 Lobby 로. 결과 요약
    [[nodiscard]] Expected<std::string> restartGame(std::vector<Outgoing>& out);
    [[nodiscard]] bool autoRestartDue();
    [[nodiscard]] u32 playerCount() const; // 역할 player 이상으로 접속한 사람 수 (네트워크 스레드)
    [[nodiscard]] WorldList listWorlds() const;
    void handlePlaybackCommand(const ValidatedCommand& c, std::vector<Outgoing>& results);

    INetworkTransport& m_transport;
    std::unique_ptr<scenario::ScenarioRunner> m_runner;
    ServerHostDesc m_desc;
    CommandValidator m_validator;
    WorldMeta m_meta; // 월드 정보 (이름 · 경계) — Sim 절반 소유, 로드하면 바뀐다 (Net 절반은 m_status.meta)
    u64 m_contentHash = 0;

    // Net 절반 소유
    std::map<ConnectionId, Client> m_clients;
    std::map<u16, ConnectionId> m_byClientId;
    std::map<std::string, Ticket> m_tickets;
    f64 m_netNow = 0;
    u16 m_nextClientId = 1;
    std::mt19937_64 m_random;
    std::vector<TransportEvent> m_events;

    // Sim 절반 소유
    std::unique_ptr<ReplicationWriter>
        m_replication; // 표(table)는 만든 뒤 바뀌지 않는다 — Net 절반이 Welcome 에 읽는다
    u64 m_steps = 0;
    std::vector<std::pair<u16, Message>> m_replicationOut;
    f64 m_nextSnapshotAt = 0;                          // 다음 스냅숏 시각 (실제 초)
    std::map<u16, std::vector<NetEntityId>> m_inspect; // 클라이언트별 선택 상세 (10B)
    content::ContentOverlay m_sentOverlay;             // 마지막으로 모두에게 보낸 월드 오버레이 (12C)
    u64 m_sentContentRevision = 0;
    u32 m_generation = 0; // 월드가 바뀐 횟수 (12D)
    std::optional<Recording> m_recording;
    std::unique_ptr<Playback> m_playback;
    u32 m_serverSequence = 0; // 서버가 넣는 명령 (재생 끝 일시정지)
    u64 m_finishedSteps = 0;  // 게임이 Finished 로 머문 서버 단계 수 (자동 재시작, M2-T3)
    // 테스트 플레이 중 맡겨 둔 편집 월드 (v0.2 M5-T1) — 돌리지 않는다, 멈추면 그대로 돌아온다
    std::unique_ptr<scenario::ScenarioRunner> m_editRunner;
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
    // Net → Sim: 선택 상세 요청 (10B) · 관심 영역 (11)
    std::vector<std::pair<u16, InspectRequest>> m_inspectRequests;
    std::vector<std::pair<u16, Subscribe>> m_subscribes;
    std::vector<WorldOpTask> m_worldOps; // Net → Sim (12D)
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
