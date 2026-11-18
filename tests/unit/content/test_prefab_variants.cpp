// Prefab 변형 · 오버라이드 · 전파 (v0.12 ST-06-5, ADR-0158): "inherits" — 부모 위에 키 단위로 덮어쓰기 · null 로 빼기 ·
// tags · name 은 안 쓰면 부모 것, 깊이 4 · 순환 · 없는 부모 · 틀린 모양 거절. 오버라이드 = 정규 JSON 의 차이 (Changed ·
// Added · Removed, core.transform · 엔진 관리 제외). EditContent{Prefab, propagate}: 개체 값 = 옛 Prefab 값인 필드만 새
// 값, 새 컴포넌트 붙임 · 없어진 컴포넌트는 옛 값 그대로일 때만 뗌 · 태그 · opaque, 변형의 개체도 따라온다, propagate
// false = 지금 동작, 같은 입력 = 같은 해시 (det), 세이브 → 로드 → 같은 해시 · 같은 오버라이드, 명령 JSON 왕복.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/command/CommandJson.hpp"
#include "core/command/SimCommand.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Movement.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/PrefabDiff.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

struct Pack {
    fs::path root;
    explicit Pack(const std::string& name) : root(fs::temp_directory_path() / "sbx-tests" / "prefab-variants" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        put("content/t/pack.json", R"({"id": "t", "version": "1"})");
        put("content/t/tags.json", R"(["ball", "red", "blue"])");
        put("content/t/terrain.json", R"([{"id": "t.floor", "moveCost": 10}])");
    }
    void put(const std::string& rel, std::string_view text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    }
    [[nodiscard]] content::ContentLoadResult load() const {
        return content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, engine());
    }
};

constexpr std::string_view kBall = R"({"id": "t.ball", "name": "공", "tags": ["ball"], "components": {
  "core.transform": {}, "core.movement": {"maxSpeed": 2, "accel": 4}, "core.collider": {"radius": 0.25},
  "render.sprite": {"material": "t/ball", "size": [1, 1]}}})";

sim::WorldDesc desc() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "t.floor";
    return d;
}

cmd::CommandResult run(sim::SimulationWorld& w, cmd::CommandPayload p) {
    const auto seq = static_cast<u32>(w.currentTick() + 1); // 틱마다 하나 — 단조 증가
    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, seq}, std::move(p)});
    w.tick();
    REQUIRE(w.lastResults().size() == 1);
    return w.lastResults().front();
}

ecs::Json toJson(const cmd::CommandPayload& p) {
    return cmd::payloadToJson(p, [](NetEntityId id) { return static_cast<u64>(id); });
}
Expected<cmd::CommandPayload> fromJson(const ecs::Json& j) {
    return cmd::payloadFromJson(j, [](u64 v) { return static_cast<NetEntityId>(v); });
}

