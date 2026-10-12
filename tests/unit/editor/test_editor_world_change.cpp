// Phase 12D (docs/10-EDITOR.md 9장, ADR-0031): 서버 월드가 바뀌면 (불러오기 · 리플레이 재생 — host.worldGeneration)
// 에디터가 옛 월드의 netId 를 가리키는 되돌리기 기록 · 기다리던 편집 · EditPreview 를 모두 비운다.
#include <doctest/doctest.h>

#include "FakeEditorHost.hpp"

using namespace sbx;
using namespace sbx::editor;
using sbx::test::FakeHost;
using sbx::test::Hand;

TEST_SUITE("editor") {

    TEST_CASE("world change (12D): undo · redo · pending edits and the move preview are dropped") {
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}, {7, {4.5f, 0.5f}}};
        host.sel = {3};
        Editor ed;
        ed.setTool(Tool::Move);
        Hand h{ed, host};
        // 받아들여진 이동 하나 = 기록 한 칸
        h.press({0.5f, 0.5f});
        h.drag({2.5f, 0.5f});
        h.release({2.5f, 0.5f});
        host.accept(1);
        h.idle();
        REQUIRE(ed.undoDepth() == 1);
        // 결과를 기다리는 두 번째 이동 — 옮겨 그리는 중 (가짜 호스트의 개체는 제자리에 그려진다)
        h.press({0.5f, 0.5f});
        h.drag({1.5f, 1.5f});
        h.release({1.5f, 1.5f});
        REQUIRE(host.sent.size() == 2);
        CHECK_FALSE(ed.idle());
        CHECK_FALSE(host.previewNow.empty());

        host.generation = 1; // 서버가 세이브를 불러왔다
        h.idle();
        CHECK(ed.undoDepth() == 0);
        CHECK(ed.redoDepth() == 0);
        CHECK(ed.idle());
        CHECK(ed.preview().empty());
        CHECK(host.previewNow.empty());
        CHECK(ed.lastMessage().find("월드가 바뀌어") != std::string::npos);

        // 옛 월드에 보낸 이동의 결과가 늦게 와도 기록되지 않는다
        host.accept(2);
        h.idle();
        CHECK(ed.undoDepth() == 0);
        CHECK_FALSE(ed.canUndo());

        // 새 월드에서 편집하면 다시 기록된다 (generation 은 그대로)
        host.sel = {7};
        h.press({4.5f, 0.5f});
        h.drag({5.5f, 0.5f});
        h.release({5.5f, 0.5f});
        host.accept(3);
        h.idle();
        CHECK(ed.undoDepth() == 1);
    }
}
