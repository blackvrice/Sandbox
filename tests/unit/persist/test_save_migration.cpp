// 세이브 게임 버전 마이그레이션 (v0.6 NG-04 — core/scenarios/SaveMigration): 계획 (같음 · 더 새 세이브 거절 · 단계 없음
// = 알림 · 체인 · 빠진 · 넘어가는 단계 거절 · SemVer 아님) · 백업 (복사 · 처음 백업 그대로)
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include "core/scenarios/SaveMigration.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

SemVer v(std::string_view s) {
    return parseSemVer(s).value();
}

project::MigrationStep step(std::string_view from, std::string_view to, std::string system) {
    return {v(from), v(to), std::move(system)};
}

} // namespace

TEST_SUITE("persist") {
    TEST_CASE("save migration: 계획 — 같음 · 더 새 세이브 거절 · 단계 없으면 알림 · 체인 · 빠진 · 넘어가는 단계 거절 "
              "(v0.6 NG-04)") {
        const std::vector<project::MigrationStep> steps{step("0.1.0", "0.2.0", "g.m2"), step("0.2.0", "0.4.0", "g.m4"),
                                                        step("0.4.0", "0.5.0", "g.m5")};
        // 같다 — 할 일 없음
        auto same = scenario::planSaveMigration("0.4.0", v("0.4.0"), steps);
        REQUIRE(same.has_value());
        CHECK(same->steps.empty());
        CHECK_FALSE(same->undeclared);
        // 더 새 세이브 — 거절
        auto newer = scenario::planSaveMigration("0.5.0", v("0.4.0"), steps);
        REQUIRE_FALSE(newer.has_value());
        CHECK(newer.error().code == ErrorCode::VersionMismatch);
        CHECK(newer.error().message.find("더 새 게임 버전 0.5.0") != std::string::npos);
        // 단계를 쓰지 않는 게임 — 지금까지처럼 열고 알린다
        auto undeclared = scenario::planSaveMigration("0.1.0", v("0.4.0"), {});
        REQUIRE(undeclared.has_value());
        CHECK(undeclared->undeclared);
        CHECK(undeclared->steps.empty());
        // 체인 0.1 → 0.2 → 0.4
        auto chain = scenario::planSaveMigration("0.1.0", v("0.4.0"), steps);
        REQUIRE(chain.has_value());
        REQUIRE(chain->steps.size() == 2);
        CHECK(chain->steps[0].system == "g.m2");
        CHECK(chain->steps[1].system == "g.m4");
        // 중간에서 시작
        auto mid = scenario::planSaveMigration("0.2.0", v("0.5.0"), steps);
        REQUIRE(mid.has_value());
        CHECK(mid->steps.size() == 2);
        // 빠진 단계 (0.3.0 에서 시작하는 단계가 없다)
        auto missing = scenario::planSaveMigration("0.3.0", v("0.4.0"), steps);
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().code == ErrorCode::NotFound);
        CHECK(missing.error().message.find("0.3.0 에서 시작하는") != std::string::npos);
        // 넘어가는 단계 (0.2 → 0.4 인데 지금 게임이 0.3)
        auto over = scenario::planSaveMigration("0.2.0", v("0.3.0"), steps);
        REQUIRE_FALSE(over.has_value());
        CHECK(over.error().message.find("넘어간다") != std::string::npos);
        // SemVer 아님
        auto bad = scenario::planSaveMigration("v1", v("0.4.0"), steps);
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == ErrorCode::InvalidArgument);
    }

    TEST_CASE("save migration: 백업 — 옆에 <이름>.bak-<버전> 복사, 이미 있으면 처음 것을 그대로") {
        const fs::path root = fs::temp_directory_path() / std::format("sbx_savemig_{:x}", std::random_device{}());
        fs::remove_all(root);
        fs::create_directories(root / "s1" / "chunks");
        std::ofstream(root / "s1" / "world.json") << "first";
        std::ofstream(root / "s1" / "chunks" / "c.bin") << "x";
        auto b = scenario::backupSave(root / "s1", "0.1.0");
        REQUIRE(b.has_value());
        CHECK(*b == root / "s1.bak-0.1.0");
        CHECK(*io::readFile(root / "s1.bak-0.1.0" / "world.json") == "first");
        CHECK(fs::exists(root / "s1.bak-0.1.0" / "chunks" / "c.bin"));
        // 세이브를 고친 뒤 다시 — 처음 백업 그대로 (가장 옛것)
        std::ofstream(root / "s1" / "world.json", std::ios::trunc) << "second";
        auto again = scenario::backupSave(root / "s1", "0.1.0");
        REQUIRE(again.has_value());
        CHECK(*io::readFile(root / "s1.bak-0.1.0" / "world.json") == "first");
        // 없는 세이브 — 오류, 반쯤 만든 백업 없음
        CHECK_FALSE(scenario::backupSave(root / "none", "0.1.0").has_value());
        CHECK_FALSE(fs::exists(root / "none.bak-0.1.0"));
        std::error_code ec;
        fs::remove_all(root, ec);
    }
}
