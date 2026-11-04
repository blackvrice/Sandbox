// 게임의 자기 머티리얼 · 에셋 (v0.6 M6-T9 — apps/client/presentation/PackMaterials): sprite 풀이 ("asset:<id>" ·
// "assets/…" · 엔진 경로), 팩 presentation/materials.json 이 엔진 머티리얼 위에, 로컬 세션이 프로젝트 · 패키지의 것을
// 읽는다
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include "apps/client/NetworkSession.hpp"
#include "apps/client/presentation/PackMaterials.hpp"
#include "core/asset/AssetDatabase.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/package/GamePackage.hpp"
#include "core/project/GameProject.hpp"
#include "core/scenarios/WorldSource.hpp"

using namespace sbx;
using namespace sbx::client;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_packmat_{}_{:x}", tag, std::random_device{}());
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void put(const fs::path& f, std::string_view bytes) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary | std::ios::trunc) << bytes;
}

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const SemVer kEngine{0, 1, 0, {}, {}}; // 새 프로젝트 범위 ">=0.1.0" — 지금 엔진 (0.5) 이 연다

// ecosystem_small 프로젝트 + 자기 그림 (대소문자 섞인 이름) + 팩 머티리얼 (자기 그림 · 엔진 이름 덮어쓰기 · 색)
fs::path makeGame(const fs::path& root, std::string& heroId) {
    const fs::path dir = root / "Game";
    project::NewProjectOptions o;
    o.gameId = "studio.art";
    o.fromScenario = "ecosystem_small";
    o.contentRoot = SBX_CONTENT_DIR;
    REQUIRE(project::createProject(dir, o, kEngine, catalog()).has_value());
    put(dir / "assets/art/Hero.png", "PNG-hero");
    REQUIRE(asset::importAssets(dir, [] { return std::string(32, 'a'); }).has_value());
    heroId = std::string(32, 'a');
    put(dir / "content/ecosystem/presentation/materials.json", std::format(R"({{"materials": {{
        "game/hero": {{ "sprite": "asset:{}" }},
        "game/flag": {{ "sprite": "assets/art/Hero.png", "color": [10, 20, 30] }},
        "eco/rabbit": {{ "color": [1, 2, 3] }}
    }}}})",
                                                                           heroId));
    return dir;
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE(
        "pack materials: sprite 풀이 — asset:<id> · assets/… 는 프로젝트 파일 (대소문자 그대로), 그 밖은 엔진 루트") {
        TempDir root("resolve");
        std::string id;
        const fs::path dir = makeGame(root.path, id);
        const render::SpriteResolver r = projectSpriteResolver(dir);
        const auto byId = r("asset:" + id);
        REQUIRE(byId.has_value());
        CHECK(*byId == dir / "assets/art/Hero.png");
        const auto byPath = r("assets/art/Hero.png");
        REQUIRE(byPath.has_value());
        CHECK(*byPath == dir / "assets/art/Hero.png");
        CHECK(r("asset:" + std::string(32, 'b')) == fs::path());  // 없는 id → 마젠타
        CHECK(r("assets/../sandbox.project.json") == fs::path()); // 프로젝트 밖 · 규칙 밖
        CHECK_FALSE(r("ecosystem/rabbit.png").has_value());       // 엔진 루트
        // 프로젝트가 없는 접속 (원격) — 프로젝트 참조는 찾지 못함, 엔진 경로는 그대로
        const render::SpriteResolver none = projectSpriteResolver({});
        CHECK(none("asset:" + id) == fs::path());
        CHECK(none("assets/art/Hero.png") == fs::path());
        CHECK_FALSE(none("ecosystem/rabbit.png").has_value());

        // 이름 · 폴더를 바꿔도 (.meta 와 같이) id 참조는 그대로 — 경로로 부른 참조만 끊긴다 (28 G2)
        fs::create_directories(dir / "assets/heroes");
        fs::rename(dir / "assets/art/Hero.png", dir / "assets/heroes/Knight.png");
        fs::rename(dir / "assets/art/Hero.png.meta", dir / "assets/heroes/Knight.png.meta");
        const render::SpriteResolver moved = projectSpriteResolver(dir);
        CHECK(moved("asset:" + id) == dir / "assets/heroes/Knight.png");
        CHECK(moved("assets/art/Hero.png") == dir / "assets/art/Hero.png"); // 경로 참조는 옛 자리 (없는 파일 → 마젠타)
    }

    TEST_CASE("pack materials: 팩 presentation/materials.json 이 엔진 머티리얼 위에 · 로컬 세션 (프로젝트 · 패키지)") {
        TempDir root("session");
        std::string id;
        const fs::path dir = makeGame(root.path, id);

        // 프로젝트를 직접 열어 — 콘텐츠의 팩 정보에 원문이 실렸다 (contentHash 밖)
        auto opened = project::openProject(dir, kEngine, catalog());
        REQUIRE(opened.has_value());
        render::MaterialLibrary mats;
        mats.set("eco/rabbit", render::Material{render::kWhiteSprite, 0xFFFFFFFFu}); // 엔진의 것
        const PackMaterialsResult r = loadPackMaterials(mats, *opened->content, dir, nullptr);
        CHECK(r.warnings.empty());
        CHECK(r.packs == 1);
        CHECK(r.materials == 3);
        CHECK(mats.contains("game/hero"));
        CHECK(mats.find("game/flag").color == render::packRgba8(10, 20, 30, 255));
        CHECK(mats.find("eco/rabbit").color == render::packRgba8(1, 2, 3, 255)); // 팩이 이긴다

        // 깨진 materials.json 은 경고 하나 — 나머지는 그대로
        put(dir / "content/ecosystem/presentation/materials.json", R"({"materials": 3})");
        auto broken = project::openProject(dir, kEngine, catalog());
        REQUIRE(broken.has_value());
        render::MaterialLibrary mats2;
        const PackMaterialsResult r2 = loadPackMaterials(mats2, *broken->content, dir, nullptr);
        CHECK(r2.packs == 0);
        CHECK(r2.warnings.size() == 1);
        put(dir / "content/ecosystem/presentation/materials.json",
            std::format(R"({{"materials": {{"game/hero": {{"sprite": "asset:{}"}}}}}})", id));

        // 로컬 세션 — 패키지로 열어도 (푼 폴더가 프로젝트) 팩 머티리얼을 읽는다
        const auto built = package::buildPackage(dir, root.path / "art.sbpkg", kEngine, catalog());
        REQUIRE(built.has_value());
        CHECK(built->index.format == 3);
        for (const std::string& world : {dir.string(), (root.path / "art.sbpkg").string()}) {
            CAPTURE(world);
            render::MaterialLibrary sessionMats;
            NetworkSessionDesc nd;
            nd.mode = NetworkSessionMode::Local;
            nd.world = world;
            nd.contentRoot = SBX_CONTENT_DIR;
            nd.packageCache = root.path / "cache";
            nd.inlineServer = true;
            auto s = NetworkSession::create(nd, sessionMats);
            REQUIRE(s.has_value());
            CHECK(sessionMats.contains("game/hero"));
        }
    }

    TEST_CASE(
        "presentation digest (v0.6 NG-14) — 팩 presentation · 프로젝트 에셋이 바뀌면 바뀌고, contentHash 는 그대로") {
        TempDir root("digest");
        std::string id;
        const fs::path dir = makeGame(root.path, id);
        const auto digestOf = [&] {
            auto opened = project::openProject(dir, kEngine, catalog());
            REQUIRE(opened.has_value());
            return std::pair{scenario::presentationDigest(*opened->content, dir), opened->content->contentHash()};
        };
        const auto [d0, h0] = digestOf();
        CHECK(d0 == digestOf().first);                                   // 같은 입력 = 같은 값
        put(dir / "content/ecosystem/presentation/extra/note.txt", "x"); // presentation 의 아무 파일
        const auto [d1, h1] = digestOf();
        CHECK(d1 != d0);
        CHECK(h1 == h0);
        put(dir / "assets/art/Hero.png", "PNG-other"); // 같은 id · 다른 그림
        const auto [d2, h2] = digestOf();
        CHECK(d2 != d1);
        CHECK(h2 == h0);
        // 프로젝트가 없으면 (원격 클라이언트) 에셋은 빠진다 — 팩 presentation 만
        auto opened = project::openProject(dir, kEngine, catalog());
        REQUIRE(opened.has_value());
        CHECK(scenario::presentationDigest(*opened->content, {}) != d2);
    }
}
