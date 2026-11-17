// "UI 디자이너" 창 (v0.10 ST-06-4 조각 2 — editor/ui/UiDesignerPanel, ADR-0145): 프로젝트의 UI 문서 목록 (styles.json
// 빼고 · 팩 · 이름 순), 미리보기 맞춤 (줄이기 · 가운데 · 1 을 넘기지 않음), 점 아래의 가장 깊은 노드, 끌기 → 오프셋
// (배율로 나눔 · 반올림), setNode (자식 지킴 · kind 없으면 거절), 창 그리기 (헤드리스 ImGui — 문서 없음 · 고른 문서 ·
// 노드 고름 · 해상도 넷).
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include <imgui.h>

#include "apps/client/ui/ImGuiLayer.hpp"
#include "editor/services/DocumentService.hpp"
#include "editor/services/UiDesignService.hpp"
#include "editor/ui/UiDesignerPanel.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::editor;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        path = fs::temp_directory_path() / std::format("sbx_uipanel_{}_{:08x}", name, std::random_device{}());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void touch(const fs::path& p, std::string_view text = "{}") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("ui designer panel (v0.10 ST-06-4): 문서 목록 · 맞춤 · 고르기 · 끌기") {
        TempDir t("files");
        touch(t.path / "content" / "b" / "presentation" / "ui" / "menu.json");
        touch(t.path / "content" / "b" / "presentation" / "ui" / "styles.json");
        touch(t.path / "content" / "a" / "presentation" / "ui" / "hud.json");
        touch(t.path / "content" / "a" / "presentation" / "ui" / "notes.txt");
        touch(t.path / "content" / "c" / "pack.json");
        const auto files = uiDocumentFiles(t.path);
        REQUIRE(files.size() == 2);
        CHECK(files[0].filename() == "hud.json");
        CHECK(files[0].parent_path().parent_path().parent_path().filename() == "a");
        CHECK(files[1].filename() == "menu.json");
        CHECK(uiDocumentFiles(t.path / "missing").empty());

        // 맞춤 — 큰 해상도는 줄이고 가운데, 작은 것은 그대로 (1 을 넘기지 않는다)
        const UiPreviewFit big = fitPreview(640, 480, 1280, 720);
        CHECK(big.zoom == doctest::Approx(0.5));
        CHECK(big.x == doctest::Approx(0));
        CHECK(big.y == doctest::Approx(60));
        const UiPreviewFit small = fitPreview(1000, 1000, 800, 600);
        CHECK(small.zoom == doctest::Approx(1));
        CHECK(small.x == doctest::Approx(100));
        CHECK(fitPreview(0, 0, 800, 600).zoom == doctest::Approx(1));

        // 고르기 — 가장 깊은 것, 같은 깊이면 나중
        const std::vector<services::UiDesignRect> rects{{{}, {0, 0, 100, 100}, 0},
                                                        {{0}, {10, 10, 50, 50}, 1},
                                                        {{0, 0}, {20, 20, 10, 10}, 2},
                                                        {{1}, {40, 40, 30, 30}, 1}};
        CHECK(*pickNode(rects, 22, 22) == services::UiNodePath{0, 0});
        CHECK(*pickNode(rects, 45, 45) == services::UiNodePath{1}); // {0} 과 {1} 이 겹친다 — 나중
        CHECK(*pickNode(rects, 90, 5) == services::UiNodePath{});
        CHECK_FALSE(pickNode(rects, 150, 5).has_value());

        // 끌기 — 화면 거리를 배율로 나눠 네 칸 같이
        CHECK(dragOffset({8, 8, 196, 236}, 10, -4, 1) == std::array<f32, 4>{18, 4, 206, 232});
        CHECK(dragOffset({0, 0, 100, 20}, 9, 3, 2) == std::array<f32, 4>{5, 2, 105, 22}); // 4.5 → 5, 1.5 → 2
    }

    TEST_CASE(
        "ui designer panel (v0.10 ST-06-4): setNode — 자식 지킴 · 되돌리기 한 칸 · kind 없으면 거절, 창을 그린다") {
        TempDir t("draw");
        const fs::path hud = t.path / "content" / "ttt" / "presentation" / "ui" / "hud.json";
        fs::create_directories(hud.parent_path());
        fs::copy_file(fs::path(SBX_SAMPLES_DIR) / "tictactoe" / "content" / "ttt" / "presentation" / "ui" / "hud.json",
                      hud);
        services::UndoService undo;
        services::DocumentService docs(&undo);
        services::UiDesignService design(docs);

        platform::HeadlessWindow window;
        client::ImGuiLayer layer(window);
        UiDesignerPanelState st;
        const auto frame = [&](const std::vector<fs::path>& files) {
            platform::PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            drawUiDesignerPanel(st, design, files);
            (void)layer.endFrame(platform::CursorShape::Arrow);
            layer.acknowledgeTextures();
        };
        frame({});    // 문서 없음
        frame({hud}); // 아직 고르지 않음
        REQUIRE(design.open(hud).has_value());
        st.file = hud;
        for (int size = 0; size < static_cast<int>(kUiPreviewSizes.size()); ++size) {
            st.size = size;
            frame({hud});
        }
        st.selected = {1}; // row0 — 속성 칸에 노드 JSON (자식 빼고)
        frame({hud});
        CHECK(st.jsonText.find("\"kind\": \"panel\"") != std::string::npos);
        CHECK(st.jsonText.find("children") == std::string::npos);
        st.selected = {9, 9}; // 없는 노드 — 고름을 푼다
        frame({hud});
        CHECK(st.selected.empty());

        // setNode — 자식을 적지 않으면 지금 것을 지킨다, 되돌리기 한 칸
        nlohmann::ordered_json row = *design.node({1});
        row.erase("children");
        row["gap"] = 9;
        REQUIRE(design.setNode({1}, row).has_value());
        CHECK((*design.node({1}))["gap"] == 9);
        CHECK((*design.node({1}))["children"].size() == 3);
        REQUIRE(undo.undo());
        CHECK((*design.node({1}))["gap"] == 4);
        CHECK_FALSE(design.setNode({1}, nlohmann::ordered_json{{"text", "kind 없음"}}).has_value());
        CHECK_FALSE(design.setNode({7}, row).has_value());
        frame({hud});
    }
}