bool hasRule(const content::ContentLoadResult& r, std::string_view rule, std::string_view text) {
    for (const content::ContentIssue& i : r.issues) {
        if (i.rule == rule && i.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

ecs::Json componentJson(const sim::SimulationWorld& w, ecs::EntityId e, std::string_view name) {
    const ecs::ComponentInfo* info = engine().find(name);
    REQUIRE(info != nullptr);
    const ecs::ComponentPoolBase* pool = w.registry().poolByStableId(info->stableId);
    if (pool == nullptr || !pool->contains(e)) {
        return nullptr;
    }
    ecs::Json j;
    info->writeJson(pool->getRaw(e), j);
    return j;
}

// saveId 차례의 개체들
std::vector<ecs::EntityId> entities(sim::SimulationWorld& w) {
    std::vector<std::pair<u64, ecs::EntityId>> out;
    for (auto [e, per] : w.registry().view<ecs::Read<comp::Persistence>>()) {
        out.emplace_back(per.saveId, e);
    }
    std::ranges::sort(out);
    std::vector<ecs::EntityId> ids;
    for (const auto& [s, e] : out) {
        ids.push_back(e);
    }
    return ids;
}

std::vector<content::InstanceComponent> instanceOf(const sim::SimulationWorld& w, ecs::EntityId e) {
    std::vector<content::InstanceComponent> out;
    for (const ecs::ComponentInfo& info : engine().all()) {
        const ecs::Json j = componentJson(w, e, info.name);
        if (!j.is_null()) {
            out.push_back({std::string(info.name), info.stableId, j});
        }
    }
    const u64 saveId = w.registry().read<comp::Persistence>(e).saveId;
    if (const auto it = w.opaqueComponents().find(saveId); it != w.opaqueComponents().end()) {
        for (const auto& [name, v] : it->second.items()) {
            out.push_back({name, 0, v["value"]});
        }
    }
    return out;
}

} // namespace

TEST_SUITE("content") {
    TEST_CASE("prefab variants (v0.12 ST-06-5): inherits — 키 단위 덮어쓰기 · null 로 빼기 · tags · name 은 부모 것") {
        Pack pack("resolve");
        pack.put("content/t/prefabs/ball.json", kBall);
        pack.put("content/t/prefabs/red.json", R"({"id": "t.red", "inherits": "t.ball", "tags": ["ball", "red"],
          "components": {"core.movement": {"maxSpeed": 5}, "core.collider": null, "core.velocity": {}}})");
        pack.put("content/t/prefabs/darkred.json",
                 R"({"id": "t.darkred", "inherits": "t.red", "components": {"core.movement": {"accel": 9}}})");
        const content::ContentLoadResult r = pack.load();
        REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().describe()));
        const content::Prefab* red = r.db.findPrefab("t.red");
        REQUIRE(red != nullptr);
        CHECK(red->name == "공"); // 부모 것
        CHECK(content::findPrefabComponent(*red, "core.collider") == nullptr);
        CHECK(content::findPrefabComponent(*red, "core.velocity") != nullptr);
        CHECK(content::findPrefabComponent(*red, "render.sprite") != nullptr);
        const content::PrefabComponent* mv = content::findPrefabComponent(*red, "core.movement");
        REQUIRE(mv != nullptr);
        CHECK(mv->value["maxSpeed"] == 5);
        CHECK(mv->value["accel"] == 4); // 부모의 키
        const content::Prefab* dark = r.db.findPrefab("t.darkred");
        REQUIRE(dark != nullptr);
        const content::PrefabComponent* dmv = content::findPrefabComponent(*dark, "core.movement");
        CHECK(dmv->value["maxSpeed"] == 5); // 할아버지 → 부모 → 자기
        CHECK(dmv->value["accel"] == 9);
        CHECK(dark->tags == red->tags); // tags 를 안 썼다
        CHECK(red->tags != r.db.findPrefab("t.ball")->tags);
        // 이름 차례 (인스턴스화의 적용 차례)
        CHECK(std::ranges::is_sorted(dark->components, {}, &content::PrefabComponent::name));
    }

    TEST_CASE("prefab variants (v0.12 ST-06-5): 거절 — 순환 · 깊이 4 넘음 · 없는 부모 · 문자열 아님 · 부모 없는 null") {
        Pack pack("reject");
        pack.put("content/t/prefabs/ball.json", kBall);
        pack.put("content/t/prefabs/loop.json",
                 R"([{"id": "t.a", "inherits": "t.b"}, {"id": "t.b", "inherits": "t.a"}])");
        pack.put("content/t/prefabs/deep.json",
                 R"([{"id": "t.d1", "inherits": "t.ball"}, {"id": "t.d2", "inherits": "t.d1"},
          {"id": "t.d3", "inherits": "t.d2"}, {"id": "t.d4", "inherits": "t.d3"}, {"id": "t.d5", "inherits": "t.d4"},
          {"id": "t.d6", "inherits": "t.d5"}])");
        pack.put("content/t/prefabs/bad.json",
                 R"([{"id": "t.orphan", "inherits": "t.nope"}, {"id": "t.num", "inherits": 3},
          {"id": "t.nul", "components": {"core.collider": null}}])");
        const content::ContentLoadResult r = pack.load();
        CHECK_FALSE(r.ok());
        CHECK(hasRule(r, "V2", "순환"));
        CHECK(hasRule(r, "V2", "단계를 넘는다"));
        CHECK(hasRule(r, "V2", "inherits 는 부모 Prefab id"));
        CHECK(hasRule(r, "V2", "t.nope"));
        CHECK(hasRule(r, "V3", "core.collider"));
    }

    TEST_CASE(
        "prefab overrides (v0.12 ST-06-5): 값의 차이 — Changed · Added · Removed, 자리 · 엔진 관리는 보지 않는다") {
        Pack pack("diff");
        pack.put("content/t/prefabs/ball.json", kBall);
        content::ContentLoadResult r = pack.load();
        REQUIRE(r.ok());
        sim::SimulationWorld w(engine(), r.db, desc());
        CHECK(run(w, cmd::CreateEntity{Vec2{1, 1}, {}, "t.ball"}).accepted);
        const ecs::EntityId e = entities(w).front();
        const content::Prefab& ball = *r.db.findPrefab("t.ball");
        CHECK(content::prefabOverrides(ball, engine(), instanceOf(w, e)).empty()); // 막 만든 개체 — 자리는 보지 않는다

        const NetEntityId net = w.registry().read<comp::NetIdentity>(e).netId;
        CHECK(run(w, cmd::ChangeComponent{net, ecs::stableIdOf<comp::Movement>, {{"maxSpeed", 7}}}).accepted);
        CHECK(run(w, cmd::RemoveComponent{net, ecs::stableIdOf<comp::Collider>}).accepted);
        CHECK(run(w, cmd::AddComponent{net, {ecs::stableIdOf<comp::Velocity>, ecs::Json::object()}}).accepted);
        const auto ov = content::prefabOverrides(ball, engine(), instanceOf(w, e));
        REQUIRE(ov.size() == 3);
        CHECK(ov[0].component == "core.collider");
        CHECK(ov[0].kind == content::OverrideKind::Removed);
        CHECK(ov[0].prefabValue["radius"] == doctest::Approx(0.25));
        CHECK(ov[1].component == "core.movement");
        CHECK(ov[1].kind == content::OverrideKind::Changed);
        CHECK(ov[1].fields == std::vector<std::string>{"maxSpeed"});
        CHECK(ov[2].component == "core.velocity");
        CHECK(ov[2].kind == content::OverrideKind::Added);

        // 패치 — 옛 값 그대로인 필드만
        const ecs::Json patch = content::followPatch({{"a", 1}, {"b", 2}, {"c", 3}}, {{"a", 10}, {"b", 20}, {"c", 3}},
                                                     {{"a", 1}, {"b", 5}, {"c", 3}});
        CHECK(patch == ecs::Json{{"a", 10}});
    }

    TEST_CASE(
        "prefab propagate (v0.12 ST-06-5, det): EditContent{Prefab, propagate} — 오버라이드 없는 필드 · 변형 · 태그 · "
        "opaque 가 따라오고, 같은 입력 = 같은 해시, 세이브 → 로드") {
        Pack pack("propagate");
        pack.put("content/t/prefabs/ball.json", kBall);
        pack.put("content/t/prefabs/red.json",
                 R"({"id": "t.red", "inherits": "t.ball", "components": {"core.collider": {"radius": 0.5}}})");
        content::ContentLoadResult r = pack.load();
        REQUIRE(r.ok());
        nlohmann::json next = nlohmann::json::parse(kBall);
        next["tags"] = {"ball", "blue"};
        next["components"]["core.movement"] = {{"maxSpeed", 3}, {"accel", 6}};
        next["components"]["core.velocity"] = nlohmann::json::object(); // 새 컴포넌트
        next["components"].erase("core.collider");                      // 없어진 컴포넌트
        next["components"]["render.sprite"]["material"] = "t/ball2";

        const auto play = [&](bool propagate, const fs::path& save) {
            sim::SimulationWorld w(engine(), r.db, desc());
            for (int i = 0; i < 3; ++i) {
                CHECK(run(w, cmd::CreateEntity{Vec2{static_cast<f32>(i), 0}, {}, "t.ball"}).accepted);
            }
            CHECK(run(w, cmd::CreateEntity{Vec2{5, 5}, {}, "t.red"}).accepted);
            const std::vector<ecs::EntityId> es = entities(w);
            REQUIRE(es.size() == 4);
            const auto net = [&](usize i) { return w.registry().read<comp::NetIdentity>(es[i]).netId; };
            // 둘째는 maxSpeed 오버라이드 · 셋째는 collider 를 바꿈 (그래서 남는다)
            CHECK(run(w, cmd::ChangeComponent{net(1), ecs::stableIdOf<comp::Movement>, {{"maxSpeed", 9}}}).accepted);
            CHECK(run(w, cmd::ChangeComponent{net(2), ecs::stableIdOf<comp::Collider>, {{"radius", 1.0}}}).accepted);
            CHECK(run(w, cmd::EditContent{content::ContentKind::Prefab, "t.ball", next, propagate}).accepted);
            if (!save.empty()) {
                REQUIRE(persist::saveWorld(w, save).has_value());
            }
            return std::make_pair(*w.worldHash(), [&] {
                std::vector<std::string> trace;
                for (const ecs::EntityId e : es) {
                    trace.push_back(componentJson(w, e, "core.movement").dump() + "|" +
                                    componentJson(w, e, "core.collider").dump() + "|" +
                                    componentJson(w, e, "core.velocity").dump() + "|" +
                                    std::to_string(w.registry().read<comp::Tags>(e).set.bits[0]) + "|" +
                                    w.opaqueComponents()
                                        .at(w.registry().read<comp::Persistence>(e).saveId)["render.sprite"]["value"]
                                        .dump());
                }
                return trace;
            }());
        };
        const auto [hashA, a] = play(true, pack.root / "worlds" / "propagated");
        const auto [hashB, b] = play(true, {});
        CHECK(hashA == hashB); // det — 같은 입력 = 같은 해시
        CHECK(a == b);
        const auto [hashOff, off] = play(false, {});
        CHECK(hashOff != hashA);

        // 첫째: 모두 따라온다 (maxSpeed 3 · accel 6 · collider 뗌 · velocity 붙음 · 태그 · 그림)
        CHECK(a[0].find("\"maxSpeed\":3.0") != std::string::npos);
        CHECK(a[0].find("\"accel\":6.0") != std::string::npos);
        CHECK(a[0].find("|null|{") != std::string::npos); // collider 없음 · velocity 있음
        CHECK(a[0].find("t/ball2") != std::string::npos);
        // 둘째: maxSpeed 오버라이드는 그대로, accel 은 따라온다
        CHECK(a[1].find("\"maxSpeed\":9.0") != std::string::npos);
        CHECK(a[1].find("\"accel\":6.0") != std::string::npos);
        // 셋째: 바꾼 collider 는 남는다
        CHECK(a[2].find("\"radius\":1.0") != std::string::npos);
        // 변형 (t.red): 부모의 movement · 그림이 따라오고, 자기 collider (0.5) 는 그대로 — 부모에서 빠져도 변형의 것
        CHECK(a[3].find("\"maxSpeed\":3.0") != std::string::npos);
        CHECK(a[3].find("\"radius\":0.5") != std::string::npos);
        // propagate false — 지금 동작 (이미 있는 개체는 그대로)
        CHECK(off[0].find("\"maxSpeed\":2.0") != std::string::npos);
        CHECK(off[0].find("t/ball2") == std::string::npos);
        CHECK(a[0].substr(a[0].rfind('|', a[0].rfind('|') - 1)) !=
              off[0].substr(off[0].rfind('|', off[0].rfind('|') - 1)));

        // 세이브 → 로드 — 같은 해시, 오버라이드 계산도 같다 (상태가 없다)
        auto loaded = persist::loadWorld(engine(), r.db, pack.root / "worlds" / "propagated");
        REQUIRE(loaded.has_value());
        CHECK(*loaded->world->worldHash() == hashA);

        // 명령 JSON (리플레이) 왕복 — propagate 는 true 일 때만 적힌다
        const cmd::CommandPayload on = cmd::EditContent{content::ContentKind::Prefab, "t.ball", next, true};
        const ecs::Json j = toJson(on);
        CHECK(j["propagate"] == true);
        auto back = fromJson(j);
        REQUIRE(back.has_value());
        CHECK(std::get<cmd::EditContent>(*back).propagate);
        const ecs::Json plain = toJson(cmd::EditContent{content::ContentKind::Prefab, "t.ball", next});
        CHECK_FALSE(plain.contains("propagate"));
        CHECK(
            fromJson(ecs::Json{
                         {"op", "content"}, {"kind", "prefab"}, {"id", "t.ball"}, {"value", nullptr}, {"propagate", 1}})
                .has_value() == false);
    }
}
