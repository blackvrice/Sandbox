// "콘텐츠" 창의 스크립트 (v0.3 M5-T2, ADR-0066): 이 컴퓨터에서 검사 (오류마다 그 소스 줄), 적용 = EditContent Script
// ({"id", "source"}), 새 파일 틀, 줄 발췌.
#include "FakeEditorHost.hpp"

#include <filesystem>
#include <fstream>

#include "core/content/ContentLoader.hpp"
#include "editor/ui/ContentPanel.hpp"

using namespace sbx;
using namespace sbx::editor;
using namespace sbx::test;
using content::ContentKind;
namespace fs = std::filesystem;

namespace {

content::ContentDatabase loadPack(const ecs::ComponentCatalog& cat) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "script-editor";
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("t/pack.json", R"({"id": "t", "version": "1"})");
    put("t/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(0) } })
)");
    put("t/scripts/regen.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.regen", onTick(ctx) { } })
)");
    content::ContentLoadResult r = content::ContentLoader::load(root, std::vector<std::string>{"t"}, cat);
    REQUIRE(r.ok());
    return std::move(r.db);
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("script editor: 검사 — 오류마다 팩 파일 자리와 그 줄, 고친 소스는 통과, 새 파일 틀도 통과") {
        FakeHost host;
        const content::ContentDatabase db = loadPack(host.cat);
        host.contentDb = &db;
        std::vector<ContentPanelState::Issue> issues;
        const std::string ok = "import { defineSystem } from \"sandbox\"\nexport default defineSystem({ id: "
                               "\"t.regen\", intervalTicks: 3, onTick(ctx) { } })\n";
        CHECK(checkContentEdit(host, ContentKind::Script, "t.regen", ok, issues));
        CHECK(issues.empty());

        const std::string bad = "import { defineSystem } from \"sandbox\"\nexport default defineSystem({ id: "
                                "\"t.regen\", intervalTicks: 0, onTick(ctx) { } })\n";
        CHECK_FALSE(checkContentEdit(host, ContentKind::Script, "t.regen", bad, issues));
        REQUIRE_FALSE(issues.empty());
        CHECK(issues[0].error);
        CHECK(issues[0].text.find("t/scripts/regen.ts:2:") != std::string::npos);
        CHECK(issues[0].text.find("S426") != std::string::npos);
        CHECK(issues[0].text.find("\n  2 | export default defineSystem({ id: \"t.regen\", intervalTicks: 0") !=
              std::string::npos);

        // 컴포넌트 정의는 오버레이로 바꿀 수 없다
        CHECK_FALSE(checkContentEdit(host, ContentKind::Script, "t.mana",
                                     "import { defineComponent, field } from \"sandbox\"\nexport const Mana = "
                                     "defineComponent({ id: \"t.mana\", fields: { current: field.f64(0) } })\n",
                                     issues));
        bool v11 = false;
        for (const auto& i : issues) {
            v11 = v11 || i.text.find("V11") != std::string::npos;
        }
        CHECK(v11);

        // 새 파일 (오버레이로 더한다) — 틀은 시스템 하나이고 검사를 통과한다
        const std::string fresh = scriptTemplate("t.ai.wander");
        CHECK(fresh.find("id: \"t.ai.wander\"") != std::string::npos);
        CHECK(checkContentEdit(host, ContentKind::Script, "t.ai.wander", fresh, issues));
        const ecs::Json value = contentEditValue(ContentKind::Script, "t.ai.wander", fresh);
        CHECK(value == ecs::Json{{"id", "t.ai.wander"}, {"source", fresh}});
        CHECK(contentTemplate(ContentKind::Script, "t.x")["source"] == scriptTemplate("t.x"));
    }

    TEST_CASE("script editor: 적용 = EditContent Script {id, source}, 줄 발췌의 경계") {
        FakeHost host;
        const content::ContentDatabase db = loadPack(host.cat);
        host.contentDb = &db;
        Editor ed;
        const std::string src = "import { defineSystem } from \"sandbox\"\nexport default defineSystem({ id: "
                                "\"t.regen\", onTick(ctx) { ctx.log(\"hi\") } })\n";
        REQUIRE(ed.editContent(host, ContentKind::Script, "t.regen",
                               contentEditValue(ContentKind::Script, "t.regen", src)));
        const auto& sent = host.last<cmd::EditContent>();
        CHECK(sent.kind == ContentKind::Script);
        CHECK(sent.id == "t.regen");
        CHECK(sent.value["source"] == src);

        CHECK(scriptExcerpt("a\nbb\nccc", "2:1") == "\n  2 | bb");
        CHECK(scriptExcerpt("a\nbb\nccc", "3:2") == "\n  3 | ccc");
        CHECK(scriptExcerpt("a\nbb", "9:1").empty());
        CHECK(scriptExcerpt("a", "x:1").empty());
        CHECK(scriptExcerpt("a", "").empty());
        const std::string longLine(300, 'x');
        CHECK(scriptExcerpt(longLine, "1:1").size() < 200);
    }
}
