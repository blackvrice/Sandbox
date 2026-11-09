// 하나의 되돌리기 줄에 붙은 에디터 (v0.8 ST-06-1 (3) — Editor 가 World 참여자, ADR-0110): 월드 편집 · 파일 문서 편집이
// 일어난 차례로 Ctrl+Z · Ctrl+Y, 월드 되돌리기는 결과를 기다린 뒤 줄에 (기다리는 동안 줄 멈춤), 일부 거절 = 칸 버림,
// 기다리는 동안의 문서 편집은 월드 되돌리기의 다시 하기를 지운다, 기다리는 동안 끝난 새 월드 편집은 그 뒤에 기록,
// 월드가 바뀌면 월드 칸만, 붙일 때 옛 기록 옮겨 적기 · 뗄 때 지우기, 상한으로 버린 칸
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <variant>

#include "FakeEditorHost.hpp"
#include "editor/services/DocumentService.hpp"

using namespace sbx;
using namespace sbx::editor;
using sbx::test::FakeHost;
using sbx::test::Hand;
namespace fs = std::filesystem;
namespace es = sbx::editor::services;

namespace {

struct TempFile {
    fs::path path;
    explicit TempFile(std::string_view text) {
        std::random_device rd;
        path = fs::temp_directory_path() / std::format("sbx_undo_line_{:08x}.txt", rd());
        std::ofstream(path, std::ios::binary) << text;
    }
    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

void key(Hand& h, bool undo) {
    auto in = h.frame({100, 100});
    (undo ? in.undo : in.redo) = true;
    h.ed.update(in, h.host);
}

// 개체 3 을 (dx, 0) 옮기는 편집 하나 — 받아들여지면 기록 한 칸
void move(Hand& h, FakeHost& host, f32 dx) {
    h.ed.setTool(Tool::Move);
    host.sel = {3};
    const Vec2 at = host.entities.front().position;
    h.press(at);
    h.drag(at + Vec2{dx, 0});
    h.release(at + Vec2{dx, 0});
    host.accept(static_cast<u32>(host.sent.size()));
    h.idle();
    host.entities.front().position += Vec2{dx, 0};
}

const cmd::MoveEntity& lastMove(const FakeHost& host) {
    const auto* m = std::get_if<cmd::MoveEntity>(&host.sent.back());
    REQUIRE(m != nullptr);
    return *m;
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("undo line: 월드 · 문서 편집이 일어난 차례로 — Ctrl+Z 는 문서를 바로, 월드는 결과를 기다린 뒤, Ctrl+Y 도 "
              "차례대로") {
        TempFile file("abc");
        es::UndoService line;
        es::DocumentService docs(&line);
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}};
        Editor ed;
        ed.attachUndo(&line);
        Hand h{ed, host};

        move(h, host, 2);
        const es::DocId doc = *docs.open(file.path);
        REQUIRE(docs.apply(doc, {3, 0, "d"}).has_value());
        REQUIRE(line.undoSteps().size() == 2);
        CHECK(ed.undoLabel() == std::format("{} 고침", file.path.filename().generic_string()));
        CHECK(ed.canUndo());

        // Ctrl+Z ① — 문서 (명령 없음)
        const usize sent = host.sent.size();
        key(h, true);
        CHECK(docs.text(doc) == "abc");
        CHECK(host.sent.size() == sent);
        CHECK(ed.undoLabel() == "MoveEntity");
        // Ctrl+Z ② — 월드: 역명령을 보내고 결과를 기다린다 (줄 멈춤)
        key(h, true);
        REQUIRE(host.sent.size() == sent + 1);
        CHECK(lastMove(host).value == Vec2{-2, 0});
        CHECK(line.pending());
        CHECK_FALSE(ed.canUndo());
        CHECK_FALSE(line.redo()); // 기다리는 중
        host.accept(static_cast<u32>(host.sent.size()));
        h.idle();
        CHECK_FALSE(line.pending());
        REQUIRE(line.canRedo());
        CHECK(line.nextRedo()->kind == es::UndoKind::World);
        CHECK(ed.redoLabel() == "MoveEntity");
        CHECK(ed.undoDepth() == 0);
        CHECK(ed.redoDepth() == 1);

        // Ctrl+Y — 월드 (기다림) → 문서
        key(h, false);
        CHECK(lastMove(host).value == Vec2{2, 0});
        host.accept(static_cast<u32>(host.sent.size()));
        h.idle();
        CHECK(ed.undoDepth() == 1);
        key(h, false);
        CHECK(docs.text(doc) == "abcd");
        CHECK_FALSE(line.canRedo());
        CHECK(line.undoSteps().size() == 2);
        CHECK(line.undoSteps()[0].kind == es::UndoKind::World);
        CHECK(line.undoSteps()[1].kind == es::UndoKind::Document);
    }

