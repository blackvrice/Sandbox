// 스크립트 디버거 서비스 · 창 (v0.9 ST-06-7 조각 3 — editor/services/ScriptDebugService · editor/ui/ScriptDebugPanel ·
// 편집기의 줄 번호 칸, ADR-0134): 기록 찾기, 열기 (팩을 콘텐츠 폴더에서 · 시작 세이브), 중단점이 세션 사이에 남는다,
// 멈춘 자리, 멈춘 동안 거두기 → 끝 (해시 같음), 끝난 뒤 바꾼 것은 처음부터에 넣는다, 열기 오류, 창 그리기 (상태마다),
// 편집기 줄 번호 칸을 누르면 그 줄, [편집기에서 보기] → 콘텐츠 창이 그 스크립트 · 줄.
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <imgui.h>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/replay/Replay.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "editor/services/ScriptDebugService.hpp"
#include "editor/ui/ContentPanel.hpp"
#include "editor/ui/ScriptDebugPanel.hpp"
#include "editor/ui/ScriptEditor.hpp"
#include "foundation/text/TextPosition.hpp"

using namespace sbx;
using namespace sbx::editor;
using script::DebugSessionState;
using script::vm::StepAction;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

constexpr std::string_view kRegen = R"(import { defineSystem, i32 } from "sandbox"
import { Mana } from "./mana"
function gain(current: number, ticks: i32): number {
  const bonus = ticks % 3 === 0 ? 2 : 1
  return current + bonus
}
export default defineSystem({
  id: "t.regen",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        m.ticks = m.ticks + 1
        m.current = gain(m.current, m.ticks)
        ctx.write(e, Mana, m)
      }
    }
  },
})
)";
constexpr std::string_view kFile = "scripts/regen.ts";
constexpr u64 kCalls = 8;

// <root>/content/t (팩), <root>/saves/rec1 (start/ + replay.sbxr), <root>/saves/plain (세이브만 — 기록 아님)
struct Project {
    fs::path root;
    content::ContentDatabase db;
    u64 finalHash = 0;

    explicit Project(std::string_view name)
        : root(fs::temp_directory_path() / "sbx-tests" / "script-debug-svc" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        const auto put = [&](const std::string& rel, std::string_view text) {
            fs::create_directories((root / rel).parent_path());
            std::ofstream(root / rel, std::ios::binary) << text;
        };
        put("content/t/pack.json", R"({"id": "t", "version": "1"})");
        put("content/t/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(1), ticks: field.i32(0) } })
)");
        put("content/t/scripts/regen.ts", kRegen);
        put("content/t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})");
        content::ContentLoadResult r =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(r.ok());
        db = std::move(r.db);

        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "core.grass";
        sim::SimulationWorld w(engine(), db, d);
        const fs::path rec = root / "saves" / "rec1";
        REQUIRE(persist::saveWorld(w, rec / "start").has_value());
        REQUIRE(persist::saveWorld(w, root / "saves" / "plain").has_value());
        replay::ReplayHeader h;
        h.startWorld = "start";
        h.startWorldHash = *w.worldHash();
        h.hashInterval = 2;
        replay::ReplayRecorder recorder(w, h);
        for (u64 c = 0; c < kCalls; ++c) {
            if (c == 0) {
                w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, 1},
                                          cmd::CreateEntity{Vec2{0.5f, 0.5f}, {}, "t.wisp"}});
            }
            w.tick();
            REQUIRE(recorder.afterTick().has_value());
        }
        REQUIRE(recorder.finish().has_value());
        REQUIRE(replay::writeReplay(recorder.replay(), rec / "replay.sbxr").has_value());
        finalHash = *w.worldHash();
    }
};

