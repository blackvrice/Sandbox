// 트리거 폼 (v0.2 M4-T3, ADR-0052): 표의 기본값으로 만든 노드는 모두 검사를 통과한다, 노드 id · 지우기 · 머리 줄 · 실행
// 표현 줄, 트리거 EditContent (되돌리기 한 칸), 폼 · 콘텐츠 패널 그리기 (ImGui 헤드리스).
#include <doctest/doctest.h>

#include <variant>

#include <imgui.h>

#include "FakeEditorHost.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/content/ContentLoader.hpp"
#include "editor/ui/ContentPanel.hpp"
#include "editor/ui/TriggerForm.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::editor;
using content::ContentKind;
using sbx::test::FakeHost;
using sbx::test::Hand;

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

std::string dump(const std::vector<ContentPanelState::Issue>& issues) {
    std::string s;
    for (const auto& i : issues) {
        s += i.text + "\n";
    }
    return s;
}

} // namespace

TEST_SUITE("editor") {

    TEST_CASE("trigger form: 틀 · 모든 종류의 기본 노드가 검사를 통과한다") {
        FakeHost host;
        host.contentDb = &eco(host.cat);
        std::vector<ContentPanelState::Issue> issues;
        const ecs::Json t = triggerTemplate("eco.fresh");
        CHECK(t == contentTemplate(ContentKind::Trigger, "eco.fresh"));
        CHECK(checkContentEdit(host, ContentKind::Trigger, "eco.fresh", t.dump(2), issues));
        INFO(dump(issues));

        for (const trigger::NodeTypeSpec& spec : trigger::nodeTypes()) {
            CAPTURE(spec.type);
            ecs::Json g{{"id", "eco.probe"}, {"nodes", ecs::Json::array()}, {"links", ecs::Json::array()}};
            if (spec.category == trigger::NodeCategory::Event) {
                g["nodes"].push_back(defaultTriggerNode(spec, "e", host.contentDb, "eco"));
                g["nodes"].push_back(ecs::Json{{"id", "x"}, {"type", "effect.event"}, {"name", "eco.x"}});
                g["links"].push_back(ecs::Json{{"from", "e"}, {"to", "x"}});
            } else {
                // 대상을 주는 이벤트 아래 — 대상이 필요한 노드도 통과해야 한다
                g["nodes"].push_back(ecs::Json{{"id", "e"}, {"type", "event.death"}});
                g["nodes"].push_back(defaultTriggerNode(spec, "n", host.contentDb, "eco"));
                g["links"].push_back(ecs::Json{{"from", "e"}, {"to", "n"}});
                if (spec.category == trigger::NodeCategory::Condition) {
                    g["nodes"].push_back(ecs::Json{{"id", "x"}, {"type", "effect.event"}, {"name", "eco.x"}});
                    g["links"].push_back(ecs::Json{{"from", "n"}, {"to", "x"}});
                }
            }
            const bool ok = checkContentEdit(host, ContentKind::Trigger, "eco.probe", g.dump(), issues);
            INFO(dump(issues));
            CHECK(ok);
        }
    }

    TEST_CASE("trigger form: 노드 id · 지우기 (연결도) · 머리 줄 · 실행 표현") {
        ecs::Json t = triggerTemplate("eco.fresh");
        CHECK(freeNodeId(t, "timer") == "timer");
        CHECK(freeNodeId(t, "start") == "start_2");
        t["nodes"].push_back(ecs::Json{{"id", "start_2"}, {"type", "event.tick"}});
        CHECK(freeNodeId(t, "start") == "start_3");
        CHECK(describeTriggerNode(t["nodes"][0]) == "[이벤트] 게임 시작 (Running) · start");
        CHECK(describeTriggerNode(ecs::Json{{"id", "q"}, {"type", "event.nope"}}) == "[?] event.nope · q");

        CHECK(removeTriggerNode(t, "say"));
        CHECK(t["nodes"].size() == 2);
        CHECK(t["links"].empty()); // say 로 가는 연결도 지웠다
        CHECK_FALSE(removeTriggerNode(t, "say"));

        FakeHost host;
        const content::TriggerDef* lost = eco(host.cat).findTrigger("eco.rabbit_lost");
        REQUIRE(lost != nullptr);
        const std::vector<std::string> lines = describeProgram(*lost);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == " 0  효과 #0 score.add");
        CHECK(lines[1] == " 1  효과 #1 event");
    }

    TEST_CASE("trigger form: 적용 = EditContent{trigger} 하나, 되돌리기는 이전 오버레이 값") {
        FakeHost host;
        host.contentDb = &eco(host.cat);
        Editor ed;
        Hand h{ed, host};
        ecs::Json t = host.contentDb->findTrigger("eco.rabbit_lost")->raw;
        t["nodes"][1]["value"] = -3;
        REQUIRE(ed.editContent(host, ContentKind::Trigger, "eco.rabbit_lost", t));
        const auto& sent = host.last<cmd::EditContent>();
        CHECK(sent.kind == ContentKind::Trigger);
        CHECK(sent.id == "eco.rabbit_lost");
        CHECK(sent.value["nodes"][1]["value"] == -3);
        host.accept(host.nextSeq);
        h.idle();
        REQUIRE(ed.undoDepth() == 1);
        CHECK(ed.undoLabel() == "trigger eco.rabbit_lost");
        auto in = h.frame({0, 0});
        in.undo = true;
        ed.update(in, host);
        CHECK(host.last<cmd::EditContent>().kind == ContentKind::Trigger);
        CHECK(host.last<cmd::EditContent>().value.is_null()); // 오버레이에 없던 것 — 팩 값으로
    }

    TEST_CASE("trigger form: 폼 · 콘텐츠 패널의 트리거 탭을 그린다 (보기만 · 고치기)") {
        FakeHost host;
        host.contentDb = &eco(host.cat);
        platform::HeadlessWindow window;
        client::ImGuiLayer layer(window);
        Editor ed;
        ContentPanelState st;
        st.kind = ContentKind::Trigger;
        st.selected = "eco.rabbit_lost";
        st.text = st.loaded = host.contentDb->findTrigger("eco.rabbit_lost")->raw.dump(2);
        bool open = true;
        TriggerFormState form;
        ecs::Json all{{"id", "eco.all"}, {"nodes", ecs::Json::array()}, {"links", ecs::Json::array()}};
        for (const trigger::NodeTypeSpec& spec : trigger::nodeTypes()) { // 모든 위젯
            all["nodes"].push_back(defaultTriggerNode(spec, freeNodeId(all, "n"), host.contentDb, "eco"));
        }
        all["links"].push_back(ecs::Json{{"from", "n"}, {"to", "n_2"}});
        for (int i = 0; i < 3; ++i) {
            host.perms.content = i != 1;
            platform::PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            drawContentPanel(ed, st, host, open);
            ImGui::Begin("all");
            ecs::Json copy = all;
            CHECK_FALSE(drawTriggerForm(copy, form, *host.contentDb, i == 1)); // 입력이 없으면 바뀌지 않는다
            CHECK(copy == all);
            ImGui::End();
            (void)layer.endFrame(platform::CursorShape::Arrow);
            layer.acknowledgeTextures();
        }
        CHECK(st.text == st.loaded);
    }
}
