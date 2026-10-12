// Phase 12D (ADR-0031): 서버의 세이브 · 로드 · 리플레이 (WorldOp · WorldList · WorldChanged), 월드가 바뀔 때 클라이언트가
// generation 을 따라가는 것 (새 월드의 스냅숏을 WorldChanged 까지 쥐고 있기), 메시지 왕복.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/Scenario.hpp"
#include "foundation/io/FileIo.hpp"
#include "network/client/ClientSession.hpp"
#include "network/replication/ReplicationWriter.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/LoopbackTransport.hpp"

using namespace sbx;
using namespace sbx::net;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const content::ContentDatabase& builtin() {
    static const content::ContentDatabase db = content::ContentDatabase::builtin();
    return db;
}

constexpr f64 kFrame = 1.0 / 30.0;

// 엔티티 50 개 (둘레를 도는 것 없이 그대로)
class QuietScenario final : public scenario::IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "quiet"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "test"; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 100; }
    void setup(sim::SimulationWorld& world) override {
        for (u32 i = 0; i < 50; ++i) {
            cmd::CreateEntity c;
            c.position = {static_cast<f32>(i), 0.f};
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{1, cmd::kServerIssuer, i + 1}, c});
        }
    }
    void beforeTick(sim::SimulationWorld&) override {}
};

// 시험마다 새 임시 폴더 (끝나면 지운다)
struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_{}_{:x}", tag, std::random_device{}());
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

struct Rig {
    LoopbackNetwork hub;
    LoopbackTransport serverTransport{hub};
    std::unique_ptr<ServerHost> host;
    f64 now = 0;

