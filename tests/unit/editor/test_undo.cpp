// 되돌리기 · Inspector 명령 (Phase 12B, docs/10-EDITOR.md 6.2 · 10장 둘째 줄): 명령 종류마다 역명령, 거절은 기록하지
// 않는다, 다시 만든 개체의 netId 잇기, 결과를 기다리는 동안은 되돌리지 않는다.
#include <doctest/doctest.h>

#include <variant>

#include "FakeEditorHost.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/Undo.hpp"

using namespace sbx;
using namespace sbx::editor;
using sbx::test::FakeHost;
using sbx::test::Hand;

namespace {

template <typename T>
const T& at(const FakeHost& h, usize i) {
    REQUIRE(i < h.sent.size());
    const T* p = std::get_if<T>(&h.sent[i]);
    REQUIRE(p != nullptr);
    return *p;
}

void undoKey(Hand& h) {
    auto in = h.frame({100, 100});
    in.undo = true;
    h.ed.update(in, h.host);
}

void redoKey(Hand& h) {
    auto in = h.frame({100, 100});
    in.redo = true;
    h.ed.update(in, h.host);
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("undo move: inverse is the opposite delta, redo sends the move again, rejected edits are not recorded") {
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}, {7, {4.5f, 0.5f}}};
        host.sel = {3, 7};
        Editor ed;
        ed.setTool(Tool::Move);
        Hand h{ed, host};
        h.press({0.5f, 0.5f});
        h.drag({2.5f, 3.5f});
        h.release({2.5f, 3.5f});
        CHECK(ed.undoDepth() == 0); // 결과 전에는 기록하지 않는다
        CHECK_FALSE(ed.idle());
        undoKey(h); // 기다리는 중 — 아무것도 보내지 않는다
        CHECK(host.sent.size() == 1);
        host.accept(1);
        h.idle();
        REQUIRE(ed.undoDepth() == 1);
        CHECK(ed.undoLabel() == "MoveEntity");
        CHECK(ed.canUndo());

        undoKey(h);
        const auto& back = at<cmd::MoveEntity>(host, 1);
        CHECK(back.targets == std::vector<NetEntityId>{3, 7});
        CHECK(back.value == Vec2{-2, -3});
        CHECK(ed.undoDepth() == 0);
        host.accept(2);
        h.idle();
        CHECK(ed.redoDepth() == 1);
        redoKey(h);
        CHECK(at<cmd::MoveEntity>(host, 2).value == Vec2{2, 3});
        host.accept(3);
        h.idle();
        CHECK(ed.undoDepth() == 1);
        CHECK(ed.redoDepth() == 0);