// 창 하나 · 기본 글꼴 — 렌더러 없이 프레임만
struct Ui {
    ImGuiContext* ctx = nullptr;
    Ui() {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1000.f, 800.f};
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
    TEST_CASE("script debug service (v0.9 ST-06-7): 기록 찾기 · 열기 · 중단점이 남는다 · 멈춘 자리 · 처음부터") {
        Project p("service");
        const auto recs = services::listRecordings(p.root / "saves");
        REQUIRE(recs.size() == 1); // plain 은 세이브만 — 기록이 아니다
        CHECK(recs[0].name == "rec1");
        CHECK(services::listRecordings(p.root / "missing").empty());
        // 한글 이름의 기록 — 이름은 UTF-8 (path::string() 은 코드 페이지라 던진다)
        fs::copy(p.root / "saves" / "rec1", p.root / "saves" / fs::path(u8"기록"), fs::copy_options::recursive);
        const auto withKorean = services::listRecordings(p.root / "saves");
        REQUIRE(withKorean.size() == 2);
        CHECK(withKorean[0].name == "rec1");
        CHECK(withKorean[1].name == "\xEA\xB8\xB0\xEB\xA1\x9D"); // "기록"

        services::ScriptDebugService svc;
        CHECK_FALSE(svc.isOpen());
        // 열기 전에 놓은 중단점은 남아 있다가 열면 들어간다
        CHECK(svc.toggleBreakpoint("t", kFile, 14));
        CHECK(svc.breakpointLines("t", kFile) == std::vector<u32>{14});
        CHECK(svc.breakpoints()[0].sites == 0);
        svc.setWatches({"m.ticks"});
        REQUIRE(svc.open(recs[0].dir, p.root / "content").has_value());
        CHECK(svc.isOpen());
        CHECK(svc.calls() == kCalls);
        CHECK(svc.breakpoints()[0].sites == 1);
        REQUIRE(svc.session() != nullptr);
        CHECK(svc.session()->state() == DebugSessionState::Ready);
        CHECK_FALSE(svc.pausedAt().has_value());

        svc.start();
        REQUIRE(svc.session()->wait(10s) == DebugSessionState::Paused);
        const auto at = svc.pausedAt();
        REQUIRE(at.has_value());
        CHECK(at->pack == "t");
        CHECK(at->file == kFile);
        CHECK(at->line == 14);
        const auto snap = svc.session()->snapshot();
        REQUIRE(snap.has_value());
        REQUIRE(snap->watches.size() == 1);
        CHECK(snap->watches[0].var.value == "1");
        // 멈춘 동안 거두기는 바로 → 끝까지, 해시는 기록과 같다
        CHECK_FALSE(svc.toggleBreakpoint("t", kFile, 14));
        CHECK(svc.breakpoints().empty());
        CHECK_FALSE(svc.pendingChanges());
        svc.session()->resume(StepAction::Continue);
        REQUIRE(svc.session()->wait(30s) == DebugSessionState::Finished);
        CHECK_FALSE(svc.session()->result().mismatch.has_value());
        CHECK(svc.session()->finalHash() == p.finalHash);

        // 끝난 뒤 바꾼 것은 처음부터에 넣는다
        svc.setBreakpoint("t", kFile, 4, "bonus 앞 ticks={ticks}");
        CHECK(svc.pendingChanges());
        CHECK(svc.breakpoints()[0].sites == 0);
        REQUIRE(svc.restart().has_value());
        CHECK_FALSE(svc.pendingChanges());
        CHECK(svc.breakpoints()[0].sites == 1);
        CHECK(svc.session()->state() == DebugSessionState::Ready);
        svc.start();
        REQUIRE(svc.session()->wait(30s) == DebugSessionState::Finished); // logpoint 만 — 멈추지 않는다
        const auto logs = svc.session()->logs();
        REQUIRE(logs.size() == kCalls);
        CHECK(logs[0] == "scripts/regen.ts:4: bonus 앞 ticks=1");

        // 틱에서 멈추기
        REQUIRE(svc.restart().has_value());
        svc.start(3);
        REQUIRE(svc.session()->wait(10s) == DebugSessionState::Paused);
        CHECK(svc.session()->snapshot()->tick == 3);
        svc.close();
        CHECK_FALSE(svc.isOpen());
        CHECK(svc.breakpoints().size() == 1); // 닫아도 중단점은 남는다

        // 열기 오류 — 기록 아님 · 팩 없는 콘텐츠 폴더
        CHECK_FALSE(svc.open(p.root / "saves" / "plain", p.root / "content").has_value());
        CHECK_FALSE(svc.open(recs[0].dir, p.root / "empty-content").has_value());
        CHECK_FALSE(svc.isOpen());
    }

