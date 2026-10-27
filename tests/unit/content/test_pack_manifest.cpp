// 팩 매니페스트의 패키지 키 (v0.4 M1-T1, ADR-0071): license (SPDX 식 모양), dependencies (범위 문자열 · {version, path
// | git + rev}), 로더의 V12 — 의존 범위 확인 (소스가 곧 버전), 요구 팩으로 합치기 (로드 순서 · 팩 경계), 자기 자신 ·
// 모르는 키 · 모양 오류.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/PackManifest.hpp"

using namespace sbx;
using namespace sbx::content;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

struct Root {
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "pack-manifest" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] ContentLoadResult load(std::vector<std::string> ids) const {
        return ContentLoader::load(root, ids, catalog());
    }
    fs::path root;
};

std::string dump(const ContentLoadResult& r) {
    std::string s;
    for (const auto& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

// 그 규칙 · 조각의 오류가 있다
bool has(const ContentLoadResult& r, std::string_view rule, std::string_view fragment) {
    for (const ContentIssue& i : r.issues) {
        if (i.severity == ContentIssue::Severity::Error && i.rule == rule &&
            i.describe().find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 팩 b (Prefab b.thing) 와 그것을 쓰는 팩 a (Prefab a.box — b.thing 의 태그를 쓴다). aPack = a 의 pack.json
Root twoPacks(std::string_view name, std::string_view aPack, std::string_view bVersion) {
    Root r(name);
    r.file("b/pack.json", std::format(R"({{"id": "b", "version": "{}", "license": "MIT"}})", bVersion))
        .file("b/tags.json", R"(["shiny"])")
        .file("b/prefabs/thing.json", R"({"id": "b.thing", "tags": ["shiny"], "components": {"core.transform": {}}})")
        .file("a/pack.json", aPack)
        .file("a/prefabs/box.json", R"({"id": "a.box", "tags": ["shiny"], "components": {"core.transform": {}}})");
    return r;
}

} // namespace

TEST_SUITE("content") {
    TEST_CASE("pack manifest: SPDX 식의 모양 — 식별자 · AND · OR · WITH · 괄호 · +, LicenseRef-") {
        for (const std::string_view ok :
             {"MIT", "Apache-2.0", "Apache-2.0 OR MIT", "(MIT AND BSD-3-Clause) OR GPL-2.0+",
              "GPL-2.0-only WITH Classpath-exception-2.0", "LicenseRef-mystudio-1", "  MIT  ",
              "MIT AND (Apache-2.0 OR CC-BY-4.0)", "NOASSERTION"}) {
            CAPTURE(ok);
            CHECK_FALSE(checkSpdxExpression(ok).has_value());
        }
        const auto bad = [](std::string_view text, std::string_view fragment) {
            CAPTURE(text);
            const auto e = checkSpdxExpression(text);
            REQUIRE(e.has_value());
            CHECK(e->find(fragment) != std::string::npos);
        };
        bad("", "비었다");
        bad("MIT AND", "빠졌다");
        bad("AND MIT", "앞에 라이선스 식별자");
        bad("mit or apache", "식이 끝나야");
        bad("(MIT", "괄호");
        bad("MIT)", "식이 끝나야");
        bad("MIT WITH", "예외 식별자");
        bad("MI$T", "식이 끝나야");
        bad("LicenseRef-", "이름");
        bad(std::string(300, 'A'), "256");
    }

    TEST_CASE("pack manifest: 의존 하나의 모양 — 범위 문자열 · {version, path} · {version, git, rev}") {
        PackDependency d;
        REQUIRE_FALSE(parsePackDependency(json("^1.2"), d).has_value());
        CHECK(d.source == PackSource::Root);
        REQUIRE(d.range.has_value());
        CHECK(d.range->text == "^1.2");

        PackDependency p;
        REQUIRE_FALSE(parsePackDependency(json{{"version", "~0.3"}, {"path", "../items"}}, p).has_value());
        CHECK(p.source == PackSource::Path);
        CHECK(p.path == "../items");
        PackDependency g;
        REQUIRE_FALSE(
            parsePackDependency(json{{"git", "https://example.com/inv.git"}, {"rev", "v1.0.0"}}, g).has_value());
        CHECK(g.source == PackSource::Git);
        CHECK(g.rev == "v1.0.0");
        CHECK_FALSE(g.range.has_value());

        const auto bad = [](const json& v, std::string_view pointer, std::string_view fragment) {
            PackDependency x;
            CAPTURE(v.dump());
            const auto e = parsePackDependency(v, x);
            REQUIRE(e.has_value());
            CHECK(e->pointer == pointer);
            CHECK(e->message.find(fragment) != std::string::npos);
        };
        bad(json(3), "", "범위 문자열");
        bad(json("1.0 - 2.0"), "", "하이픈");
        bad(json("^1 || ^2"), "", "'||'");
        bad(json{{"versoin", "^1"}}, "/versoin", "모르는 키");
        bad(json{{"version", 1}}, "/version", "문자열");
        bad(json{{"path", "a"}, {"git", "u"}, {"rev", "r"}}, "", "하나만");
        bad(json{{"path", "C:/items"}}, "/path", "상대 경로");
        bad(json{{"path", "/abs"}}, "/path", "상대 경로");
        bad(json{{"path", "..\\items"}}, "/path", "상대 경로");
        bad(json{{"path", "x"}, {"rev", "v1"}}, "/rev", "git 소스에만");
        bad(json{{"git", "https://x"}}, "/rev", "rev");
        bad(json{{"git", ""}, {"rev", "v1"}}, "/git", "URL");
        bad(json{{"rev", "v1"}}, "/rev", "git 소스에만");
    }

    TEST_CASE("pack manifest: 로더 — dependencies 는 requires 와 같은 뜻 (로드 순서 · 팩 경계), 범위를 만족하면 통과") {
        const Root r = twoPacks("ok", R"({"id": "a", "version": "0.1.0", "license": "Apache-2.0 OR MIT",
            "dependencies": {"b": "^1.2"}})",
                                "1.4.0");
        const ContentLoadResult res = r.load({"a"});
        REQUIRE_MESSAGE(res.ok(), dump(res));
        REQUIRE(res.db.packs().size() == 2);
        CHECK(res.db.packs()[0].id == "b"); // 요구 팩이 먼저
        const PackInfo& a = res.db.packs()[1];
        CHECK(a.id == "a");
        CHECK(a.license == "Apache-2.0 OR MIT");
        REQUIRE(a.dependencies.size() == 1);
        CHECK(a.dependencies[0] == "b");
        REQUIRE(a.packages.size() == 1);
        CHECK(a.packages[0].range->text == "^1.2");
        CHECK(res.db.packs()[0].license == "MIT");

        // requires 와 함께 적어도 하나로 · 소스 (기록만 — 받기는 M2) · "*"
        const Root both = twoPacks("both", R"({"id": "a", "requires": ["b"],
            "dependencies": {"b": {"version": "*", "path": "../vendor/b"}}})",
                                   "2.0.0");
        const ContentLoadResult rb = both.load({"a"});
        REQUIRE_MESSAGE(rb.ok(), dump(rb));
        const PackInfo& a2 = rb.db.packs()[1];
        CHECK(a2.dependencies.size() == 1);
        REQUIRE(a2.packages.size() == 1);
        CHECK(a2.packages[0].source == PackSource::Path);
        CHECK(a2.packages[0].path == "../vendor/b");
    }

    TEST_CASE(
        "pack manifest: V12 — 범위 밖 · SemVer 아님 · 모양 · 자기 자신 · 라이선스, 오류는 누가 무엇을 요구했는지") {
        const ContentLoadResult old =
            twoPacks("old", R"({"id": "a", "dependencies": {"b": "^1.2"}})", "1.1.9").load({"a"});
        CHECK(has(old, "V12", "팩 'a' 은 'b' ^1.2 을 요구하는데 'b' 은 1.1.9 이다"));
        CHECK(has(old, "V12", "a/pack.json:/dependencies/b"));

        const ContentLoadResult loose =
            twoPacks("loose", R"({"id": "a", "dependencies": {"b": "~1"}})", "1").load({"a"});
        CHECK(has(loose, "V12", "version '1' 이 SemVer 가 아니다"));

        // 범위가 없으면 version 을 보지 않는다 (지금의 requires 와 같다)
        const ContentLoadResult free =
            twoPacks("free", R"({"id": "a", "dependencies": {"b": {"path": "../b"}}})", "1").load({"a"});
        CHECK_MESSAGE(free.ok(), dump(free));

        CHECK(has(twoPacks("shape", R"({"id": "a", "dependencies": ["b"]})", "1.0.0").load({"a"}), "V12", "객체"));
        CHECK(has(twoPacks("key", R"({"id": "a", "dependencies": {"B!": "^1"}})", "1.0.0").load({"a"}), "V12",
                  "팩 id 가 아니다"));
        CHECK(has(twoPacks("core", R"({"id": "a", "dependencies": {"core": "^1"}})", "1.0.0").load({"a"}), "V12",
                  "core 는 엔진"));
        CHECK(has(
            twoPacks("git", R"({"id": "a", "dependencies": {"b": {"git": "https://x/b.git"}}})", "1.0.0").load({"a"}),
            "V12", "a/pack.json:/dependencies/b/rev"));
        CHECK(has(twoPacks("self", R"({"id": "a", "dependencies": {"a": "*"}})", "1.0.0").load({"a"}), "V12",
                  "자기 자신"));
        CHECK(has(twoPacks("lic", R"({"id": "a", "license": "MIT OR"})", "1.0.0").load({"a"}), "V12",
                  "license 'MIT OR'"));
        CHECK(has(twoPacks("lictype", R"({"id": "a", "license": 3})", "1.0.0").load({"a"}), "V12", "SPDX"));
        // 없는 팩을 요구 — 지금의 V2 그대로
        const ContentLoadResult missing =
            Root("missing").file("a/pack.json", R"({"id": "a", "dependencies": {"zzz": "^1"}})").load({"a"});
        CHECK(has(missing, "V2", "팩 'zzz' 을 찾을 수 없다"));
    }
}
