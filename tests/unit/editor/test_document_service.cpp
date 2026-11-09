// 파일 문서 · 하나의 되돌리기 줄 (v0.8 ST-06-1 (2) — editor/services/DocumentService · UndoService, ADR-0109): 열기
// (같은 경로 = 같은 문서) · 편집 (범위 · UTF-8 가운데 거절 · 빈 편집) · dirty, 저장 (원자적 · .json 검사 · 바깥 변경 ·
// 없어진 파일 · 강제), 다시 읽기 · 닫기, 되돌리기 줄 (두 문서의 차례 · 다시 하기 · 새 편집이 다시 하기를 비움 · 닫으면
// 그 문서 칸만 지움 · 참여자가 못 하면 줄 그대로 · 종류로 지우기)
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "editor/services/DocumentService.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        std::random_device rd;
        path = fs::temp_directory_path() / std::format("sbx_docs_{}_{:08x}", name, rd());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void write(const fs::path& p, std::string_view text) {
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p) {
    return *io::readFile(p);
}

// 되돌리기 줄의 World 칸을 흉내 내는 참여자 — 기록 · 되돌리기 · 다시 하기를 적고, 바쁘면 거절한다
class FakeWorld final : public IUndoParticipant {
public:
    std::vector<std::string> done;
    std::vector<std::string> undone;
    bool busy = false;
    bool undoLast() override {
        if (busy || done.empty()) {
            return false;
        }
        undone.push_back(done.back());
        done.pop_back();
        return true;
    }
    bool redoLast() override {
        if (busy || undone.empty()) {
            return false;
        }
        done.push_back(undone.back());
        undone.pop_back();
        return true;
    }
    void dropRedo() override { undone.clear(); }
    void edit(UndoService& u, std::string name) {
        done.push_back(name);
        u.record(UndoKind::World, std::move(name));
    }
};

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("document service: 열기 · 편집 (범위 · UTF-8 가운데 · 빈 편집) · dirty · setText") {
        TempDir t("edit");
        write(t.path / "a.txt", "가나다");
        DocumentService docs;
        const Expected<DocId> a = docs.open(t.path / "a.txt");
        REQUIRE(a.has_value());
        CHECK(*docs.open(t.path / "." / "a.txt") == *a); // 같은 경로 = 같은 문서
        CHECK_FALSE(docs.open(t.path / "missing.txt").has_value());
        CHECK(docs.text(*a) == "가나다");
        CHECK_FALSE(docs.dirty(*a));

        REQUIRE(docs.apply(*a, {3, 3, "x"}).has_value()); // "나" → "x"
        CHECK(docs.text(*a) == "가x다");
        CHECK(docs.dirty(*a));
        CHECK(docs.apply(*a, {1, 0, "y"}).error().code == ErrorCode::InvalidArgument); // "가" 가운데
        CHECK(docs.apply(*a, {0, 2, ""}).error().code == ErrorCode::InvalidArgument);  // 끝이 글자 가운데
        CHECK(docs.apply(*a, {8, 0, "z"}).error().code == ErrorCode::OutOfRange);
        CHECK(docs.apply(*a, {7, 2, ""}).error().code == ErrorCode::OutOfRange);
        CHECK(docs.apply(99, {0, 0, "z"}).error().code == ErrorCode::NotFound);
        REQUIRE(docs.apply(*a, {0, 0, ""}).has_value()); // 바뀐 것 없음
        REQUIRE(docs.apply(*a, {7, 0, "!"}).has_value());
        CHECK(docs.text(*a) == "가x다!");
        REQUIRE(docs.setText(*a, "가나다").has_value());
        CHECK_FALSE(docs.dirty(*a)); // 저장한 글과 같다
        CHECK(docs.openDocuments() == std::vector<DocId>{*a});
        CHECK(docs.path(*a).filename() == "a.txt");
    }

    TEST_CASE("document service: 저장 — 원자적 쓰기 · .json 검사 · 바깥 변경 · 없어진 파일 · 강제, 다시 읽기 · 닫기") {
        TempDir t("save");
        const fs::path json = t.path / "units.json";
        write(json, R"({"a": 1})");
        DocumentService docs;
        const DocId id = *docs.open(json);

        REQUIRE(docs.setText(id, R"({"a": 2})").has_value());
        REQUIRE(docs.save(id).has_value());
        CHECK(read(json) == R"({"a": 2})");
        CHECK_FALSE(docs.dirty(id));
        CHECK(docs.diskState(id) == DiskState::Same);

        // 검사 — JSON 이 아니면 쓰지 않는다
        REQUIRE(docs.setText(id, R"({"a": )").has_value());
        const Expected<void> bad = docs.save(id);
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == ErrorCode::ValidationFailed);
        CHECK(read(json) == R"({"a": 2})");
        // 확장자마다 검사를 바꿀 수 있다
        docs.setValidator(".json", [](const fs::path&, std::string_view) { return std::optional<std::string>(); });
        REQUIRE(docs.setText(id, R"({"a": 3})").has_value());

        // 바깥 변경 — 덮어쓰지 않는다, 강제면 쓴다
        write(json, R"({"a": 99})");
        CHECK(docs.diskState(id) == DiskState::Changed);
        const Expected<void> changed = docs.save(id);
        REQUIRE_FALSE(changed.has_value());
        CHECK(changed.error().code == ErrorCode::AlreadyExists);
        CHECK(read(json) == R"({"a": 99})");
        REQUIRE(docs.save(id, true).has_value());
        CHECK(read(json) == R"({"a": 3})");
        CHECK(docs.diskState(id) == DiskState::Same);

        // 없어진 파일 — 강제면 다시 만든다
        fs::remove(json);
        CHECK(docs.diskState(id) == DiskState::Missing);
        CHECK(docs.save(id).error().code == ErrorCode::NotFound);
        REQUIRE(docs.save(id, true).has_value());
        CHECK(fs::exists(json));

        // 다시 읽기 — 디스크의 것 (고친 것을 버린다)
        REQUIRE(docs.setText(id, R"({"a": 4})").has_value());
        write(json, R"({"b": 1})");
        REQUIRE(docs.reload(id).has_value());
        CHECK(docs.text(id) == R"({"b": 1})");
        CHECK_FALSE(docs.dirty(id));
        CHECK(docs.diskState(id) == DiskState::Same);

        // 닫기 — 고친 채로는 강제만
        REQUIRE(docs.setText(id, R"({"b": 2})").has_value());
        CHECK(docs.close(id).error().code == ErrorCode::InvalidArgument);
        REQUIRE(docs.close(id, true).has_value());
        CHECK_FALSE(docs.isOpen(id));
        CHECK(read(json) == R"({"b": 1})");
        CHECK(docs.save(id).error().code == ErrorCode::NotFound);
    }

    TEST_CASE("undo service: 두 문서 · 월드 칸이 일어난 차례로 — 되돌리기 · 다시 하기 · 새 편집 · 닫기 · 바쁜 참여자 · "
              "종류 지우기") {
        TempDir t("undo");
        write(t.path / "a.txt", "a");
        write(t.path / "b.txt", "b");
        UndoService undo;
        FakeWorld world;
        undo.attach(UndoKind::World, &world);
        DocumentService docs(&undo);
        const DocId a = *docs.open(t.path / "a.txt");
        const DocId b = *docs.open(t.path / "b.txt");

        REQUIRE(docs.apply(a, {1, 0, "1"}).has_value()); // a1
        world.edit(undo, "배치");
        REQUIRE(docs.apply(b, {1, 0, "2"}, "b 에 2").has_value()); // b2
        REQUIRE(docs.apply(a, {2, 0, "3"}).has_value());           // a13
        REQUIRE(undo.undoSteps().size() == 4);
        CHECK(undo.nextUndo()->label == "a.txt 고침");
        CHECK(undo.nextUndo()->tag == a);

        // 일어난 거꾸로: a 의 "3" → b 의 "2" → 월드 → a 의 "1"
        REQUIRE(undo.undo());
        CHECK(docs.text(a) == "a1");
        REQUIRE(undo.undo());
        CHECK(docs.text(b) == "b");
        CHECK(undo.nextUndo()->kind == UndoKind::World);
        world.busy = true; // 결과를 기다리는 중 — 줄 그대로
        CHECK_FALSE(undo.undo());
        CHECK(undo.undoSteps().size() == 2);
        world.busy = false;
        REQUIRE(undo.undo());
        CHECK(world.done.empty());
        REQUIRE(undo.undo());
        CHECK(docs.text(a) == "a");
        CHECK_FALSE(undo.undo());
        CHECK_FALSE(docs.dirty(a));

        // 다시 하기 — 차례대로
        REQUIRE(undo.redo());
        CHECK(docs.text(a) == "a1");
        REQUIRE(undo.redo());
        CHECK(world.done == std::vector<std::string>{"배치"});
        CHECK(undo.nextRedo()->label == "b 에 2");
        // 새 편집은 다시 하기를 모두 비운다 (참여자 더미도)
        REQUIRE(docs.apply(a, {2, 0, "x"}).has_value());
        CHECK_FALSE(undo.canRedo());
        CHECK_FALSE(undo.redo());
        CHECK(world.undone.empty());
        CHECK(docs.text(a) == "a1x");

        // 닫기 — 그 문서의 칸만 지운다 (a 의 두 칸), 월드 칸은 남는다
        REQUIRE(docs.save(a).has_value());
        REQUIRE(docs.close(a).has_value());
        REQUIRE(undo.undoSteps().size() == 1);
        CHECK(undo.nextUndo()->kind == UndoKind::World);
        REQUIRE(docs.apply(b, {1, 0, "y"}).has_value());
        REQUIRE(undo.undo());
        CHECK(docs.text(b) == "b");
        // 월드가 바뀌면 (불러오기) 월드 칸만
        undo.clear(UndoKind::World);
        CHECK_FALSE(undo.canUndo());
        CHECK(undo.canRedo()); // b 의 다시 하기는 남는다
        REQUIRE(undo.redo());
        CHECK(docs.text(b) == "by");
        // 다시 읽기도 그 문서의 칸을 지운다
        REQUIRE(docs.reload(b).has_value());
        CHECK_FALSE(undo.canUndo());
        CHECK(docs.text(b) == "b");
    }

    TEST_CASE("undo service: 상한 — 1000 칸을 넘으면 가장 오래된 칸을 버리고 센다") {
        UndoService undo;
        FakeWorld world;
        undo.attach(UndoKind::World, &world);
        for (usize i = 0; i < UndoService::kMaxSteps + 5; ++i) {
            world.edit(undo, std::to_string(i));
        }
        CHECK(undo.undoSteps().size() == UndoService::kMaxSteps);
        CHECK(undo.dropped() == 5);
        CHECK(undo.undoSteps().front().label == "5");
        CHECK_FALSE(undo.canRedo());
    }
}