    TEST_CASE("undo line: 일부 거절된 되돌리기는 칸을 버린다, 기다리는 동안의 문서 편집은 월드 되돌리기의 다시 하기를 "
              "지운다") {
        TempFile file("x");
        es::UndoService line;
        es::DocumentService docs(&line);
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}};
        Editor ed;
        ed.attachUndo(&line);
        Hand h{ed, host};

        // 거절된 되돌리기 — 줄 · 에디터 모두에서 빠진다
        move(h, host, 1);
        key(h, true);
        host.reject(static_cast<u32>(host.sent.size()));
        h.idle();
        CHECK_FALSE(line.pending());
        CHECK_FALSE(line.canUndo());
        CHECK_FALSE(line.canRedo());
        CHECK(ed.undoDepth() == 0);
        CHECK(ed.redoDepth() == 0);
        CHECK(ed.lastMessage().find("거절") != std::string::npos);

        // 되돌리기를 기다리는 동안 문서를 고쳤다 — 새 기록이므로 월드 되돌리기는 다시 하기로 가지 않는다
        move(h, host, 1);
        key(h, true);
        REQUIRE(line.pending());
        const es::DocId doc = *docs.open(file.path);
        REQUIRE(docs.apply(doc, {1, 0, "y"}).has_value());
        host.accept(static_cast<u32>(host.sent.size()));
        h.idle();
        CHECK_FALSE(line.pending());
        CHECK_FALSE(line.canRedo());
        CHECK(ed.redoDepth() == 0);
        REQUIRE(line.undoSteps().size() == 1);
        CHECK(line.nextUndo()->kind == es::UndoKind::Document);
    }

    TEST_CASE("undo line: 다시 하기를 기다리는 동안 끝난 새 월드 편집은 그 뒤에, 월드가 바뀌면 월드 칸만, 붙일 때 옮겨 "
              "적기 · 뗄 때 지우기") {
        TempFile file("q");
        es::UndoService line;
        es::DocumentService docs(&line);
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}, {7, {6.5f, 0.5f}}};
        Editor ed;
        Hand h{ed, host};

        // 붙이기 전의 기록은 붙일 때 줄에 옮겨 적는다
        move(h, host, 1);
        ed.attachUndo(&line);
        REQUIRE(line.undoSteps().size() == 1);
        CHECK(line.nextUndo()->label == "MoveEntity");

        // 되돌린 뒤 다시 하기를 기다리는 동안 새 편집 (지우기) 이 끝난다 — 다시 한 칸 다음에 기록
        key(h, true);
        host.accept(static_cast<u32>(host.sent.size()));
        h.idle();
        key(h, false); // 다시 하기 시작
        REQUIRE(line.pending());
        ed.setTool(Tool::Erase);
        h.click({6.5f, 0.5f});
        const u32 eraseSeq = static_cast<u32>(host.sent.size());
        const u32 redoSeq = eraseSeq - 1;
        host.accept(eraseSeq);
        h.idle();
        CHECK(line.undoSteps().empty()); // 아직 — 다시 하기가 끝나지 않았다
        host.accept(redoSeq);
        h.idle();
        REQUIRE(line.undoSteps().size() == 2);
        CHECK(line.undoSteps()[0].label == "MoveEntity");
        CHECK(line.undoSteps()[1].label == "DeleteEntity");
        CHECK(ed.undoDepth() == 2);

        // 월드가 바뀌면 (불러오기) 월드 칸만 지운다 — 문서 칸은 남는다
        const es::DocId doc = *docs.open(file.path);
        REQUIRE(docs.apply(doc, {0, 1, "r"}).has_value());
        host.generation += 1;
        h.idle();
        REQUIRE(line.undoSteps().size() == 1);
        CHECK(line.nextUndo()->kind == es::UndoKind::Document);
        key(h, true);
        CHECK(docs.text(doc) == "q");

        // 뗄 때 월드 칸을 지운다
        move(h, host, 1);
        CHECK(line.undoSteps().size() == 1);
        ed.attachUndo(nullptr);
        CHECK_FALSE(line.canUndo());
        CHECK_FALSE(line.canRedo()); // 문서의 다시 하기는 새 월드 편집이 이미 지웠다
        key(h, true);                // 떼면 에디터 자기 기록
        CHECK(ed.redoDepth() == 0);
        CHECK(lastMove(host).value == Vec2{-1, 0});
    }

    TEST_CASE(
        "undo line: 에디터의 상한 (100) 으로 버린 가장 오래된 월드 칸은 줄에서도 버린다 — 차례가 어긋나지 않는다") {
        es::UndoService line;
        FakeHost host;
        host.entities = {{3, {0.5f, 0.5f}}};
        Editor ed;
        ed.attachUndo(&line);
        Hand h{ed, host};
        for (usize i = 0; i < Editor::kMaxUndo + 3; ++i) {
            move(h, host, 1);
        }
        CHECK(ed.undoDepth() == Editor::kMaxUndo);
        CHECK(line.undoSteps().size() == Editor::kMaxUndo);
        for (usize i = 0; i < Editor::kMaxUndo; ++i) {
            key(h, true);
            host.accept(static_cast<u32>(host.sent.size()));
            h.idle();
        }
        CHECK_FALSE(line.canUndo());
        CHECK(ed.undoDepth() == 0);
        CHECK(ed.redoDepth() == Editor::kMaxUndo);
    }
}
