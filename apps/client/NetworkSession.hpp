#pragma once
// SandboxClient 의 월드 = 서버의 복제본 (Phase 10B, docs/08-NETWORK.md 4 · 6.4 · 9장, docs/16-ROADMAP.md 10.5,
// ADR-0026). --direct-sim(클라이언트가 Core 를 직접 돌리는 임시 경로, ADR-0020 · 0021)을 대신한다.
//
//   Local   --world <시나리오|세이브 폴더>: 같은 프로세스의 LocalServerHost(ServerHost + Loopback)에 접속한다.
//           창 실행은 서버가 자기 스레드(Simulation · Net IO)에서 돈다. 헤드리스 · 시험은 inlineServer — update(dt) 가
//           그 자리에서 서버를 돌린다 (같은 dt 열이면 같은 결과).
//   Remote  --connect host:port: ENet 으로 SandboxServer 에 접속한다. 콘텐츠가 다르다고 거절되면 서버가 알려 준 팩을
//           --content 루트에서 읽어 한 번 다시 접속한다 (sbx_net_probe 와 같은 규칙). 네트워크가 끊기면(시간 초과)
//           같은 세션 토큰으로 2 초마다 다시 접속한다 (60 초까지, 11.4) — 그동안 옛 복제본을 그린다.
//
// 둘 다 같은 길: ClientSession 이 Snapshot 을 ClientWorld 에 적용하고, 프레임마다 InterpolationClock 의 renderTick 으로
// SpriteExtraction 이 그릴 거리를 만든다. 일시정지 · 한 틱 · 속도는 서버로 가는 명령 (역할이 모자라면 거절 — 상태 줄과
// 네트워크 패널에 보인다). 선택은 netId 로, 선택한 개체의 서버 전용 상태는 Inspect 로 받는다.
// Phase 12A: 에디터의 IEditorHost — 툴 명령을 서버로 보내고 결과를 돌려주며, EditPreview 를 그릴 때 더한다 (ADR-0028).
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
#include "editor/EditorHost.hpp"
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

class NetworkSession final : public IWorldSession, public editor::IEditorHost {
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
    void setView(render::WorldRect visible) override { m_view = visible; }
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
    [[nodiscard]] editor::IEditorHost* editorHost() override { return this; }

    // ---- IEditorHost (12A) ----
    u32 submit(cmd::CommandPayload payload) override;
    [[nodiscard]] std::vector<editor::CommandOutcome> takeOutcomes() override;
    [[nodiscard]] const content::ContentDatabase* content() const override;
    [[nodiscard]] const world::WorldGrid* grid() const override;
    [[nodiscard]] std::optional<NetEntityId> pickAt(Vec2 world) const override;
    [[nodiscard]] std::vector<NetEntityId> pickBox(render::WorldRect area) const override;
    [[nodiscard]] std::span<const NetEntityId> selection() const override { return m_selection; }
    void setSelection(std::vector<NetEntityId> ids) override;
    void setPreview(std::span<const editor::PreviewOffset> offsets) override;
    [[nodiscard]] u64 snapshotsApplied() const override { return m_seenSnapshots; }

    // 시험 · 끝 요약
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const net::ClientWorld* clientWorld() const noexcept;
    [[nodiscard]] net::ClientSession* session() noexcept { return m_session.get(); }
    [[nodiscard]] net::LocalServerHost* localServer() noexcept { return m_local.get(); }
    [[nodiscard]] const WorldSnapshot& lastSnapshot() const noexcept { return m_snapshot; }
    [[nodiscard]] f64 renderTick() const override { return m_snapshot.renderTick; }
    [[nodiscard]] u64 serverTick() const override;
    [[nodiscard]] bool paused() const noexcept;
    [[nodiscard]] f32 speed() const noexcept;
    [[nodiscard]] u64 commandsRejected() const noexcept { return m_commandsRejected; }
    [[nodiscard]] f64 applyMs() const noexcept { return m_applyMs; }

