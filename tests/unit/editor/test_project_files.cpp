// 프로젝트 파일 (v0.13 ST-08-3 — editor/services/ProjectFiles, ADR-0167): 트리 (폴더 먼저 · 이름 순 · 숨김 · .meta 빼기 ·
// 심볼릭 링크 빼기) · 거르기 (위 폴더 남김) · 새 폴더 · 이름 바꾸기 · 옮기기 · 복제 · 휴지통 — 에셋은 .meta 동행 · 경로 참조
// 고침 · 참조가 있으면 지우기 거절. 경로 탈출 (..) · 절대 경로 · 지킴 (매니페스트 · 맨 위 폴더) · 이름 규칙 거절. 새 스크립트
// 자리 (팩의 scripts/ · id 마디 규칙 · 이미 있음).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/asset/AssetDatabase.hpp"
#include "core/asset/AssetRefs.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/project/GameProject.hpp"
#include "editor/services/ProjectFiles.hpp"
#include "editor/ui/ProjectExplorerPanel.hpp"
#include "foundation/io/FileIo.hpp"
#include "platform/common/HeadlessWindow.hpp"
#include "render/asset/Image.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        path = fs::temp_directory_path() / std::format("sbx_pfiles_{}_{:08x}", name, std::random_device{}());
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

// 빈 프로젝트 + 팩 game (스크립트 · materials.json) + 에셋 그림 둘 (hero = 경로 참조, tree = 참조 없음)
struct Project {
    TempDir dir;
    std::string heroId;
    explicit Project(std::string_view name) : dir(name) {
        project::NewProjectOptions o;
        o.gameId = "test.files";
        REQUIRE(project::createProject(root(), o, SemVer{0, 13, 0, {}, {}}, catalog()).has_value());
        fs::create_directories(root() / "assets" / "art");
        REQUIRE(render::savePng(root() / "assets" / "art" / "hero.png", render::Image::filled(8, 8, 255, 0, 0)));
        REQUIRE(render::savePng(root() / "assets" / "art" / "tree.png", render::Image::filled(8, 8, 0, 255, 0)));
        u32 n = 0;
        REQUIRE(asset::importAssets(root(), [&] { return std::format("{:032x}", ++n); }).has_value());
        for (const asset::AssetEntry& e : asset::scanProjectAssets(root()).assets) {
            if (e.path.ends_with("hero.png")) {
                heroId = e.id;
            }
        }
        write(root() / "content" / "game" / "pack.json", R"({"id": "game", "version": "0.1.0"})");
        write(root() / "content" / "game" / "scripts" / "rules.ts", "// rules\n");
        write(root() / "content" / "game" / "presentation" / "materials.json",
              R"({"materials": {"game/hero": {"sprite": "assets/art/hero.png"}}})");
        write(root() / ".sandbox" / "cache.bin", "x");
        write(root() / "build" / "out.sbpkg", "x");
    }
    [[nodiscard]] fs::path root() const { return dir.path / "p"; }
};