    explicit Rig(ServerHostDesc desc) {
        desc.randomSeed = 11;
        desc.worldName = "ops";
        desc.ownerKey = "k";
        host = std::make_unique<ServerHost>(
            serverTransport,
            std::make_unique<scenario::ScenarioRunner>(catalog(), builtin(), std::make_unique<QuietScenario>(), 1),
            desc);
        REQUIRE(host->start({"srv", 7777}).has_value());
    }
    void step() {
        now += kFrame;
        host->update(kFrame);
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
        d.contentHash = builtin().contentHash();
        d.ownerKey = std::move(ownerKey);
        d.catalog = &catalog();
        d.content = &builtin();
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

// 요청 하나 → 그 결과 (같은 종류의 첫 결과)
WorldOpResultMsg worldOp(Rig& rig, std::initializer_list<Peer*> all, Peer& who, WorldOpKind op, std::string name = {}) {
    REQUIRE(who.session.requestWorldOp(op, std::move(name)));
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

WorldList listOf(Rig& rig, std::initializer_list<Peer*> all, Peer& who) {
    const u64 before = who.session.worldListRevision();
    REQUIRE(who.session.requestWorldOp(WorldOpKind::List));
    for (int i = 0; i < 30 && who.session.worldListRevision() == before; ++i) {
        pump(rig, all, 1);
    }
    REQUIRE(who.session.worldListRevision() != before);
    return *who.session.worldList();
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

template <class T>
T roundTrip(const T& in) {
    const auto bytes = encodeMessage(Message{in});
    const auto back = decodeMessage(bytes);
    REQUIRE_MESSAGE(back.has_value(), back.error().describe());
    REQUIRE(std::holds_alternative<T>(*back));
    return std::get<T>(*back);
}

} // namespace

TEST_SUITE("network") {

    TEST_CASE("messages (12D): WorldOp · WorldOpResult · WorldList · WorldChanged · generation · stats round-trip") {
        CHECK(kProtocolVersion == 7);
        const WorldOpRequest op = roundTrip(WorldOpRequest{WorldOpKind::RecordStart, "rec_1"});
        CHECK(op.op == WorldOpKind::RecordStart);
        CHECK(op.name == "rec_1");
        const WorldOpResultMsg res = roundTrip(WorldOpResultMsg{WorldOpKind::Play, "rec_1", true, "재생 끝 — 일치"});
        CHECK(res.accepted);
        CHECK(res.detail == "재생 끝 — 일치");

        WorldList list;
        list.worlds = {{"a", false, 300, 50}, {"r", true, 12, 7}};
        CHECK(roundTrip(list).worlds == list.worlds);

        WorldChanged wc;
        wc.generation = 9;
        wc.world.name = "loaded";
        wc.world.minChunkX = -3;
        wc.world.maxChunkY = 2;
        wc.world.paused = true;
        wc.world.speed = 2.f;
        wc.reason = WorldChangeReason::Replay;
        wc.name = "r";
        const WorldChanged wc2 = roundTrip(wc);
        CHECK(wc2.generation == 9);
        CHECK(wc2.world.minChunkX == -3);
        CHECK(wc2.world.paused);
        CHECK(wc2.reason == WorldChangeReason::Replay);
        CHECK(wc2.name == "r");
        CHECK(channelOf(Message{wc}) == Channel::Bulk); // 지형 · 오버레이와 같은 채널 — 그 순서가 지켜진다
        CHECK(channelOf(Message{WorldOpRequest{}}) == Channel::Control);

        Welcome w;
        w.generation = 4;
        w.sessionToken.assign(kSessionTokenBytes, std::byte{1});
        CHECK(roundTrip(w).generation == 4);
        Snapshot s;
        s.generation = 5;
        s.snapshotId = 1;
        s.epoch = 2;
        CHECK(roundTrip(s).generation == 5);

        ServerStats st;
        st.mode = 2;
        st.modeName = "r";
        st.replayCall = 30;
        st.replayCalls = 600;
        st.hashesChecked = 3;
        st.mismatch = true;
        st.replayPaused = true;
        st.replaySpeed = 4.f;
        const ServerStats st2 = roundTrip(st);
        CHECK(st2.mode == 2);
        CHECK(st2.modeName == "r");
        CHECK(st2.replayCall == 30);
        CHECK(st2.replayCalls == 600);
        CHECK(st2.hashesChecked == 3);
        CHECK(st2.mismatch);
        CHECK(st2.replayPaused);
        CHECK(st2.replaySpeed == 4.f);

        // 이름 규칙 · 상한
        CHECK(isValidSaveName("quick_save-2"));
        CHECK_FALSE(isValidSaveName(""));
        CHECK_FALSE(isValidSaveName("../x"));
        CHECK_FALSE(isValidSaveName("a b"));
        CHECK_FALSE(isValidSaveName("세이브"));
        CHECK_FALSE(isValidSaveName(std::string(kMaxSaveNameBytes + 1, 'a')));
        CHECK(isValidSaveName(std::string(kMaxSaveNameBytes, 'a')));
        const auto bad = decodeMessage(encodeMessage(Message{WorldOpRequest{static_cast<WorldOpKind>(9), "a"}}));
        CHECK_FALSE(bad.has_value());
    }

    TEST_CASE("world ops: save · list · load replace the world for everyone, that batch's commands are refused") {
        TempDir dir("ops");
        ServerHostDesc d;
        d.savesRoot = dir.path;
        Rig rig(d);
        Peer owner(rig, "owner", "k");
        Peer ed(rig, "ed");
        pump(rig, {&owner, &ed}, 10);
        REQUIRE(owner.session.state() == ClientState::Connected);
        REQUIRE(owner.session.world() != nullptr);
        CHECK(owner.session.generation() == 0);

        CHECK(listOf(rig, {&owner, &ed}, ed).worlds.empty()); // 목록은 누구나
        auto r = worldOp(rig, {&owner, &ed}, ed, WorldOpKind::Save, "s1");
        CHECK_FALSE(r.accepted);
        CHECK(r.detail.find("admin") != std::string::npos);
        r = worldOp(rig, {&owner, &ed}, owner, WorldOpKind::Save, "../x");
        CHECK_FALSE(r.accepted);
        CHECK_FALSE(worldOp(rig, {&owner, &ed}, owner, WorldOpKind::Load, "nope").accepted);
        CHECK_FALSE(worldOp(rig, {&owner, &ed}, owner, WorldOpKind::RecordStop).accepted);

        r = worldOp(rig, {&owner, &ed}, owner, WorldOpKind::Save, "s1");
        REQUIRE_MESSAGE(r.accepted, r.detail);
        const WorldList list = listOf(rig, {&owner, &ed}, owner);
        REQUIRE(list.worlds.size() == 1);
        CHECK(list.worlds[0].name == "s1");
        CHECK_FALSE(list.worlds[0].replay);
        CHECK(list.worlds[0].entities == 50);
        const u64 savedTick = list.worlds[0].tick;
        CHECK(savedTick > 0);

        // 월드를 바꿔 둔다
        REQUIRE(command(rig, {&owner, &ed}, ed, cmd::CreateEntity{}).accepted);
        pump(rig, {&owner, &ed}, 20);
        CHECK(rig.host->world().registry().aliveCount() == 51);
        const u32 epochBefore = ed.session.world()->epoch();

        // 불러오기와 같은 배치의 명령은 옛 월드의 것 — 거절
        REQUIRE(owner.session.requestWorldOp(WorldOpKind::Load, "s1"));
        const u32 seq = ed.session.sendCommand(cmd::CreateEntity{});
        std::optional<WorldOpResultMsg> loaded;
        std::optional<CommandResultMsg> refused;
        for (int i = 0; i < 30 && !(loaded && refused); ++i) {
            pump(rig, {&owner, &ed}, 1);
            for (auto& x : owner.session.takeWorldOpResults()) {
                loaded = x;
            }
            for (auto& x : ed.session.takeResults()) {
                if (x.sequence == seq) {
                    refused = x;
                }
            }
        }
        REQUIRE(loaded);
        REQUIRE(refused);
        CHECK_MESSAGE(loaded->accepted, loaded->detail);
        CHECK_FALSE(refused->accepted);
        CHECK(refused->detail.find("월드가 바뀌어") != std::string::npos);

        pump(rig, {&owner, &ed}, 10);
        sim::SimulationWorld& world = rig.host->world();
        CHECK(world.registry().aliveCount() == 50);
        CHECK(world.currentTick() >= savedTick);
        CHECK(world.currentTick() < savedTick + 30);
        CHECK(rig.host->stats().generation == 1);
        for (Peer* p : {&owner, &ed}) {
            CAPTURE(p->session.welcome()->clientId);
            CHECK(p->session.generation() == 1);
            CHECK(p->session.worldChanges() == 1);
            REQUIRE(p->session.lastWorldChange());
            CHECK(p->session.lastWorldChange()->name == "s1");
            CHECK(p->session.lastWorldChange()->reason == WorldChangeReason::Load);
            REQUIRE(p->session.world() != nullptr);
            CHECK(p->session.world()->entityCount() == 50);
            CHECK(p->session.world()->serverTick() + 5 >= world.currentTick());
        }
        CHECK(ed.session.world()->epoch() > epochBefore); // 새 epoch 로 처음부터

        // 나중에 들어온 사람은 Welcome 에 새 generation
        Peer late(rig, "late");
        pump(rig, {&owner, &ed, &late}, 10);
        REQUIRE(late.session.welcome());
        CHECK(late.session.generation() == 1);
        CHECK(late.session.worldChanges() == 0);
        CHECK(late.session.world()->entityCount() == 50);

        // 같은 이름으로 다시 저장 = 덮어쓰기 (폴더째 바꿔 넣는다)
        REQUIRE(worldOp(rig, {&owner, &ed, &late}, owner, WorldOpKind::Save, "s1").accepted);
        CHECK(listOf(rig, {&owner, &ed, &late}, late).worlds.size() == 1);
    }

    TEST_CASE("world ops: a save made with other content is refused, no saves folder refuses everything but the list") {
        TempDir dir("mismatch");
        ServerHostDesc d;
        d.savesRoot = dir.path;
        Rig rig(d);
        Peer owner(rig, "owner", "k");
        pump(rig, {&owner}, 6);
        REQUIRE(worldOp(rig, {&owner}, owner, WorldOpKind::Save, "other").accepted);
        // world.json 의 콘텐츠 해시를 바꾼다 (다른 팩으로 만든 세이브인 셈)
        const fs::path file = dir.path / "other" / "world.json";
        auto text = io::readFile(file);
        REQUIRE(text.has_value());
        ecs::Json j = ecs::Json::parse(*text);
        j["content"]["contentHash"] = "0x0000000000001234";
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << j.dump(2);
        }
        const auto r = worldOp(rig, {&owner}, owner, WorldOpKind::Load, "other");
        CHECK_FALSE(r.accepted);
        CHECK(r.detail.find("다른 콘텐츠") != std::string::npos);
        CHECK(owner.session.generation() == 0);

        Rig bare(ServerHostDesc{}); // savesRoot 없음
        Peer p(bare, "p", "k");
        pump(bare, {&p}, 6);
        const auto s = worldOp(bare, {&p}, p, WorldOpKind::Save, "x");
        CHECK_FALSE(s.accepted);
        CHECK(s.detail.find("--saves") != std::string::npos);
        CHECK(listOf(bare, {&p}, p).worlds.empty());
    }

    TEST_CASE("world ops: record → play replays the recording (hashes match), edits are refused, controls drive it") {
        TempDir dir("replay");
        ServerHostDesc d;
        d.savesRoot = dir.path;
        Rig rig(d);
        Peer owner(rig, "owner", "k");
        Peer ed(rig, "ed");
        const std::initializer_list<Peer*> all{&owner, &ed};
        pump(rig, all, 6);

        auto r = worldOp(rig, all, owner, WorldOpKind::RecordStart, "r1");
        REQUIRE_MESSAGE(r.accepted, r.detail);
        CHECK(rig.host->stats().mode == 1);
        CHECK_FALSE(worldOp(rig, all, owner, WorldOpKind::RecordStart, "r2").accepted); // 이미 기록 중
        CHECK_FALSE(worldOp(rig, all, ed, WorldOpKind::RecordStop).accepted);           // editor 는 못 한다

        // 편집 · 실행 제어 (일시정지 편집 단계 포함)
        const auto created = command(rig, all, ed, cmd::CreateEntity{{3.f, 4.f}, {}, {}});
        REQUIRE(created.accepted);
        REQUIRE(created.created.size() == 1);
        REQUIRE(command(rig, all, ed, cmd::MoveEntity{{created.created[0]}, {1.f, 1.f}, false}).accepted);
        REQUIRE(command(rig, all, owner, cmd::PauseSimulation{}).accepted);
        REQUIRE(command(rig, all, ed, cmd::MoveEntity{{created.created[0]}, {9.f, 9.f}, true}).accepted);
        REQUIRE(command(rig, all, owner, cmd::StepSimulation{2}).accepted);
        REQUIRE(command(rig, all, owner, cmd::ResumeSimulation{}).accepted);
        REQUIRE(command(rig, all, owner, cmd::SetSimulationSpeed{2.f}).accepted);
        pump(rig, all, 20);
        REQUIRE(command(rig, all, ed, cmd::DeleteEntity{{created.created[0]}}).accepted);
        REQUIRE(command(rig, all, owner, cmd::SetSimulationSpeed{1.f}).accepted);
        pump(rig, all, 20);
        CHECK(rig.host->stats().replayCall > 40);

        r = worldOp(rig, all, owner, WorldOpKind::RecordStop);
        REQUIRE_MESSAGE(r.accepted, r.detail);
        CHECK(r.name == "r1");
        CHECK(r.detail.find("call") != std::string::npos);
        CHECK(rig.host->stats().mode == 0);
        CHECK(fs::is_regular_file(dir.path / "r1" / "replay.sbxr"));
        CHECK(fs::is_regular_file(dir.path / "r1" / "start" / "world.json"));
        const WorldList list = listOf(rig, all, owner);
        REQUIRE(list.worlds.size() == 1);
        CHECK(list.worlds[0].replay);
        CHECK(list.worlds[0].entities == 50);

        // 리플레이 폴더에는 저장 · 불러오기 · 새 기록을 하지 않는다
        CHECK_FALSE(worldOp(rig, all, owner, WorldOpKind::Save, "r1").accepted);
        CHECK_FALSE(worldOp(rig, all, owner, WorldOpKind::Load, "r1").accepted);
        CHECK_FALSE(worldOp(rig, all, owner, WorldOpKind::RecordStart, "r1").accepted);
        CHECK_FALSE(worldOp(rig, all, ed, WorldOpKind::Play, "r1").accepted);

        r = worldOp(rig, all, owner, WorldOpKind::Play, "r1");
        REQUIRE_MESSAGE(r.accepted, r.detail);
        CHECK(rig.host->stats().mode == 2);
        CHECK(owner.session.generation() == 1);
        CHECK(ed.session.lastWorldChange()->reason == WorldChangeReason::Replay);
        CHECK_FALSE(worldOp(rig, all, owner, WorldOpKind::RecordStart, "r3").accepted); // 재생 중에는 기록 못 함

        // 편집은 거절, 실행 제어는 재생을 다룬다
        auto cr = command(rig, all, ed, cmd::CreateEntity{});
        CHECK_FALSE(cr.accepted);
        CHECK(cr.detail.find("리플레이") != std::string::npos);
        REQUIRE(command(rig, all, owner, cmd::PauseSimulation{}).accepted);
        pump(rig, all, 3);
        const u64 at = rig.host->stats().replayCall;
        pump(rig, all, 40); // 1 초 넘게 — 통계도 온다
        CHECK(rig.host->stats().replayCall == at);
        REQUIRE(owner.session.lastStats());
        CHECK(owner.session.lastStats()->mode == 2);
        CHECK(owner.session.lastStats()->replayPaused);
        CHECK(owner.session.lastStats()->modeName == "r1");
        CHECK(owner.session.lastStats()->replayCalls == rig.host->stats().replayCalls);
        REQUIRE(command(rig, all, owner, cmd::StepSimulation{3}).accepted);
        pump(rig, all, 6);
        CHECK(rig.host->stats().replayCall == at + 3);
        REQUIRE(command(rig, all, owner, cmd::SetSimulationSpeed{4.f}).accepted);
        REQUIRE(command(rig, all, owner, cmd::ResumeSimulation{}).accepted);

        // 끝까지 — 결과가 모두에게, 해시가 모두 맞다
        std::optional<WorldOpResultMsg> done;
        for (int i = 0; i < 900 && !done; ++i) {
            pump(rig, all, 1);
            for (auto& x : ed.session.takeWorldOpResults()) {
                if (x.op == WorldOpKind::Play) {
                    done = x;
                }
            }
        }
        REQUIRE(done);
        CHECK_MESSAGE(done->accepted, done->detail);
        CHECK(done->detail.find("일치") != std::string::npos);
        pump(rig, all, 3);
        CHECK(rig.host->stats().mode == 0);
        CHECK(rig.host->world().clock().paused()); // 보통 월드로 돌아와 멈춰 있다
        // 이제 다시 편집된다
        CHECK(command(rig, all, ed, cmd::CreateEntity{}).accepted);
    }

    TEST_CASE("world ops: stopping the server writes the recording in progress") {
        TempDir dir("stoprec");
        ServerHostDesc d;
        d.savesRoot = dir.path;
        Rig rig(d);
        Peer owner(rig, "owner", "k");
        pump(rig, {&owner}, 6);
        REQUIRE(worldOp(rig, {&owner}, owner, WorldOpKind::RecordStart, "live").accepted);
        pump(rig, {&owner}, 15);
        rig.host->stop();
        CHECK(fs::is_regular_file(dir.path / "live" / "replay.sbxr"));
    }

    TEST_CASE("client: snapshots of a newer world wait for WorldChanged, an early WorldChanged beats an old Welcome") {
        LoopbackNetwork hub;
        LoopbackTransport server(hub);
        REQUIRE(server.listen({"srv", 7777}, 4).has_value());
        LoopbackTransport clientT(hub);
        ClientSessionDesc cd = Peer::desc("c", {});
        ClientSession client(clientT, cd);
        REQUIRE(client.connect({"srv", 7777}, 0).has_value());
        const ReplicationWriter table(catalog());

        ConnectionId conn = kInvalidConnection;
        std::vector<Message> got; // 서버가 받은 것
        const auto serverPoll = [&] {
            std::vector<TransportEvent> ev;
            server.poll(ev);
            for (auto& e : ev) {
                if (e.type == TransportEvent::Type::Connected) {
                    conn = e.connection;
                } else if (e.type == TransportEvent::Type::Received) {
                    auto m = decodeMessage(e.data);
                    REQUIRE(m.has_value());
                    got.push_back(std::move(*m));
                }
            }
        };
        const auto send = [&](const Message& m) { server.send(conn, channelOf(m), encodeMessage(m)); };
        const auto round = [&] {
            client.update(0);
            serverPoll();
            client.update(0);
            serverPoll();
        };
        round();
        REQUIRE(conn != kInvalidConnection);
        send(Challenge{7});
        round();

        // Welcome(generation 1) 보다 WorldChanged(generation 2) 가 먼저 왔다 (Bulk 채널)
        WorldChanged early;
        early.generation = 2;
        early.world.name = "loaded";
        early.world.minChunkX = -1;
        early.world.minChunkY = -1;
        early.world.maxChunkX = 0;
        early.world.maxChunkY = 0;
        early.reason = WorldChangeReason::Load;
        early.name = "s";
        send(early);
        Welcome w;
        w.clientId = 1;
        w.role = Role::Editor;
        w.generation = 1;
        w.world.name = "old";
        w.world.minChunkX = -4;
        w.world.minChunkY = -4;
        w.world.maxChunkX = 3;
        w.world.maxChunkY = 3;
        w.sessionToken.assign(kSessionTokenBytes, std::byte{1});
        w.replicated = table.table();
        send(w);
        round();
        REQUIRE(client.state() == ClientState::Connected);
        CHECK(client.generation() == 2);
        CHECK(client.welcome()->world.name == "loaded");
        CHECK(client.welcome()->world.maxChunkX == 0);
        CHECK(client.worldChanges() == 1);
        REQUIRE(client.world() != nullptr);

        // 이번엔 새 월드(3)의 스냅숏이 WorldChanged 보다 먼저 — 쥐고 있다가 그 뒤에 적용 · ack
        Snapshot s3;
        s3.generation = 3;
        s3.epoch = 1;
        s3.snapshotId = 1;
        s3.serverTick = 77;
        send(s3);
        Snapshot s2; // 옛 월드(2)의 것은 그대로 적용
        s2.generation = 2;
        s2.epoch = 1;
        s2.snapshotId = 1;
        s2.serverTick = 40;
        send(s2);
        got.clear();
        round();
        CHECK(client.world()->serverTick() == 40);
        REQUIRE(got.size() == 1); // s2 의 ack 만
        CHECK(client.world()->grid().bounds().maxChunk.x == 0);

        WorldChanged next = early;
        next.generation = 3;
        next.reason = WorldChangeReason::Replay;
        next.world.maxChunkX = 1; // 경계가 다른 월드
        send(next);
        Snapshot old; // 바뀐 뒤 온 옛 월드 것 — 버린다 (ack 없음)
        old.generation = 2;
        old.epoch = 1;
        old.snapshotId = 2;
        old.serverTick = 41;
        send(old);
        got.clear();
        round();
        CHECK(client.generation() == 3);
        CHECK(client.worldChanges() == 2);
        CHECK(client.world()->grid().bounds().maxChunk.x == 1); // 새 경계로 다시 만든 복제 월드
        CHECK(client.world()->serverTick() == 77);
        REQUIRE(got.size() == 1);
        REQUIRE(std::holds_alternative<SnapshotAck>(got[0]));
        CHECK(std::get<SnapshotAck>(got[0]).snapshotId == 1);
        CHECK(std::get<SnapshotAck>(got[0]).epoch == 1);
        // 같은 generation 의 WorldChanged 가 또 와도 무시
        send(next);
        round();
        CHECK(client.worldChanges() == 2);
    }
}
