#pragma once
// SandboxClient 의 월드 = 서버의 복제본 (Phase 10B, docs/08-NETWORK.md 4 · 6.4 · 9장, docs/16-ROADMAP.md 10.5,
// ADR-0026). --direct-sim(클라이언트가 Core 를 직접 돌리는 임시 경로, ADR-0020 · 0021)을 대신한다.
//
//   Local   --world <시나리오|세이브 폴더>: 같은 프로세스의 LocalServerHost(ServerHost + Loopback)에 접속한다.
//           창 실행은 서버가 자기 스레드(Simulation · Net IO)에서 돈다. 헤드리스 · 시험은 inlineServer — update(dt) 가
//           그 자리에서 서버를 돌린다 (같은 dt 열이면 같은 결과).
//   Remote  --connect host:port: ENet 으로 SandboxServer 에 접속한다. 콘텐츠가 다르다고 거절되면 서버가 알려 준 팩을
//           --content 루트에서 읽어 한 번 다시 접속한다 (sbx_net_probe 와 같은 규칙).
//
// 둘 다 같은 길: ClientSession 이 Snapshot 을 ClientWorld 에 적용하고, 프레임마다 InterpolationClock 의 renderTick 으로
// SpriteExtraction 이 그릴 거리를 만든다. 일시정지 · 한 틱 · 속도는 서버로 가는 명령 (역할이 모자라면 거절 — 상태 줄과
// 네트워크 패널에 보인다). 선택은 netId 로, 선택한 개체의 서버 전용 상태는 Inspect 로 받는다.
// 한 스레드(Main)에서 쓴다.

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "apps/client/WorldSession.hpp"
#include "apps/client/presentation/SpriteExtraction.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "network/client/ClientSession.hpp"
#include "network/client/InterpolationClock.hpp"
#include "network/server/LocalServerHost.hpp"

namespace sbx::client {

enum class NetworkSessionMode : u8 { Local = 0, Remote };

struct NetworkSessionDesc {
    NetworkSessionMode mode = NetworkSessionMode::Local;
    std::string world;   // Local: 시나리오 이름 · 세이브 폴더
    std::string connect; // Remote: host[:port] (기본 포트 7777)
    std::string displayName = "player";
    std::filesystem::path contentRoot;
    u64 seed = 1;
    u32 workers = 0;           // Local: 시뮬레이션 Job Worker 수
    bool inlineServer = false; // Local: update(dt) 안에서 서버를 돌린다 (헤드리스 · 시험)
    f64 connectTimeoutSeconds = 10;
    // Remote: Transport 를 만든다 (없으면 ENet). 시험은 Loopback 을 넣는다
    std::function<Expected<std::unique_ptr<net::INetworkTransport>>()> transportFactory;
};

class NetworkSession final : public IWorldSession {
public:
    static Expected<std::unique_ptr<NetworkSession>> create(const NetworkSessionDesc& desc,
                                                            render::MaterialLibrary& materials);
    ~NetworkSession() override; // 접속을 끊고 (로컬이면) 서버를 멈춘다

    NetworkSession(const NetworkSession&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    NetworkSession(NetworkSession&&) = delete;
    NetworkSession& operator=(NetworkSession&&) = delete;

    void update(f64 dtSeconds) override;
    [[nodiscard]] bool ready() const override;
    [[nodiscard]] std::optional<std::string> failure() const override { return m_failure; }
    void extract(render::RenderWorld& out) override;
    [[nodiscard]] render::WorldRect bounds() const override;
    void togglePause() override;
    void stepOnce() override;
    void changeSpeed(int dir) override;
    [[nodiscard]] std::string status() const override;
    void selectAt(Vec2 world, bool additive) override;
    void selectBox(render::WorldRect area, bool additive) override;
    void clearSelection() override;
    void setDetailOverlay(bool on) override { m_detailOverlay = on; }
    [[nodiscard]] std::string selectionStatus() const override;
    [[nodiscard]] WorldInfo info() const override;

    // 시험 · 끝 요약
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const net::ClientWorld* clientWorld() const noexcept;
    [[nodiscard]] net::ClientSession* session() noexcept { return m_session.get(); }
    [[nodiscard]] net::LocalServerHost* localServer() noexcept { return m_local.get(); }
    [[nodiscard]] const std::vector<NetEntityId>& selection() const noexcept { return m_selection; }
    [[nodiscard]] const WorldSnapshot& lastSnapshot() const noexcept { return m_snapshot; }
    [[nodiscard]] f64 renderTick() const noexcept { return m_snapshot.renderTick; }
    [[nodiscard]] u64 serverTick() const noexcept;
    [[nodiscard]] bool paused() const noexcept;
    [[nodiscard]] f32 speed() const noexcept;
    [[nodiscard]] u64 commandsRejected() const noexcept { return m_commandsRejected; }
    [[nodiscard]] f64 applyMs() const noexcept { return m_applyMs; }

    static constexpr f32 kSpeeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};

private:
    NetworkSession(const NetworkSessionDesc& desc, render::MaterialLibrary& materials);
    [[nodiscard]] Expected<void> connect();
    void handleRejected();
    void send(cmd::CommandPayload payload);
    void selectionChanged();

    NetworkSessionDesc m_desc;
    std::string m_name;
    // 선언 순서 = 소멸 역순: 세션(복제 월드) → Transport → 로컬 서버 → 콘텐츠 → 카탈로그
    std::unique_ptr<ecs::ComponentCatalog> m_catalog;
    std::unique_ptr<content::ContentDatabase> m_ownContent; // Remote (Local 은 서버의 것을 같이 읽는다)
    std::unique_ptr<net::LocalServerHost> m_local;
    std::unique_ptr<net::INetworkTransport> m_ownTransport; // Remote
    std::unique_ptr<net::ClientSession> m_session;
    net::Endpoint m_target;
    bool m_retriedContent = false;
    std::optional<std::string> m_failure;

    SpriteExtraction m_extraction;
    net::InterpolationClock m_clock;
    WorldSnapshot m_snapshot;
    const net::ClientWorld* m_seenWorld = nullptr;
    u64 m_seenSnapshots = 0;
    f64 m_now = 0;

    std::vector<NetEntityId> m_selection; // 오름차순
    bool m_detailOverlay = true;

    // 보낸 명령 (결과를 기다린다) — 거절 문구와 일시정지 · 속도의 "보낸 값"
    std::map<u32, std::string> m_pendingNames;
    std::optional<std::pair<u32, bool>> m_pendingPause; // (sequence, 보낸 paused)
    std::optional<std::pair<u32, f32>> m_pendingSpeed;
    u64 m_commandsRejected = 0;
    std::string m_lastRejection;

    // 받은 바이트 (최근 1 초)
    f64 m_bytesWindowStart = 0;
    u64 m_bytesAtWindowStart = 0;
    f64 m_receivedKBps = 0;
    f64 m_applyMs = 0; // ClientSession::update (받기 · 적용) 시간의 지수 평균
};

} // namespace sbx::client
