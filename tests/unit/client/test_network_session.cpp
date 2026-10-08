// Phase 10B 클라이언트: NetworkSession (로컬 서버 · 원격 서버 — 접속 · 스냅숏 · 보간 · 명령 · 선택 · 선택 상세),
// Extraction(ClientWorld → RenderWorld), Application 의 Connecting · InWorld 흐름 · 카메라 조작. docs/16-ROADMAP.md
// 10B, ADR-0026 (Phase 8A 의 --direct-sim 시험을 대신한다).
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/ClientOptions.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/presentation/SelectionOverlay.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "network/transport/LoopbackTransport.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

std::unique_ptr<NetworkSession> makeLocal(render::MaterialLibrary& mats, std::string_view world = "ecosystem_small") {
    NetworkSessionDesc d;
    d.mode = NetworkSessionMode::Local;
    d.world = std::string(world);
    d.seed = 1;
    d.contentRoot = SBX_CONTENT_DIR;
    d.inlineServer = true;
    auto ns = NetworkSession::create(d, mats);
    REQUIRE_MESSAGE(ns.has_value(), (ns ? std::string() : ns.error().describe()));
    return std::move(*ns);
}

void untilReady(NetworkSession& ns) {
    for (int i = 0; i < 120 && !ns.ready(); ++i) {
        ns.update(kDt);
    }
    REQUIRE(ns.ready());
}

template <class Pred>
void runUntil(NetworkSession& ns, Pred pred, int maxFrames = 200) {
    for (int i = 0; i < maxFrames && !pred(); ++i) {
        ns.update(kDt);
        render::RenderWorld w;
        ns.extract(w);
    }
    REQUIRE(pred());
}

render::MaterialLibrary materials() {
    render::MaterialLibrary m;
    m.set("eco/rabbit", {render::kWhiteSprite, render::packRgba8(200, 180, 150)});
    m.set("terrain/eco.grassland", {render::kWhiteSprite, render::packRgba8(44, 62, 38)});
    return m;
}

// 가짜 월드: 호출 기록만
struct FakeWorld final : IWorldSession {
    int updates = 0, extracts = 0, pauses = 0, steps = 0, speed = 0;
    bool isReady = true;
    std::optional<std::string> fail;
    [[nodiscard]] bool ready() const override { return isReady; }
    [[nodiscard]] std::optional<std::string> failure() const override { return fail; }
    f64 lastDt = 0;
    void update(f64 dt) override {
        ++updates;
        lastDt = dt;
    }
    void extract(render::RenderWorld& out) override {
        ++extracts;
        out.sprites.push_back({.position = {1, 2}});
    }
    [[nodiscard]] render::WorldRect bounds() const override { return {{0, 0}, {100, 50}}; }
    void togglePause() override { ++pauses; }
    void stepOnce() override { ++steps; }
    void changeSpeed(int dir) override { speed += dir; }
    [[nodiscard]] std::string status() const override { return "fake tick 7"; }
    // 8B 선택
    int picks = 0, boxes = 0, clears = 0, additive = 0;
    Vec2 lastPick{};
    render::WorldRect lastBox{};
    bool details = true;
    void selectAt(Vec2 p, bool add) override {
        ++picks;
        additive += add ? 1 : 0;
        lastPick = p;
    }
    void selectBox(render::WorldRect r, bool add) override {
        ++boxes;
        additive += add ? 1 : 0;
        lastBox = r;
    }
    void clearSelection() override { ++clears; }
    void setDetailOverlay(bool on) override { details = on; }
    [[nodiscard]] std::string selectionStatus() const override { return picks > 0 ? "선택 fake" : ""; }
};

struct FakeRenderer final : IFrameRenderer {
    int renders = 0, worldRenders = 0;
    usize lastSprites = 0;
    void resize(u32, u32) override {}
    void render(f64, const render::RenderWorld* w, ImDrawData*) override {
        ++renders;
        if (w != nullptr) {
            ++worldRenders;
            lastSprites = w->sprites.size();
        }
    }
    [[nodiscard]] std::string status() const override { return "fake"; }
};

