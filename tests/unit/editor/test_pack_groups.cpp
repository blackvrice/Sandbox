// 팩별 묶음 (v0.4 M4-T3): 항목 id 의 팩 → 출처 (프로젝트 content/ · 설치한 패키지 .sandbox/packages · 그 밖) · 머리 글
// · 차례 (프로젝트 → 설치 → 팩 → 기타, 묶음 안은 받은 차례).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "editor/PackGroups.hpp"

using namespace sbx;
using namespace sbx::editor;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

void put(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

void pack(const fs::path& dir, std::string_view id, std::string_view extra) {
    put(dir / "pack.json",
        std::string(R"({"id": ")") + std::string(id) + "\", \"version\": \"1.0.0\"" + std::string(extra) + "}");
    put(dir / "prefabs/a.json",
        std::string(R"({"id": ")") + std::string(id) + R"(.a", "components": {"core.transform": {}}})");
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("pack groups: 프로젝트 → 설치한 패키지 → 기타, 머리 글, 묶음 안은 받은 차례") {
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "pack-groups";
        std::error_code ec;
        fs::remove_all(root, ec);
        pack(root / "content/eco", "eco", "");
        pack(root / "content/arena", "arena", "");
        pack(root / ".sandbox/packages/inv", "inv", R"(, "license": "MIT")");
        const std::vector<content::ContentRoot> roots{{root / "content", "content/"},
                                                      {root / ".sandbox/packages", ".sandbox/packages/"}};
        const std::vector<std::string> ids{"eco", "arena", "inv"};
        const content::ContentLoadResult r = content::ContentLoader::load(roots, ids, engine());
        REQUIRE(r.ok());

        const std::vector<std::string_view> items{"inv.a", "eco.a", "zzz.x", "arena.a", "eco.b", "nodot"};
        const std::vector<PackGroup> g = groupByPack(r.db, items);
        REQUIRE(g.size() == 4);
        CHECK(g[0].pack == "arena");
        CHECK(g[0].origin == PackOrigin::Project);
        CHECK(g[0].label == "arena · 프로젝트");
        CHECK(g[0].items == std::vector<usize>{3});
        CHECK(g[1].pack == "eco");
        CHECK(g[1].items == std::vector<usize>{1, 4});
        CHECK(g[2].pack == "inv");
        CHECK(g[2].origin == PackOrigin::Installed);
        CHECK(g[2].label == "inv 1.0.0 · MIT · 설치한 패키지");
        CHECK(g[3].origin == PackOrigin::Unknown);
        CHECK(g[3].label == "기타");
        CHECK(g[3].items == std::vector<usize>{2, 5});

        // 프로젝트가 아닌 콘텐츠 (시나리오 · 세이브 폴더 — 접두사 없음) 는 팩 이름만, 한 팩이면 묶음 하나
        const content::ContentLoadResult plain =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"eco"}, engine());
        REQUIRE(plain.ok());
        const std::vector<std::string_view> one{"eco.a"};
        const std::vector<PackGroup> p = groupByPack(plain.db, one);
        REQUIRE(p.size() == 1);
        CHECK(p[0].origin == PackOrigin::Pack);
        CHECK(p[0].label == "eco");
        CHECK(groupByPack(plain.db, {}).empty());
        fs::remove_all(root, ec);
    }
}
