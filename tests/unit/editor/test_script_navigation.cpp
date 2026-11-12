// 스크립트 편집기의 색인 (v0.9 ST-06-8 조각 2 — ContentPanel · ScriptEditor · LanguageService 스니펫, ADR-0136):
// 스크립트를 옮겨 다녀도 고친 글이 남는다 (drafts — 작업 공간도 그 글), F12 정의로 (다른 파일을 연다), Shift+F12 참조
// 목록, F2 이름 바꾸기 (이 파일은 편집기 · 다른 파일은 drafts — 바꾼 뒤 진단 없음), Ctrl+. 빠른 수정, 스니펫 (문장 처음
// · 들여쓰기), 위젯 키 (F12 · Shift+F12 · F2 · Ctrl+. → 요청) · 편집이 입력 칸의 콜백으로 (되돌리기).
#include "FakeEditorHost.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <imgui.h>

#include "core/content/ContentLoader.hpp"
#include "editor/services/ScriptAssist.hpp"
#include "editor/ui/ContentPanel.hpp"
#include "editor/ui/ScriptEditor.hpp"

using namespace sbx;
using namespace sbx::editor;
using namespace sbx::test;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kMana = R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(1) } })
export function refill(current: number): number {
  return current + 1
}
)";

constexpr std::string_view kRegen = R"(import { defineSystem } from "sandbox"
import { Mana, refill } from "./mana"
export default defineSystem({
  id: "t.regen",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        m.current = refill(m.current)
        ctx.write(e, Mana, m)
      }
    }
  },
})
)";

content::ContentDatabase loadPack(const ecs::ComponentCatalog& cat) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "script-navigation";
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("t/pack.json", R"({"id": "t", "version": "1"})");
    put("t/scripts/mana.ts", kMana);
    put("t/scripts/regen.ts", kRegen);
    content::ContentLoadResult r = content::ContentLoader::load(root, std::vector<std::string>{"t"}, cat);
    REQUIRE(r.ok());
    return std::move(r.db);
}

std::string applied(std::string text, std::vector<TextReplace> edits) {
    std::sort(edits.begin(), edits.end(), [](const TextReplace& a, const TextReplace& b) { return a.begin > b.begin; });
    for (const TextReplace& e : edits) {
        text.replace(e.begin, e.end - e.begin, e.text);
    }
    return text;
}

