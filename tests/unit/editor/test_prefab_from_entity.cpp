// Object → Prefab (v0.13 ST-08-6 — editor/PrefabFromEntity · Editor::prefabFromEntity, ADR-0167): id 검사 (꼴 · 읽은 팩 ·
// 이미 있음), 개체 값 → Prefab JSON (바탕 Prefab 의 표시 컴포넌트 · 태그, 개체에만 뜻 있는 것 · position · 개체 참조 빼기),
// 제안 id. 로컬 서버에서: 만들고 이으면 오버라이드 0 · 되돌리기 한 번에 둘 다 돌아온다 · 새 Prefab 으로 배치.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "apps/client/NetworkSession.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentDatabase.hpp"
#include "editor/Editor.hpp"
#include "editor/PrefabFromEntity.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/renderer/RenderWorld.hpp"

using namespace sbx;
using namespace sbx::client;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

std::unique_ptr<NetworkSession> localSession(render::MaterialLibrary& mats) {
    NetworkSessionDesc d;
    d.world = "ecosystem_small";
    d.contentRoot = SBX_CONTENT_DIR;
    d.inlineServer = true;
    auto ns = NetworkSession::create(d, mats);
    REQUIRE_MESSAGE(ns.has_value(), (ns ? std::string() : ns.error().describe()));
    return std::move(*ns);
}

