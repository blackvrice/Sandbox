// 게임 패키지 (v0.2 M5-T2, ADR-0054): 빌드 (담는 것 · 빼는 것 · 같은 프로젝트 = 같은 바이트), 검증 (고친 · 자른 ·
// 머리가 다른 패키지 거절), 캐시에 풀기 (다시 쓰기 · 고친 캐시는 다시 풀기), --world <패키지> (openWorldSource), 검사
// 오류 프로젝트는 빌드 거절.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/package/GamePackage.hpp"
#include "core/package/PackSource.hpp"
#include "core/project/GameProject.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_pkg_{}_{:x}", tag, std::random_device{}());
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

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const SemVer kEngine{0, 1, 0, {}, {}};

fs::path makeDemo(const fs::path& root) {
    const fs::path dir = root / "Demo";
    project::NewProjectOptions o;
    o.gameId = "studio.demo";
    o.fromScenario = "ecosystem_small";
    o.contentRoot = SBX_CONTENT_DIR;
    o.gameMode = "eco.protect";
    auto made = project::createProject(dir, o, kEngine, catalog());
    REQUIRE_MESSAGE(made.has_value(), made.error().describe());
    return dir;
}

bool hasFile(const package::PackageIndex& idx, std::string_view path) {
    return std::any_of(idx.files.begin(), idx.files.end(),
                       [&](const package::PackageEntry& e) { return e.path == path; });
}

void flipByte(const fs::path& file, std::streamoff fromEnd) {
    std::string bytes = *io::readFile(file);
    bytes[bytes.size() - static_cast<usize>(fromEnd)] ^= 0x5A;
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

} // namespace