bool listed(const std::vector<ProjectFileEntry>& entries, std::string_view rel) {
    return std::ranges::any_of(entries, [&](const ProjectFileEntry& e) { return e.rel == rel; });
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("project files (v0.13 ST-08-3): 트리 · 숨김 · 차례 · 종류 · 거르기") {
        Project p("tree");
        const std::vector<ProjectFileEntry> all = listProjectFiles(p.root());
        REQUIRE_FALSE(all.empty());
        CHECK(listed(all, "assets"));
        CHECK(listed(all, "assets/art/hero.png"));
        CHECK(listed(all, "content/game/scripts/rules.ts"));
        CHECK(listed(all, std::string(project::kManifestFile)));
        CHECK_FALSE(listed(all, "assets/art/hero.png.meta")); // .meta 는 원본과 함께 — 보이지 않는다
        CHECK_FALSE(listed(all, ".sandbox"));
        CHECK_FALSE(listed(all, ".sandbox/cache.bin"));
        CHECK_FALSE(listed(all, "build"));
        // 폴더 먼저 · 깊이 우선: assets → assets/art → assets/art/hero.png … 맨 위 파일 (매니페스트) 은 폴더 뒤
        const auto at = [&](std::string_view rel) {
            return std::ranges::find_if(all, [&](const ProjectFileEntry& e) { return e.rel == rel; }) - all.begin();
        };
        CHECK(at("assets") < at("assets/art"));
        CHECK(at("assets/art") < at("assets/art/hero.png"));
        CHECK(at("assets/art/hero.png") < at("assets/art/tree.png"));
        CHECK(at("content") < at(project::kManifestFile));
        CHECK(all[static_cast<usize>(at("assets/art/hero.png"))].depth == 2);
        CHECK(all[static_cast<usize>(at("assets/art/hero.png"))].kind == ProjectFileKind::Image);
        CHECK(all[static_cast<usize>(at("content/game/scripts/rules.ts"))].kind == ProjectFileKind::Script);
        CHECK(all[static_cast<usize>(at("assets"))].kind == ProjectFileKind::Folder);
        CHECK(projectFileKind("a.JSON") == ProjectFileKind::Json);
        CHECK(projectFileKind("noext") == ProjectFileKind::Other);

        // 거르기 — 맞는 파일과 그 위 폴더 (대소문자 무시)
        const std::vector<ProjectFileEntry> f = filterProjectFiles(all, "RULES");
        REQUIRE(f.size() == 4);
        CHECK(f[0].rel == "content");
        CHECK(f[1].rel == "content/game");
        CHECK(f[2].rel == "content/game/scripts");
        CHECK(f[3].rel == "content/game/scripts/rules.ts");
        CHECK(filterProjectFiles(all, "").size() == all.size());
        CHECK(filterProjectFiles(all, "zzz").empty());
        CHECK(listProjectFiles(p.root() / "nope").empty());
    }

    TEST_CASE("project files (v0.13 ST-08-3): 새 폴더 · 이름 바꾸기 · 옮기기 · 복제 · 거절") {
        Project p("edit");
        const auto made = createProjectFolder(p.root(), "content/game", "data");
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(*made == "content/game/data");
        CHECK(fs::is_directory(p.root() / "content" / "game" / "data"));
        CHECK_FALSE(createProjectFolder(p.root(), "content/game", "data").has_value());  // 이미 있음
        CHECK_FALSE(createProjectFolder(p.root(), "content/game", "..").has_value());    // 탈출
        CHECK_FALSE(createProjectFolder(p.root(), "content/game", "a/b").has_value());   // 마디 하나
        CHECK_FALSE(createProjectFolder(p.root(), "content/game", "한글").has_value());   // P4 — ASCII
        CHECK_FALSE(createProjectFolder(p.root(), "content/game", "con").has_value());    // Windows 예약
        CHECK_FALSE(createProjectFolder(p.root(), "../x", "a").has_value());             // 부모 탈출
        CHECK_FALSE(createProjectFolder(p.root(), ".sandbox", "a").has_value());         // 숨김 폴더
        CHECK_FALSE(createProjectFolder(p.root(), "C:/Windows", "a").has_value());       // 절대 경로
        CHECK(createProjectFolder(p.root(), "", "docs").has_value());                    // 맨 위

        // 이름 바꾸기 — 일반 파일 · 지킴
        const auto renamed = renameProjectEntry(p.root(), "content/game/scripts/rules.ts", "logic.ts");
        REQUIRE_MESSAGE(renamed.has_value(), renamed.error().describe());
        CHECK(*renamed == "content/game/scripts/logic.ts");
        CHECK(fs::exists(p.root() / "content" / "game" / "scripts" / "logic.ts"));
        CHECK_FALSE(renameProjectEntry(p.root(), std::string(project::kManifestFile), "x.json").has_value());
        CHECK_FALSE(renameProjectEntry(p.root(), "assets", "art2").has_value());
        CHECK_FALSE(renameProjectEntry(p.root(), "content", "c").has_value());
        CHECK_FALSE(renameProjectEntry(p.root(), "content/game/nope.ts", "x.ts").has_value()); // 없음
        CHECK_FALSE(renameProjectEntry(p.root(), "content/game/scripts/logic.ts", "../x.ts").has_value());
        CHECK(renameProjectEntry(p.root(), "content/game/scripts/logic.ts", "logic.ts") ==
              std::string("content/game/scripts/logic.ts")); // 같은 이름 — 그대로

        // 에셋 이름 바꾸기 — .meta 동행 · 경로 참조 고침 (asset::moveAsset 과 같은 길)
        const auto hero = renameProjectEntry(p.root(), "assets/art/hero.png", "player.png");
        REQUIRE_MESSAGE(hero.has_value(), hero.error().describe());
        CHECK(fs::exists(p.root() / "assets" / "art" / "player.png.meta"));
        CHECK_FALSE(fs::exists(p.root() / "assets" / "art" / "hero.png.meta"));
        CHECK(asset::scanProjectAssets(p.root()).find(p.heroId)->path == "assets/art/player.png");
        const auto mats = nlohmann::json::parse(
            *io::readFile(p.root() / "content" / "game" / "presentation" / "materials.json"));
        CHECK(mats["materials"]["game/hero"]["sprite"] == "assets/art/player.png");

        // 에셋 폴더 옮기기 — 안의 파일마다 (참조 고침), 옛 폴더는 사라진다
        REQUIRE(createProjectFolder(p.root(), "assets", "chars").has_value());
        const auto movedFolder = moveProjectEntry(p.root(), "assets/art", "assets/chars");
        REQUIRE_MESSAGE(movedFolder.has_value(), movedFolder.error().describe());
        CHECK(*movedFolder == "assets/chars/art");
        CHECK_FALSE(fs::exists(p.root() / "assets" / "art"));
        CHECK(asset::scanProjectAssets(p.root()).find(p.heroId)->path == "assets/chars/art/player.png");
        const auto mats2 = nlohmann::json::parse(
            *io::readFile(p.root() / "content" / "game" / "presentation" / "materials.json"));
        CHECK(mats2["materials"]["game/hero"]["sprite"] == "assets/chars/art/player.png");
        CHECK_FALSE(moveProjectEntry(p.root(), "assets/chars", "assets/chars/art").has_value()); // 자기 안
        CHECK_FALSE(moveProjectEntry(p.root(), "assets/chars/art/player.png", "content/game").has_value()); // 에셋 밖
        CHECK_FALSE(moveProjectEntry(p.root(), "content", "docs").has_value()); // 지킴
        // 일반 파일 옮기기 (assets 로 들어가는 것은 된다 — 그 뒤 [가져오기] 로 id)
        const auto movedScript = moveProjectEntry(p.root(), "content/game/scripts/logic.ts", "content/game/data");
        REQUIRE_MESSAGE(movedScript.has_value(), movedScript.error().describe());
        CHECK(*movedScript == "content/game/data/logic.ts");

        // 복제 — 파일 "_copy" · 다시 "_copy2", 에셋은 .meta 없이 (같은 id 둘은 충돌), 폴더도
        const auto c1 = duplicateProjectEntry(p.root(), "content/game/data/logic.ts");
        REQUIRE(c1.has_value());
        CHECK(*c1 == "content/game/data/logic_copy.ts");
        CHECK(duplicateProjectEntry(p.root(), "content/game/data/logic.ts") ==
              std::string("content/game/data/logic_copy2.ts"));
        const auto png = duplicateProjectEntry(p.root(), "assets/chars/art/player.png");
        REQUIRE(png.has_value());
        CHECK(*png == "assets/chars/art/player_copy.png");
        CHECK_FALSE(fs::exists(p.root() / "assets" / "chars" / "art" / "player_copy.png.meta"));
        CHECK(asset::scanProjectAssets(p.root()).unidentified.size() == 1); // 사본은 id 없음 — 충돌 아님
        const auto folderCopy = duplicateProjectEntry(p.root(), "assets/chars");
        REQUIRE(folderCopy.has_value());
        CHECK(*folderCopy == "assets/chars_copy");
        CHECK(fs::exists(p.root() / "assets" / "chars_copy" / "art" / "player.png"));
        CHECK_FALSE(fs::exists(p.root() / "assets" / "chars_copy" / "art" / "player.png.meta"));
        CHECK_FALSE(duplicateProjectEntry(p.root(), std::string(project::kManifestFile)).has_value());
    }

    TEST_CASE("project files (v0.13 ST-08-3): 휴지통 — 참조가 있는 에셋 거절 · force · 폴더 · 되살릴 자리") {
        Project p("trash");
        // 참조가 있는 에셋 — 거절, force 면 옮긴다 (.meta 함께)
        const auto refused = trashProjectEntry(p.root(), "assets/art/hero.png");
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message.find("쓴다") != std::string::npos);
        // 참조가 있는 에셋을 담은 폴더도 거절
        CHECK_FALSE(trashProjectEntry(p.root(), "assets/art").has_value());
        const auto tree = trashProjectEntry(p.root(), "assets/art/tree.png"); // 참조 없음
        REQUIRE_MESSAGE(tree.has_value(), tree.error().describe());
        CHECK(fs::exists(*tree));
        CHECK(fs::exists(fs::path(tree->string() + ".meta")));
        CHECK(tree->string().find(".sandbox") != std::string::npos);
        const auto forced = trashProjectEntry(p.root(), "assets/art", true);
        REQUIRE(forced.has_value());
        CHECK_FALSE(fs::exists(p.root() / "assets" / "art"));
        CHECK(fs::exists(*forced / "hero.png"));
        // 일반 파일 · 폴더
        const auto script = trashProjectEntry(p.root(), "content/game/scripts/rules.ts");
        REQUIRE(script.has_value());
        CHECK_FALSE(fs::exists(p.root() / "content" / "game" / "scripts" / "rules.ts"));
        CHECK(io::readFile(*script).value() == "// rules\n");
        CHECK_FALSE(trashProjectEntry(p.root(), "content/game/scripts/rules.ts").has_value()); // 이미 없음
        CHECK_FALSE(trashProjectEntry(p.root(), std::string(project::kManifestFile)).has_value());
        CHECK_FALSE(trashProjectEntry(p.root(), "worlds").has_value());
        CHECK_FALSE(trashProjectEntry(p.root(), ".sandbox/cache.bin").has_value());
        CHECK_FALSE(trashProjectEntry(p.root(), "").has_value());
    }

    TEST_CASE("project files (v0.13 ST-08-3): 심볼릭 링크 — 보이지 않고 지나는 경로는 고치지 않는다") {
        Project p("link");
        TempDir outside("outside");
        write(outside.path / "secret.txt", "s");
        std::error_code ec;
        fs::create_directory_symlink(outside.path, p.root() / "content" / "linked", ec);
        if (ec) {
            MESSAGE("심볼릭 링크를 만들 수 없다 (권한 — Windows 개발자 모드 아님): ", ec.message());
            return;
        }
        CHECK_FALSE(listed(listProjectFiles(p.root()), "content/linked"));
        CHECK(checkProjectEdit(p.root(), "content/linked/secret.txt", false).has_value());
        CHECK_FALSE(trashProjectEntry(p.root(), "content/linked/secret.txt").has_value());
        CHECK_FALSE(createProjectFolder(p.root(), "content/linked", "x").has_value());
        CHECK(fs::exists(outside.path / "secret.txt"));
    }

    TEST_CASE("project files (v0.13 ST-08-3 · ST-08-4): 새 스크립트 자리") {
        Project p("script");
        CHECK(newScriptPath(p.root(), "content/game", "enemy_ai") == std::string("content/game/scripts/enemy_ai.ts"));
        CHECK(newScriptPath(p.root(), "content/game/scripts", "enemy_ai.ts") ==
              std::string("content/game/scripts/enemy_ai.ts"));
        REQUIRE(createProjectFolder(p.root(), "content/game/scripts", "ai").has_value());
        CHECK(newScriptPath(p.root(), "content/game/scripts/ai", "wander") ==
              std::string("content/game/scripts/ai/wander.ts"));
        const auto exists = newScriptPath(p.root(), "content/game/scripts", "rules");
        REQUIRE_FALSE(exists.has_value());
        CHECK(exists.error().code == ErrorCode::AlreadyExists);
        CHECK_FALSE(newScriptPath(p.root(), "content/game", "Enemy").has_value());   // 대문자
        CHECK_FALSE(newScriptPath(p.root(), "content/game", "1st").has_value());     // 숫자로 시작
        CHECK_FALSE(newScriptPath(p.root(), "content/game", "a-b").has_value());     // '-'
        CHECK_FALSE(newScriptPath(p.root(), "content/game", "").has_value());
        CHECK_FALSE(newScriptPath(p.root(), "assets", "x").has_value());             // 팩 밖
        CHECK_FALSE(newScriptPath(p.root(), "content", "x").has_value());            // 팩이 아니다
        CHECK_FALSE(newScriptPath(p.root(), "content/game/presentation", "x").has_value());
        CHECK_FALSE(newScriptPath(p.root(), "content/nopack", "x").has_value());     // 없는 팩
        REQUIRE(createProjectFolder(p.root(), "content/game/scripts", "Bad").has_value());
        CHECK_FALSE(newScriptPath(p.root(), "content/game/scripts/Bad", "x").has_value()); // id 마디 아님
    }

    TEST_CASE("project explorer panel (v0.13 ST-08-3, ImGui 헤드리스): 트리 · 에셋 id · 묻는 창 · 이름 바꾸기 · 휴지통 거절") {
        Project p("panel");
        platform::HeadlessWindow window;
        client::ImGuiLayer layer(window);
        editor::ProjectExplorerState st;
        std::optional<editor::ProjectExplorerAction> last;
        const auto frame = [&] {
            platform::PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            if (auto a = editor::drawProjectExplorer(st, p.root())) {
                last = std::move(a);
            }
            (void)layer.endFrame(platform::CursorShape::Arrow);
            layer.acknowledgeTextures();
        };
        frame();
        frame();
        CHECK(listed(st.entries, "content/game/scripts/rules.ts"));
        REQUIRE(st.assetIds.contains("assets/art/hero.png"));
        CHECK(st.assetIds.at("assets/art/hero.png") == p.heroId); // 끌기 원천 = 에셋 id (월드 · UI 디자이너가 받는다)
        const ImGuiWindow* w = ImGui::FindWindowByName("프로젝트");
        REQUIRE(w != nullptr);
        CHECK(w->WasActive);
        CHECK(editor::projectParentOf("a/b/c.ts") == "a/b");
        CHECK(editor::projectParentOf("top.json").empty());

        // 묻는 창 — 열림 (OpenPopup) 을 그린 뒤, [바꾸기] 와 같은 길 (applyExplorerAsk) → FilesChanged (새 · 옛 경로).
        // 클릭 · Enter 자체는 창 QA (MANUAL-QA v0.13 ST-08-3)
        st.ask = editor::ProjectExplorerState::Ask::Rename;
        st.askOpen = true;
        st.askTarget = "content/game/scripts/rules.ts";
        st.askInput = {};
        const std::string_view name = "logic.ts";
        std::copy(name.begin(), name.end(), st.askInput.begin());
        frame();
        CHECK(ImGui::IsPopupOpen("##project-ask", ImGuiPopupFlags_AnyPopupId));
        CHECK_FALSE(st.askOpen);
        last = editor::applyExplorerAsk(st, p.root());
        REQUIRE(last.has_value());
        CHECK(last->kind == editor::ProjectExplorerAction::Kind::FilesChanged);
        CHECK(last->rel == "content/game/scripts/logic.ts");
        CHECK(last->oldRel == "content/game/scripts/rules.ts");
        CHECK(fs::exists(p.root() / "content" / "game" / "scripts" / "logic.ts"));
        CHECK(st.ask == editor::ProjectExplorerState::Ask::None);
        CHECK_FALSE(st.statusError);
        frame();
        CHECK(listed(st.entries, "content/game/scripts/logic.ts")); // 다시 훑었다
        CHECK_FALSE(listed(st.entries, "content/game/scripts/rules.ts"));
        CHECK_FALSE(ImGui::IsPopupOpen("##project-ask", ImGuiPopupFlags_AnyPopupId)); // 다음 그리기에서 닫혔다
        // 에셋 이름 바꾸기 → 그 에셋 id (앱이 스프라이트를 다시 올린다), 휴지통 거절 (쓰는 곳) → 상태 글 오류
        st.ask = editor::ProjectExplorerState::Ask::Rename;
        st.askTarget = "assets/art/hero.png";
        st.askInput = {};
        const std::string_view png = "player.png";
        std::copy(png.begin(), png.end(), st.askInput.begin());
        last = editor::applyExplorerAsk(st, p.root());
        REQUIRE(last.has_value());
        CHECK(last->assetId == p.heroId);
        frame();
        st.ask = editor::ProjectExplorerState::Ask::Trash;
        st.askTarget = "assets/art/player.png";
        CHECK_FALSE(editor::applyExplorerAsk(st, p.root()).has_value());
        CHECK(st.statusError);
        CHECK(fs::exists(p.root() / "assets" / "art" / "player.png"));

        // 새 스크립트 (v0.13 ST-08-4) — 팩 폴더에서만, 자리만 정하고 NewScript (만들기 · 열기는 앱)
        CHECK(editor::canCreateScriptIn("content/game"));
        CHECK(editor::canCreateScriptIn("content/game/scripts"));
        CHECK(editor::canCreateScriptIn("content/game/scripts/ai"));
        CHECK_FALSE(editor::canCreateScriptIn("content"));
        CHECK_FALSE(editor::canCreateScriptIn("content/game/presentation"));
        CHECK_FALSE(editor::canCreateScriptIn("assets"));
        CHECK_FALSE(editor::canCreateScriptIn(""));
        st.ask = editor::ProjectExplorerState::Ask::NewScript;
        st.askTarget = "content/game";
        st.askInput = {};
        const std::string_view script = "enemy_ai";
        std::copy(script.begin(), script.end(), st.askInput.begin());
        last = editor::applyExplorerAsk(st, p.root());
        REQUIRE(last.has_value());
        CHECK(last->kind == editor::ProjectExplorerAction::Kind::NewScript);
        CHECK(last->rel == "content/game/scripts/enemy_ai.ts");
        CHECK_FALSE(fs::exists(p.root() / "content" / "game" / "scripts" / "enemy_ai.ts")); // 앱이 저장 길로 만든다
        st.ask = editor::ProjectExplorerState::Ask::NewScript;
        st.askInput = {};
        const std::string_view bad = "Bad-Name";
        std::copy(bad.begin(), bad.end(), st.askInput.begin());
        CHECK_FALSE(editor::applyExplorerAsk(st, p.root()).has_value());
        CHECK(st.statusError);
    }
}
