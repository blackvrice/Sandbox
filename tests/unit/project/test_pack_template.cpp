// 패키지 틀 · 프로젝트 자동 플레이 목록 (v0.4 M4-T2): createPack — 검사를 통과한 틀 (컴포넌트 · 함수 · 시스템 · Prefab,
// digest), 다른 팩이 "@<팩>/main" 으로 가져와 쓴다, 거절 (팩 id · core · 라이선스 · 비지 않은 폴더 — 아무것도 남기지
// 않는다). projectPlayScripts — playthrough.json 다음 tests/*.json 이름 순.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/package/PackSource.hpp"
#include "core/package/PackTemplate.hpp"
#include "core/scenarios/PlayScript.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
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

struct Dir {
    explicit Dir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "pack-template" / name) {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    Dir(const Dir&) = delete;
    Dir& operator=(const Dir&) = delete;
    fs::path path;
};

void write(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("new --pack: 검사를 통과한 틀 — 다른 팩이 @<팩>/main 으로 가져와 쓴다") {
        Dir d("ok");
        auto made = package::createPack(d.path / "packs" / "kit", "kit", "MIT", engine());
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(made->id == "kit");
        CHECK(made->license == "MIT");
        CHECK(made->files == 4);
        CHECK(made->digest == package::snapshotPack(d.path / "packs" / "kit")->digest);
        CHECK_FALSE(fs::exists(d.path / "packs" / "kit.sbx-new"));
        CHECK(fs::is_regular_file(d.path / "packs/kit/scripts/main.ts"));

        // 게임 팩이 kit 에 의존하고 그 컴포넌트 · 함수를 쓴다 (ADR-0075)
        write(d.path / "packs/arena/pack.json", R"({"id": "arena", "version": "0.1.0", "requires": ["kit"]})");
        write(d.path / "packs/arena/scripts/twice.ts", R"(import { defineSystem } from "sandbox"
import { Counter, bump } from "@kit/main"
export default defineSystem({
  id: "arena.twice",
  onTick(ctx) {
    for (const e of ctx.query(Counter)) {
      const c = ctx.read(e, Counter)
      if (c !== null) { c.value = bump(c.value, 2); ctx.write(e, Counter, c) }
    }
  },
})
)");
        const std::vector<std::string> ids{"arena"};
        const content::ContentLoadResult loaded = content::ContentLoader::load(d.path / "packs", ids, engine());
        std::string issues;
        for (const auto& i : loaded.issues) {
            issues += i.describe() + "\n";
        }
        REQUIRE_MESSAGE(loaded.ok(), issues);
        CHECK(loaded.issues.empty());
        CHECK(loaded.db.findPrefab("kit.thing") != nullptr);
        REQUIRE(loaded.db.scriptPrograms());
        CHECK(loaded.db.scriptPrograms()->systemCount(script::api::HostDomain::Server) == 2); // kit.tick · arena.twice
    }

    TEST_CASE("new --pack: 거절 — 팩 id · core · 라이선스 · 비지 않은 폴더, 아무것도 남기지 않는다") {
        Dir d("reject");
        CHECK(package::createPack(d.path / "a", "Kit", {}, engine()).error().code == ErrorCode::InvalidArgument);
        CHECK(package::createPack(d.path / "a", "core", {}, engine()).error().code == ErrorCode::InvalidArgument);
        CHECK(package::createPack(d.path / "a", "kit", "not a license!", engine()).error().code ==
              ErrorCode::InvalidArgument);
        CHECK_FALSE(fs::exists(d.path / "a"));
        write(d.path / "full/keep.txt", "x");
        CHECK(package::createPack(d.path / "full", "kit", {}, engine()).error().code == ErrorCode::AlreadyExists);
        CHECK(io::readFile(d.path / "full/keep.txt").value() == "x");
        // 빈 폴더는 된다, 라이선스를 적지 않으면 LicenseRef-proprietary
        fs::create_directories(d.path / "empty");
        auto made = package::createPack(d.path / "empty", "kit", {}, engine());
        REQUIRE(made.has_value());
        CHECK(made->license == package::kDefaultPackLicense);
        CHECK(io::readFile(d.path / "empty/pack.json")->find("LicenseRef-proprietary") != std::string::npos);
    }

    TEST_CASE("sbx test 의 목록: playthrough.json 다음 tests/*.json 이름 순 (하위 폴더 · 다른 확장자 빼고)") {
        Dir d("scripts");
        CHECK(scenario::projectPlayScripts(d.path).empty());
        write(d.path / "tests/b.json", "{}");
        write(d.path / "tests/a.json", "{}");
        write(d.path / "tests/notes.txt", "");
        write(d.path / "tests/deep/c.json", "{}");
        write(d.path / "playthrough.json", "{}");
        const auto list = scenario::projectPlayScripts(d.path);
        REQUIRE(list.size() == 3);
        CHECK(list[0].filename() == "playthrough.json");
        CHECK(list[1].filename() == "a.json");
        CHECK(list[2].filename() == "b.json");
        scenario::PlayScript s;
        CHECK(scenario::playStepLimit(s) == 2'000'000);
        s.maxTicks = 100;
        CHECK(scenario::playStepLimit(s) == 1'200);
    }
}