struct Ui {
    ImGuiContext* ctx = nullptr;
    Ui() {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1000.f, 700.f};
        io.DeltaTime = 1.f / 60.f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefaultBitmap();
    }
    ~Ui() { ImGui::DestroyContext(ctx); }
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
};

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("script navigation (v0.9 ST-06-8): 고친 글은 옮겨 다녀도 남고 작업 공간도 그 글이다") {
        FakeHost host;
        const content::ContentDatabase db = loadPack(host.cat);
        ContentPanelState st;
        REQUIRE(openScript(st, db, "t.regen"));
        CHECK(st.kind == content::ContentKind::Script);
        CHECK(st.text == kRegen);
        st.text += "// 고치는 중\n";
        REQUIRE(openScript(st, db, "t.mana"));
        CHECK(st.drafts.contains("t.regen"));
        CHECK(st.text == kMana);
        REQUIRE(openScript(st, db, "t.regen", 3));
        CHECK(st.text.ends_with("// 고치는 중\n"));
        CHECK(st.loaded == kRegen);
        CHECK(st.script.gotoOffset == std::optional<usize>(3));
        // 팩 값으로 돌려놓고 옮기면 draft 를 지운다
        st.text = st.loaded;
        REQUIRE(openScript(st, db, "t.mana"));
        CHECK_FALSE(st.drafts.contains("t.regen"));
        CHECK_FALSE(openScript(st, db, "t.nope"));

        // 작업 공간은 drafts 를 쓴다 — mana 의 refill 을 다른 이름으로 고쳐 두면 regen 의 진단이 그것을 본다
        ScriptDrafts drafts{{"t.mana", std::string(kMana).replace(std::string(kMana).find("refill"), 6, "other")}};
        const auto ws = scriptWorkspace(db, "t.regen", kRegen, &drafts);
        REQUIRE(ws);
        CHECK_FALSE(analyzeScript(*ws).empty());
        const auto clean = scriptWorkspace(db, "t.regen", kRegen);
        REQUIRE(clean);
        CHECK(analyzeScript(*clean).empty());
    }

    TEST_CASE("script navigation (v0.9 ST-06-8): F12 정의로 · Shift+F12 참조 · F2 이름 바꾸기 · Ctrl+. 빠른 수정") {
        FakeHost host;
        const content::ContentDatabase db = loadPack(host.cat);
        ContentPanelState st;
        REQUIRE(openScript(st, db, "t.regen"));
        const usize call = st.text.find("refill(m.current)");

        // 정의 — 다른 파일 (mana) 을 열고 그 선언으로
        REQUIRE(goToDefinition(st, db, call + 2));
        CHECK(st.selected == "t.mana");
        REQUIRE(st.script.gotoOffset.has_value());
        CHECK(*st.script.gotoOffset == std::string(kMana).find("refill("));
        // 같은 파일이면 커서만
        REQUIRE(openScript(st, db, "t.regen"));
        st.script.gotoOffset.reset();
        REQUIRE(goToDefinition(st, db, st.text.find("m.current =")));
        CHECK(st.selected == "t.regen");
        CHECK(*st.script.gotoOffset == st.text.find("m = ctx.read"));
        CHECK_FALSE(goToDefinition(st, db, st.text.find("defineSystem({")));

        // 참조 — 선언 (mana) · import · 호출
        REQUIRE(findScriptReferences(st, db, call) == 3);
        CHECK(st.referencesOf == "refill");
        CHECK(st.references[0].id == "t.mana");
        CHECK(st.references[0].line == 3);
        CHECK(st.references[0].preview == "export function refill(current: number): number {");
        CHECK(st.references[1].id == "t.regen");
        CHECK(st.references[2].line == 9);

        // 이름 바꾸기 — 이 파일은 편집기 (pendingEdits), mana 는 draft
        CHECK(renameInScripts(st, db, call, "replenish").empty());
        CHECK(st.script.pendingEdits.size() == 2);
        REQUIRE(st.drafts.contains("t.mana"));
        CHECK(st.drafts["t.mana"].find("export function replenish(") != std::string::npos);
        const std::string regen = applied(st.text, st.script.pendingEdits);
        CHECK(regen.find("import { Mana, replenish } from \"./mana\"") != std::string::npos);
        const auto ws = scriptWorkspace(db, "t.regen", regen, &st.drafts);
        REQUIRE(ws);
        CHECK(analyzeScript(*ws).empty()); // 두 파일이 함께 바뀌었다
        CHECK_FALSE(st.issues.empty());
        CHECK(st.issues.back().text.find("다른 파일 1 개") != std::string::npos);
        // 거절은 이유
        st.script.pendingEdits.clear();
        CHECK_FALSE(renameInScripts(st, db, call, "for").empty());
        CHECK(st.script.pendingEdits.empty());

        // 빠른 수정 — import 를 지운 글에서 refill 위
        ContentPanelState q;
        REQUIRE(openScript(q, db, "t.regen"));
        q.text.replace(q.text.find("Mana, refill"), 12, "Mana");
        const auto fixes = scriptQuickFixes(q, db, q.text.find("refill("));
        REQUIRE_FALSE(fixes.empty());
        CHECK(fixes[0].title == "import { refill } from \"./mana\"");
        applyQuickFix(q, fixes[0]);
        const std::string fixed = applied(q.text, q.script.pendingEdits);
        CHECK(fixed.find("import { Mana, refill } from \"./mana\"") != std::string::npos);
    }

    TEST_CASE("script navigation (v0.9 ST-06-8): 스니펫 — 문장 처음에서만 · 넣으면 줄의 들여쓰기") {
        const std::string head = "import { defineSystem } from \"sandbox\"\nexport function f(): void {\n  for";
        const std::string text = head + "\n}\n";
        const std::vector<script::SourceFile> files{{"scripts/a.ts", text}};
        const usize cursor = head.size();
        const auto items = script::complete(script::LanguageQuery{files, 0, cursor, {}});
        const auto it = std::find_if(items.begin(), items.end(), [](const script::CompletionItem& c) {
            return c.kind == script::CompletionKind::Snippet && c.label == "forquery";
        });
        REQUIRE(it != items.end());
        CHECK_FALSE(it->insertText.empty());
        // 식 안 (문장 처음이 아님) 에는 없다
        const std::string expr = "export function f(): number {\n  return 1 + sys\n}\n";
        const std::vector<script::SourceFile> files2{{"scripts/a.ts", expr}};
        const auto items2 = script::complete(script::LanguageQuery{files2, 0, expr.find("sys") + 3, {}});
        CHECK(std::none_of(items2.begin(), items2.end(),
                           [](const script::CompletionItem& c) { return c.kind == script::CompletionKind::Snippet; }));

        // 넣기 — 둘째 줄부터 "  " 들여쓰기
        ScriptWorkspace ws;
        ws.files = files;
        ScriptAssist a;
        a.openCompletion(ws, cursor);
        REQUIRE(a.completionOpen());
        while (a.completionItems()[a.selected()].label != "forquery") {
            a.moveSelection(1);
        }
        const auto r = a.acceptCompletion(text, cursor);
        REQUIRE(r.has_value());
        CHECK(r->text.starts_with("for (const e of ctx.query(Thing)) {\n    const c = ctx.read(e, Thing)\n"));
        CHECK(r->text.ends_with("\n  }"));
    }

    TEST_CASE("script navigation (v0.9 ST-06-8): 위젯 키 → 요청, 편집은 입력 칸의 콜백으로") {
        Ui ui;
        ImGuiIO& io = ImGui::GetIO();
        std::string text(kRegen);
        ScriptEditorState st;
        const auto draw = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0.f, 0.f});
            ImGui::SetNextWindowSize({900.f, 600.f});
            ImGui::Begin("nav-test");
            (void)drawScriptEditor("##ed", text, {800.f, 500.f}, false, st, {});
            ImGui::End();
            ImGui::Render();
        };
        const auto press = [&](ImGuiKeyChord chord) {
            const bool shift = (chord & ImGuiMod_Shift) != 0;
            const bool ctrl = (chord & ImGuiMod_Ctrl) != 0;
            const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
#ifdef __APPLE__
            const ImGuiKey ctrlKey = ImGuiKey_LeftSuper; // ImGui 는 macOS 에서 Cmd ↔ Ctrl 을 바꾼다
#else
            const ImGuiKey ctrlKey = ImGuiKey_LeftCtrl;
#endif
            if (shift) {
                io.AddKeyEvent(ImGuiKey_LeftShift, true);
                io.AddKeyEvent(ImGuiMod_Shift, true);
            }
            if (ctrl) {
                io.AddKeyEvent(ctrlKey, true);
#ifdef __APPLE__
                io.AddKeyEvent(ImGuiMod_Super, true);
#else
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
#endif
            }
            io.AddKeyEvent(key, true);
            draw();
            io.AddKeyEvent(key, false);
            if (shift) {
                io.AddKeyEvent(ImGuiKey_LeftShift, false);
                io.AddKeyEvent(ImGuiMod_Shift, false);
            }
            if (ctrl) {
                io.AddKeyEvent(ctrlKey, false);
#ifdef __APPLE__
                io.AddKeyEvent(ImGuiMod_Super, false);
#else
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
#endif
            }
            draw();
        };
        const usize at = text.find("refill(m.current)") + 2;
        st.gotoOffset = at;
        for (int i = 0; i < 4; ++i) {
            draw();
        }
        REQUIRE(st.active);
        REQUIRE(st.cursor == at);
        using R = ScriptEditorState::Request;
        press(ImGuiKey_F12);
        REQUIRE(st.request.has_value());
        CHECK(st.request->first == R::Definition);
        CHECK(st.request->second == at);
        st.request.reset();
        press(ImGuiMod_Shift | ImGuiKey_F12);
        REQUIRE(st.request.has_value());
        CHECK(st.request->first == R::References);
        st.request.reset();
        press(ImGuiKey_F2);
        REQUIRE(st.request.has_value());
        CHECK(st.request->first == R::Rename);
        st.request.reset();
        press(ImGuiMod_Ctrl | ImGuiKey_Period);
        REQUIRE(st.request.has_value());
        CHECK(st.request->first == R::QuickFix);
        st.request.reset();

        // 편집 — 활성 입력 칸이면 콜백 안에서 (커서는 앞 편집만큼 민다)
        const usize imp = text.find("refill }");
        st.pendingEdits = {{imp, imp + 6, "replenish"}, {at - 2, at + 4, "replenish"}};
        draw();
        draw();
        CHECK(st.pendingEdits.empty());
        CHECK(text.find("import { Mana, replenish } from") != std::string::npos);
        CHECK(text.find("m.current = replenish(m.current)") != std::string::npos);
        CHECK(st.cursor == at - 2 + 9 + 3); // 바꾼 이름 끝 (앞 줄의 import 편집이 3 바이트 민다)
        CHECK(st.active);                   // 바깥 변경이 아니다 — 입력 칸 그대로
    }
}