    static constexpr f32 kSpeeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};
    static constexpr f64 kSubscribeInterval = 0.5; // 관심 영역은 바뀌어도 초당 2 번까지 (08 8장)
    static constexpr f64 kReconnectSeconds = 60;   // 이만큼 다시 접속하지 못하면 실패 (서버 토큰도 60 초)
    static constexpr f64 kRetryInterval = 2;
    [[nodiscard]] u32 reconnects() const noexcept { return m_reconnects; }
    [[nodiscard]] bool reconnecting() const noexcept { return m_lostAt >= 0; }
    // 보이는 사각형 → 관심 청크 사각형 (1 청크 여유, 월드 경계로 자른다 — 항상 올바른 사각형)
    [[nodiscard]] static net::Subscribe interestFor(render::WorldRect visible, const net::WorldMeta& world);

private:
    NetworkSession(const NetworkSessionDesc& desc, render::MaterialLibrary& materials);
    [[nodiscard]] Expected<void> connect();
    [[nodiscard]] Expected<std::unique_ptr<net::INetworkTransport>> makeTransport() const;
    [[nodiscard]] net::ClientSessionDesc sessionDesc(const content::ContentDatabase& content) const;
    void updateReconnect();
    void handleRejected();
    u32 send(cmd::CommandPayload payload); // sequence (0 = 보내지 못함)
    void selectionChanged();
    void applyPreview(); // 스냅숏의 그릴 위치에 EditPreview 를 더한다 (고르기 · 외곽선도 옮긴 자리로)

    NetworkSessionDesc m_desc;
    std::string m_name;
    // 선언 순서 = 소멸 역순: 세션(복제 월드) → Transport → 로컬 서버 → 콘텐츠 → 카탈로그
    std::unique_ptr<ecs::ComponentCatalog> m_catalog;
    std::unique_ptr<content::ContentDatabase> m_ownContent; // Remote (Local 은 서버의 것을 같이 읽는다)
    std::unique_ptr<net::LocalServerHost> m_local;
    std::unique_ptr<net::INetworkTransport> m_ownTransport; // Remote
    std::unique_ptr<net::ClientSession> m_session;
    // 다시 접속 (11.4): 끊긴 뒤 kRetryInterval 마다 같은 토큰으로. 새 연결이 그릴 수 있게 되면 바꿔 낀다
    std::unique_ptr<net::INetworkTransport> m_retryTransport;
    std::unique_ptr<net::ClientSession> m_retry;
    f64 m_lostAt = -1; // 끊긴 시각 (다시 접속 중이면 ≥ 0)
    f64 m_nextRetryAt = 0;
    u32 m_reconnects = 0;
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
    std::optional<render::WorldRect> m_view; // 화면에 보이는 사각형 (Application 이 프레임마다)
    f64 m_lastSubscribeAt = -1e9;

    // 보낸 명령 (결과를 기다린다) — 거절 문구와 일시정지 · 속도의 "보낸 값"
    std::map<u32, std::string> m_pendingNames;
    std::optional<std::pair<u32, bool>> m_pendingPause; // (sequence, 보낸 paused)
    std::optional<std::pair<u32, f32>> m_pendingSpeed;
    u64 m_commandsRejected = 0;
    std::string m_lastRejection;
    // 에디터가 보낸 명령 (결과를 IEditorHost::takeOutcomes 로) · EditPreview (netId 오름차순) — 12A
    std::vector<u32> m_editorSequences;
    std::vector<editor::CommandOutcome> m_outcomes;
    std::vector<editor::PreviewOffset> m_preview;

    // 받은 바이트 (최근 1 초)
    f64 m_bytesWindowStart = 0;
    u64 m_bytesAtWindowStart = 0;
    f64 m_receivedKBps = 0;
    f64 m_applyMs = 0; // ClientSession::update (받기 · 적용) 시간의 지수 평균
};

} // namespace sbx::client
