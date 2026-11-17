// Game UI Designer 모델 (v0.10 ST-06-4 조각 1 — editor/services/UiDesignService, ADR-0145): UI 문서 (samples/tictactoe
// 의 hud.json 복사본) 를 DocumentService 로 열어 — 목록 (앞 순서 · 깊이 · 표시 이름), 편집하지 않은 저장은 바이트
// 그대로, 속성 고치기 · 되돌리기 · 다시, 요소 라이브러리의 기본값이 모두 검사를 통과, 자식을 못 받는 요소 · 모르는 요소
// 거절, 옮기기 · 지우기 (root 거절), 자리 (앵커 · 오프셋) → 미리보기 사각형, 오류 있는 문서는 저장 거절 (파일 그대로) ·
// 배치 없음, 깨진 JSON.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include "editor/services/DocumentService.hpp"
#include "editor/services/UiDesignService.hpp"
#include "editor/services/UndoService.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        path = fs::temp_directory_path() / std::format("sbx_uidesign_{}_{:08x}", name, std::random_device{}());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

fs::path copyHud(const TempDir& t) {
    const fs::path to = t.path / "presentation" / "ui" / "hud.json";
    fs::create_directories(to.parent_path());
    fs::copy_file(fs::path(SBX_SAMPLES_DIR) / "tictactoe" / "content" / "ttt" / "presentation" / "ui" / "hud.json", to);
    return to;
}

