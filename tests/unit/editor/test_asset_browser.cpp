// Asset Browser (v0.10 ST-06-3 — core/asset/AssetRefs · editor/ui/AssetBrowserPanel, ADR-0146): 참조 찾기 (팩
// materials.json — id · 경로, 엔진 에셋은 아니다), 옮기기 (원본 · .meta 같이, 경로 참조를 고쳐 쓴다 — id 참조는 그대로,
// 검사 A6 없음), 거절 (있는 자리 · assets 밖 · .meta · 없는 원본 · 틀린 경로), 지우기 (참조가 있으면 거절, force · 참조
// 없음 → .sandbox/trash/<n>), 머티리얼 만들기 (끌어다 놓기), 창 도우미 (거르기 · 참조 수 · 썸네일 크기), 창 그리기
// (헤드리스 — 썸네일 텍스처 · 고름), UI 디자이너의 image 에 에셋을 놓기.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include <imgui.h>

#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/asset/AssetRefs.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/project/GameProject.hpp"
#include "editor/services/DocumentService.hpp"
#include "editor/services/UiDesignService.hpp"
#include "editor/ui/AssetBrowserPanel.hpp"
#include "editor/ui/UiDesignerPanel.hpp"
#include "foundation/io/FileIo.hpp"
#include "platform/common/HeadlessWindow.hpp"
#include "render/asset/Image.hpp"

using namespace sbx;
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        path = fs::temp_directory_path() / std::format("sbx_assets_{}_{:08x}", name, std::random_device{}());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog c = [] {
        ecs::ComponentCatalog made;
        (void)comp::registerCoreComponents(made);
        return made;
    }();
    return c;
}

