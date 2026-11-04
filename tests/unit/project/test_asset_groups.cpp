// Asset Group (v0.6 NG-02 — core/asset/AssetGroups): groups.json 모양 (A7) · glob · 라벨 (.meta labels) · 그룹 풀기 ·
// 크기 보고 · 어디서도 부르지 않는 에셋 (A8) · groups.json 은 에셋이 아니다
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include "core/asset/AssetDatabase.hpp"
#include "core/asset/AssetGroups.hpp"

using namespace sbx;
namespace fs = std::filesystem;
using Severity = content::ContentIssue::Severity;

namespace {

void put(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

std::string metaWith(unsigned n, std::string_view labels) {
    return std::format(R"({{"id": "{:032x}", "importer": "image@1", "labels": [{}]}})", n, labels);
}

bool mentions(const std::vector<content::ContentIssue>& issues, std::string_view text) {
    return std::any_of(issues.begin(), issues.end(),
                       [&](const content::ContentIssue& i) { return i.message.find(text) != std::string::npos; });
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("asset groups: .meta labels — 정렬 · 왕복, 라벨 없는 .meta 의 바이트는 그대로, 틀린 라벨 거절") {
        const auto m = asset::parseAssetMeta(
            R"({"id": "0123456789abcdef0123456789abcdef", "importer": "image@1", "labels": ["ui", "enemy"]})");
        REQUIRE(m.has_value());
        CHECK(m->labels == std::vector<std::string>{"enemy", "ui"});
        const std::string text = asset::writeAssetMeta(*m);
        CHECK(text.find(R"("labels": ["enemy","ui"])") != std::string::npos);
        CHECK(asset::writeAssetMeta(*asset::parseAssetMeta(text)) == text);
        const asset::AssetMeta plain{"0123456789abcdef0123456789abcdef", "image@1", "{}", {}};
        CHECK(asset::writeAssetMeta(plain) == "{\n  \"id\": \"0123456789abcdef0123456789abcdef\",\n  \"importer\": "
                                              "\"image@1\",\n  \"settings\": {}\n}\n");
        for (const std::string_view bad :
             {R"(["UI"])", R"(["a b"])", R"([""])", R"(["x", "x"])", R"("ui")", R"([1])"}) {
            CHECK_FALSE(
                asset::parseAssetMeta(
                    std::format(R"({{"id": "0123456789abcdef0123456789abcdef", "importer": "image@1", "labels": {}}})",
                                bad))
                    .has_value());
        }
        CHECK(asset::isAssetLabel("enemy_2-a"));
        CHECK_FALSE(asset::isAssetLabel(std::string(33, 'a')));
    }

    TEST_CASE("asset groups: glob — * 는 폴더를 넘지 않고 ** 는 넘는다, ? 한 글자, assets/ 기준") {
        CHECK(asset::matchAssetGlob("enemies/*.png", "assets/enemies/a.png"));
        CHECK_FALSE(asset::matchAssetGlob("enemies/*.png", "assets/enemies/boss/a.png"));
        CHECK(asset::matchAssetGlob("enemies/**/*.png", "assets/enemies/boss/a.png"));
        CHECK(asset::matchAssetGlob("enemies/**/*.png", "assets/enemies/a.png")); // ** = 폴더 0 개도
        CHECK(asset::matchAssetGlob("**", "assets/x/y/z.txt"));
        CHECK(asset::matchAssetGlob("enemy_?.png", "assets/enemy_1.png"));
        CHECK_FALSE(asset::matchAssetGlob("enemy_?.png", "assets/enemy_10.png"));
        CHECK_FALSE(asset::matchAssetGlob("*.png", "assets/a.PNG")); // 대소문자 그대로
        CHECK_FALSE(asset::matchAssetGlob("*.png", "content/a.png"));
    }

    TEST_CASE("asset groups: groups.json 모양 (A7) — 키 · 이름 · 겹침 · include 의 glob · 라벨") {
        const auto good = asset::parseAssetGroups(
            R"({"groups": [{"name": "enemies", "include": ["enemies/**/*.png", "label:enemy"]}, {"name": "ui", "include": ["label:ui"]}]})");
        REQUIRE(good.ok());
        REQUIRE(good.groups.size() == 2);
        CHECK(good.groups[0].name == "enemies");
        CHECK(good.groups[0].include == std::vector<std::string>{"enemies/**/*.png", "label:enemy"});
        const auto bad = [](std::string_view text, std::string_view expect) {
            const auto r = asset::parseAssetGroups(text);
            CHECK_FALSE(r.ok());
            CHECK(r.groups.empty());
            CHECK_MESSAGE(mentions(r.issues, expect), text);
            CHECK(r.issues.front().rule == "A7");
            CHECK(r.issues.front().file == "assets/groups.json");
        };
        bad("[]", "객체");
        bad(R"({"groups": [], "x": 1})", "다른 키 없음");
        bad(R"({"groups": [{"name": "a", "include": ["*"], "extra": 1}]})", "{name, include}");
        bad(R"({"groups": [{"name": "Enemies", "include": ["*"]}]})", "그룹 이름");
        bad(R"({"groups": [{"name": "a", "include": ["*"]}, {"name": "a", "include": ["*"]}]})", "두 번");
        bad(R"({"groups": [{"name": "a", "include": []}]})", "include 는 1");
        bad(R"({"groups": [{"name": "a", "include": ["../x/*.png"]}]})", "'../x/*.png'");
        bad(R"({"groups": [{"name": "a", "include": ["/abs/*.png"]}]})", "'/abs/*.png'");
        bad(R"({"groups": [{"name": "a", "include": ["a//b"]}]})", "'a//b'");
        bad(R"({"groups": [{"name": "a", "include": ["a\\b"]}]})", "glob");
        bad(R"({"groups": [{"name": "a", "include": ["label:UI"]}]})", "'label:UI'");
        bad(R"({"groups": [{"name": "a", "include": [3]}]})", "문자열");
    }

    TEST_CASE(
        "asset groups: 프로젝트 — 그룹마다 개수 · 크기, 빈 include 경고, 부르지 않는 에셋 A8, groups.json 은 에셋이 "
        "아니다") {
        const fs::path dir = fs::temp_directory_path() / std::format("sbx_groups_{:x}", std::random_device{}());
        fs::remove_all(dir);
        CHECK_FALSE(asset::loadAssetGroups(dir).present);
        put(dir / "assets/enemies/a.png", "AAAA");
        put(dir / "assets/enemies/a.png.meta", metaWith(1, ""));
        put(dir / "assets/enemies/boss/b.png", "BBBBBBBB");
        put(dir / "assets/enemies/boss/b.png.meta", metaWith(2, ""));
        put(dir / "assets/ui/btn.png", "UU");
        put(dir / "assets/ui/btn.png.meta", metaWith(3, R"("ui")"));
        put(dir / "assets/misc/x.png", "X");
        put(dir / "assets/misc/x.png.meta", metaWith(4, ""));
        put(dir / "assets/misc/ref.png", "R");
        put(dir / "assets/misc/ref.png.meta", metaWith(5, ""));
        put(dir / "assets/groups.json", R"({"groups": [
            {"name": "enemies", "include": ["enemies/**/*.png"]},
            {"name": "ui", "include": ["label:ui", "nothing/*.png"]}]})");
        const asset::AssetScan scan = asset::scanProjectAssets(dir);
        CHECK(scan.ok());
        CHECK(scan.assets.size() == 5);
        CHECK(scan.unidentified.empty()); // groups.json 에 .meta 를 바라지 않는다
        CHECK(scan.issues.empty());
        CHECK(scan.find(std::format("{:032x}", 3))->labels == std::vector<std::string>{"ui"});
        const asset::AssetGroupsFile groups = asset::loadAssetGroups(dir);
        REQUIRE(groups.ok());
        CHECK(groups.present);
        const std::vector<std::string> refs{std::format("{:032x}", 5)}; // misc/ref.png 는 materials.json 이 부른다
        const asset::AssetUsage usage = asset::summarizeAssetUsage(scan, groups.groups, refs);
        REQUIRE(usage.groups.size() == 2);
        CHECK(usage.groups[0].name == "enemies");
        CHECK(usage.groups[0].ids.size() == 2);
        CHECK(usage.groups[0].bytes == 12);
        CHECK(usage.groups[1].ids.size() == 1);
        CHECK(usage.groups[1].bytes == 2);
        CHECK(usage.unreferenced == std::vector<std::string>{"assets/misc/x.png"});
        REQUIRE(usage.issues.size() == 2);
        CHECK(usage.issues[0].rule == "A7");
        CHECK(usage.issues[0].pointer == "/groups/1/include/1");
        CHECK(usage.issues[0].severity == Severity::Warning);
        CHECK(usage.issues[1].rule == "A8");
        CHECK(usage.issues[1].message.find("1 개 (첫째 'assets/misc/x.png')") != std::string::npos);
        // 가져오기는 groups.json 에 .meta 를 쓰지 않는다 · 새 id 도 주지 않는다
        CHECK(*asset::importAssets(dir) == 0);
        CHECK_FALSE(fs::exists(dir / "assets/groups.json.meta"));
        CHECK_FALSE(asset::assignNewAssetId(dir, "assets/groups.json").has_value());
        // 깨진 groups.json
        put(dir / "assets/groups.json", "{");
        CHECK_FALSE(asset::loadAssetGroups(dir).ok());
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