bool hasError(const std::vector<ui::UiIssue>& issues) {
    return std::ranges::any_of(issues, [](const ui::UiIssue& i) { return i.severity == ui::UiIssue::Severity::Error; });
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("ui designer (v0.10 ST-06-4): 목록 · 편집하지 않은 저장은 바이트 그대로 · 고치기 · 되돌리기 · 다시") {
        TempDir t("edit");
        const fs::path file = copyHud(t);
        const std::string original = *io::readFile(file);
        UndoService undo;
        DocumentService docs(&undo);
        UiDesignService design(docs);
        REQUIRE(design.open(file).has_value());
        REQUIRE(design.isOpen());
        const auto& nodes = design.nodes();
        REQUIRE(nodes.size() == 14); // board · title · row 셋 · 버튼 아홉
        CHECK(nodes[0].path.empty());
        CHECK(nodes[0].kind == "panel");
        CHECK(nodes[0].id == "board");
        CHECK(nodes[1].label == "text #title '틱택토 — 칸을 눌러 X 를 둔다'");
        CHECK(nodes[2].path == UiNodePath{1});
        CHECK(nodes[3].path == UiNodePath{1, 0});
        CHECK(nodes[3].depth == 2);
        CHECK(nodes[3].label == "button #c0 '{0}'");
        CHECK(design.issues().empty());
        CHECK(design.parseError().empty());

        // 편집하지 않은 저장 — 손으로 맞춘 배치 그대로
        REQUIRE(design.save().has_value());
        CHECK(*io::readFile(file) == original);

        // 속성 하나 = 되돌리기 한 칸
        REQUIRE(design.setProperty({1, 0}, "text", "X?").has_value());
        CHECK((*design.node({1, 0}))["text"] == "X?");
        CHECK(docs.dirty(*design.document()));
        CHECK(design.nodes()[3].label == "button #c0 'X?'");
        REQUIRE(undo.undo());
        CHECK((*design.node({1, 0}))["text"] == "{0}");
        REQUIRE(undo.redo());
        CHECK((*design.node({1, 0}))["text"] == "X?");
        REQUIRE(design.removeProperty({1, 0}, "size").has_value());
        CHECK_FALSE(design.node({1, 0})->contains("size"));
        CHECK_FALSE(design.removeProperty({1, 0}, "kind").has_value());
        CHECK_FALSE(design.setProperty({1, 0}, "children", Json::array()).has_value());
        CHECK_FALSE(design.setProperty({9}, "text", "x").has_value()); // 없는 노드

        // 저장한 것을 다시 읽으면 같은 값 (키 차례 그대로 — 들여쓰기 2)
        REQUIRE(design.save().has_value());
        const Json saved = Json::parse(*io::readFile(file));
        CHECK(saved["root"]["children"][1]["children"][0]["text"] == "X?");
        CHECK(saved.begin().key() == "id");
        CHECK_FALSE(docs.dirty(*design.document()));
    }

    TEST_CASE("ui designer (v0.10 ST-06-4): 요소 라이브러리 기본값은 검사를 통과 · 자식 규칙 · 옮기기 · 지우기 · id 가 "
              "겹치지 않는다") {
        TempDir t("library");
        const fs::path file = copyHud(t);
        DocumentService docs;
        UiDesignService design(docs);
        REQUIRE(design.open(file).has_value());
        const auto panel = design.addChild({}, "panel");
        REQUIRE(panel.has_value());
        CHECK(*panel == UiNodePath{4});
        CHECK((*design.node(*panel))["id"] == "panel1");
        for (const std::string_view kind : kUiLibrary) {
            CAPTURE(kind);
            const auto made = design.addChild(*panel, kind);
            REQUIRE_MESSAGE(made.has_value(), made.error().describe());
            CHECK((*design.node(*made))["kind"] == std::string(kind));
            for (const ui::UiIssue& i : design.issues()) {
                CAPTURE(i.rule);
                CAPTURE(i.pointer);
                CAPTURE(i.message);
                CHECK(i.severity != ui::UiIssue::Severity::Error);
            }
        }
        CHECK((*design.node({4, 0}))["id"] == "panel2"); // panel1 이 있으니 다음
        // 자식을 못 받는 요소 · 모르는 요소
        CHECK_FALSE(design.addChild({1, 0}, "text").has_value()); // button
        CHECK_FALSE(design.addChild(*panel, "window").has_value());
        // 옮기기 — 끝을 넘으면 끝, root 는 옮기지 않는다
        const auto moved = design.move({1}, 10);
        REQUIRE(moved.has_value());
        CHECK(*moved == UiNodePath{4});
        CHECK((*design.node({4}))["id"] == "row0");
        CHECK((*design.node({3}))["id"] == "panel1");
        CHECK(*design.move({4}, 0) == UiNodePath{4});
        CHECK_FALSE(design.move({}, 1).has_value());
        // 지우기
        REQUIRE(design.remove({4}).has_value());
        // root · title · row 둘 · 버튼 여섯 · panel1 · 라이브러리 열셋 · list 의 항목 틀 하나 · tabs 의 판 둘
        CHECK(design.nodes().size() == 1 + 1 + 2 + 6 + 1 + 13 + 1 + 2);
        CHECK_FALSE(design.remove({}).has_value());
        // list 의 항목 틀 (item — 자식 0) 은 목록에 보이지만 지우거나 옮기지 않는다
        const auto list = std::ranges::find(design.nodes(), std::string("list"), &UiDesignNode::kind);
        REQUIRE(list != design.nodes().end());
        UiNodePath item = list->path;
        item.push_back(0);
        CHECK((*design.node(item))["text"] == "항목");
        REQUIRE(design.setProperty(item, "text", "줄").has_value());
        CHECK_FALSE(design.remove(item).has_value());
        CHECK_FALSE(design.move(item, 1).has_value());
        CHECK(design.save().has_value());
    }

    TEST_CASE(
        "ui designer (v0.10 ST-06-4): 자리 → 미리보기 사각형 · 배율, 오류 있는 문서는 저장 거절 · 배치 없음, 깨진 "
        "JSON") {
        TempDir t("layout");
        const fs::path file = copyHud(t);
        const std::string original = *io::readFile(file);
        UndoService undo;
        DocumentService docs(&undo);
        UiDesignService design(docs);
        REQUIRE(design.open(file).has_value());
        // 판 (root) 을 오른쪽 위 모서리에 — 오른쪽 끝에서 110 · 위에서 10, 크기 100 × 30 (쌓기 안의 자식은 앵커가
        // 아니라 쌓기를 따른다 — 자리는 root · 쌓지 않는 panel 의 자식에 쓴다)
        REQUIRE(design.setPlacement({}, {1, 0}, {1, 0}, {-110, 10, -10, 40}).has_value());
        const Json board = *design.node({});
        CHECK(board["anchor"]["min"] == Json::array({1, 0}));
        CHECK(board["offset"] == Json::array({-110, 10, -10, 40}));
        const auto rects = design.layout(800, 600, 1.0f);
        REQUIRE_FALSE(rects.empty());
        CHECK(rects[0].path.empty()); // root 먼저
        CHECK(rects[0].rect == ui::UiRect{690, 10, 100, 30});
        CHECK(rects[0].depth == 0);
        const auto big = design.layout(800, 600, 2.0f);
        REQUIRE_FALSE(big.empty());
        CHECK(big[0].rect == ui::UiRect{580, 20, 200, 60}); // 오프셋에 배율
        // 버튼 아홉 — 사각형마다 경로
        CHECK(std::ranges::count_if(rects, [](const UiDesignRect& r) { return r.path.size() == 2; }) == 9);

        // 모르는 요소 — 검사 오류, 저장 거절 (파일 그대로), 배치 없음
        REQUIRE(design.setProperty({0}, "kind", "window").has_value());
        CHECK(hasError(design.issues()));
        CHECK(design.layout(800, 600, 1.0f).empty());
        const auto refused = design.save();
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::ValidationFailed);
        CHECK(refused.error().message.find("U3") != std::string::npos);
        CHECK(*io::readFile(file) == original);
        REQUIRE(undo.undo());
        CHECK_FALSE(hasError(design.issues()));

        // 깨진 JSON (글 편집기에서 고치다 만 것) — 목록이 비고 이유, 편집은 거절
        REQUIRE(docs.setText(*design.document(), "{ \"id\": ").has_value());
        CHECK(design.nodes().empty());
        CHECK_FALSE(design.parseError().empty());
        CHECK_FALSE(design.setProperty({}, "text", "x").has_value());
        CHECK(design.layout(800, 600, 1.0f).empty());
        REQUIRE(undo.undo());
        CHECK(design.nodes().size() == 14);
    }
}
