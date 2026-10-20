// v0.2 M2-T3 (ADR-0045): 게임 진행을 네트워크로 — GameControl 명령 (역할 · 참가자 수), WorldOp Restart (시작 월드를
// 다시 열고 Lobby), GameMode.restart auto. 서버는 eco 팩 사본 + 시험용 모드, 시작 월드는 세이브 폴더.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/game/GameSession.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/game/GameRules.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "network/client/ClientSession.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/LoopbackTransport.hpp"

using namespace sbx;
using namespace sbx::net;
namespace fs = std::filesystem;
using comp::GameSession;
using comp::GameState;

namespace {

constexpr f64 kFrame = 1.0 / 30.0;

// hunt: 카운트다운 1 초, 2 초 지나면 lose. quick: 카운트다운 없음, 0.1 초 (3 틱) 에 win, 0.2 초 (6 틱) 뒤 자동 재시작
constexpr std::string_view kHunt = R"({"id": "eco.hunt", "name": "사냥", "players": {"min": 1, "max": 4},
  "start": {"countdownSeconds": 1}, "end": [{"when": {"time": {"op": ">=", "seconds": 2}}, "result": "lose"}]})";
constexpr std::string_view kQuick = R"({"id": "eco.quick", "name": "빠른 판",
  "end": [{"when": {"time": {"op": ">=", "seconds": 0.1}}, "result": "win"}],
  "restart": {"mode": "auto", "delaySeconds": 0.2}})";

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_game_{}_{:x}", tag, std::random_device{}());
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const content::ContentDatabase& gameContent() {
    static const TempDir dir("content");
    static const content::ContentDatabase db = [] {
        const fs::path pack = dir.path / "ecosystem";
        fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", pack, fs::copy_options::recursive);
        fs::create_directories(pack / "modes");
        std::ofstream(pack / "modes" / "hunt.json", std::ios::binary) << kHunt;
        std::ofstream(pack / "modes" / "quick.json", std::ios::binary) << kQuick;
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(dir.path, ids, catalog());
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    return db;
}

// 시작 월드: 작은 eco 월드 (토끼 하나) 를 저장한 폴더
void writeEntryWorld(const fs::path& dir) {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "eco.grassland";
    sim::SimulationWorld w(catalog(), gameContent(), d);
    w.enqueue(
        cmd::SimCommand{cmd::CommandHeader{1, cmd::kServerIssuer, 1}, cmd::CreateEntity{{2.f, 2.f}, {}, "eco.rabbit"}});
    w.tick();
    REQUIRE(persist::saveWorld(w, dir).has_value());
}

struct Rig {
    TempDir dir{"rig"};
    LoopbackNetwork hub;
    LoopbackTransport serverTransport{hub};
    std::unique_ptr<ServerHost> host;
    f64 now = 0;

    Rig(std::string_view mode, u32 minPlayers) {
        const fs::path entry = dir.path / "entry";
        writeEntryWorld(entry);
        auto opened = scenario::openSave(entry, gameContent(), catalog());
        REQUIRE(opened.has_value());
        REQUIRE(game::resetSession(opened->runner->world(), mode).has_value());
        ServerHostDesc d;
        d.randomSeed = 5;
        d.worldName = "game";
        d.ownerKey = "k";
        d.entryWorld = entry;
        d.gameMode = std::string(mode);
        d.minPlayers = minPlayers;
        host = std::make_unique<ServerHost>(serverTransport, std::move(opened->runner), d);
        REQUIRE(host->start({"srv", 7777}).has_value());
    }
    void step() {
        now += kFrame;
        host->update(kFrame);
    }
    [[nodiscard]] const GameSession& session() {
        const GameSession* s = game::findSession(host->world().registry());
        REQUIRE(s != nullptr);
        return *s;
    }
};

struct Peer {
    LoopbackTransport transport;
    ClientSession session;
    Peer(Rig& rig, std::string name, std::string ownerKey = {})
        : transport(rig.hub), session(transport, desc(std::move(name), std::move(ownerKey))) {
        REQUIRE(session.connect({"srv", 7777}, rig.now).has_value());
    }
    static ClientSessionDesc desc(std::string name, std::string ownerKey) {
        ClientSessionDesc d;
        d.displayName = std::move(name);
        d.contentHash = gameContent().contentHash();
        d.ownerKey = std::move(ownerKey);
        d.catalog = &catalog();
        d.content = &gameContent();
        return d;
    }
};

void pump(Rig& rig, std::initializer_list<Peer*> peers, int frames = 4) {
    for (int i = 0; i < frames; ++i) {
        for (auto* p : peers) {
            p->session.update(rig.now);
        }
        rig.step();
        for (auto* p : peers) {
            p->session.update(rig.now);
        }
    }
}

CommandResultMsg command(Rig& rig, std::initializer_list<Peer*> all, Peer& who, cmd::CommandPayload payload) {
    const u32 seq = who.session.sendCommand(std::move(payload));
    REQUIRE(seq != 0);
    for (int i = 0; i < 30; ++i) {
        pump(rig, all, 1);
        for (auto& r : who.session.takeResults()) {
            if (r.sequence == seq) {
                return r;
            }
        }
    }
    FAIL("결과가 오지 않았다");
    return {};
}

WorldOpResultMsg worldOp(Rig& rig, std::initializer_list<Peer*> all, Peer& who, WorldOpKind op) {
    REQUIRE(who.session.requestWorldOp(op, {}));
    for (int i = 0; i < 30; ++i) {
        pump(rig, all, 1);
        for (auto& r : who.session.takeWorldOpResults()) {
            if (r.op == op) {
                return r;
            }
        }
    }
    FAIL("WorldOpResult 가 오지 않았다");
    return {};
}

// 클라이언트 복제 월드의 세션 (복제 컴포넌트 — 위치가 없어 관심과 무관하게 온다)
const GameSession* replicatedSession(const Peer& p) {
    return p.session.world() != nullptr ? game::findSession(p.session.world()->registry()) : nullptr;
}

} // namespace

