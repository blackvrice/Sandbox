#pragma once
// 시험용 가짜 IEditorHost (Phase 12A · 12B · 12C): 보낸 명령을 모으고, 결과는 시험이 넣는다. 개체 = 위치 하나 (0.5 칸
// 안을 누르면 고른다), 컴포넌트 값은 comps 에, 역할 권한은 perms 에, 콘텐츠는 contentDb(없으면 내장)에. 32 px/칸
// 손(Hand)으로 한 프레임씩 입력한다.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/world/WorldGrid.hpp"
#include "editor/Editor.hpp"

namespace sbx::test {

using namespace sbx::editor;

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

    ecs::ComponentCatalog cat;
    std::map<NetEntityId, std::vector<ComponentState>> comps; // 12B: 개체별 컴포넌트 값 (시험이 채운다)
    EditPermissions perms;                                    // 12C
    u32 generation = 0;                                       // 12D
    const content::ContentDatabase* contentDb = nullptr;      // 12C: 오버레이 시험 (없으면 내장 db)

    FakeHost() : worldGrid(*world::WorldGrid::create({{-1, -1}, {0, 0}}, db, 0)) { // 64 × 64 타일, -32 ~ 31
        REQUIRE(comp::registerCoreComponents(cat).has_value());
    }

    u32 submit(cmd::CommandPayload payload) override {
        if (!connected) {
            return 0;
        }
        sent.push_back(std::move(payload));
        return ++nextSeq;
    }
    std::vector<CommandOutcome> takeOutcomes() override { return std::exchange(outcomes, {}); }
    const content::ContentDatabase* content() const override { return contentDb != nullptr ? contentDb : &db; }
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
    const ecs::ComponentCatalog* catalog() const override { return &cat; }
    std::vector<ComponentState> components(NetEntityId id) const override {
        const auto it = comps.find(id);
        return it != comps.end() ? it->second : std::vector<ComponentState>{};
    }
    void listEntities(std::vector<EntityRow>& out) const override {
        out.clear();
        for (const FakeEntity& e : entities) {
            out.push_back({e.id, {}});
        }
        std::ranges::sort(out, {}, &EntityRow::id);
    }
    std::optional<Vec2> positionOf(NetEntityId id) const override {
        for (const FakeEntity& e : entities) {
            if (e.id == id) {
                return e.position;
            }
        }
        return std::nullopt;
    }
    EditPermissions permissions() const override { return perms; }
    u32 worldGeneration() const override { return generation; }
    // 시험이 서버 대신 결과를 준다
    void accept(u32 seq, std::vector<NetEntityId> created = {}) {
        outcomes.push_back({seq, true, tick, std::move(created), {}});
    }
    void reject(u32 seq, std::string why = "거절") { outcomes.push_back({seq, false, 0, {}, std::move(why)}); }
    // 이름으로 컴포넌트 값 하나 (core.transform 등)
    ComponentState state(std::string_view name, ecs::Json value, bool replicated = true) const {
        const ecs::ComponentInfo* info = cat.find(name);
        REQUIRE(info != nullptr);
        return {info->stableId, info->name, std::move(value), replicated};
    }

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

} // namespace sbx::test