        // 거절된 편집은 기록하지 않는다 (지우기 클릭)
        ed.setTool(Tool::Erase);
        h.click({4.5f, 0.5f});
        host.reject(4);
        h.idle();
        CHECK(ed.undoDepth() == 1);
    }

    TEST_CASE("undo create deletes the new entity, redo creates again and later undo targets the newest id") {
        FakeHost host;
        Editor ed;
        ed.setTool(Tool::Place);
        ed.settings().prefab = "eco.rabbit";
        ed.settings().placeSpacing = 2;
        Hand h{ed, host};
        h.press({0, 0});
        h.drag({2.5f, 0});
        h.release({2.5f, 0}); // 끌어서 둘 = 기록 한 칸
        REQUIRE(host.sent.size() == 2);
        host.accept(1, {40});
        host.accept(2, {41});
        h.idle();
        REQUIRE(ed.undoDepth() == 1);
        CHECK(ed.undoLabel() == "배치");
        undoKey(h);
        // 뒤에 만든 것부터 지운다
        CHECK(at<cmd::DeleteEntity>(host, 2).targets == std::vector<NetEntityId>{41});
        CHECK(at<cmd::DeleteEntity>(host, 3).targets == std::vector<NetEntityId>{40});
        host.accept(3);
        host.accept(4);
        h.idle();
        redoKey(h);
        CHECK(at<cmd::CreateEntity>(host, 4).prefab == "eco.rabbit");
        host.accept(5, {50});
        host.accept(6, {51});
        h.idle();
        CHECK(ed.remap().resolve(40) == 50);
        CHECK(ed.remap().resolve(41) == 51);
        undoKey(h);
        CHECK(at<cmd::DeleteEntity>(host, 6).targets == std::vector<NetEntityId>{51});
        CHECK(at<cmd::DeleteEntity>(host, 7).targets == std::vector<NetEntityId>{50});
    }

    TEST_CASE("undo delete recreates from the prefab and known values, the new id is mapped for older entries") {
        FakeHost host;
        host.entities = {{3, {1.5f, 2.5f}}};
        host.comps[3] = {host.state("core.prefab", {{"id", "eco.rabbit"}}),
                         host.state("core.transform", {{"position", {1.5, 2.5}}, {"rotation", 0.0}}),
                         host.state("life.energy", {{"value", 30.0}, {"max", 100.0}, {"drainPerSecond", 1.0}}),
                         host.state("core.movement", {{"maxSpeed", 2.5}}, false)};
        host.sel = {3};
        Editor ed;
        ed.setTool(Tool::Move);
        Hand h{ed, host};
        h.press({1.5f, 2.5f});
        h.drag({3.5f, 2.5f});
        h.release({3.5f, 2.5f});
        host.accept(1);
        h.idle();
        auto in = h.frame({100, 100});
        in.deleteSelection = true;
        ed.update(in, host);
        REQUIRE(std::holds_alternative<cmd::DeleteEntity>(host.sent.back()));
        host.accept(2);
        h.idle();
        REQUIRE(ed.undoDepth() == 2);

        undoKey(h); // 지운 것을 되살린다
        const auto& c = at<cmd::CreateEntity>(host, 2);
        CHECK(c.prefab == "eco.rabbit");
        CHECK(c.position == Vec2{1.5f, 2.5f});
        REQUIRE(c.components.size() == 3); // transform · energy · movement (prefab 은 prefab 칸으로)
        CHECK(c.components[0].value["position"][0] == 1.5);
        host.accept(3, {9});
        h.idle();
        CHECK(ed.remap().resolve(3) == 9);
        undoKey(h); // 그 전의 이동을 되돌린다 — 새 개체에
        const auto& mv = at<cmd::MoveEntity>(host, 3);
        CHECK(mv.targets == std::vector<NetEntityId>{9});
        CHECK(mv.value == Vec2{-2, 0});
    }

    TEST_CASE("undo paint restores each cell's previous material, one stroke = one entry") {
        FakeHost host;
        const auto sand = *host.db.findMaterial("core.sand");
        const Vec2i pre[] = {{1, 0}};
        host.worldGrid.paint(pre, sand, host.db.terrainMaterials()[sand]); // (1,0) 은 모래, 나머지는 풀
        Editor ed;
        ed.setTool(Tool::TerrainBrush);
        ed.settings().material = "core.water";
        ed.settings().brushRadius = 0;
        Hand h{ed, host};
        h.press({0.5f, 0.5f});
        h.drag({1.5f, 0.5f});
        h.release({1.5f, 0.5f});
        REQUIRE(host.sent.size() == 2);
        host.accept(1);
        host.accept(2);
        h.idle();
        REQUIRE(ed.undoDepth() == 1);
        CHECK(ed.undoLabel() == "지형 core.water");
        undoKey(h);
        // 뒤 프레임 것부터: (1,0) → 모래, (0,0) → 풀
        CHECK(at<cmd::PaintTerrain>(host, 2).materialId == "core.sand");
        CHECK(at<cmd::PaintTerrain>(host, 2).cells == std::vector<Vec2i>{{1, 0}});
        CHECK(at<cmd::PaintTerrain>(host, 3).materialId == "core.grass");
        CHECK(at<cmd::PaintTerrain>(host, 3).cells == std::vector<Vec2i>{{0, 0}});
    }

    TEST_CASE("inspector: setField patches every selected entity with the component, undo restores each old value") {
        FakeHost host;
        host.comps[3] = {host.state("life.energy", {{"value", 50.0}, {"max", 100.0}, {"drainPerSecond", 1.0}})};
        host.comps[7] = {host.state("life.energy", {{"value", 20.0}, {"max", 100.0}, {"drainPerSecond", 1.0}})};
        host.comps[9] = {host.state("core.prefab", {{"id", "eco.grass"}})};
        host.sel = {3, 7, 9};
        const ecs::StableId energy = host.cat.find("life.energy")->stableId;
        Editor ed;
        Hand h{ed, host};
        ed.setField(host, energy, "value", 80.0);
        REQUIRE(host.sent.size() == 2); // 9 에는 그 컴포넌트가 없다
        const auto& a = at<cmd::ChangeComponent>(host, 0);
        CHECK(a.target == 3);
        CHECK(a.stableId == energy);
        CHECK(a.patch == ecs::Json{{"value", 80.0}});
        CHECK(at<cmd::ChangeComponent>(host, 1).target == 7);
        host.accept(1);
        host.reject(2); // 하나는 거절 — 받아들여진 것만 기록
        h.idle();
        REQUIRE(ed.undoDepth() == 1);
        CHECK(ed.undoLabel() == "life.energy.value");
        undoKey(h);
        REQUIRE(host.sent.size() == 3);
        CHECK(at<cmd::ChangeComponent>(host, 2).target == 3);
        CHECK(at<cmd::ChangeComponent>(host, 2).patch == ecs::Json{{"value", 50.0}});

        // 같은 값이면 보내지 않는다
        host.accept(3);
        h.idle();
        ed.setField(host, energy, "max", 100.0);
        CHECK(host.sent.size() == 3);
    }

    TEST_CASE("inspector: add and remove component with inverses, undo rejection drops the entry") {
        FakeHost host;
        host.comps[3] = {host.state("life.energy", {{"value", 50.0}, {"max", 100.0}, {"drainPerSecond", 1.0}})};
        host.sel = {3};
        const ecs::StableId energy = host.cat.find("life.energy")->stableId;
        const ecs::StableId age = host.cat.find("life.age")->stableId;
        Editor ed;
        Hand h{ed, host};
        ed.addComponent(host, energy); // 이미 있다
        CHECK(host.sent.empty());
        ed.addComponent(host, age);
        CHECK(at<cmd::AddComponent>(host, 0).component.stableId == age);
        ed.removeComponent(host, energy);
        CHECK(at<cmd::RemoveComponent>(host, 1).stableId == energy);
        host.accept(1);
        host.accept(2);
        h.idle();
        REQUIRE(ed.undoDepth() == 2);
        undoKey(h); // 빼기를 되돌린다 = 옛 값으로 더하기
        const auto& add = at<cmd::AddComponent>(host, 2);
        CHECK(add.component.stableId == energy);
        CHECK(add.component.value["value"] == 50.0);
        host.reject(3, "권한");
        h.idle();
        CHECK(ed.undoDepth() == 1); // 거절된 되돌리기는 기록에서 뺀다
        CHECK(ed.redoDepth() == 0);
        CHECK(ed.lastMessage().find("되돌리기") != std::string::npos);
        undoKey(h); // 더하기를 되돌린다 = 빼기
        CHECK(at<cmd::RemoveComponent>(host, 3).stableId == age);
    }

    TEST_CASE("undo helpers: id remap chains, entity refs are dropped, redo is cleared by a new edit") {
        IdRemap r;
        r.set(3, 9);
        r.set(9, 12);
        CHECK(r.resolve(3) == 12);
        CHECK(r.resolve(5) == 5);
        cmd::CommandPayload p = cmd::DeleteEntity{{3, 5}};
        r.apply(p);
        CHECK(std::get<cmd::DeleteEntity>(p).targets == std::vector<NetEntityId>{12, 5});

        ecs::ComponentInfo info;
        info.fields = {ecs::FieldDesc{"target", ecs::Hint::EntityRef}, ecs::FieldDesc{"x"}};
        CHECK(withoutEntityRefs({{"target", 5}, {"x", 1}}, info) == ecs::Json{{"x", 1}});

        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}};
        host.sel = {3};
        Editor ed;
        ed.setTool(Tool::Move);
        Hand h{ed, host};
        h.press({0.5f, 0.5f});
        h.drag({1.5f, 0.5f});
        h.release({1.5f, 0.5f});
        host.accept(1);
        h.idle();
        undoKey(h);
        host.accept(2);
        h.idle();
        REQUIRE(ed.redoDepth() == 1);
        h.press({0.5f, 0.5f});
        h.drag({0.5f, 2.5f});
        h.release({0.5f, 2.5f});
        host.accept(3);
        h.idle();
        CHECK(ed.redoDepth() == 0);
        CHECK(ed.undoDepth() == 1);
    }
}
