// Phase 12C (docs/10-EDITOR.md 7 · 8장, ADR-0030): 역할이 막는 툴 · 편집, 콘텐츠 편집(EditContent)과 그 되돌리기,
// 콘텐츠 패널의 검사 · 틀.
#include <doctest/doctest.h>

#include <variant>

#include "FakeEditorHost.hpp"
#include "core/content/ContentLoader.hpp"
#include "editor/ui/ContentPanel.hpp"

using namespace sbx;
using namespace sbx::editor;
using content::ContentKind;
using sbx::test::FakeHost;
using sbx::test::Hand;

#ifndef SBX_CONTENT_DIR
#error "SBX_CONTENT_DIR 가 정의되어야 한다 (tests/CMakeLists.txt)"
#endif

namespace {

const content::ContentDatabase& eco(const ecs::ComponentCatalog& catalog) {
    static const content::ContentDatabase db = [&] {
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(SBX_CONTENT_DIR, ids, catalog);
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    return db;
}

ecs::Json grazesWith(const content::ContentDatabase& db, f64 energy) {
    ecs::Json j = db.findRule("eco.rabbit_grazes")->raw;
    j["effects"][0]["value"] = energy;
    return j;
}

} // namespace

TEST_SUITE("editor") {

    TEST_CASE("roles: tools the role cannot use are refused, a revoked tool falls back to select") {
        CHECK(toolAllowed(Tool::Select, EditPermissions{false, false, false, false}));
        CHECK_FALSE(toolAllowed(Tool::Move, EditPermissions{false, true, true, true}));
        CHECK_FALSE(toolAllowed(Tool::TerrainBrush, EditPermissions{true, false, true, true}));
        CHECK(toolAllowed(Tool::Erase, EditPermissions{true, false, false, false}));

        FakeHost host;
        Editor ed;
        Hand h{ed, host};
        h.key(Tool::Move);
        CHECK(ed.tool() == Tool::Move);
        // 역할이 player 로 바뀌었다 — 다음 프레임에 선택 툴로
        host.perms = EditPermissions{false, false, false, false};
        h.idle();
        CHECK(ed.tool() == Tool::Select);
        CHECK(ed.lastMessage().find("선택 툴") != std::string::npos);
        h.key(Tool::Place);
        CHECK(ed.tool() == Tool::Select);
        h.key(Tool::TerrainBrush);
        CHECK(ed.tool() == Tool::Select);

        // Delete · Inspector · 컴포넌트는 보내지 않는다
        host.entities.push_back({7, {1, 1}});
        host.sel = {7};
        host.comps[7] = {host.state("core.transform", ecs::Json{{"position", {1.0, 1.0}}, {"rotation", 0.0}})};
        auto in = h.frame({0, 0});
        in.deleteSelection = true;
        ed.update(in, host);
        const ecs::ComponentInfo* tr = host.cat.find("core.transform");
        ed.setField(host, tr->stableId, "rotation", 1.0);
        ed.removeComponent(host, tr->stableId);
        ed.addComponent(host, host.cat.find("life.age")->stableId);
        CHECK(host.sent.empty());

        // 지형만 되는 역할 (가상) — 지형 툴은 되고 이동은 안 된다
        host.perms = EditPermissions{false, true, false, false};
        h.key(Tool::TerrainBrush);
        CHECK(ed.tool() == Tool::TerrainBrush);
        h.key(Tool::Move);
        CHECK(ed.tool() == Tool::TerrainBrush);
    }

    TEST_CASE("content edit: one EditContent per apply, undo restores the previous overlay value") {
        FakeHost host;
        const content::ContentDatabase& base = eco(host.cat);
        host.contentDb = &base;
        Editor ed;
        Hand h{ed, host};

        REQUIRE(ed.editContent(host, ContentKind::Rule, "eco.rabbit_grazes", grazesWith(base, 30)));
        const auto& sent = host.last<cmd::EditContent>();
        CHECK(sent.kind == ContentKind::Rule);
        CHECK(sent.id == "eco.rabbit_grazes");
        CHECK(sent.value["effects"][0]["value"] == 30);
        host.accept(host.nextSeq);
        h.idle();
        REQUIRE(ed.undoDepth() == 1);
        CHECK(ed.undoLabel() == "rule eco.rabbit_grazes");

        // 서버가 적용한 콘텐츠 (오버레이가 있다)
        content::ContentOverlay o;
        o.set(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(base, 30));
        auto overlaid = content::ContentLoader::applyOverlay(base, o, host.cat);
        REQUIRE(overlaid.ok());
        host.contentDb = &overlaid.db;
        // 같은 값 = 보내지 않는다
        const usize before = host.sent.size();
        CHECK_FALSE(ed.editContent(host, ContentKind::Rule, "eco.rabbit_grazes", grazesWith(base, 30)));
        CHECK(host.sent.size() == before);

        // 되돌리기 = 이전 오버레이 값 (없었다 → null)
        auto in = h.frame({0, 0});
        in.undo = true;
        ed.update(in, host);
        const auto& undo = host.last<cmd::EditContent>();
        CHECK(undo.value.is_null());
        host.accept(host.nextSeq);
        h.idle();
        CHECK(ed.redoDepth() == 1);

        // 오버레이에 있던 값을 다시 바꾸면 역명령은 그 값
        REQUIRE(ed.editContent(host, ContentKind::Rule, "eco.rabbit_grazes", grazesWith(base, 50)));
        host.accept(host.nextSeq);
        h.idle();
        in = h.frame({0, 0});
        in.undo = true;
        ed.update(in, host);
        CHECK(host.last<cmd::EditContent>().value["effects"][0]["value"] == 30);

        // 콘텐츠를 바꿀 수 없는 역할
        host.perms.content = false;
        CHECK_FALSE(ed.editContent(host, ContentKind::Rule, "eco.rabbit_grazes", nullptr));
        CHECK(ed.lastMessage().find("콘텐츠") != std::string::npos);
    }

    TEST_CASE("content panel: local check reports JSON and schema problems, templates validate") {
        FakeHost host;
        host.contentDb = &eco(host.cat);
        std::vector<ContentPanelState::Issue> issues;
        CHECK(checkContentEdit(host, ContentKind::Rule, "eco.rabbit_grazes", grazesWith(eco(host.cat), 22).dump(2),
                               issues));
        CHECK(issues.empty());

        CHECK_FALSE(checkContentEdit(host, ContentKind::Rule, "eco.rabbit_grazes", "{ \"id\": ", issues));
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].error);

        ecs::Json bad = grazesWith(eco(host.cat), 1);
        bad["target"]["tags"]["all"] = ecs::Json::array({"dragon"});
        CHECK_FALSE(checkContentEdit(host, ContentKind::Rule, "eco.rabbit_grazes", bad.dump(), issues));
        REQUIRE_FALSE(issues.empty());
        CHECK(issues[0].text.find("overlay/rules/eco.rabbit_grazes.json") != std::string::npos);

        for (const ContentKind k : {ContentKind::Prefab, ContentKind::Rule, ContentKind::Behavior}) {
            CAPTURE(content::contentKindName(k));
            const ecs::Json t = contentTemplate(k, "eco.fresh");
            CHECK(t["id"] == "eco.fresh");
            CHECK(checkContentEdit(host, k, "eco.fresh", t.dump(2), issues));
        }
        // 읽지 않은 팩 접두사
        CHECK_FALSE(checkContentEdit(host, ContentKind::Rule, "zoo.fresh",
                                     contentTemplate(ContentKind::Rule, "zoo.fresh").dump(), issues));
    }
}
