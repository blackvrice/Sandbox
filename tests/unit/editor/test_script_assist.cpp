// 스크립트 편집기 (v0.9 M6-T8 조각 2 — editor/services/ScriptAssist · editor/ui/ScriptEditor, ADR-0130): 작업 공간 (팩
// · 오버레이 · 의존 팩 — 콘텐츠 로더와 같은 진단), 밑줄 범위, 입력 멈춤 300 ms, 완성 세션 (거르기 · 다시 묻기 · 넣기),
// hover 기억, 위젯 (ImGui 프레임 — 글 배치 왕복, '.' 팝업 · 아래 · Enter 넣기 · Esc 는 팝업만 · 진단은 300 ms 뒤 ·
// 이동)
#include "FakeEditorHost.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

#include <imgui.h>

#include "core/content/ContentLoader.hpp"
#include "editor/services/DocumentService.hpp"
#include "editor/services/ScriptAssist.hpp"
#include "editor/ui/ContentPanel.hpp"
#include "editor/ui/ScriptEditor.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/text/Utf8.hpp"

using namespace sbx;
using namespace sbx::editor;
using namespace sbx::test;
using content::ContentKind;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kRegen = R"(import { defineSystem } from "sandbox"
import { Mana, refill } from "@inv/mana"
export default defineSystem({
  id: "rpg.regen",
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

// inv (컴포넌트 · 함수) ← rpg (시스템 · 함수 파일 하나)
content::ContentDatabase loadPacks(const ecs::ComponentCatalog& cat, std::string_view folder = "script-assist") {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / folder;
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("inv/pack.json", R"({"id": "inv", "version": "1.0.0"})");
    put("inv/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "inv.mana", fields: { current: field.f32(10) } })
export function refill(current: number): number { return current + 5 }
)");
    put("rpg/pack.json", R"({"id": "rpg", "version": "1.0.0", "requires": ["inv"]})");
    put("rpg/scripts/regen.ts", kRegen);
    put("rpg/scripts/util.ts", "export function twice(x: number): number { return x * 2 }\n");
    content::ContentLoadResult r = content::ContentLoader::load(root, std::vector<std::string>{"inv", "rpg"}, cat);
    REQUIRE(r.ok());
    return std::move(r.db);
}

const script::SourceFile* fileOf(const ScriptWorkspace& ws, std::string_view path) {
    const auto it =
        std::find_if(ws.files.begin(), ws.files.end(), [&](const script::SourceFile& f) { return f.path == path; });
    return it != ws.files.end() ? &*it : nullptr;
}

std::string readAll(const fs::path& path) {
    auto bytes = io::readFile(path);
    REQUIRE(bytes);
    return *bytes;
}

void writeAll(const fs::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}

// 파일 하나짜리 작업 공간 (위젯 · 완성 시험)
ScriptWorkspace single(std::string_view text) {
    ScriptWorkspace ws;
    ws.files.push_back(script::SourceFile{"scripts/a.ts", std::string(text)});
    ws.imports.push_back({std::string(), {}});
    return ws;
}

bool hasLabel(std::span<const script::CompletionItem> items, std::string_view label) {
    return std::any_of(items.begin(), items.end(), [&](const script::CompletionItem& c) { return c.label == label; });
}

// 창 하나 · 기본 비트맵 글꼴 (글자 폭이 구현마다 같다) — 렌더러 없이 프레임만
struct Ui {
    ImGuiContext* ctx = nullptr;
    Ui() {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1000.f, 700.f};
        io.DeltaTime = 1.f / 60.f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures; // 글꼴 텍스처는 만들지 않는다 (요청만)
        io.Fonts->AddFontDefaultBitmap();
    }
    ~Ui() { ImGui::DestroyContext(ctx); }
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    template <class F>
    void frame(F&& body) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.f, 0.f});
        ImGui::SetNextWindowSize({900.f, 600.f});
        ImGui::Begin("script-test");
        body();
        ImGui::End();
        ImGui::Render();
    }
};

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("script assist (v0.9 M6-T8): 작업 공간 — 편집 중인 글 · 팩 파일 · 의존 팩 (@inv) · 오버레이 소스") {
        FakeHost host;
        const content::ContentDatabase db = loadPacks(host.cat);
        const auto ws = scriptWorkspace(db, "rpg.regen", "// 고치는 중\n");
        REQUIRE(ws);
        REQUIRE(ws->file < ws->files.size());
        CHECK(ws->files[ws->file].path == "scripts/regen.ts");
        CHECK(ws->files[ws->file].text == "// 고치는 중\n");
        CHECK(fileOf(*ws, "scripts/util.ts") != nullptr);
        REQUIRE(fileOf(*ws, "@inv/scripts/mana.ts") != nullptr);
        CHECK(fileOf(*ws, "@inv/scripts/mana.ts")->text.find("inv.mana") != std::string::npos);
        REQUIRE(ws->imports.size() == 2);
        CHECK(ws->imports[0].pack.empty());
        CHECK(ws->imports[0].allowed == std::vector<std::string>{"inv"});
        CHECK(ws->imports[1].pack == "inv");

        // 팩에 없는 파일 (새 파일) 은 더한다, 읽지 않은 팩 · 규칙 밖 id 는 없다
        const auto added = scriptWorkspace(db, "rpg.extra", "export const x = 1\n");
        REQUIRE(added);
        CHECK(added->files[added->file].path == "scripts/extra.ts");
        CHECK_FALSE(scriptWorkspace(db, "nope.regen", "").has_value());
        CHECK_FALSE(scriptWorkspace(db, "Rpg.Regen", "").has_value());

        // 오버레이 소스가 팩 원본을 대신한다 (콘텐츠 로더와 같다)
        content::ContentOverlay overlay;
        overlay.set(
            ContentKind::Script, "rpg.util",
            ecs::Json{{"id", "rpg.util"}, {"source", "export function twice(x: number): number { return x * 3 }\n"}});
        const content::ContentLoadResult over = content::ContentLoader::applyOverlay(db, overlay, host.cat);
        REQUIRE(over.ok());
        const auto ws2 = scriptWorkspace(over.db, "rpg.regen", std::string(kRegen));
        REQUIRE(ws2);
        REQUIRE(fileOf(*ws2, "scripts/util.ts") != nullptr);
        CHECK(fileOf(*ws2, "scripts/util.ts")->text.find("x * 3") != std::string::npos);
    }

    TEST_CASE("script assist (v0.9 M6-T8): 진단 — 콘텐츠 로더의 검사와 같은 코드 · 자리, 그 파일 것만, 이름 밑줄") {
        FakeHost host;
        const content::ContentDatabase db = loadPacks(host.cat);
        host.contentDb = &db;
        const auto good = scriptWorkspace(db, "rpg.regen", std::string(kRegen));
        REQUIRE(good);
        CHECK(analyzeScript(*good).empty());

        std::string bad(kRegen);
        const usize at = bad.find("refill(m.current)");
        bad.replace(at, 6, "refil"); // 없는 이름
        const auto ws = scriptWorkspace(db, "rpg.regen", bad);
        REQUIRE(ws);
        const std::vector<ScriptMark> marks = analyzeScript(*ws);
        REQUIRE(marks.size() == 1);
        CHECK(marks[0].error);
        CHECK(marks[0].begin == at);
        CHECK(marks[0].end == at + 5);
        CHECK(bad.substr(marks[0].begin, marks[0].end - marks[0].begin) == "refil");

        std::vector<ContentPanelState::Issue> issues;
        CHECK_FALSE(checkContentEdit(host, ContentKind::Script, "rpg.regen", bad, issues));
        REQUIRE_FALSE(issues.empty());
        const std::string lineCol = marks[0].text.substr(0, marks[0].text.find(": "));
        CAPTURE(issues[0].text);
        CAPTURE(marks[0].text);
        CHECK(issues[0].text.find("regen.ts:" + lineCol) != std::string::npos);
        CHECK(issues[0].text.find(marks[0].code) != std::string::npos);
        CHECK(marks[0].text.find("고치는 법") != std::string::npos);

        // 다른 파일의 오류는 이 파일의 밑줄이 아니다
        ScriptWorkspace broken = *good;
        for (script::SourceFile& f : broken.files) {
            if (f.path == "scripts/util.ts") {
                f.text = "export function twice(: number\n";
            }
        }
        CHECK(analyzeScript(broken).empty());
    }

    TEST_CASE("script assist (v0.9 M6-T8): 밑줄 범위 — 이름 · 글자 하나 · 줄 끝은 앞 글자 · 빈 줄 · UTF-8") {
        const std::string t = "ab cd;\n가나\n";
        CHECK(markRange(t, 0) == std::pair<usize, usize>{0, 2});
        CHECK(markRange(t, 3) == std::pair<usize, usize>{3, 5});
        CHECK(markRange(t, 5) == std::pair<usize, usize>{5, 6});    // ';'
        CHECK(markRange(t, 6) == std::pair<usize, usize>{5, 6});    // 줄 끝 → 앞 글자
        CHECK(markRange(t, 7) == std::pair<usize, usize>{7, 13});   // 한글 이름 (6 바이트)
        CHECK(markRange(t, 13) == std::pair<usize, usize>{10, 13}); // 줄 끝 → 앞 글자 "나" (가운데를 자르지 않는다)
        CHECK(markRange(t, 14) == std::pair<usize, usize>{14, 14}); // 글 끝 · 빈 줄
        CHECK(markRange(t, 999) == std::pair<usize, usize>{14, 14});
        CHECK(markRange("", 0) == std::pair<usize, usize>{0, 0});
        CHECK(markRange("(\r\n", 1) == std::pair<usize, usize>{0, 1}); // CRLF 의 CR 도 줄 끝
    }

    TEST_CASE("script assist (v0.9 M6-T8): 입력 멈춤 300 ms · 늦은 결과는 버린다 · reset") {
        ScriptAssist a;
        CHECK(a.analysisDue(0.0)); // 처음은 기다리지 않는다
        CHECK(a.analyzed(a.version(), {}));
        CHECK_FALSE(a.analysisDue(0.0));
        CHECK_FALSE(a.marksStale());

        a.edited(1.0);
        CHECK(a.marksStale());
        CHECK_FALSE(a.analysisDue(1.2));
        CHECK(a.analysisDue(1.31));
        const u64 old = a.version();
        a.edited(1.25); // 그사이 또 고쳤다
        CHECK_FALSE(a.analysisDue(1.5));
        CHECK_FALSE(a.analyzed(old, {ScriptMark{}}));
        CHECK(a.marks().empty());
        CHECK(a.analyzed(a.version(), {ScriptMark{2, 5, true, "S406", "1:3: 오류 S406 — x"}}));
        CHECK_FALSE(a.analysisDue(9.0));
        CHECK(a.markAt(2) != nullptr);
        CHECK(a.markAt(4) != nullptr);
        CHECK(a.markAt(5) == nullptr);
        CHECK(a.markAt(1) == nullptr);

        a.reset();
        CHECK(a.marks().empty());
        CHECK(a.analysisDue(0.0));
    }

    TEST_CASE("script assist (v0.9 M6-T8): 완성 세션 — 거르기 · 고르기 · 넣기 · 이름을 벗어나면 닫기 · 다시 묻기") {
        const std::string head = "import { defineSystem } from \"sandbox\"\nexport default defineSystem({ id: \"t.a\", "
                                 "onTick(ctx) {\n  const counter = 1\n  ";
        const std::string tail = "\n} })\n";
        ScriptAssist a;

        // ctx. — 접두 없이 물은 후보 (서버 ctx)
        std::string text = head + "ctx." + tail;
        usize cursor = head.size() + 4;
        a.openCompletion(single(text), cursor);
        REQUIRE(a.completionOpen());
        CHECK(a.completionAnchor() == cursor);
        CHECK(hasLabel(a.completionItems(), "read"));
        CHECK(hasLabel(a.completionItems(), "write"));
        const usize all = a.completionItems().size();

        // 계속 친다 — 다시 묻지 않고 거른다
        text = head + "ctx.re" + tail;
        cursor += 2;
        CHECK_FALSE(a.refreshCompletion(text, cursor));
        REQUIRE(a.completionOpen());
        CHECK(a.completionItems().size() < all);
        CHECK(hasLabel(a.completionItems(), "read"));
        for (const script::CompletionItem& c : a.completionItems()) {
            CHECK((c.label[0] == 'r' || c.label[0] == 'R'));
        }
        a.moveSelection(-1); // 처음에서 위 → 끝
        CHECK(a.selected() == a.completionItems().size() - 1);
        a.moveSelection(1);
        CHECK(a.selected() == 0);
        const std::string first = a.completionItems()[0].label;
        const auto r = a.acceptCompletion(cursor);
        REQUIRE(r);
        CHECK(r->begin == head.size() + 4);
        CHECK(r->end == cursor);
        CHECK(r->text == first);
        CHECK_FALSE(a.completionOpen());

        // 이름 밖 글자를 쳤다 → 닫는다
        text = head + "ctx." + tail;
        cursor = head.size() + 4;
        a.openCompletion(single(text), cursor);
        REQUIRE(a.completionOpen());
        text = head + "ctx.(" + tail;
        CHECK_FALSE(a.refreshCompletion(text, cursor + 1));
        CHECK_FALSE(a.completionOpen());

        // 이름 완성 (접두가 있어야 한다) — 접두가 처음보다 짧아지면 다시 묻는다
        text = head + "cou" + tail;
        cursor = head.size() + 3;
        a.openCompletion(single(text), cursor);
        REQUIRE(a.completionOpen());
        CHECK(hasLabel(a.completionItems(), "counter"));
        text = head + "co" + tail;
        CHECK(a.refreshCompletion(text, cursor - 1));
        CHECK_FALSE(a.completionOpen());
        // 다 쓴 이름은 보이지 않는다 (complete 와 같다) — 남은 것이 없으면 닫는다
        text = head + "cou" + tail;
        a.openCompletion(single(text), cursor);
        REQUIRE(a.completionOpen());
        text = head + "counter" + tail;
        CHECK_FALSE(a.refreshCompletion(text, head.size() + 7));
        CHECK_FALSE(hasLabel(a.completionItems(), "counter"));

        // 빈 자리의 Ctrl+Space 는 아무것도 열지 않는다
        text = head + tail;
        a.openCompletion(single(text), head.size());
        CHECK_FALSE(a.completionOpen());
    }

    TEST_CASE("script assist (v0.9 M6-T8): hover 기억 — 같은 편집 번호 · 자리만") {
        ScriptAssist a;
        CHECK(a.cachedHover(5) == nullptr);
        a.storeHover(5, std::nullopt);
        REQUIRE(a.cachedHover(5) != nullptr);
        CHECK_FALSE(a.cachedHover(5)->has_value());
        CHECK(a.cachedHover(6) == nullptr);
        a.storeHover(6, script::HoverInfo{"함수 f(): number", "설명", 6, 7});
        REQUIRE(a.cachedHover(6) != nullptr);
        CHECK((*a.cachedHover(6))->title == "함수 f(): number");
        a.edited(0.0);
        CHECK(a.cachedHover(6) == nullptr);
    }

    TEST_CASE("script save (v0.9 M6-T8 조각 3): 줄 끝 보존 · 바깥 변경 · 덮어쓰기 · 없어진 파일 · 새 파일") {
        const fs::path dir = fs::temp_directory_path() / "sbx-tests" / "script-save";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        services::DocumentService docs(nullptr);

        // CRLF 파일 — 편집기가 넣은 LF 를 CRLF 로 되돌린다
        const fs::path crlf = dir / "crlf.ts";
        writeAll(crlf, "const a = 1\r\nconst b = 2\r\n");
        ScriptSaveResult r = saveScriptFile(docs, crlf, "const a = 1\r\nconst b = 2\nconst c = 3\n",
                                            std::string_view("const a = 1\r\nconst b = 2\r\n"), false);
        CHECK(r.status == ScriptSaveResult::Status::Saved);
        CHECK(readAll(crlf) == "const a = 1\r\nconst b = 2\r\nconst c = 3\r\n");
        CHECK(r.written == readAll(crlf));

        // LF 파일은 LF, 섞인 파일은 그대로
        const fs::path lf = dir / "lf.ts";
        writeAll(lf, "x\ny\n");
        r = saveScriptFile(docs, lf, "x\ny\nz\n", std::string_view("x\ny\n"), false);
        CHECK(r.status == ScriptSaveResult::Status::Saved);
        CHECK(readAll(lf) == "x\ny\nz\n");
        const fs::path mixed = dir / "mixed.ts";
        writeAll(mixed, "x\r\ny\n");
        r = saveScriptFile(docs, mixed, "x\r\ny\nz\n", std::string_view("x\r\ny\n"), false);
        CHECK(readAll(mixed) == "x\r\ny\nz\n");

        // 바깥에서 고쳤다 — base 와 다르면 쓰지 않고, force 면 쓴다 (그 문서가 열려 있어도)
        writeAll(crlf, "// 다른 도구가 고침\r\n");
        r = saveScriptFile(docs, crlf, "const a = 9\n", std::string_view(r.written), false);
        CHECK(r.status == ScriptSaveResult::Status::DiskChanged);
        CHECK(readAll(crlf) == "// 다른 도구가 고침\r\n");
        r = saveScriptFile(docs, crlf, "const a = 9\n", std::string_view("const a = 1\r\n"), true);
        CHECK(r.status == ScriptSaveResult::Status::Saved);
        CHECK(readAll(crlf) == "const a = 9\r\n"); // 줄 끝은 base 의 것

        // 없어졌다 — 묻고, force 면 다시 만든다
        fs::remove(lf);
        r = saveScriptFile(docs, lf, "x\n", std::string_view("x\ny\nz\n"), false);
        CHECK(r.status == ScriptSaveResult::Status::DiskChanged);
        CHECK_FALSE(fs::exists(lf));
        r = saveScriptFile(docs, lf, "x\n", std::string_view("x\ny\nz\n"), true);
        CHECK(r.status == ScriptSaveResult::Status::Saved);
        CHECK(readAll(lf) == "x\n");

        // 새 파일 (base 없음) — 폴더까지 만든다
        const fs::path fresh = dir / "ai" / "deep" / "wander.ts";
        r = saveScriptFile(docs, fresh, "export const w = 1\n", std::nullopt, false);
        CHECK(r.status == ScriptSaveResult::Status::Saved);
        CHECK(readAll(fresh) == "export const w = 1\n");
        CHECK_FALSE(docs.dirty(*docs.find(fresh)));
    }

    TEST_CASE(
        "script save (v0.9 M6-T8 조각 3): 콘텐츠 창 — 팩 폴더의 그 파일 · base 이어가기 · 다시 읽으면 그 글로 컴파일") {
        FakeHost host;
        const content::ContentDatabase db = loadPacks(host.cat, "script-save-pack");
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "script-save-pack";
        const fs::path file = root / "rpg" / "scripts" / "regen.ts";
        services::DocumentService docs(nullptr);
        std::map<std::string, SavedScript, std::less<>> saved;

        std::string text(kRegen);
        text.replace(text.find("refill(m.current)"), 17, "refill(m.current) + 1");
        ScriptSaveResult r = saveScriptEdit(db, docs, "rpg.regen", text, saved, false);
        REQUIRE(r.status == ScriptSaveResult::Status::Saved);
        CHECK(readAll(file) == text);
        REQUIRE(saved.contains("rpg.regen"));
        CHECK(saved["rpg.regen"].text == text);

        // 다음 저장의 base 는 이 창이 쓴 것 (팩을 읽은 때의 원문이 아니다)
        text += "// 두 번째\n";
        r = saveScriptEdit(db, docs, "rpg.regen", text, saved, false);
        CHECK(r.status == ScriptSaveResult::Status::Saved);
        // 바깥에서 고치면 멈춘다
        writeAll(file, "// 다른 도구\n");
        r = saveScriptEdit(db, docs, "rpg.regen", text + "// 세 번째\n", saved, false);
        CHECK(r.status == ScriptSaveResult::Status::DiskChanged);
        CHECK(readAll(file) == "// 다른 도구\n");
        r = saveScriptEdit(db, docs, "rpg.regen", text, saved, true);
        CHECK(r.status == ScriptSaveResult::Status::Saved);

        // 팩에 없던 새 파일 · 모르는 팩
        r = saveScriptEdit(db, docs, "rpg.ai.wander", "export const w = 1\n", saved, false);
        CHECK(r.status == ScriptSaveResult::Status::Saved);
        CHECK(fs::exists(root / "rpg" / "scripts" / "ai" / "wander.ts"));
        r = saveScriptEdit(db, docs, "nope.x", "x", saved, false);
        CHECK(r.status == ScriptSaveResult::Status::Failed);

        // 다시 읽으면 저장한 글 — 컴파일된다
        content::ContentLoadResult again =
            content::ContentLoader::load(root, std::vector<std::string>{"inv", "rpg"}, host.cat);
        CAPTURE(again.issues.size());
        REQUIRE(again.ok());
        const auto ws = scriptWorkspace(again.db, "rpg.regen", text);
        REQUIRE(ws);
        CHECK(fileOf(*ws, "scripts/ai/wander.ts") != nullptr);
        CHECK(analyzeScript(*ws).empty());
    }

    TEST_CASE("script editor widget (v0.9 M6-T8): 글 배치 — 자리 → 점 → 자리 왕복, 줄 밖은 없음") {
        Ui ui;
        ui.frame([] {
            const std::string t = "ab\n가c\n\nxyz";
            const f32 lineH = ImGui::GetFontSize();
            for (usize i = 0; i < t.size();) {
                if (t[i] == '\n') {
                    ++i;
                    continue;
                }
                const ImVec2 p = scriptTextPos(t, i);
                CAPTURE(i);
                CHECK(scriptTextOffsetAt(t, {p.x + 0.5f, p.y + 1.f}) == std::optional<usize>(i));
                usize next = i;
                (void)utf8::decodeNext(t, next);
                i = next;
            }
            CHECK(scriptTextPos(t, 0).x == 0.f);
            CHECK(scriptTextPos(t, 3).y == doctest::Approx(lineH));
            CHECK(scriptTextPos(t, 10).y == doctest::Approx(lineH * 3.f));
            CHECK(scriptTextPos(t, 2).x == doctest::Approx(scriptTextPos(t, 10).x * 2.f)); // "ab" 끝 = "x" 두 칸
            CHECK_FALSE(scriptTextOffsetAt(t, {500.f, 1.f}).has_value());                  // 줄 끝 너머
            CHECK_FALSE(scriptTextOffsetAt(t, {1.f, lineH * 2.f + 1.f}).has_value());      // 빈 줄
            CHECK_FALSE(scriptTextOffsetAt(t, {1.f, lineH * 9.f}).has_value());            // 줄 없음
            CHECK_FALSE(scriptTextOffsetAt(t, {-1.f, 1.f}).has_value());
        });
    }

    TEST_CASE(
        "script editor widget (v0.9 M6-T8): '.' 팝업 → 아래 → Enter 넣기, Esc 는 팝업만 닫는다, 진단은 300 ms 뒤") {
        Ui ui;
        ImGuiIO& io = ImGui::GetIO();
        std::string text = "import { defineSystem } from \"sandbox\"\nexport default defineSystem({ id: \"t.a\", "
                           "onTick(ctx) {\n  \n} })\n";
        const usize body = text.find("  \n") + 2;
        ScriptEditorState st;
        const ScriptWorkspaceFn ws = [](std::string_view t) -> std::optional<ScriptWorkspace> { return single(t); };
        const auto draw = [&] {
            ui.frame([&] { (void)drawScriptEditor("##ed", text, {800.f, 400.f}, false, st, ws); });
        };
        const auto press = [&](ImGuiKey key) {
            io.AddKeyEvent(key, true);
            draw();
            io.AddKeyEvent(key, false);
            draw();
        };
        const auto type = [&](std::string_view s) {
            for (const char c : s) {
                io.AddInputCharacter(static_cast<unsigned>(c));
                draw();
            }
        };

        // 이동 — 포커스를 주고 커서를 그 바이트로
        st.gotoOffset = body;
        for (int i = 0; i < 4; ++i) {
            draw();
        }
        REQUIRE(st.active);
        CHECK(st.cursor == body);
        CHECK_FALSE(st.gotoOffset.has_value());
        CHECK(st.assist.marks().empty()); // 처음 분석 — 문제 없음
        CHECK_FALSE(st.assist.marksStale());

        type("ctx.");
        REQUIRE(st.assist.completionOpen());
        const std::vector<script::CompletionItem> items(st.assist.completionItems().begin(),
                                                        st.assist.completionItems().end());
        REQUIRE(items.size() > 1);
        CHECK(hasLabel(items, "read"));
        CHECK(st.assist.marksStale()); // 친 뒤 300 ms 가 안 지났다

        press(ImGuiKey_DownArrow);
        CHECK(st.assist.selected() == 1);
        CHECK(st.cursor == body + 4); // 위아래는 팝업 것 — 커서는 그대로
        press(ImGuiKey_Enter);
        CHECK_FALSE(st.assist.completionOpen());
        CAPTURE(text);
        CHECK(text.substr(body).starts_with("ctx." + items[1].label + "\n} })")); // 줄바꿈 없이 넣었다
        CHECK(st.cursor == body + 4 + items[1].label.size());

        // Esc — 팝업만 닫고 고친 것은 그대로 (ImGui 기본은 활성화 뒤 고친 것을 모두 되돌린다)
        type(";ctx.");
        REQUIRE(st.assist.completionOpen());
        const std::string before = text;
        press(ImGuiKey_Escape);
        CHECK_FALSE(st.assist.completionOpen());
        CHECK(text == before);
        CHECK(st.active);

        // 입력이 멈추고 300 ms 뒤 진단 ("ctx." 뒤 이름 없음 — 구문 오류)
        CHECK(st.assist.marksStale());
        for (int i = 0; i < 24; ++i) {
            draw();
        }
        CHECK_FALSE(st.assist.marksStale());
        REQUIRE_FALSE(st.assist.marks().empty());
        CHECK(st.assist.marks()[0].error);

        // 바깥에서 글을 바꾸면 (다른 파일) 진단 · 팝업을 지우고 다시 분석
        text = "export const x = 1\n";
        draw();
        CHECK(st.assist.marks().empty());
        CHECK_FALSE(st.assist.marksStale());
        for (int i = 0; i < 3; ++i) {
            draw(); // 활성이던 입력 칸이 옛 글로 덮지 않는다 · 다시 활성 (커서는 글 안)
        }
        CHECK(text == "export const x = 1\n");
        CHECK(st.active);
        CHECK(st.cursor <= text.size());
    }

    TEST_CASE("script editor widget (v0.9 M6-T8): 5,000 줄 — 첫 분석 · 쉬는 프레임 · 타자 프레임 시간 (MESSAGE)") {
        Ui ui;
        ImGuiIO& io = ImGui::GetIO();
        std::string text = "import { defineSystem } from \"sandbox\"\n";
        for (int i = 0; i < 1000; ++i) { // 1,000 × 5 줄
            text += std::format("function f{0}(x: number): number {{\n  const y = x * {0}\n  return y + 1\n}}\n", i);
            text += std::format("const bad{} = nope{}\n", i, i); // 줄마다 오류 하나씩 (밑줄 · 줄 점)
        }
        text += "export default defineSystem({ id: \"t.a\", onTick(ctx) {\n  \n} })\n";
        const auto lines = static_cast<usize>(std::count(text.begin(), text.end(), '\n'));
        ScriptEditorState st;
        const ScriptWorkspaceFn ws = [](std::string_view t) -> std::optional<ScriptWorkspace> { return single(t); };
        const auto draw = [&] {
            ui.frame([&] { (void)drawScriptEditor("##big", text, {800.f, 400.f}, false, st, ws); });
        };
        using Clock = std::chrono::steady_clock;
        const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };

        auto t0 = Clock::now();
        draw(); // 첫 프레임 — 배치 + 분석
        const double first = ms(Clock::now() - t0);
        REQUIRE_FALSE(st.assist.marks().empty());
        st.gotoOffset = text.size() - 8;
        for (int i = 0; i < 3; ++i) {
            draw();
        }
        constexpr int kFrames = 60;
        t0 = Clock::now();
        for (int i = 0; i < kFrames; ++i) {
            draw();
        }
        const double idle = ms(Clock::now() - t0) / kFrames;
        t0 = Clock::now();
        for (int i = 0; i < 10; ++i) {
            io.AddInputCharacter('a');
            draw();
        }
        const double typing = ms(Clock::now() - t0) / 10;
        CHECK(st.assist.marksStale()); // 친 동안은 분석하지 않는다
        MESSAGE(std::format(
            "스크립트 편집기 {} 줄 · 밑줄 {}: 첫 프레임 (분석) {:.1f} ms, 쉬는 프레임 {:.2f} ms, 타자 프레임 "
            "{:.2f} ms",
            lines, st.assist.marks().size(), first, idle, typing));
    }
}
