// 에디터 툴 (Phase 12A, docs/10-EDITOR.md 10장 첫 줄): 가짜 IEditorHost 로 툴 조작 → 기대 명령 · 선택 · EditPreview.
#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/world/WorldGrid.hpp"
#include "editor/Editor.hpp"

using namespace sbx;
using namespace sbx::editor;

namespace {

struct FakeEntity {
    NetEntityId id;
    Vec2 position;
};

struct FakeHost final : IEditorHost {
    const content::ContentDatabase& db = content::ContentDatabase::builtin();
    world::WorldGrid worldGrid;
    std::vector<FakeEntity> entities;
    std::vector<NetEntityId> sel;
    std::vector<PreviewOffset> previewNow;
    std::vector<cmd::CommandPayload> sent;
    std::vector<CommandOutcome> outcomes;
    bool connected = true;
    u32 nextSeq = 0;
    u64 snapshots = 0;
    u64 tick = 100;
    f64 drawn = 99;

    FakeHost() : worldGrid(*world::WorldGrid::create({{-1, -1}, {0, 0}}, db, 0)) {} // 64 × 64 타일, -32 ~ 31

    u32 submit(cmd::CommandPayload payload) override {
        if (!connected) {
            return 0;
        }
        sent.push_back(std::move(payload));
        return ++nextSeq;
    }
    std::vector<CommandOutcome> takeOutcomes() override { return std::exchange(outcomes, {}); }
    const content::ContentDatabase* content() const override { return &db; }
    const world::WorldGrid* grid() const override { return &worldGrid; }
    std::optional<NetEntityId> pickAt(Vec2 p) const override {
        for (const FakeEntity& e : entities) {
            if (std::abs(p.x - e.position.x) <= 0.5f && std::abs(p.y - e.position.y) <= 0.5f) {
                return e.id;
            }
        }
        return std::nullopt;
    }
    std::vector<NetEntityId> pickBox(render::WorldRect a) const override {
        const Vec2 lo{std::min(a.min.x, a.max.x), std::min(a.min.y, a.max.y)};
        const Vec2 hi{std::max(a.min.x, a.max.x), std::max(a.min.y, a.max.y)};
        std::vector<NetEntityId> out;
        for (const FakeEntity& e : entities) {
            if (e.position.x >= lo.x && e.position.x <= hi.x && e.position.y >= lo.y && e.position.y <= hi.y) {
                out.push_back(e.id);
            }
        }
        std::ranges::sort(out);
        return out;
    }
    std::span<const NetEntityId> selection() const override { return sel; }
    void setSelection(std::vector<NetEntityId> ids) override { sel = std::move(ids); }
    void setPreview(std::span<const PreviewOffset> o) override { previewNow.assign(o.begin(), o.end()); }
    u64 snapshotsApplied() const override { return snapshots; }
    u64 serverTick() const override { return tick; }
    f64 renderTick() const override { return drawn; }