TEST_SUITE("project") {

    TEST_CASE("package: 빌드 — 매니페스트 · 팩 (presentation 포함) · 시작 월드만, 같은 프로젝트면 같은 바이트") {
        TempDir root("build");
        const fs::path dir = makeDemo(root.path);
        // 다른 세이브 · 쓰레기는 담지 않는다
        fs::create_directories(dir / "worlds" / "other");
        std::ofstream(dir / "worlds" / "other" / "world.json") << "{}";
        std::ofstream(dir / "notes.txt") << "메모";

        const auto a = package::buildPackage(dir, {}, kEngine, catalog());
        REQUIRE_MESSAGE(a.has_value(), a.error().describe());
        CHECK(a->file == dir / "build" / "studio.demo-0.1.0.sbpkg");
        const package::PackageIndex& idx = a->index;
        CHECK(idx.gameId == "studio.demo");
        CHECK(idx.version == "0.1.0");
        CHECK(idx.entryWorld == "worlds/main");
        CHECK(idx.gameMode == "eco.protect");
        CHECK(hasFile(idx, "sandbox.project.json"));
        CHECK(hasFile(idx, "content/ecosystem/pack.json"));
        CHECK(hasFile(idx, "content/ecosystem/presentation/input.json"));
        CHECK(hasFile(idx, "content/ecosystem/triggers/rabbit_lost.json"));
        CHECK(hasFile(idx, "worlds/main/world.json"));
        CHECK_FALSE(hasFile(idx, "worlds/other/world.json"));
        CHECK_FALSE(hasFile(idx, "notes.txt"));
        CHECK(idx.digest == package::packageDigest(idx.files));
        CHECK(std::is_sorted(idx.files.begin(), idx.files.end(),
                             [](const auto& x, const auto& y) { return x.path < y.path; }));

        // 두 번째 빌드 (첫 빌드의 build/ 가 있어도 담지 않는다) — 같은 바이트
        const auto b = package::buildPackage(dir, root.path / "again.sbpkg", kEngine, catalog());
        REQUIRE(b.has_value());
        CHECK(b->index.digest == idx.digest);
        CHECK(*io::readFile(a->file) == *io::readFile(b->file));

        const auto read = package::readPackageIndex(a->file);
        REQUIRE(read.has_value());
        CHECK(read->digest == idx.digest);
        CHECK(read->files.size() == idx.files.size());
        CHECK(package::verifyPackage(a->file).has_value());
    }

    TEST_CASE("package: 고친 · 자른 · 머리가 다른 · index 가 틀린 패키지는 열지 않는다") {
        TempDir root("tamper");
        const auto built = package::buildPackage(makeDemo(root.path), root.path / "g.sbpkg", kEngine, catalog());
        REQUIRE(built.has_value());
        const std::string good = *io::readFile(built->file);
        const auto variant = [&](std::string_view tag, auto&& mutate) {
            const fs::path f = root.path / std::format("{}.sbpkg", tag);
            std::string bytes = good;
            mutate(bytes);
            std::ofstream(f, std::ios::binary | std::ios::trunc) << bytes;
            return f;
        };
        // 파일 내용 한 바이트 — 색인은 맞아도 그 파일의 sha256 이 다르다
        const fs::path flipped = variant("flip", [](std::string& b) { b[b.size() - 5] ^= 0x01; });
        CHECK(package::readPackageIndex(flipped).has_value()); // 모양은 맞다
        const auto v = package::verifyPackage(flipped);
        REQUIRE_FALSE(v.has_value());
        CHECK(v.error().message.find("sha256 과 다르다") != std::string::npos);
        CHECK_FALSE(package::extractPackage(flipped, root.path / "cache").has_value());
        // 잘림 · 덧붙임 · 머리
        CHECK(package::readPackageIndex(variant("cut", [](std::string& b) { b.pop_back(); }))
                  .error()
                  .message.find("길이") != std::string::npos);
        CHECK_FALSE(package::readPackageIndex(variant("tail", [](std::string& b) { b += "x"; })).has_value());
        CHECK(package::readPackageIndex(variant("magic", [](std::string& b) { b[0] = 'X'; }))
                  .error()
                  .message.find("게임 패키지") != std::string::npos);
        CHECK(package::readPackageIndex(variant("format", [](std::string& b) { b[8] = 3; })).error().code ==
              ErrorCode::VersionMismatch);
        CHECK(package::readPackageIndex(variant("format0", [](std::string& b) { b[8] = 0; })).error().code ==
              ErrorCode::VersionMismatch);
        // index 의 digest 를 바꾸면 (파일은 그대로) — digest 가 목록과 다르다
        const fs::path digest = variant("digest", [](std::string& b) {
            const usize at = b.find("\"digest\":\"") + 10;
            b[at] = b[at] == 'a' ? 'b' : 'a';
        });
        CHECK(package::readPackageIndex(digest).error().message.find("digest") != std::string::npos);
        CHECK_FALSE(package::readPackageIndex(root.path / "nope.sbpkg").has_value());
    }

    TEST_CASE("package: 라이선스 요약 (형식 2 — v0.4 M5-T2) — 팩마다 version · license · 팩 digest, 고친 요약은 거절, "
              "형식 1 도 읽는다") {
        TempDir root("sbom");
        const fs::path dir = makeDemo(root.path);
        const auto built = package::buildPackage(dir, root.path / "g.sbpkg", kEngine, catalog());
        REQUIRE(built.has_value());
        CHECK(built->index.format == 2);
        REQUIRE(built->index.packs.size() == 1);
        const package::PackageIndex::Pack& eco = built->index.packs[0];
        CHECK(eco.id == "eco");
        CHECK(eco.version == "0.1.0");
        CHECK(eco.license.empty()); // 저장소의 eco 팩은 라이선스를 적지 않았다
        CHECK(eco.path == "content/ecosystem");
        CHECK(eco.digest == package::snapshotPack(dir / "content/ecosystem")->digest); // PackSource 와 같은 규칙
        const auto read = package::verifyPackage(built->file);
        REQUIRE(read.has_value());
        CHECK(read->packs == built->index.packs);

        const std::string good = *io::readFile(built->file);
        const auto variant = [&](std::string_view tag, auto&& mutate) {
            const fs::path f = root.path / std::format("{}.sbpkg", tag);
            std::string bytes = good;
            mutate(bytes);
            std::ofstream(f, std::ios::binary | std::ios::trunc) << bytes;
            return f;
        };
        // 요약의 version 만 고친다 (파일은 그대로) — 모양은 맞지만 pack.json 과 다르다
        const fs::path lied = variant("lied", [](std::string& b) {
            const std::string key = R"("path":"content/ecosystem","version":"0.1.0")";
            const usize at = b.find(key);
            REQUIRE(at != std::string::npos);
            b[at + key.size() - 2] = '9';
        });
        CHECK(package::readPackageIndex(lied).has_value());
        CHECK(package::verifyPackage(lied).error().message.find("라이선스 요약") != std::string::npos);
        CHECK_FALSE(package::extractPackage(lied, root.path / "cache").has_value());
        // 요약의 팩 digest 를 고친다 — 파일 목록에서 다시 계산한 것과 다르다
        const fs::path badDigest = variant("pack-digest", [&](std::string& b) {
            const usize at = b.find(eco.digest);
            REQUIRE(at != std::string::npos);
            b[at] = b[at] == 'a' ? 'b' : 'a';
        });
        CHECK(package::readPackageIndex(badDigest).error().message.find("팩 'eco' 의 digest") != std::string::npos);

        // 형식 1 (v0.2 · v0.3 이 만든 패키지 — packs 없음) 도 읽고 푼다
        const fs::path v1 = variant("v1", [](std::string& b) {
            u64 n = 0;
            for (int i = 0; i < 8; ++i) {
                n |= static_cast<u64>(static_cast<u8>(b[12 + i])) << (8 * i);
            }
            auto j = nlohmann::json::parse(b.substr(20, static_cast<usize>(n)));
            j.erase("packs");
            j["format"] = 1;
            const std::string index = j.dump();
            std::string out = b.substr(0, 8);
            for (int i = 0; i < 4; ++i) {
                out += static_cast<char>((1u >> (8 * i)) & 0xFF);
            }
            for (int i = 0; i < 8; ++i) {
                out += static_cast<char>((static_cast<u64>(index.size()) >> (8 * i)) & 0xFF);
            }
            b = out + index + b.substr(20 + static_cast<usize>(n));
        });
        const auto old = package::verifyPackage(v1);
        REQUIRE_MESSAGE(old.has_value(), old.error().describe());
        CHECK(old->format == 1);
        CHECK(old->packs.empty());
        CHECK(old->digest == built->index.digest);
        CHECK(package::extractPackage(v1, root.path / "cache-v1").has_value());
    }

    TEST_CASE("package: 캐시에 풀기 — 같은 digest 는 다시 쓴다, 캐시를 고치면 다시 푼다, --world <패키지>") {
        TempDir root("extract");
        const auto built = package::buildPackage(makeDemo(root.path), root.path / "g.sbpkg", kEngine, catalog());
        REQUIRE(built.has_value());
        const fs::path cache = root.path / "cache";
        const auto first = package::extractPackage(built->file, cache);
        REQUIRE_MESSAGE(first.has_value(), first.error().describe());
        CHECK_FALSE(first->reused);
        CHECK(first->dir == cache / built->index.digest.substr(0, 16));
        CHECK(project::isProjectFolder(first->dir));
        CHECK(fs::exists(first->dir / "worlds" / "main" / "world.json"));
        const auto second = package::extractPackage(built->file, cache);
        REQUIRE(second.has_value());
        CHECK(second->reused);
        // 풀어 둔 파일을 누가 고쳤다 — 다시 푼다
        std::ofstream(first->dir / "sandbox.project.json", std::ios::trunc) << "{}";
        const auto third = package::extractPackage(built->file, cache);
        REQUIRE(third.has_value());
        CHECK_FALSE(third->reused);
        CHECK(*io::readFile(third->dir / "sandbox.project.json") != "{}");

        // --world <패키지>: 프로젝트처럼 연다 (game · package · projectDir)
        CHECK(package::isPackageFile(built->file));
        CHECK_FALSE(package::isPackageFile(root.path / "Demo"));
        CHECK(package::packageSavesRoot(built->file) == root.path / "g-saves");
        auto src = scenario::openWorldSource(built->file.string(), {}, 1, catalog());
        REQUIRE_MESSAGE(src.has_value(), src.error().describe());
        REQUIRE(src->game);
        CHECK(src->game->gameId == "studio.demo");
        REQUIRE(src->package);
        CHECK(src->package->digest == built->index.digest);
        CHECK(project::isProjectFolder(src->projectDir));
        CHECK(src->runner->world().content().findMode("eco.protect") != nullptr);

        // 고친 패키지는 --world 도 거절
        flipByte(built->file, 3);
        CHECK_FALSE(scenario::openWorldSource(built->file.string(), {}, 1, catalog()).has_value());
    }

    TEST_CASE("package: 검사 오류가 있는 프로젝트는 빌드하지 않는다") {
        TempDir root("invalid");
        const fs::path dir = makeDemo(root.path);
        std::ofstream(dir / "content" / "ecosystem" / "modes" / "broken.json") << R"({"id": "eco.broken"})";
        const auto r = package::buildPackage(dir, {}, kEngine, catalog());
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::ValidationFailed);
        CHECK_FALSE(fs::exists(dir / "build"));
    }
}