ActionMap defaults() {
    auto m = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
    REQUIRE(m.has_value());
    return std::move(*m);
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE(
        "network session (local): connects to the in-process server, sprites per entity, render.sprite, terrain") {
        auto mats = materials();
        auto ns = makeLocal(mats);
        CHECK_FALSE(ns->ready());
        CHECK(ns->status().find("접속 중") != std::string::npos);
        untilReady(*ns);
        render::RenderWorld w;
        ns->extract(w);
        const ExtractionStats st = ns->lastSnapshot().stats;
        REQUIRE(st.entities > 0);
        CHECK(w.sprites.size() == st.entities);
        CHECK(st.entities == ns->clientWorld()->entityCount());
        CHECK(st.withSprite == st.entities); // eco 팩의 Prefab 은 모두 render.sprite 가 있다 (Opaque 로 온다)
        const render::WorldRect b = ns->bounds();
        CHECK(b.min.x == -32);
        CHECK(b.max.y == 32);
        CHECK(ns->status().find("ecosystem_small tick") == 0);

        // 지형: ecosystem_small 경계 = 청크 -1..0 (2 × 2, 32 타일). 팔레트 = 콘텐츠 머티리얼 순서의 "terrain/<id>" 색
        const render::TerrainView& t = w.terrain;
        CHECK(t.worldId != 0);
        CHECK(t.chunkSize == 32);
        CHECK(t.minChunkX == -1);
        CHECK(t.chunksX == 2);
        REQUIRE(t.chunks.size() == 4);
        REQUIRE(t.palette);
        const content::ContentDatabase& content = ns->localServer()->content();
        CHECK(t.palette->size() == content.materialCount());
        const auto grass = content.findMaterial("eco.grassland");
        REQUIRE(grass.has_value());
        CHECK((*t.palette)[*grass] == render::packRgba8(44, 62, 38));
        const world::WorldGrid& serverGrid = ns->localServer()->host().world().grid();
        for (const render::TerrainChunk& c : t.chunks) {
            REQUIRE(c.tiles);
            CHECK(c.tiles->size() == 32u * 32u);
            const Vec2i tile{c.x * 32 + 5, c.y * 32 + 7};
            CHECK((*c.tiles)[7 * 32 + 5] == serverGrid.materialAt(tile)); // 서버 지형이 복제됐다
        }
        // 지형이 바뀌지 않으면 다음 프레임도 같은 버퍼를 공유한다
        ns->update(kDt);
        render::RenderWorld w2;
        ns->extract(w2);
        CHECK(ns->lastSnapshot().stats.terrainCopied == 0);
        CHECK(w2.terrain.chunks[0].tiles.get() == t.chunks[0].tiles.get());

        // 토끼: 0.8 크기, layer 10, 머티리얼 색. 늑대: 머티리얼이 없어 대체 색
        const auto rabbit = std::ranges::find_if(w.sprites, [](const auto& s) { return s.size.x == 0.8f; });
        REQUIRE(rabbit != w.sprites.end());
        CHECK(rabbit->layer == 10);
        CHECK(rabbit->color == render::packRgba8(200, 180, 150));
        CHECK(rabbit->depth == -rabbit->position.y);
        const u32 wolfColor = render::MaterialLibrary::fallbackColor("eco/wolf");
        CHECK(std::ranges::any_of(w.sprites, [&](const auto& s) { return s.color == wolfColor && s.layer == 10; }));

        // 보간: 스냅숏 사이(1/60 초)에도 움직이는 개체의 위치가 바뀐다 (netId 로 비교)
        auto positions = [&] {
            std::unordered_map<NetEntityId, Vec2> m;
            for (const auto& s : ns->lastSnapshot().sprites) {
                m[s.id] = s.position;
            }
            return m;
        };
        for (int i = 0; i < 10; ++i) {
            ns->update(kDt);
        }
        render::RenderWorld a;
        ns->extract(a);
        const auto p0 = positions();
        const f64 r0 = ns->renderTick();
        ns->update(1.0 / 60.0);
        render::RenderWorld c2;
        ns->extract(c2);
        CHECK(ns->renderTick() > r0);
        usize moved = 0;
        for (const auto& [id, p] : positions()) {
            if (const auto it = p0.find(id); it != p0.end() && it->second != p) {
                ++moved;
            }
        }
        CHECK(moved > 0);
    }

    TEST_CASE("network session (local): pause · step · speed go to the server, renderTick trails the server tick") {
        auto mats = materials();
        auto ns = makeLocal(mats);
        untilReady(*ns);
        runUntil(*ns, [&] { return ns->serverTick() >= 20; });
        const f64 behind = static_cast<f64>(ns->serverTick()) - ns->renderTick();
        CHECK(behind > 0.5); // 보간 지연 (3 틱) 안팎
        CHECK(behind < 6.0);

        ns->togglePause();
        runUntil(*ns, [&] { return ns->paused(); });
        CHECK(ns->status().find("일시정지") != std::string::npos);
        const u64 tick = ns->serverTick();
        for (int i = 0; i < 10; ++i) {
            ns->update(kDt);
        }
        render::RenderWorld w;
        ns->extract(w);
        CHECK(ns->serverTick() == tick);
        CHECK(ns->renderTick() == static_cast<f64>(tick)); // 일시정지 중에는 서버 틱 그대로
        ns->stepOnce();
        runUntil(*ns, [&] { return ns->serverTick() == tick + 1; });
        ns->togglePause();
        runUntil(*ns, [&] { return !ns->paused(); });
        ns->stepOnce(); // 진행 중에는 보내지 않는다
        CHECK(ns->commandsRejected() == 0);

        ns->changeSpeed(+1);
        runUntil(*ns, [&] { return ns->speed() == 2.f; });
        for (int i = 0; i < 10; ++i) {
            ns->changeSpeed(+1); // 결과를 기다리지 않아도 보낸 값에서 한 칸씩
        }
        runUntil(*ns, [&] { return ns->speed() == 8.f; });
        for (int i = 0; i < 10; ++i) {
            ns->changeSpeed(-1);
        }
        runUntil(*ns, [&] { return ns->speed() == 0.25f; });
        CHECK(ns->commandsRejected() == 0);
        const WorldInfo info = ns->info();
        REQUIRE(info.net.has_value());
        CHECK(info.net->local);
        CHECK(info.net->role == "owner");
        CHECK(info.net->snapshots > 0);
    }

    TEST_CASE("selection: pick · toggle · box · clear, server-only details via inspect, outlines, title text") {
        auto mats = materials();
        auto ns = makeLocal(mats);
        untilReady(*ns);
        runUntil(*ns, [&] { return ns->serverTick() >= 15; }); // 늑대 · 토끼가 움직이기 시작하게
        ns->togglePause();
        runUntil(*ns, [&] { return ns->paused(); });
        render::RenderWorld w;
        ns->extract(w);
        CHECK(ns->selection().empty());
        CHECK(ns->selectionStatus().empty());
        CHECK(w.selection.empty());

        // 토끼(layer 10)는 풀(layer 0) 위에 그려진다 — 겹친 자리에서 토끼가 골라진다
        const auto rabbit = std::ranges::find_if(w.sprites, [](const auto& s) { return s.size.x == 0.8f; });
        REQUIRE(rabbit != w.sprites.end());
        ns->selectAt(rabbit->position, false);
        REQUIRE(ns->selection().size() == 1);
        // 서버 전용 상태(행동 · 감지 반경)는 InspectResult 로 온다
        runUntil(*ns, [&] {
            const auto& sel = ns->lastSnapshot().selected;
            return sel.size() == 1 && sel[0].sensorRadius > 0 && !sel[0].state.empty();
        });
        const std::string title = ns->selectionStatus();
        CHECK(title.find("선택 eco.rabbit #") == 0);
        CHECK(title.find("에너지") != std::string::npos);
        render::RenderWorld s1;
        ns->extract(s1);
        CHECK(s1.selection.lines().size() == 4); // 외곽선 상자
        CHECK(s1.debug.lines().size() >= 48);    // 감지 반경 원(48) + 경로 · 대상 · 속도 선
        ns->setDetailOverlay(false);
        render::RenderWorld s2;
        ns->extract(s2);
        CHECK(s2.debug.empty());
        CHECK(s2.selection.lines().size() == 4);
        ns->setDetailOverlay(true);

        // Shift + 클릭: 이미 있으면 뺀다 · 없으면 더한다. 빈 곳 클릭(더하기 아님)은 비운다
        ns->selectAt(rabbit->position, true);
        CHECK(ns->selection().empty());
        ns->selectAt(rabbit->position, true);
        CHECK(ns->selection().size() == 1);
        ns->selectAt({1000, 1000}, false);
        CHECK(ns->selection().empty());

        // 박스: 월드 전체 → 모든 개체, 더하기는 합친다 (중복 없이)
        ns->selectBox({{-40, -40}, {40, 40}}, false);
        CHECK(ns->selection().size() == w.sprites.size());
        CHECK(ns->selectionStatus() == std::format("선택 {}", w.sprites.size()));
        ns->selectBox({{0, 0}, {-40, -40}}, true);
        CHECK(ns->selection().size() == w.sprites.size());
        CHECK(std::ranges::is_sorted(ns->selection()));
        ns->clearSelection();
        CHECK(ns->selection().empty());
    }

    TEST_CASE("selection helpers: topmost pick, box, outlines skip dead ids, merge") {
        WorldSnapshot snap;
        snap.sprites.push_back({.id = 5, .position = {0, 0}, .size = {2, 2}, .layer = 0});
        snap.sprites.push_back({.id = 9, .position = {0.5f, 0.2f}, .size = {1, 1}, .layer = 10});
        snap.sprites.push_back({.id = 3, .position = {0.4f, 0}, .size = {1, 1}, .layer = 10});
        // layer 10 둘이 겹친다: depth(-y) 가 큰 쪽 = y 가 작은 쪽(3)이 위
        CHECK(pickAt(snap, {0.45f, 0.1f}) == NetEntityId{3});
        CHECK(pickAt(snap, {-0.9f, -0.9f}) == NetEntityId{5}); // layer 0 만 있는 자리
        CHECK_FALSE(pickAt(snap, {5, 5}).has_value());
        CHECK(pickBox(snap, {{0.3f, -1}, {1, 1}}) == std::vector<NetEntityId>{3, 9});
        render::DebugDrawList out;
        const NetEntityId sel[] = {3, 4, 9}; // 4 는 스냅숏에 없다
        CHECK(drawSelectionOutlines(snap, sel, out) == 2);
        CHECK(out.lines().size() == 8);
        std::vector<NetEntityId> a{1, 4, 7};
        const NetEntityId b[] = {2, 4, 9};
        mergeSelection(a, b);
        CHECK(a == std::vector<NetEntityId>{1, 2, 4, 7, 9});
        CHECK(describeSelection(snap, {}).empty());
        CHECK(describeSelection(snap, std::span<const NetEntityId>(sel, 1)) == "선택 #3"); // 자세한 상태 없음
    }

    TEST_CASE("network session (remote): content mismatch → reads the server's packs and reconnects, editor role "
              "rejection is reported, server shutdown is a failure") {
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        auto src = scenario::openWorldSource("ecosystem_small", SBX_CONTENT_DIR, 1, catalog);
        REQUIRE(src.has_value());
        net::LoopbackNetwork hub;
        net::LoopbackTransport serverT(hub);
        net::ServerHostDesc hd;
        hd.worldName = src->name;
        hd.packs = src->packs;
        hd.defaultRole = net::Role::Editor; // 일시정지는 admin 부터 → 거절
        hd.randomSeed = 5;
        net::ServerHost host(serverT, std::move(src->runner), hd);
        REQUIRE(host.start({"srv", 7777}).has_value());

        auto mats = materials();
        NetworkSessionDesc d;
        d.mode = NetworkSessionMode::Remote;
        d.connect = "srv:7777";
        d.displayName = "원격";
        d.contentRoot = SBX_CONTENT_DIR;
        d.transportFactory = [&]() -> Expected<std::unique_ptr<net::INetworkTransport>> {
            return std::unique_ptr<net::INetworkTransport>(std::make_unique<net::LoopbackTransport>(hub));
        };
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        auto ns = std::move(*made);
        auto frame = [&] {
            ns->update(kDt);
            host.update(kDt);
            render::RenderWorld w;
            ns->extract(w);
        };
        for (int i = 0; i < 200 && !ns->ready() && !ns->failure(); ++i) {
            frame();
        }
        REQUIRE_FALSE(ns->failure().has_value());
        REQUIRE(ns->ready()); // 내장 콘텐츠로 거절 → eco 팩을 읽어 다시 접속
        const WorldInfo info = ns->info();
        REQUIRE(info.net.has_value());
        CHECK_FALSE(info.net->local);
        CHECK(info.net->role == "editor");
        CHECK(info.net->server == "srv:7777");
        CHECK(ns->status().find("RTT") != std::string::npos);
        // 원격 기본 예산 256 KB/s — 처음 몇 스냅숏은 일부만 (round-robin), 곧 다 따라온다
        for (int i = 0; i < 100 && !ns->clientWorld()->lastSnapshotComplete(); ++i) {
            frame();
        }
        CHECK(ns->clientWorld()->lastSnapshotComplete());

        ns->togglePause();
        for (int i = 0; i < 30 && ns->commandsRejected() == 0; ++i) {
            frame();
        }
        CHECK(ns->commandsRejected() == 1);
        CHECK(ns->info().net->lastRejection.find("PauseSimulation") != std::string::npos);
        CHECK_FALSE(ns->paused());

        host.stop();
        for (int i = 0; i < 30 && !ns->failure(); ++i) {
            frame();
        }
        REQUIRE(ns->failure().has_value());
        CHECK(ns->failure()->find("끊겼습니다") != std::string::npos);
    }

    TEST_CASE("network session (remote): a dropped network reconnects with the session token, a gone server fails") {
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        auto src = scenario::openWorldSource("ecosystem_small", SBX_CONTENT_DIR, 1, catalog);
        REQUIRE(src.has_value());
        net::LoopbackNetwork hub;
        net::LoopbackTransport serverT(hub);
        net::ServerHostDesc hd;
        hd.packs = src->packs;
        hd.defaultRole = net::Role::Admin;
        hd.randomSeed = 9;
        auto host = std::make_unique<net::ServerHost>(serverT, std::move(src->runner), hd);
        REQUIRE(host->start({"srv", 7777}).has_value());

        auto mats = materials();
        std::vector<net::LoopbackTransport*> made;
        NetworkSessionDesc d;
        d.mode = NetworkSessionMode::Remote;
        d.connect = "srv:7777";
        d.contentRoot = SBX_CONTENT_DIR;
        d.transportFactory = [&]() -> Expected<std::unique_ptr<net::INetworkTransport>> {
            auto t = std::make_unique<net::LoopbackTransport>(hub);
            made.push_back(t.get());
            return std::unique_ptr<net::INetworkTransport>(std::move(t));
        };
        auto created = NetworkSession::create(d, mats);
        REQUIRE(created.has_value());
        auto ns = std::move(*created);
        auto frame = [&] {
            ns->update(kDt);
            if (host) {
                host->update(kDt);
            }
            render::RenderWorld w;
            ns->extract(w);
        };
        for (int i = 0; i < 200 && !ns->ready(); ++i) {
            frame();
        }
        REQUIRE(ns->ready());
        const u16 id = ns->session()->welcome()->clientId;
        const usize before = ns->clientWorld()->entityCount();

        made.back()->severAll(); // 네트워크가 끊긴다
        frame();
        CHECK(ns->reconnecting());
        CHECK_FALSE(ns->failure().has_value());
        CHECK(ns->status().find("다시 접속 중") != std::string::npos);
        CHECK(ns->clientWorld()->entityCount() == before); // 그동안 옛 복제본을 그린다
        for (int i = 0; i < 150 && ns->reconnecting(); ++i) {
            frame();
        }
        REQUIRE_FALSE(ns->reconnecting());
        CHECK(ns->reconnects() == 1);
        CHECK(ns->session()->welcome()->clientId == id); // 같은 번호 · 역할
        CHECK(ns->session()->welcome()->role == net::Role::Admin);
        for (int i = 0; i < 10; ++i) {
            frame();
        }
        CHECK(ns->ready());
        CHECK(ns->info().net->reconnects == 1);

        // 서버가 없어지면 60 초 동안 2 초마다 시도하다 실패
        made.back()->severAll();
        host->stop();
        host.reset();
        for (int i = 0; i < 30 * 70 && !ns->failure(); ++i) {
            frame();
        }
        REQUIRE(ns->failure().has_value());
        CHECK(ns->failure()->find("다시 접속하지 못했습니다") != std::string::npos);
    }

    TEST_CASE("network session (local, server threads): the window path — server ticks on its own, client follows") {
        auto mats = materials();
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = SBX_CONTENT_DIR;
        d.workers = 2;
        d.inlineServer = false; // 창 실행과 같다: Simulation · Net IO 스레드
        auto made = NetworkSession::create(d, mats);
        REQUIRE(made.has_value());
        auto ns = std::move(*made);
        using Clock = std::chrono::steady_clock;
        auto last = Clock::now();
        auto pump = [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto now = Clock::now();
            ns->update(std::chrono::duration<f64>(now - last).count());
            last = now;
            render::RenderWorld w;
            ns->extract(w);
        };
        for (int i = 0; i < 2000 && !(ns->ready() && ns->serverTick() >= 10); ++i) {
            pump();
        }
        REQUIRE(ns->ready());
        CHECK(ns->serverTick() >= 10);
        CHECK(ns->lastSnapshot().stats.entities == ns->clientWorld()->entityCount());
        ns->togglePause();
        for (int i = 0; i < 2000 && !ns->paused(); ++i) {
            pump();
        }
        CHECK(ns->paused());
        ns.reset(); // 접속을 끊고 서버 스레드를 멈춘다
    }

    TEST_CASE("network session: the visible rect becomes the interest (1 chunk margin, clamped, 2 Hz)") {
        net::WorldMeta w;
        w.minChunkX = -4;
        w.minChunkY = -4;
        w.maxChunkX = 3;
        w.maxChunkY = 3;
        // 청크 0 의 가운데 일부 → 0 ± 1
        const auto a = NetworkSession::interestFor({{5, 5}, {20, 20}}, w);
        CHECK_FALSE(a.all);
        CHECK(a.minChunkX == -1);
        CHECK(a.maxChunkX == 1);
        CHECK(a.minChunkY == -1);
        CHECK(a.maxChunkY == 1);
        // 월드 전체가 보이면 all, 월드 밖 아주 먼 곳 · NaN 도 올바른 사각형
        CHECK(NetworkSession::interestFor({{-1000, -1000}, {1000, 1000}}, w).all);
        const auto far = NetworkSession::interestFor({{1e30f, 1e30f}, {2e30f, 2e30f}}, w);
        CHECK(far.minChunkX == 3);
        CHECK(far.maxChunkX == 3);
        const auto nan = NetworkSession::interestFor({{std::nanf(""), 0}, {1, 1}}, w);
        CHECK(nan.minChunkX <= nan.maxChunkX);

        // 로컬 서버 (ecosystem_survival 8 × 8 청크): 구석을 보면 그 근처 개체만 받는다
        auto mats = materials();
        auto ns = makeLocal(mats, "ecosystem_survival");
        untilReady(*ns);
        const render::WorldRect b = ns->bounds();
        ns->setView({b.min, b.min + Vec2{20, 20}});
        for (int i = 0; i < 60; ++i) {
            ns->update(kDt);
        }
        const usize server = ns->localServer()->host().world().registry().aliveCount();
        const usize client = ns->clientWorld()->entityCount();
        MESSAGE("server ", server, " client ", client);
        CHECK(client * 4 < server);
        CHECK(ns->info().net->interest.find("청크") == 0);
        ns->setView(b); // 다시 전체
        for (int i = 0; i < 60; ++i) {
            ns->update(kDt);
        }
        CHECK(ns->info().net->interest == "월드 전체");
        CHECK(ns->clientWorld()->entityCount() * 10 > server * 9);
    }

    TEST_CASE("network session: unknown world and bad address fail at create") {
        auto mats = materials();
        NetworkSessionDesc bad;
        bad.world = "no_such_world";
        bad.contentRoot = SBX_CONTENT_DIR;
        bad.inlineServer = true;
        const auto err = NetworkSession::create(bad, mats);
        REQUIRE_FALSE(err.has_value());
        CHECK(err.error().message.find("ecosystem_small") != std::string::npos); // 있는 이름을 알려 준다
        NetworkSessionDesc addr;
        addr.mode = NetworkSessionMode::Remote;
        addr.connect = "host:notaport";
        CHECK_FALSE(NetworkSession::create(addr, mats).has_value());
    }

    TEST_CASE("app with a world session: goes InWorld, fits the camera, pans · zooms · drives sim actions") {
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        FakeWorld world;
        FakeRenderer renderer;
        AppConfig cfg;
        cfg.world = &world;
        cfg.renderer = &renderer;
        cfg.fixedDt = 0.1;
        cfg.titleEveryFrames = 1;
        Application app(window, defaults(), audio, cfg);
        REQUIRE(app.frame()); // Boot → MainMenu
        REQUIRE(app.frame()); // → Connecting
        REQUIRE(app.frame()); // → InWorld (Connecting 에서 접속을 진행하려고 update 한 번)
        CHECK(app.state() == AppState::InWorld);
        CHECK(world.updates == 1);
        REQUIRE(app.frame());
        CHECK(world.updates == 2);
        CHECK(world.lastDt == 0.1);
        CHECK(renderer.worldRenders == 1);
        CHECK(renderer.lastSprites == 1);
        // 창 1600×900 에 100×50 (+ 양쪽 5 % 여백 → 110×55) 을 맞춘다 → min(1600/110, 900/55)
        const f32 fitted = 1600.f / 110.f;
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(fitted));
        CHECK(app.camera().center == Vec2{50, 25});
        CHECK(window.title().find("fake tick 7") != std::string::npos);

        // D 를 누르고 있으면 오른쪽으로 (화면 짧은 변 0.6 배/초 × 0.1 초 = 54 px)
        window.inject(KeyDown{Key::D, 0, {}, false});
        REQUIRE(app.frame());
        window.inject(KeyUp{Key::D, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.camera().center.x == doctest::Approx(50 + 54.f / fitted));

        // 휠 한 칸: 커서 아래 점을 고정한 채 ×1.15
        window.inject(MouseMove{{400, 300}, {0, 0}});
        REQUIRE(app.frame());
        const Vec2 before = app.camera().screenToWorld({400, 300});
        window.inject(MouseWheel{{0, 1}});
        REQUIRE(app.frame());
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(fitted * 1.15f));
        const Vec2 after = app.camera().screenToWorld({400, 300});
        CHECK(after.x == doctest::Approx(before.x));
        CHECK(after.y == doctest::Approx(before.y));

        // Space · . · = · -
        for (Key k : {Key::Space, Key::Period, Key::Equal, Key::Equal, Key::Minus}) {
            window.inject(KeyDown{k, 0, {}, false});
            window.inject(KeyUp{k, 0, {}});
            REQUIRE(app.frame());
        }
        CHECK(world.pauses == 1);
        CHECK(world.steps == 1);
        CHECK(world.speed == 1);

        // Home: 다시 맞춤
        window.inject(KeyDown{Key::Home, 0, {}, false});
        window.inject(KeyUp{Key::Home, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(fitted));

        // 8B 선택: 왼쪽 클릭(움직이지 않음) = 점, 끌기 = 박스(끄는 동안 박스 선), Shift = 더하기, Esc = 해제
        window.inject(MouseMove{{400, 300}, {0, 0}});
        window.inject(MouseButtonDown{MouseButton::Left, {400, 300}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {400, 300}});
        REQUIRE(app.frame());
        CHECK(world.picks == 1);
        CHECK(world.boxes == 0);
        const Vec2 expect = app.camera().screenToWorld({400, 300});
        CHECK(world.lastPick.x == doctest::Approx(expect.x));
        CHECK(world.lastPick.y == doctest::Approx(expect.y));
        CHECK(window.title().find("선택 fake") != std::string::npos);

        window.inject(MouseButtonDown{MouseButton::Left, {100, 100}});
        REQUIRE(app.frame());
        window.inject(MouseMove{{300, 250}, {200, 150}});
        REQUIRE(app.frame());
        CHECK(app.renderWorld().selection.lines().size() == 4); // 끄는 중의 박스
        CHECK(app.camera().center.x == doctest::Approx(50));    // 왼쪽 끌기는 더 이상 카메라를 옮기지 않는다
        window.inject(MouseButtonUp{MouseButton::Left, {300, 250}});
        REQUIRE(app.frame());
        CHECK(world.boxes == 1);
        CHECK(world.additive == 0);
        const Vec2 a = app.camera().screenToWorld({100, 100}), b = app.camera().screenToWorld({300, 250});
        CHECK(world.lastBox.min.x == doctest::Approx(a.x));
        CHECK(world.lastBox.max.y == doctest::Approx(b.y));

        window.inject(KeyDown{Key::LeftShift, 0, {}, false});
        window.inject(MouseButtonDown{MouseButton::Left, {300, 250}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {300, 250}});
        window.inject(KeyUp{Key::LeftShift, 0, {}});
        REQUIRE(app.frame());
        CHECK(world.picks == 2);
        CHECK(world.additive == 1);

        // 가운데 끌기는 카메라
        window.inject(MouseButtonDown{MouseButton::Middle, {300, 250}});
        REQUIRE(app.frame());
        window.inject(MouseMove{{310, 250}, {10, 0}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Middle, {310, 250}});
        REQUIRE(app.frame());
        CHECK(app.camera().center.x == doctest::Approx(50 - 10.f / fitted));

        for (Key k : {Key::Escape, Key::G, Key::V}) {
            window.inject(KeyDown{k, 0, {}, false});
            window.inject(KeyUp{k, 0, {}});
            REQUIRE(app.frame());
        }
        CHECK(world.clears == 1);
        CHECK(app.renderWorld().overlay.grid);
        CHECK_FALSE(world.details);
    }

    TEST_CASE("app waits in Connecting until the session is ready, and ends when it fails") {
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        FakeWorld world;
        world.isReady = false;
        AppConfig cfg;
        cfg.world = &world;
        cfg.fixedDt = 0.1;
        Application app(window, defaults(), audio, cfg);
        for (int i = 0; i < 6; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(app.state() == AppState::Connecting);
        CHECK(world.updates == 4); // Connecting 에서 프레임마다 (접속 진행)
        CHECK(world.extracts == 0);
        world.isReady = true;
        REQUIRE(app.frame());
        CHECK(app.state() == AppState::InWorld);
        world.fail = "서버와 연결이 끊겼습니다";
        CHECK_FALSE(app.frame()); // 실패를 보고 Shutdown (이 프레임 끝에 적용)
        CHECK(app.state() == AppState::Shutdown);
        CHECK(app.run(nullptr) == 1);
    }

    TEST_CASE("client options: --world · --connect · --name · --seed · --content · --assets") {
        const std::string_view args[] = {"--world",    "ecosystem_survival", "--seed",    "42",     "--content",
                                         "c:/content", "--assets",           "c:/assets", "--name", "철수"};
        auto o = parseClientOptions(args);
        REQUIRE(o.has_value());
        CHECK(o->world == "ecosystem_survival");
        CHECK_FALSE(o->connect.has_value());
        CHECK(o->seed == 42);
        CHECK(o->name == "철수");
        CHECK(o->contentRoot == "c:/content");
        CHECK(o->assetRoot == "c:/assets");
        const std::string_view remote[] = {"--connect", "192.168.0.5:7777"};
        CHECK(parseClientOptions(remote)->connect == "192.168.0.5:7777");
        const std::string_view both[] = {"--world", "w", "--connect", "h"};
        CHECK_FALSE(parseClientOptions(both).has_value());
        const std::string_view bad[] = {"--seed", "x"};
        CHECK_FALSE(parseClientOptions(bad).has_value());
        const std::string_view missing[] = {"--world"};
        CHECK_FALSE(parseClientOptions(missing).has_value());
        const std::string_view gone[] = {"--direct-sim", "ecosystem_small"}; // 지웠다 (10B)
        CHECK_FALSE(parseClientOptions(gone).has_value());
        CHECK(clientUsage().find("--connect") != std::string::npos);
        CHECK(clientUsage().find("--direct-sim") == std::string::npos);
    }
}