    template <typename T>
    const T& last() const {
        REQUIRE_FALSE(sent.empty());
        const T* p = std::get_if<T>(&sent.back());
        REQUIRE(p != nullptr);
        return *p;
    }
};

// 한 프레임씩 마우스를 움직이는 작은 도우미 (32 px/칸)
struct Hand {
    Editor& ed;
    FakeHost& host;
    f64 now = 0;
    EditorInput frame(Vec2 cursor) {
        EditorInput in;
        in.now = (now += 1.0 / 60.0);
        in.cursor = cursor;
        return in;
    }
    void click(Vec2 at, bool additive = false) {
        auto in = frame(at);
        in.primaryPressed = true;
        in.additive = additive;
        ed.update(in, host); // 같은 프레임에 누르고 뗀다
    }
    void press(Vec2 at, bool secondary = false) {
        auto in = frame(at);
        (secondary ? in.secondaryPressed : in.primaryPressed) = true;
        (secondary ? in.secondaryDown : in.primaryDown) = true;
        ed.update(in, host);
    }
    void drag(Vec2 to, bool secondary = false) {
        auto in = frame(to);
        (secondary ? in.secondaryDown : in.primaryDown) = true;
        ed.update(in, host);
    }
    void release(Vec2 at) { ed.update(frame(at), host); }
    void idle() { ed.update(frame({100, 100}), host); }
    void key(Tool t) {
        auto in = frame({0, 0});
        in.tool = t;
        ed.update(in, host);
    }
};

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("select tool: click picks the top entity, box picks centers inside, Shift adds or toggles") {
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}, {7, {4.5f, 0.5f}}, {9, {10.5f, 10.5f}}};
        Editor ed;
        Hand h{ed, host};
        h.click({0.4f, 0.6f});
        CHECK(host.sel == std::vector<NetEntityId>{3});
        h.click({4.6f, 0.4f}, true);
        CHECK(host.sel == std::vector<NetEntityId>{3, 7});
        h.click({0.5f, 0.5f}, true); // 이미 있으면 뺀다
        CHECK(host.sel == std::vector<NetEntityId>{7});
        h.press({-1, -1});
        h.drag({6, 2});
        h.release({6, 2});
        CHECK(host.sel == std::vector<NetEntityId>{3, 7});
        h.click({20, 20}); // 빈 곳 = 비운다
        CHECK(host.sel.empty());
        CHECK(host.sent.empty()); // 선택은 로컬 — 명령이 없다
    }

    TEST_CASE("move tool: dragging previews the offset, release sends MoveEntity, preview lasts until drawn") {
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}, {7, {4.5f, 0.5f}}};
        host.sel = {3, 7};
        Editor ed;
        Hand h{ed, host};
        h.key(Tool::Move);
        CHECK(ed.tool() == Tool::Move);
        h.press({4.5f, 0.5f}); // 선택한 개체를 잡는다 → 선택 전부를 옮긴다
        h.drag({6.5f, 3.5f});
        REQUIRE(host.previewNow.size() == 2);
        CHECK(host.previewNow[0].offset == Vec2{2, 3});
        CHECK(host.sent.empty()); // 끄는 동안은 명령이 없다
        h.release({6.5f, 3.5f});
        const auto& mv = host.last<cmd::MoveEntity>();
        CHECK(mv.targets == std::vector<NetEntityId>{3, 7});
        CHECK(mv.value == Vec2{2, 3});
        CHECK_FALSE(mv.absolute);
        CHECK(host.previewNow.size() == 2); // 결과 전 — 그대로

        // 받아들여짐 → 그 뒤 처음 온 스냅숏(틱 101)까지 그린 틱이 따라오면 걷는다
        host.outcomes.push_back({1, true, 101, {}, {}});
        h.idle();
        CHECK(host.previewNow.size() == 2);
        host.snapshots = 1;
        host.tick = 101;
        h.idle();
        CHECK(host.previewNow.size() == 2); // 그린 틱 99
        host.drawn = 101;
        h.idle();
        CHECK(host.previewNow.empty());
        CHECK(ed.stats().accepted == 1);
    }

    TEST_CASE("move tool: rejection drops the preview at once, snap rounds the delta, unselected entity is grabbed") {
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}, {7, {4.5f, 0.5f}}};
        host.sel = {7};
        Editor ed;
        ed.setTool(Tool::Move);
        ed.settings().snap = true;
        Hand h{ed, host};
        h.press({0.5f, 0.5f}); // 선택에 없는 개체 → 그것만 고른다
        CHECK(host.sel == std::vector<NetEntityId>{3});
        h.drag({1.9f, -0.8f});
        h.release({1.9f, -0.8f});
        CHECK(host.last<cmd::MoveEntity>().value == Vec2{1, -1});
        REQUIRE(host.previewNow.size() == 1);
        host.outcomes.push_back({1, false, 0, {}, "MoveEntity 거절: 권한"});
        h.idle();
        CHECK(host.previewNow.empty());
        CHECK(ed.lastMessage() == "MoveEntity 거절: 권한");
        CHECK(ed.stats().rejected == 1);

        // 클릭만 (끌지 않음) = 명령 없음, 결과가 오지 않으면 3 초 뒤 preview 를 거둔다
        h.press({0.5f, 0.5f});
        h.release({0.5f, 0.5f});
        CHECK(host.sent.size() == 1);
        h.press({0.5f, 0.5f});
        h.drag({3.5f, 0.5f});
        h.release({3.5f, 0.5f});
        CHECK(host.sent.size() == 2);
        for (int i = 0; i < 200; ++i) {
            h.idle();
        }
        CHECK(host.previewNow.empty());
    }

    TEST_CASE("place tool: click creates the prefab, dragging places one every spacing, outside the world is refused") {
        FakeHost host;
        Editor ed;
        ed.setTool(Tool::Place);
        Hand h{ed, host};
        h.click({1.2f, 2.7f});
        CHECK(host.sent.empty()); // builtin 콘텐츠엔 Prefab 이 없다 — 고르라고 알린다
        CHECK_FALSE(ed.lastMessage().empty());
        ed.settings().prefab = "eco.rabbit";
        ed.settings().placeSpacing = 2;
        h.click({1.2f, 2.7f});
        const auto& c = host.last<cmd::CreateEntity>();
        CHECK(c.prefab == "eco.rabbit");
        CHECK(c.position == Vec2{1.2f, 2.7f});
        host.sent.clear();
        h.press({0, 0});
        h.drag({1, 0});
        h.drag({2.1f, 0});
        h.drag({3, 0});
        h.drag({4.2f, 0});
        h.release({4.2f, 0});
        CHECK(host.sent.size() == 3); // 0 · 2.1 · 4.2
        ed.settings().snap = true;
        host.sent.clear();
        h.click({5.2f, -3.9f});
        CHECK(host.last<cmd::CreateEntity>().position == Vec2{5.5f, -3.5f}); // 칸 가운데
        host.sent.clear();
        h.click({500, 0});
        CHECK(host.sent.empty());
        // 받아들여지면 새 netId
        host.outcomes.push_back({4, true, 10, {42}, {}});
        h.idle();
        CHECK(ed.lastCreated() == std::vector<NetEntityId>{42});
    }

    TEST_CASE("terrain brush: one PaintTerrain per frame with new cells only, inside the world, right = fill") {
        FakeHost host;
        Editor ed;
        ed.setTool(Tool::TerrainBrush);
        ed.settings().material = "core.water";
        ed.settings().brushRadius = 1;
        ed.settings().brushShape = cmd::BrushShape::Square;
        Hand h{ed, host};
        h.press({0.5f, 0.5f});
        REQUIRE(host.sent.size() == 1);
        const auto& p = host.last<cmd::PaintTerrain>();
        CHECK(p.materialId == "core.water");
        CHECK(p.cells.size() == 9);
        h.drag({0.6f, 0.6f}); // 같은 칸 — 새 칸이 없다
        CHECK(host.sent.size() == 1);
        h.drag({1.5f, 0.5f}); // 오른쪽 한 칸 → 새 열 3 칸
        REQUIRE(host.sent.size() == 2);
        CHECK(host.last<cmd::PaintTerrain>().cells.size() == 3);
        h.drag({8.5f, 0.5f}); // 빠르게 끌어도 사이 칸이 빠지지 않는다
        CHECK(host.last<cmd::PaintTerrain>().cells.size() == 21);
        h.release({8.5f, 0.5f});

        // 경계 끝 (타일 31 이 마지막): 밖은 뺀다
        host.sent.clear();
        h.press({31.5f, 31.5f});
        CHECK(host.last<cmd::PaintTerrain>().cells.size() == 4);
        h.release({31.5f, 31.5f});

        // 이미 그 머티리얼인 칸은 보내지 않는다 (바탕 = core.grass 위의 오른쪽 클릭 = 보낼 것이 없다)
        host.sent.clear();
        h.press({0.5f, 0.5f}, true);
        CHECK(host.sent.empty());
        // 칠해진 칸이면 바탕으로 되돌린다
        const Vec2i t[] = {{0, 0}};
        const auto water = *host.db.findMaterial("core.water");
        host.worldGrid.paint(t, water, host.db.terrainMaterials()[water]);
        h.drag({0.5f, 0.5f}, true);
        h.release({0.5f, 0.5f});
        h.press({0.5f, 0.5f}, true);
        REQUIRE(host.sent.size() == 1);
        CHECK(host.last<cmd::PaintTerrain>().materialId == "core.grass");
        CHECK(host.last<cmd::PaintTerrain>().cells == std::vector<Vec2i>{{0, 0}});
    }

    TEST_CASE("erase tool and Delete key send DeleteEntity, Esc clears, no connection = not sent") {
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}, {7, {4.5f, 0.5f}}, {9, {10.5f, 10.5f}}};
        Editor ed;
        ed.setTool(Tool::Erase);
        Hand h{ed, host};
        h.click({4.5f, 0.5f});
        CHECK(host.last<cmd::DeleteEntity>().targets == std::vector<NetEntityId>{7});
        h.press({-1, -1});
        h.drag({11, 11});
        h.release({11, 11});
        CHECK(host.last<cmd::DeleteEntity>().targets == std::vector<NetEntityId>{3, 7, 9});
        const usize before = host.sent.size();
        h.click({20, 20}); // 빈 곳
        CHECK(host.sent.size() == before);

        host.sel = {3, 9};
        auto in = h.frame({0, 0});
        in.deleteSelection = true;
        ed.update(in, host);
        CHECK(host.last<cmd::DeleteEntity>().targets == std::vector<NetEntityId>{3, 9});
        CHECK(host.sel.empty());

        host.sel = {3};
        in = h.frame({0, 0});
        in.cancel = true;
        ed.update(in, host);
        CHECK(host.sel.empty());

        host.connected = false;
        h.click({0.5f, 0.5f});
        CHECK(ed.stats().notSent == 1);
        CHECK_FALSE(ed.lastMessage().empty());
    }

    TEST_CASE("defaults come from the content, overlay draws the brush and the box, status names the tool") {
        FakeHost host;
        Editor ed;
        Hand h{ed, host};
        h.idle();
        CHECK(ed.settings().material == "core.rock"); // 바탕(core.grass) 이 아닌 첫 머티리얼
        CHECK(ed.status() == "툴 선택");
        ed.setTool(Tool::TerrainBrush);
        CHECK(ed.status() == "툴 지형 core.rock r1");
        render::DebugDrawList lines;
        ed.drawOverlay(lines);
        CHECK(!lines.empty());
        ed.setTool(Tool::Select);
        h.press({0, 0});
        h.drag({5, 5});
        render::DebugDrawList box;
        ed.drawOverlay(box);
        CHECK(box.lines().size() == 4);
        CHECK(toolName(Tool::Erase) == "지우기");
    }
}
