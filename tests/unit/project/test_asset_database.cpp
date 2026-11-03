// 프로젝트 에셋 데이터베이스 (v0.6 NG-12 — core/asset/AssetDatabase, ADR-0090 · ADR-0097 3): .meta 형식, 같은 id 의
// 사본 (같은 내용 = 하나), 충돌 (다른 내용 = 오류 A3 — 고르지 않음), .meta 없음 · 깨짐 (A2 · A1 — 경로 해시로 돌아가지
// 않음) · 가져오기 · 새 id, 짝 없는 .meta (A4), 경로 · 링크 (A5), 프로젝트 검사에 붙음
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include "core/asset/AssetDatabase.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/project/GameProject.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_asset_{}_{:x}", tag, std::random_device{}());
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

void write(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

std::string meta(std::string_view id) {
    return std::format(R"({{"id": "{}", "importer": "image@1", "settings": {{}}}})", id);
}

constexpr std::string_view kIdA = "0123456789abcdef0123456789abcdef";
constexpr std::string_view kIdB = "fedcba9876543210fedcba9876543210";

// 1, 2, 3 … 을 32 글자로 — 시험용 고정 생성기
asset::IdGenerator counter(u32 start = 1) {
    return [n = start]() mutable { return std::format("{:032x}", n++); };
}

bool hasRule(const asset::AssetScan& s, std::string_view rule, std::string_view file = {}) {
    for (const auto& i : s.issues) {
        if (i.rule == rule && (file.empty() || i.file == file)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("asset: .meta 형식 — 정규형 왕복, 모양이 틀리면 거절") {
        CHECK(asset::isAssetId(kIdA));
        CHECK_FALSE(asset::isAssetId("0123456789ABCDEF0123456789ABCDEF")); // 소문자만
        CHECK_FALSE(asset::isAssetId("0123"));
        const auto m = asset::parseAssetMeta(R"({"settings": {"b": 1, "a": 2}, "importer": "image@1", "id": ")" +
                                             std::string(kIdA) + "\"}");
        REQUIRE(m.has_value());
        CHECK(m->id == kIdA);
        CHECK(m->importer == "image@1");
        CHECK(m->settings == R"({"a":2,"b":1})"); // 키 순
        const std::string text = asset::writeAssetMeta(*m);
        CHECK(text ==
              std::format(
                  "{{\n  \"id\": \"{}\",\n  \"importer\": \"image@1\",\n  \"settings\": {{\"a\":2,\"b\":1}}\n}}\n",
                  kIdA));
        const auto again = asset::parseAssetMeta(text);
        REQUIRE(again.has_value());
        CHECK(asset::writeAssetMeta(*again) == text);
        for (const std::string& bad :
             {std::string("[]"), std::string("{"), meta("xyz"), std::format(R"({{"id": "{}"}})", kIdA),
              std::format(R"({{"id": "{}", "importer": "image@1", "settings": []}})", kIdA),
              std::format(R"({{"id": "{}", "importer": "image@1", "guid": 1}})", kIdA)}) {
            CAPTURE(bad);
            CHECK_FALSE(asset::parseAssetMeta(bad).has_value());
        }
        CHECK(asset::importerFor("assets/a/Hero.PNG") == "image@1");
        CHECK(asset::importerFor("assets/a/notes") == "file@1");
        CHECK(asset::isAssetId(asset::randomAssetId()));
        CHECK(asset::randomAssetId() != asset::randomAssetId());
    }

    TEST_CASE("asset: assets/ 가 없으면 빈 결과 · 같은 id 같은 내용은 하나 (사본)") {
        TempDir dir("copy");
        CHECK(asset::scanProjectAssets(dir.path).assets.empty());
        CHECK(asset::scanProjectAssets(dir.path).ok());

        write(dir.path / "assets/art/hero.png", "PNG-hero");
        write(dir.path / "assets/art/hero.png.meta", meta(kIdA));
        write(dir.path / "assets/backup/hero.png", "PNG-hero"); // 같은 바이트 · 같은 .meta 를 복사
        write(dir.path / "assets/backup/hero.png.meta", meta(kIdA));
        const auto s = asset::scanProjectAssets(dir.path);
        INFO(s.issues.size());
        CHECK(s.ok());
        CHECK(s.issues.empty());
        REQUIRE(s.assets.size() == 1);
        const asset::AssetEntry* e = s.find(kIdA);
        REQUIRE(e != nullptr);
        CHECK(e->path == "assets/art/hero.png"); // 바이트 순 첫째
        CHECK(e->size == 8);
        REQUIRE(e->copies.size() == 1);
        CHECK(e->copies[0] == "assets/backup/hero.png");
        CHECK(s.find(kIdB) == nullptr);
    }

    TEST_CASE("asset: 같은 id 다른 내용은 오류 A3 — 어느 쪽도 고르지 않는다, 복제본에 새 id 로 고친다") {
        TempDir dir("clash");
        write(dir.path / "assets/hero.png", "PNG-hero");
        write(dir.path / "assets/hero.png.meta", meta(kIdA));
        write(dir.path / "assets/hero_red.png", "PNG-red"); // 복사한 뒤 고친 그림 — .meta 도 함께 복사됐다
        write(dir.path / "assets/hero_red.png.meta", meta(kIdA));
        auto s = asset::scanProjectAssets(dir.path);
        CHECK_FALSE(s.ok());
        CHECK(hasRule(s, "A3", "assets/hero.png"));
        CHECK(s.find(kIdA) == nullptr); // 고르지 않는다
        bool named = false;
        for (const auto& i : s.issues) {
            named = named || (i.message.find("assets/hero.png") != std::string::npos &&
                              i.message.find("assets/hero_red.png") != std::string::npos);
        }
        CHECK(named);
        // 가져오기는 오류가 있으면 쓰지 않는다
        CHECK_FALSE(asset::importAssets(dir.path, counter()).has_value());

        const auto fresh = asset::assignNewAssetId(dir.path, "assets/hero_red.png", counter(7));
        REQUIRE(fresh.has_value());
        CHECK(*fresh == std::format("{:032x}", 7));
        s = asset::scanProjectAssets(dir.path);
        CHECK(s.ok());
        REQUIRE(s.find(kIdA) != nullptr);
        CHECK(s.find(kIdA)->path == "assets/hero.png");
        REQUIRE(s.find(*fresh) != nullptr);
        CHECK(s.find(*fresh)->path == "assets/hero_red.png");
        // importer · settings 는 그대로
        const auto kept = asset::parseAssetMeta(*io::readFile(dir.path / "assets/hero_red.png.meta"));
        REQUIRE(kept.has_value());
        CHECK(kept->importer == "image@1");
        // 잘못된 대상
        CHECK_FALSE(asset::assignNewAssetId(dir.path, "assets/none.png", counter()).has_value());
        CHECK_FALSE(asset::assignNewAssetId(dir.path, "assets/hero.png.meta", counter()).has_value());
        CHECK_FALSE(asset::assignNewAssetId(dir.path, "../outside.png", counter()).has_value());
        CHECK_FALSE(asset::assignNewAssetId(dir.path, "content/x.png", counter()).has_value());
    }

    TEST_CASE("asset: .meta 없음 (A2) · 깨짐 (A1) 은 id 없는 에셋 — 가져오기가 겹치지 않는 새 id 를 쓴다") {
        TempDir dir("import");
        write(dir.path / "assets/a.png", "A");
        write(dir.path / "assets/b.png", "B");
        write(dir.path / "assets/b.png.meta", "{ broken");
        write(dir.path / "assets/c.png", "C");
        write(dir.path / "assets/c.png.meta", meta(std::format("{:032x}", 1))); // 생성기의 첫 값과 같은 id
        auto s = asset::scanProjectAssets(dir.path);
        CHECK(s.ok()); // 경고만
        CHECK(hasRule(s, "A2", "assets/a.png"));
        CHECK(hasRule(s, "A1", "assets/b.png.meta"));
        CHECK(s.unidentified == std::vector<std::string>{"assets/a.png", "assets/b.png"});
        CHECK(s.assets.size() == 1);

        const auto written = asset::importAssets(dir.path, counter(1));
        REQUIRE(written.has_value());
        CHECK(*written == 2);
        s = asset::scanProjectAssets(dir.path);
        CHECK(s.issues.empty());
        CHECK(s.unidentified.empty());
        REQUIRE(s.assets.size() == 3);
        // 1 은 c 가 쓰고 있어 건너뛰었다 — a = 2, b = 3
        CHECK(s.find(std::format("{:032x}", 2))->path == "assets/a.png");
        CHECK(s.find(std::format("{:032x}", 3))->path == "assets/b.png");
        // 다시 가져오면 쓸 것이 없다
        CHECK(*asset::importAssets(dir.path, counter(100)) == 0);
        // 늘 같은 값만 주는 생성기는 오류 (겹치는 id 를 쓰지 않는다)
        write(dir.path / "assets/d.png", "D");
        CHECK_FALSE(asset::importAssets(dir.path, [] { return std::format("{:032x}", 2); }).has_value());
        CHECK_FALSE(fs::exists(dir.path / "assets/d.png.meta"));
    }

    TEST_CASE("asset: 짝 없는 .meta (A4) · 경로 규칙 (A5) · 대소문자 · 링크") {
        TempDir dir("paths");
        write(dir.path / "assets/gone.png.meta", meta(kIdB)); // 원본을 밖에서 지웠다
        write(dir.path / "assets/ok.png", "OK");
        auto s = asset::scanProjectAssets(dir.path);
        CHECK(hasRule(s, "A4", "assets/gone.png.meta"));
        CHECK(s.ok());
        // 그 id 는 새 id 로 쓰지 않는다 — 원본이 돌아올 수 있다
        REQUIRE(asset::importAssets(dir.path, [n = 0]() mutable {
                    return n++ == 0 ? std::string(kIdB) : std::string(kIdA);
                }).has_value());
        CHECK(asset::parseAssetMeta(*io::readFile(dir.path / "assets/ok.png.meta"))->id == kIdA);

        write(dir.path / "assets/my hero.png", "X"); // 공백 — P4 경로 규칙
        s = asset::scanProjectAssets(dir.path);
        CHECK_FALSE(s.ok());
        CHECK(hasRule(s, "A5", "assets/my hero.png"));
        fs::remove(dir.path / "assets/my hero.png");

        // 대소문자 · 링크는 만들 수 있는 파일 시스템에서만
        write(dir.path / "assets/Ok.png", "other");
        if (*io::readFile(dir.path / "assets/ok.png") != "other") {
            CHECK(hasRule(asset::scanProjectAssets(dir.path), "A5", "assets"));
            fs::remove(dir.path / "assets/Ok.png");
        } else {
            MESSAGE("대소문자를 가르지 않는 파일 시스템 — 대소문자 갈래는 건너뛴다");
            write(dir.path / "assets/ok.png", "OK");
        }
        write(dir.path / "secret.txt", "열쇠");
        std::error_code ec;
        fs::create_symlink(dir.path / "secret.txt", dir.path / "assets/link.png", ec);
        if (ec) {
            MESSAGE("심볼릭 링크를 만들 수 없어 건너뛴다: " << ec.message());
        } else {
            s = asset::scanProjectAssets(dir.path);
            CHECK(hasRule(s, "A5", "assets/link.png"));
            CHECK_FALSE(s.ok());
        }
    }

    TEST_CASE("asset: 프로젝트 검사에 붙는다 — 에셋 수 · A3 는 검사 오류") {
        TempDir dir("project");
        const ecs::ComponentCatalog catalog = [] {
            ecs::ComponentCatalog c;
            (void)comp::registerCoreComponents(c);
            return c;
        }();
        project::NewProjectOptions o;
        o.gameId = "studio.assets";
        REQUIRE(project::createProject(dir.path / "Game", o, SemVer{0, 1, 0, {}, {}}, catalog).has_value());
        const fs::path game = dir.path / "Game";
        auto r = project::validateProject(game, SemVer{0, 1, 0, {}, {}}, catalog);
        CHECK(r.ok());
        CHECK_FALSE(r.hasAssets);

        write(game / "assets/hero.png", "PNG");
        write(game / "assets/hero.png.meta", meta(kIdA));
        write(game / "assets/raw.png", "RAW");
        r = project::validateProject(game, SemVer{0, 1, 0, {}, {}}, catalog);
        CHECK(r.ok()); // A2 는 경고
        CHECK(r.hasAssets);
        CHECK(r.assets == 1);
        CHECK(r.unidentifiedAssets == 1);

        write(game / "assets/copy.png", "OTHER");
        write(game / "assets/copy.png.meta", meta(kIdA));
        r = project::validateProject(game, SemVer{0, 1, 0, {}, {}}, catalog);
        CHECK_FALSE(r.ok());
    }
}