struct Rig {
    render::MaterialLibrary mats;
    std::unique_ptr<NetworkSession> ns = localSession(mats);
    editor::Editor ed;
    f64 now = 0;
    void idle() {
        ns->update(kDt);
        render::RenderWorld w;
        ns->extract(w);
        editor::EditorInput in;
        in.now = (now += kDt);
        in.cursor = {1e4f, 1e4f};
        ed.update(in, *ns);
    }
    template <class Pred>
    void until(Pred pred, const char* what, int frames = 150) {
        for (int i = 0; i < frames && !pred(); ++i) {
            idle();
        }
        INFO(std::string(what), " — ", ed.lastMessage());
        REQUIRE(pred());
    }
    [[nodiscard]] std::string prefabOf(NetEntityId id) const {
        for (const editor::ComponentState& s : ns->components(id)) {
            if (s.name == "core.prefab" && s.value.is_object() && s.value.contains("id")) {
                return s.value["id"].get<std::string>();
            }
        }
        return {};
    }
};

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("prefab from entity (v0.13 ST-08-6): id 검사 · 개체 값 → Prefab JSON · 제안") {
        Rig r; // ecosystem_small 의 팩 (eco) 을 읽은 콘텐츠
        r.until([&] { return r.ns->ready(); }, "step 1");
        const ecs::ComponentCatalog& cat = *r.ns->catalog();
        const content::ContentDatabase& db = *r.ns->content();
        REQUIRE_FALSE(db.packs().empty());
        const std::string pack = db.packs().front().id;
        CHECK_FALSE(editor::checkNewPrefabId(db, pack + ".fresh_one").has_value());
        CHECK(editor::checkNewPrefabId(db, "fresh").has_value());              // 팩 없음
        CHECK(editor::checkNewPrefabId(db, pack + ".Upper").has_value());      // 대문자
        CHECK(editor::checkNewPrefabId(db, pack + ".a-b").has_value());        // '-'
        CHECK(editor::checkNewPrefabId(db, "nopack.thing").has_value());       // 읽지 않은 팩
        CHECK(editor::checkNewPrefabId(db, pack + "..x").has_value());
        if (!db.prefabs().empty()) {
            CHECK(editor::checkNewPrefabId(db, db.prefabs().front().id).has_value()); // 이미 있음
        }

        // 개체 값: transform (position 빼고 rotation 남김) · 실행 상태 · core.prefab 빼기
        const auto* transform = cat.find("core.transform");
        const auto* prefabComp = cat.find("core.prefab");
        const auto* velocity = cat.find("core.velocity");
        REQUIRE(transform != nullptr);
        REQUIRE(prefabComp != nullptr);
        std::vector<editor::ComponentState> comps;
        comps.push_back({transform->stableId, "core.transform", ecs::Json{{"position", {3.0, 4.0}}, {"rotation", 0.5}}});
        comps.push_back({prefabComp->stableId, "core.prefab", ecs::Json{{"id", "x.old"}}});
        if (velocity != nullptr) {
            comps.push_back({velocity->stableId, "core.velocity", ecs::Json{{"value", {1.0, 0.0}}}});
        }
        content::Prefab base;
        base.id = pack + ".base";
        base.category = "Animals";
        base.raw = ecs::Json{{"id", base.id},
                             {"tags", {"animal"}},
                             {"components", {{"render.sprite", {{"material", "eco/rabbit"}}}, {"core.prefab", {}}}}};
        const ecs::Json p = editor::prefabFromComponents(pack + ".copy", &base, comps, cat);
        CHECK(p["id"] == pack + ".copy");
        CHECK(p["name"] == "copy");
        CHECK(p["category"] == "Animals");
        CHECK(p["tags"] == ecs::Json::array({"animal"}));
        CHECK(p["components"]["render.sprite"]["material"] == "eco/rabbit"); // 바탕의 표시 컴포넌트
        CHECK(p["components"]["core.transform"]["rotation"] == 0.5);
        CHECK_FALSE(p["components"]["core.transform"].contains("position"));
        CHECK_FALSE(p["components"].contains("core.prefab"));
        CHECK_FALSE(p["components"].contains("core.velocity"));
        // 바탕 없음 — Objects, transform 은 늘 있다
        const ecs::Json bare = editor::prefabFromComponents(pack + ".bare", nullptr, {}, cat);
        CHECK(bare["category"] == "Objects");
        CHECK(bare["components"].contains("core.transform"));

        CHECK(editor::suggestPrefabId(db, &base) == pack + ".base_variant");
        CHECK(editor::suggestPrefabId(db, nullptr).ends_with(".object"));
    }

    TEST_CASE("prefab from entity (v0.13 ST-08-6, 로컬 서버): 만들고 잇기 · 오버라이드 0 · 되돌리기 한 번 · 새 Prefab 배치") {
        Rig r;
        r.until([&] { return r.ns->ready(); }, "step 2");
        r.ns->togglePause();
        r.until([&] { return r.ns->paused(); }, "step 3");
        // 토끼 하나를 놓고 값을 바꾼다 (오버라이드)
        r.ed.settings().prefab = "eco.rabbit";
        REQUIRE(r.ed.placePrefab(*r.ns, "eco.rabbit", {2.5f, 2.5f}));
        r.until([&] { return !r.ed.lastCreated().empty(); }, "step 4");
        const NetEntityId id = r.ed.lastCreated().front();
        r.ns->setSelection({id});
        r.until([&] { return !r.ns->components(id).empty(); }, "step 5");
        const auto* movement = r.ns->catalog()->find("core.movement");
        REQUIRE(movement != nullptr);
        r.ed.setField(*r.ns, movement->stableId, "maxSpeed", ecs::Json(7.5));
        const auto maxSpeed = [&] {
            for (const editor::ComponentState& c : r.ns->components(id)) {
                if (c.name == "core.movement" && c.value.contains("maxSpeed")) {
                    return c.value["maxSpeed"].get<double>();
                }
            }
            return -1.0;
        };
        // 서버 값 (선택 상세 — 한 스냅숏 늦을 수 있다) 이 7.5 가 될 때까지
        r.until([&] { return r.ed.idle() && maxSpeed() == 7.5; }, "step 6");
        CHECK_FALSE(editor::Editor::prefabOverrides(*r.ns, id).empty());

        // 만들기 — 거절 (틀린 id · 이미 있는 id) · 성공
        CHECK_FALSE(r.ed.prefabFromEntity(*r.ns, id, "Bad"));
        CHECK_FALSE(r.ed.prefabFromEntity(*r.ns, id, "eco.rabbit"));
        REQUIRE(r.ed.prefabFromEntity(*r.ns, id, "eco.fast_rabbit"));
        r.until([&] { return r.ns->content()->findPrefab("eco.fast_rabbit") != nullptr && r.prefabOf(id) == "eco.fast_rabbit"; }, "step 7");
        r.until([&] { return r.ed.idle(); }, "step 8");
        CHECK(editor::Editor::prefabOverrides(*r.ns, id).empty()); // 같은 값 — 오버라이드 없음
        const content::Prefab* made = r.ns->content()->findPrefab("eco.fast_rabbit");
        REQUIRE(made != nullptr);
        CHECK(made->raw["components"]["core.movement"]["maxSpeed"] == 7.5);
        CHECK(made->raw["components"].contains("render.sprite")); // 토끼 그림 (바탕)
        CHECK(r.ed.stats().rejected == 0);

        // 새 Prefab 으로 배치
        REQUIRE(r.ed.placePrefab(*r.ns, "eco.fast_rabbit", {6.5f, 2.5f}));
        r.until([&] { return !r.ed.lastCreated().empty() && r.ed.lastCreated().front() != id; }, "step 9");
        const NetEntityId placed = r.ed.lastCreated().front();
        r.until([&] { return r.prefabOf(placed) == "eco.fast_rabbit"; }, "step 10");

        // 되돌리기: 배치 → 만들기 (Prefab · 잇기 둘 다 한 번에)
        r.ed.undo(*r.ns);
        r.until([&] { return r.ed.idle(); }, "step 11");
        r.ed.undo(*r.ns);
        r.until([&] { return r.ed.idle() && r.ns->content()->findPrefab("eco.fast_rabbit") == nullptr; }, "step 12");
        r.until([&] { return r.prefabOf(id) == "eco.rabbit"; }, "step 13");
    }
}