    TEST_CASE("script debugger window (v0.9 ST-06-7): 상태마다 그리기 · 편집기 줄 번호 칸 · [편집기에서 보기]") {
        Project p("window");
        Ui ui;
        ImGuiIO& io = ImGui::GetIO();
        services::ScriptDebugService svc;
        ScriptDebugPanelState panel;
        bool open = true;
        const auto frame = [&] {
            ImGui::NewFrame();
            const ScriptDebugPanelResult r =
                drawScriptDebugPanel(svc, panel, p.root / "saves", p.root / "content", open);
            ImGui::Render();
            return r;
        };
        (void)frame(); // 닫힌 세션 — 기록 목록
        REQUIRE(panel.recordings.size() == 1);
        REQUIRE(svc.open(panel.recordings[0].dir, p.root / "content").has_value());
        svc.setBreakpoint("t", kFile, 14);
        svc.setWatches({"m.ticks", "e"});
        (void)frame(); // Ready
        svc.start();
        REQUIRE(svc.session()->wait(10s) == DebugSessionState::Paused);
        for (int i = 0; i < 3; ++i) {
            (void)frame(); // Paused — 스택 · 지역 · 감시
        }
        CHECK(panel.frameStop == 1);
        svc.session()->resume(StepAction::Into);
        REQUIRE(svc.session()->wait(10s) == DebugSessionState::Paused);
        (void)frame(); // 새 멈춤 — 창이 안쪽 프레임으로 돌아간다
        CHECK(panel.frameStop == 2);
        CHECK(panel.frame == 0);
        panel.frame = 1; // 바깥 프레임 고르기 — 감시를 디버그 스레드에 묻는다
        (void)frame();
        (void)frame();
        REQUIRE(panel.frameWatches.size() == 2);
        CHECK(panel.frameWatches[0].var.value == "1");
        svc.session()->abort();
        REQUIRE(svc.session()->wait(10s) == DebugSessionState::Finished);
        (void)frame(); // 끝 — 결과 줄

        // 편집기의 줄 번호 칸: 중단점 줄 · 멈춘 줄을 그리고, 누른 줄이 나온다
        std::string text(kRegen);
        ScriptEditorState st;
        st.breakpointGutter = true;
        st.breakpointLines = {14};
        st.execLine = 4;
        ImVec2 base{};
        f32 padY = 0.f;
        f32 lineH = 0.f;
        const auto drawEditor = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0.f, 0.f});
            ImGui::SetNextWindowSize({700.f, 600.f});
            ImGui::Begin("editor-test");
            base = ImGui::GetCursorScreenPos();
            padY = ImGui::GetStyle().FramePadding.y;
            lineH = ImGui::GetFontSize();
            (void)drawScriptEditor("##ed", text, {600.f, 500.f}, false, st, {});
            ImGui::End();
            ImGui::Render();
        };
        drawEditor();
        CHECK(st.shownExecLine == std::optional<u32>(4));
        CHECK_FALSE(st.toggledLine.has_value());
        io.AddMousePosEvent(base.x + 4.f, base.y + padY + lineH * 1.5f); // 2 줄의 줄 번호 칸
        drawEditor();
        io.AddMouseButtonEvent(0, true);
        drawEditor();
        CHECK(st.toggledLine == std::optional<u32>(2));
        io.AddMouseButtonEvent(0, false);
        drawEditor();
        CHECK_FALSE(st.toggledLine.has_value());
        // 칸 밖 (글) 을 누르면 아니다
        io.AddMousePosEvent(base.x + 200.f, base.y + padY + lineH * 1.5f);
        drawEditor();
        io.AddMouseButtonEvent(0, true);
        drawEditor();
        CHECK_FALSE(st.toggledLine.has_value());
        io.AddMouseButtonEvent(0, false);
        drawEditor();
        // 디버거가 없는 편집기는 칸을 눌러도 아니다
        st.breakpointGutter = false;
        io.AddMousePosEvent(base.x + 4.f, base.y + padY + lineH * 1.5f);
        drawEditor();
        io.AddMouseButtonEvent(0, true);
        drawEditor();
        CHECK_FALSE(st.toggledLine.has_value());
        io.AddMouseButtonEvent(0, false);
        drawEditor();

        // [편집기에서 보기] — 콘텐츠 창이 그 스크립트 · 줄로
        content::ContentLoadResult loaded =
            content::ContentLoader::load(p.root / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(loaded.ok());
        ContentPanelState cp;
        CHECK(showScriptLine(cp, loaded.db, {"t", std::string(kFile), 14}));
        CHECK(cp.kind == content::ContentKind::Script);
        CHECK(cp.selected == "t.regen");
        CHECK(cp.text == kRegen);
        REQUIRE(cp.script.gotoOffset.has_value());
        CHECK(*cp.script.gotoOffset == text::offsetAt(kRegen, {14, 1}));
        // 이미 고친 채 열려 있으면 글은 그대로, 줄만
        cp.text += "// 고치는 중\n";
        CHECK(showScriptLine(cp, loaded.db, {"t", std::string(kFile), 5}));
        CHECK(cp.text.ends_with("// 고치는 중\n"));
        CHECK(*cp.script.gotoOffset == text::offsetAt(cp.text, {5, 1}));
        CHECK_FALSE(showScriptLine(cp, loaded.db, {"t", "scripts/없음.ts", 1}));
        CHECK_FALSE(showScriptLine(cp, loaded.db, {"t", "Scripts/Bad.ts", 1}));
    }
}
