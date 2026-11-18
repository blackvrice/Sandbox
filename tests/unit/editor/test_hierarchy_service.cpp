// Hierarchy 검색 · 편집기 그룹 (v0.12 ST-06-6, ADR-0162): 그룹 만들기 · 이름 · 옮기기 (개체 하나는 그룹 하나) · 빼기 ·
// 접기 · prune · editor.json 왕복 · 틀린 파일 거절, 검색 낱말 (prefab 글자 · #번호 · prefab: 앞 · tag: · group: · 모두
// 맞아야 · 대소문자 무시 · 모르는 태그는 아무것도), 목록 (그룹 머리 · 펼친 개체 · 접힌 그룹 · 검색에 맞은 것 없는 그룹
// 빼기 · 그룹 없는 개체), 10k 개체 · 그룹 50 의 목록 만들기 측정 (14 7.x).
#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentDatabase.hpp"
#include "editor/services/HierarchyService.hpp"

using namespace sbx;
using namespace sbx::editor;

TEST_SUITE("editor") {
    TEST_CASE("editor groups (v0.12 ST-06-6): 만들기 · 이름 · 옮기기 · 빼기 · 접기 · prune · editor.json 왕복") {
        EditorGroups g;
        CHECK(g.create("wolves", std::vector<SaveId>{5, 3, 3}));
        CHECK(g.create("rabbits", std::vector<SaveId>{7}));
        CHECK_FALSE(g.create("wolves", {}));             // 겹침
        CHECK_FALSE(g.create("", {}));                   // 빈 이름
        CHECK_FALSE(g.create(std::string(65, 'a'), {})); // 긴 이름
        CHECK(g.find("wolves")->members == std::vector<SaveId>{3, 5});
        CHECK(g.add("rabbits", std::vector<SaveId>{5})); // wolves 에서 옮긴다
        CHECK(g.find("wolves")->members == std::vector<SaveId>{3});
        CHECK(g.groupOf(5)->name == "rabbits");
        CHECK_FALSE(g.add("nope", std::vector<SaveId>{1}));
        CHECK(g.rename("rabbits", "prey"));
        CHECK_FALSE(g.rename("prey", "wolves"));
        CHECK(g.setCollapsed("prey", true));
        g.removeMembers(std::vector<SaveId>{3});
        CHECK(g.find("wolves")->members.empty());
        CHECK(g.prune(std::vector<SaveId>{7}) == 1); // 5 는 지워진 개체
        CHECK(g.find("prey")->members == std::vector<SaveId>{7});

        EditorGroups back;
        REQUIRE(back.fromJson(g.toJson()).has_value());
        CHECK(back.groups() == g.groups());
        CHECK(back.find("prey")->collapsed);
        // 틀린 파일 — 그대로
        CHECK_FALSE(back.fromJson("{}").has_value());
        CHECK_FALSE(back.fromJson(R"({"format": "sandbox.editor", "version": 2})").has_value());
        CHECK_FALSE(
            back.fromJson(R"({"format": "sandbox.editor", "version": 1, "groups": [{"name": "a", "members": [-1]}]})")
                .has_value());
        CHECK_FALSE(
            back.fromJson(
                    R"({"format": "sandbox.editor", "version": 1, "groups": [{"name": "a", "members": []}, {"name": "a", "members": []}]})")
                .has_value());
        CHECK(back.groups() == g.groups());
        CHECK(g.remove("wolves"));
        CHECK_FALSE(g.remove("wolves"));
    }

    TEST_CASE("hierarchy search (v0.12 ST-06-6): 낱말 · #번호 · prefab: · tag: · group: · 목록 (그룹 · 접기 · 빼기)") {
        ecs::ComponentCatalog cat;
        REQUIRE(comp::registerCoreComponents(cat).has_value());
        const content::ContentDatabase& db = content::ContentDatabase::builtin();
        // 태그 — builtin 의 첫 태그를 쓴다 (없으면 태그 검사는 건너뛴다)
        std::optional<content::TagIndex> someTag;
        std::string someTagName;
        if (!db.tags().empty()) {
            someTagName = db.tags().front();
            someTag = db.findTag(someTagName);
        }
        std::vector<HierarchyRow> rows = {
            {1, 10, "eco.rabbit", {}}, {2, 11, "eco.wolf", {}}, {3, 12, "", {}}, {4, 13, "Eco.Rabbit", {}}};
        if (someTag) {
            rows[1].tags.set(*someTag);
        }
        EditorGroups groups;
        REQUIRE(groups.create("pack", std::vector<SaveId>{11, 13}));

        const auto ids = [&](std::string_view text) {
            std::vector<HierarchyLine> lines;
            buildHierarchy(rows, groups, parseHierarchyQuery(text, &db), lines);
            std::vector<std::string> out;
            for (const HierarchyLine& l : lines) {
                out.push_back(l.kind == HierarchyLine::Kind::Group
                                  ? std::format("[{}:{}]", groups.groups()[l.group].name, l.matches)
                                  : std::to_string(rows[l.row].id));
            }
            return out;
        };
        CHECK(ids("") == std::vector<std::string>{"[pack:2]", "2", "4", "1", "3"});
        CHECK(ids("RABBIT") == std::vector<std::string>{"[pack:1]", "4", "1"}); // 대소문자 무시
        CHECK(ids("#3") == std::vector<std::string>{"3"});                      // 그룹에 맞은 것 없음 — 뺀다
        CHECK(ids("개체") == std::vector<std::string>{"3"});
        CHECK(ids("prefab:eco.w") == std::vector<std::string>{"[pack:1]", "2"});
        CHECK(ids("prefab:rabbit").empty()); // 앞부분만
        CHECK(ids("group:pa") == std::vector<std::string>{"[pack:2]", "2", "4"});
        CHECK(ids("eco rabbit #1") == std::vector<std::string>{"1"}); // 모두 맞아야
        CHECK(ids("tag:no_such_tag").empty());
        if (someTag) {
            CHECK(ids("tag:" + someTagName) == std::vector<std::string>{"[pack:1]", "2"});
        }
        REQUIRE(groups.setCollapsed("pack", true));
        CHECK(ids("") == std::vector<std::string>{"[pack:2]", "1", "3"}); // 접힘 — 머리만 (맞은 수는 그대로)
        // saveId 를 모르면 (원격) 그룹 없이
        rows[1].saveId = kInvalidSaveId;
        CHECK(ids("") == std::vector<std::string>{"[pack:1]", "1", "2", "3"});
    }

    TEST_CASE("hierarchy 10k (v0.12 ST-06-6 — 측정): 개체 10,000 · 그룹 50 · 검색") {
        std::vector<HierarchyRow> rows;
        const std::array<std::string_view, 4> prefabs = {"eco.rabbit", "eco.wolf", "eco.grass", ""};
        for (u32 i = 0; i < 10'000; ++i) {
            rows.push_back({i + 1, 1000 + i, prefabs[i % prefabs.size()], {}});
        }
        EditorGroups groups;
        for (u32 g = 0; g < 50; ++g) {
            std::vector<SaveId> members;
            for (u32 k = 0; k < 100; ++k) {
                members.push_back(1000 + g * 100 + k);
            }
            REQUIRE(groups.create(std::format("g{}", g), members));
        }
        std::vector<HierarchyLine> lines;
        const auto measure = [&](std::string_view text) {
            const HierarchyQuery q = parseHierarchyQuery(text, nullptr);
            const auto t0 = std::chrono::steady_clock::now();
            constexpr int kRuns = 5;
            for (int r = 0; r < kRuns; ++r) {
                buildHierarchy(rows, groups, q, lines);
            }
            const f64 ms =
                std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count() / kRuns;
            std::printf("hierarchy 10k '%.*s': %.3f ms, %zu 줄\n", static_cast<int>(text.size()), text.data(), ms,
                        lines.size());
            return ms;
        };
        const f64 all = measure("");
        CHECK(lines.size() == 50 + 10'000);
        const f64 search = measure("rabbit");
        CHECK(lines.size() == 50 + 2'500);
        (void)measure("group:g4 prefab:eco.");
        // 예산: 프레임 (16 ms) 의 한 부분 — Debug 는 넉넉히 (Release 측정은 14 · DEVELOPMENT_LOG)
        CHECK(all < 50.0);
        CHECK(search < 50.0);
    }
}