void write(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// 빈 월드 프로젝트 + 팩 game (materials.json) + 그림 둘 (id 를 붙인다)
struct Project {
    TempDir dir;
    std::string heroId;
    std::string treeId;
    explicit Project(std::string_view name) : dir(name) {
        project::NewProjectOptions o;
        o.gameId = "test.assets";
        REQUIRE(project::createProject(dir.path / "p", o, SemVer{0, 10, 0, {}, {}}, catalog()).has_value());
        fs::create_directories(root() / "assets" / "art");
        REQUIRE(render::savePng(root() / "assets" / "art" / "hero.png", render::Image::filled(80, 40, 255, 0, 0)));
        REQUIRE(render::savePng(root() / "assets" / "art" / "tree.png", render::Image::filled(8, 8, 0, 255, 0)));
        u32 n = 0;
        REQUIRE(asset::importAssets(root(), [&] { return std::format("{:032x}", ++n); }).has_value());
        const asset::AssetScan scan = asset::scanProjectAssets(root());
        REQUIRE(scan.assets.size() == 2);
        for (const asset::AssetEntry& e : scan.assets) {
            (e.path.ends_with("hero.png") ? heroId : treeId) = e.id;
        }
        write(root() / "content" / "game" / "pack.json", R"({"id": "game", "version": "0.1.0"})");
        write(root() / "content" / "game" / "presentation" / "materials.json",
              std::format(R"({{"materials": {{"game/hero": {{"sprite": "asset:{}"}}, "game/hero2": {{"sprite":
"assets/art/hero.png"}}, "game/tree": {{"sprite": "assets/art/tree.png"}}, "game/engine": {{"sprite": "sprites/x.png"}}}}}})",
                          heroId));
    }
    [[nodiscard]] fs::path root() const { return dir.path / "p"; }
};

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("asset browser (v0.10 ST-06-3): 참조 · 옮기기 (참조 유지) · 거절") {
        Project p("move");
        const auto refs = asset::findAssetReferences(p.root(), asset::scanProjectAssets(p.root()));
        REQUIRE(refs.size() == 3); // 엔진 에셋 (sprites/…) 은 아니다
        CHECK(refs[0].file == "content/game/presentation/materials.json");
        CHECK(refs[0].pointer == "/materials/game/hero/sprite");
        CHECK(refs[0].byId);
        CHECK(refs[0].path == "assets/art/hero.png");
        CHECK(asset::referencesTo(refs, "assets/art/hero.png").size() == 2);
        CHECK(asset::referencesTo(refs, "assets/art/tree.png").size() == 1);
        CHECK(editor::countReferences(refs, "assets/art/hero.png") == 2);

        const auto moved = asset::moveAsset(p.root(), "assets/art/hero.png", "assets/chars/Hero.png");
        REQUIRE_MESSAGE(moved.has_value(), moved.error().describe());
        CHECK(moved->movedMeta);
        CHECK(moved->rewritten == 1); // 경로 참조 하나 — id 참조는 그대로
        CHECK_FALSE(fs::exists(p.root() / "assets" / "art" / "hero.png"));
        CHECK_FALSE(fs::exists(p.root() / "assets" / "art" / "hero.png.meta"));
        const asset::AssetScan after = asset::scanProjectAssets(p.root());
        REQUIRE(after.find(p.heroId) != nullptr);
        CHECK(after.find(p.heroId)->path == "assets/chars/Hero.png"); // 같은 id · 새 경로
        const auto refsAfter = asset::findAssetReferences(p.root(), after);
        CHECK(asset::referencesTo(refsAfter, "assets/chars/Hero.png").size() == 2);
        const Json mats = Json::parse(*io::readFile(p.root() / "content" / "game" / "presentation" / "materials.json"));
        CHECK(mats["materials"]["game/hero2"]["sprite"] == "assets/chars/Hero.png");
        CHECK(mats["materials"]["game/hero"]["sprite"] == std::format("asset:{}", p.heroId));
        CHECK(mats["materials"].begin().key() == "game/hero"); // 키 차례 그대로

        // 거절 — 있는 자리 · assets 밖 · .meta · 없는 원본 · 틀린 경로
        CHECK(asset::moveAsset(p.root(), "assets/art/tree.png", "assets/chars/Hero.png").error().code ==
              ErrorCode::AlreadyExists);
        CHECK_FALSE(asset::moveAsset(p.root(), "assets/art/tree.png", "content/tree.png").has_value());
        CHECK_FALSE(asset::moveAsset(p.root(), "assets/art/tree.png.meta", "assets/x.png").has_value());
        CHECK(asset::moveAsset(p.root(), "assets/art/none.png", "assets/x.png").error().code == ErrorCode::NotFound);
        CHECK_FALSE(asset::moveAsset(p.root(), "assets/art/tree.png", "assets/../tree.png").has_value());
        CHECK(fs::exists(p.root() / "assets" / "art" / "tree.png"));
    }

    TEST_CASE("asset browser (v0.10 ST-06-3): 안전 지우기 (참조가 있으면 거절 · force · 휴지통) · 머티리얼 만들기") {
        Project p("delete");
        const auto refused = asset::deleteAsset(p.root(), "assets/art/tree.png");
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message.find("1 곳이 쓴다") != std::string::npos);
        CHECK(refused.error().message.find("/materials/game/tree/sprite") != std::string::npos);
        CHECK(fs::exists(p.root() / "assets" / "art" / "tree.png"));
        const auto forced = asset::deleteAsset(p.root(), "assets/art/tree.png", true);
        REQUIRE(forced.has_value());
        CHECK(forced->references == 1);
        CHECK(forced->trashed == p.root() / ".sandbox" / "trash" / "1" / "assets" / "art" / "tree.png");
        CHECK(fs::exists(forced->trashed));
        CHECK(fs::exists(fs::path(forced->trashed.string() + ".meta")));
        CHECK_FALSE(fs::exists(p.root() / "assets" / "art" / "tree.png"));
        // 참조를 고친 뒤에는 그냥 지운다 — 다음 휴지통 칸
        REQUIRE(render::savePng(p.root() / "assets" / "loose.png", render::Image::filled(2, 2, 1, 2, 3)));
        const auto loose = asset::deleteAsset(p.root(), "assets/loose.png");
        REQUIRE(loose.has_value());
        CHECK(loose->trashed.parent_path().parent_path().filename() == "2");
        CHECK_FALSE(asset::deleteAsset(p.root(), "assets/loose.png").has_value()); // 이제 없다

        // 머티리얼 만들기 — 새 팩 폴더 (파일 없음) · 같은 것 다시 · 다른 그림 이름 거절 · 틀린 id · 없는 팩
        write(p.root() / "content" / "ui" / "pack.json", R"({"id": "ui", "version": "0.1.0"})");
        REQUIRE(asset::ensureAssetMaterial(p.root(), "ui", p.heroId, "ui/hero").has_value());
        const Json made = Json::parse(*io::readFile(p.root() / "content" / "ui" / "presentation" / "materials.json"));
        CHECK(made["materials"]["ui/hero"]["sprite"] == std::format("asset:{}", p.heroId));
        CHECK(asset::ensureAssetMaterial(p.root(), "ui", p.heroId, "ui/hero").has_value());
        CHECK(asset::ensureAssetMaterial(p.root(), "game", p.heroId, "game/tree").error().code ==
              ErrorCode::AlreadyExists);
        CHECK_FALSE(asset::ensureAssetMaterial(p.root(), "ui", "zz", "ui/x").has_value());
        CHECK_FALSE(asset::ensureAssetMaterial(p.root(), "nope", p.heroId, "nope/x").has_value());
        CHECK_FALSE(asset::ensureAssetMaterial(p.root(), "../x", p.heroId, "x").has_value());
    }

    TEST_CASE("asset browser (v0.10 ST-06-3): 창 도우미 · 창 그리기 (썸네일 텍스처) · UI 디자이너 image 에 놓기") {
        Project p("panel");
        const asset::AssetScan scan = asset::scanProjectAssets(p.root());
        CHECK(editor::filterAssets(scan, "").size() == 2);
        CHECK(editor::filterAssets(scan, "TREE").size() == 1);
        CHECK(editor::filterAssets(scan, p.heroId).size() == 1);
        CHECK(editor::filterAssets(scan, "zzz").empty());
        CHECK(editor::thumbnailSize(80, 40, 48) == std::array<u32, 2>{48, 24});
        CHECK(editor::thumbnailSize(10, 200, 48) == std::array<u32, 2>{2, 48});
        CHECK(editor::thumbnailSize(8, 8, 48) == std::array<u32, 2>{8, 8});
        CHECK(editor::thumbnailSize(0, 8, 48) == std::array<u32, 2>{1, 1});

        platform::HeadlessWindow window;
        client::ImGuiLayer layer(window);
        editor::AssetBrowserState st;
        for (int i = 0; i < 3; ++i) {
            platform::PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            if (i == 1) {
                st.selected = "assets/art/hero.png";
            }
            CHECK_FALSE(editor::drawAssetBrowser(st, p.root()).has_value());
            (void)layer.endFrame(platform::CursorShape::Arrow);
            layer.acknowledgeTextures();
        }
        CHECK(st.scan.assets.size() == 2);
        CHECK(st.refs.size() == 3);
        // 썸네일 둘이 사용자 텍스처로 등록되었다 (헤드리스 — ImGuiLayer 가 받은 척)
        usize user = 0;
        for (ImTextureData* t : ImGui::GetPlatformIO().Textures) {
            user += t->Width <= 48 && t->Height <= 48 && t->Status == ImTextureStatus_OK ? 1 : 0;
        }
        CHECK(user >= 2);

        // UI 디자이너 — image 노드에 에셋 (id) 을 놓으면 팩 머티리얼을 만들고 material 로
        const fs::path hud = p.root() / "content" / "game" / "presentation" / "ui" / "hud.json";
        write(
            hud,
            R"({"id": "game.hud", "root": {"kind": "panel", "children": [{"kind": "image", "material": "x", "size": [8, 8]}]}})");
        editor::services::DocumentService docs;
        editor::services::UiDesignService design(docs);
        REQUIRE(design.open(hud).has_value());
        editor::UiDesignerPanelState ds;
        ds.file = hud;
        ds.selected = {0};
        REQUIRE(editor::dropAssetOnImage(ds, design, p.treeId).has_value());
        CHECK((*design.node({0}))["material"] == "game/tree");
        const Json mats = Json::parse(*io::readFile(p.root() / "content" / "game" / "presentation" / "materials.json"));
        CHECK(mats["materials"]["game/tree"]["sprite"] == "assets/art/tree.png"); // 이미 있던 이름 — 다른 그림이면 거절
        REQUIRE(editor::dropAssetOnImage(ds, design, p.heroId).has_value());
        CHECK((*design.node({0}))["material"] == "game/hero");
        CHECK_FALSE(editor::dropAssetOnImage(ds, design, std::string(32, 'f')).has_value()); // 없는 id
    }
}