TEST_SUITE("network") {

    TEST_CASE("game: 시작은 admin 부터 · 참가자가 모여야, 중단 · 재시작은 모두에게 (v0.2 M2-T3)") {
        Rig rig("eco.hunt", 3);
        Peer owner(rig, "owner", "k");
        Peer ed(rig, "ed"); // 기본 역할 editor (≥ player)
        pump(rig, {&owner, &ed}, 10);
        REQUIRE(owner.session.state() == ClientState::Connected);
        CHECK(rig.session().gameState() == GameState::Lobby);
        const sim::Tick lobbyTick = rig.host->world().currentTick();
        pump(rig, {&owner, &ed}, 10);
        CHECK(rig.host->world().currentTick() == lobbyTick); // Lobby = 멈춤
        // 클라이언트도 세션을 본다
        const GameSession* seen = replicatedSession(ed);
        REQUIRE(seen != nullptr);
        CHECK(seen->gameState() == GameState::Lobby);
        CHECK(seen->mode == std::string_view("eco.hunt"));

        auto r = command(rig, {&owner, &ed}, ed, cmd::GameControl{cmd::GameControlOp::Start});
        CHECK_FALSE(r.accepted);
        CHECK(r.reason == ErrorCode::PermissionDenied);
        r = command(rig, {&owner, &ed}, owner, cmd::GameControl{cmd::GameControlOp::Start});
        CHECK_FALSE(r.accepted); // 2 명 < 최소 3 명
        CHECK(r.detail.find("참가자") != std::string::npos);

        Peer third(rig, "third");
        pump(rig, {&owner, &ed, &third}, 10);
        r = command(rig, {&owner, &ed, &third}, owner, cmd::GameControl{cmd::GameControlOp::Start});
        REQUIRE_MESSAGE(r.accepted, r.detail);
        pump(rig, {&owner, &ed, &third}, 40); // 카운트다운 30 틱
        CHECK(rig.session().gameState() == GameState::Running);
        CHECK(rig.host->world().currentTick() > lobbyTick + 30);
        pump(rig, {&owner, &ed, &third}, 4);
        REQUIRE(replicatedSession(third) != nullptr);
        CHECK(replicatedSession(third)->gameState() == GameState::Running);

        r = command(rig, {&owner, &ed, &third}, owner, cmd::GameControl{cmd::GameControlOp::Abort});
        REQUIRE_MESSAGE(r.accepted, r.detail);
        CHECK(rig.session().gameState() == GameState::Finished);
        const sim::Tick end = rig.host->world().currentTick();
        pump(rig, {&owner, &ed, &third}, 10);
        CHECK(rig.host->world().currentTick() == end); // Finished = 멈춤, hunt 는 자동 재시작 없음

        // 재시작: editor 는 못 한다, owner 는 시작 월드를 다시 연다 → 모두에게 WorldChanged(Restart), 세션 Lobby
        CHECK_FALSE(worldOp(rig, {&owner, &ed, &third}, ed, WorldOpKind::Restart).accepted);
        const WorldOpResultMsg rs = worldOp(rig, {&owner, &ed, &third}, owner, WorldOpKind::Restart);
        REQUIRE_MESSAGE(rs.accepted, rs.detail);
        pump(rig, {&owner, &ed, &third}, 10);
        CHECK(rig.session().gameState() == GameState::Lobby);
        CHECK(rig.host->stats().generation == 1);
        CHECK(rig.host->world().currentTick() < end); // 시작 월드의 틱으로
        for (Peer* p : {&owner, &ed, &third}) {
            REQUIRE(p->session.lastWorldChange());
            CHECK(p->session.lastWorldChange()->reason == WorldChangeReason::Restart);
            REQUIRE(replicatedSession(*p) != nullptr);
            CHECK(replicatedSession(*p)->gameState() == GameState::Lobby);
        }
    }

    TEST_CASE("game: restart auto — Finished 뒤 delay 만큼 지나면 서버가 스스로 다시 연다") {
        Rig rig("eco.quick", 0);
        Peer owner(rig, "owner", "k");
        pump(rig, {&owner}, 10);
        REQUIRE(command(rig, {&owner}, owner, cmd::GameControl{cmd::GameControlOp::Start}).accepted);
        bool finished = false;
        for (int i = 0; i < 20 && !finished; ++i) {
            pump(rig, {&owner}, 1);
            finished = rig.host->stats().generation == 0 && rig.session().gameState() == GameState::Finished;
        }
        REQUIRE(finished);
        CHECK(rig.session().outcome() == comp::GameOutcome::Win);
        for (int i = 0; i < 30 && rig.host->stats().generation == 0; ++i) {
            pump(rig, {&owner}, 1);
        }
        CHECK(rig.host->stats().generation == 1);
        CHECK(rig.session().gameState() == GameState::Lobby);
        pump(rig, {&owner}, 4);
        REQUIRE(owner.session.lastWorldChange());
        CHECK(owner.session.lastWorldChange()->reason == WorldChangeReason::Restart);
    }

    TEST_CASE("test play (v0.2 M5-T1): 편집 월드의 복사본에서 게임 · 멈추면 편집 월드 그대로 (틱 · 해시 · 개체)") {
        Rig rig("eco.quick", 0); // 0.1 초에 win, 0.2 초 뒤 자동 재시작 — 테스트 플레이 중에는 하지 않는다
        Peer owner(rig, "owner", "k");
        Peer ed(rig, "ed");
        pump(rig, {&owner, &ed}, 10);
        REQUIRE(owner.session.state() == ClientState::Connected);
        // 편집: Lobby 에서 개체 하나 더
        REQUIRE(command(rig, {&owner, &ed}, owner, cmd::CreateEntity{{-3.f, 1.f}, {}, "eco.rabbit"}).accepted);
        const sim::Tick editTick = rig.host->world().currentTick();
        const u64 editHash = *rig.host->world().worldHash();
        const usize editEntities = rig.host->world().registry().aliveCount();
        CHECK(rig.session().gameState() == GameState::Lobby);

        CHECK_FALSE(worldOp(rig, {&owner, &ed}, ed, WorldOpKind::TestPlay).accepted); // admin 부터
        const WorldOpResultMsg start = worldOp(rig, {&owner, &ed}, owner, WorldOpKind::TestPlay);
        REQUIRE_MESSAGE(start.accepted, start.detail);
        CHECK(start.detail.find("게임 eco.quick 시작") != std::string::npos);
        CHECK(rig.host->stats().generation == 1);
        CHECK(rig.host->stats().mode == 3);
        pump(rig, {&owner, &ed}, 4);
        REQUIRE(owner.session.lastWorldChange());
        CHECK(owner.session.lastWorldChange()->reason == WorldChangeReason::TestPlay);

        // 복사본에서 게임이 돈다 — 끝나도 자동 재시작은 없다 (테스트 플레이는 멈출 때까지)
        REQUIRE(command(rig, {&owner, &ed}, ed, cmd::CreateEntity{{3.f, -1.f}, {}, "eco.wolf"}).accepted);
        for (int i = 0; i < 40 && rig.session().gameState() != GameState::Finished; ++i) {
            pump(rig, {&owner, &ed}, 1);
        }
        CHECK(rig.session().gameState() == GameState::Finished);
        CHECK(rig.session().outcome() == comp::GameOutcome::Win);
        pump(rig, {&owner, &ed}, 30);
        CHECK(rig.host->stats().generation == 1);
        CHECK(rig.host->world().currentTick() > editTick);

        // 그동안 월드를 바꾸는 다른 요청은 거절
        const WorldOpResultMsg restart = worldOp(rig, {&owner, &ed}, owner, WorldOpKind::Restart);
        CHECK_FALSE(restart.accepted);
        CHECK(restart.detail.find("테스트 플레이 중") != std::string::npos);
        CHECK(worldOp(rig, {&owner, &ed}, owner, WorldOpKind::TestPlay).detail.find("이미") != std::string::npos);

        const WorldOpResultMsg stop = worldOp(rig, {&owner, &ed}, owner, WorldOpKind::TestStop);
        REQUIRE_MESSAGE(stop.accepted, stop.detail);
        CHECK(rig.host->stats().generation == 2);
        CHECK(rig.host->stats().mode == 0);
        CHECK(rig.host->world().currentTick() == editTick);
        CHECK(*rig.host->world().worldHash() == editHash);
        CHECK(rig.host->world().registry().aliveCount() == editEntities);
        CHECK(rig.session().gameState() == GameState::Lobby);
        pump(rig, {&owner, &ed}, 4);
        REQUIRE(ed.session.lastWorldChange());
        CHECK(ed.session.lastWorldChange()->reason == WorldChangeReason::TestStop);
        CHECK(worldOp(rig, {&owner, &ed}, owner, WorldOpKind::TestStop).detail.find("중이 아닙니다") !=
              std::string::npos);
    }

    TEST_CASE("game: 게임 프로젝트가 아닌 서버는 재시작을 거절한다") {
        TempDir dir("plain");
        writeEntryWorld(dir.path / "w");
        auto opened = scenario::openSave(dir.path / "w", gameContent(), catalog());
        REQUIRE(opened.has_value());
        LoopbackNetwork hub;
        LoopbackTransport serverTransport{hub};
        ServerHostDesc d;
        d.randomSeed = 5;
        d.ownerKey = "k";
        ServerHost host(serverTransport, std::move(opened->runner), d);
        REQUIRE(host.start({"srv", 7777}).has_value());
        LoopbackTransport ct{hub};
        ClientSession s(ct, Peer::desc("owner", "k"));
        REQUIRE(s.connect({"srv", 7777}, 0).has_value());
        f64 now = 0;
        std::optional<WorldOpResultMsg> res;
        for (int i = 0; i < 60 && !res; ++i) {
            s.update(now);
            now += kFrame;
            host.update(kFrame);
            s.update(now);
            if (i == 10) {
                REQUIRE(s.requestWorldOp(WorldOpKind::Restart, {}));
            }
            for (auto& r : s.takeWorldOpResults()) {
                res = r;
            }
        }
        REQUIRE(res);
        CHECK_FALSE(res->accepted);
        CHECK(res->detail.find("프로젝트") != std::string::npos);
    }

} // TEST_SUITE
