// Studio 명령 표 (v0.8 ST-06-1 — editor/services/CommandService, ADR-0108): 단축키 맞춘 꼴 (차례 · 대소문자 · 별칭 ·
// 틀린 꼴), 등록 거절 (id · 겹침 · 제목 · 실행 · 단축키), when · run · 단축키 실행 (그 곳 먼저 · global · 실행할 수
// 없는 것 건너뛰기), 팔레트 찾기 차례, 단축키 충돌 진단
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "editor/services/CommandService.hpp"

using namespace sbx;
using namespace sbx::editor::services;

namespace {

std::string norm(std::string_view s) {
    const Expected<std::string> r = normalizeShortcut(s);
    return r ? *r : "ERR";
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("command service: 단축키 맞춘 꼴 — 수정자 차례 · 대소문자 · 별칭 · F 키 · 이름 키, 틀린 꼴은 오류") {
        CHECK(norm("ctrl+shift+p") == "Ctrl+Shift+P");
        CHECK(norm("Shift+Control+z") == "Ctrl+Shift+Z");
        CHECK(norm("alt+cmd+1") == "Alt+Super+1");
        CHECK(norm("win+Option+f12") == "Alt+Super+F12");
        CHECK(norm("F5") == "F5");
        CHECK(norm("ctrl+esc") == "Ctrl+Escape");
        CHECK(norm("DEL") == "Delete");
        CHECK(norm("ctrl+pageup") == "Ctrl+PageUp");
        CHECK(norm("ctrl+=") == "Ctrl+=");
        CHECK(norm("").empty());
        for (const char* bad : {"ctrl", "ctrl+", "+p", "ctrl++", "ctrl+ctrl+p", "p+ctrl", "ctrl+pp", "f25", "f0", "f01",
                                "ctrl+é", "ctrl+shift+p+q"}) {
            CHECK_MESSAGE(norm(bad) == "ERR", bad);
        }
        CHECK(isCommandId("studio.build"));
        CHECK(isCommandId("edit.undo-all_2"));
        for (const char* bad : {"build", ".a", "a.", "a..b", "Studio.build", "a.b c", ""}) {
            CHECK_FALSE_MESSAGE(isCommandId(bad), bad);
        }
    }

    TEST_CASE("command service: 등록 · when · run · 단축키 실행 (그 곳 먼저, global, 실행할 수 없는 것 건너뛰기)") {
        CommandService s;
        std::vector<std::string> ran;
        bool projectOpen = false;
        REQUIRE(s.add({"studio.build", "빌드", "ctrl+b", "global", [&] { return projectOpen; },
                       [&] { ran.push_back("build"); }})
                    .has_value());
        REQUIRE(s.add({"edit.undo", "되돌리기", "Ctrl+Z", "global", {}, [&] { ran.push_back("undo"); }}).has_value());
        REQUIRE(
            s.add({"script.undo", "스크립트 되돌리기", "ctrl+z", "script", {}, [&] { ran.push_back("script.undo"); }})
                .has_value());
        CHECK(s.find("studio.build")->shortcut == "Ctrl+B"); // 맞춘 꼴로 적는다
        CHECK(s.commands().size() == 3);

        // 거절
        CHECK(s.add({"studio.build", "다시", "", "global", {}, [] {}}).error().code == ErrorCode::AlreadyExists);
        CHECK_FALSE(s.add({"Bad", "x", "", "global", {}, [] {}}).has_value());
        CHECK_FALSE(s.add({"a.b", "", "", "global", {}, [] {}}).has_value());       // 제목 없음
        CHECK_FALSE(s.add({"a.c", "x", "", "global", {}, {}}).has_value());         // 실행 없음
        CHECK_FALSE(s.add({"a.d", "x", "ctrl+", "global", {}, [] {}}).has_value()); // 단축키 꼴
        CHECK_FALSE(s.add({"a.e", "x", "", "", {}, [] {}}).has_value());            // 빈 context
        CHECK(s.commands().size() == 3);

        // when · run
        CHECK_FALSE(s.enabled("studio.build"));
        CHECK(s.run("studio.build") == RunResult::Disabled);
        CHECK(s.run("studio.nothing") == RunResult::Unknown);
        CHECK(ran.empty());
        projectOpen = true;
        CHECK(s.enabled("studio.build"));
        CHECK(s.run("studio.build") == RunResult::Ran);
        CHECK(ran == std::vector<std::string>{"build"});

        // 단축키 — 그 곳 (script) 의 것이 global 보다 먼저, 다른 곳에서는 global
        ran.clear();
        CHECK(s.runShortcut("ctrl+z", "script") == std::optional<std::string>("script.undo"));
        CHECK(s.runShortcut("Control+Z", "scene") == std::optional<std::string>("edit.undo"));
        CHECK(s.runShortcut("ctrl+z", "global") == std::optional<std::string>("edit.undo"));
        CHECK(s.runShortcut("ctrl+b", "scene") == std::optional<std::string>("studio.build"));
        projectOpen = false;
        CHECK_FALSE(s.runShortcut("ctrl+b", "scene").has_value()); // 실행할 수 없다
        CHECK_FALSE(s.runShortcut("ctrl+q", "scene").has_value()); // 없는 단축키
        CHECK_FALSE(s.runShortcut("ctrl+", "scene").has_value());  // 틀린 꼴
        CHECK(ran == std::vector<std::string>{"script.undo", "undo", "undo", "build"});
    }

    TEST_CASE("command service: 팔레트 찾기 (제목 앞 → 제목 안 → id, 대소문자 무시) · 단축키 충돌 진단") {
        CommandService s;
        const auto none = [] {};
        REQUIRE(s.add({"studio.build", "Build Project", "ctrl+b", "global", {}, none}).has_value());
        REQUIRE(s.add({"studio.rebuild", "Clean and Build", "", "global", {}, none}).has_value());
        REQUIRE(s.add({"file.save", "Save", "ctrl+s", "global", {}, none}).has_value());
        REQUIRE(s.add({"script.save", "Save Script", "ctrl+s", "script", {}, none}).has_value()); // global 과 겹침
        REQUIRE(s.add({"scene.bake", "Bake", "ctrl+shift+b", "scene", {}, none}).has_value());
        REQUIRE(s.add({"script.bake", "Bake Script", "Shift+Ctrl+B", "script", {}, none})
                    .has_value()); // 다른 곳 — 충돌 아님
        REQUIRE(s.add({"scene.frame", "Frame", "f", "scene", {}, none}).has_value());
        REQUIRE(s.add({"scene.focus", "Focus", "F", "scene", {}, none}).has_value()); // 같은 곳

        const auto ids = [](const std::vector<const CommandSpec*>& v) {
            std::vector<std::string> out;
            for (const CommandSpec* c : v) {
                out.push_back(c->id);
            }
            return out;
        };
        CHECK(ids(s.search("build")) == std::vector<std::string>{"studio.build", "studio.rebuild"});
        CHECK(ids(s.search("SAVE")) == std::vector<std::string>{"file.save", "script.save"});
        CHECK(ids(s.search("script")) == std::vector<std::string>{"script.save", "script.bake"}); // 제목 안
        CHECK(ids(s.search("rebuild")) == std::vector<std::string>{"studio.rebuild"});            // id 만
        CHECK(s.search("").size() == 8);
        CHECK(s.search("zzz").empty());

        const std::vector<ShortcutConflict> c = s.conflicts();
        REQUIRE(c.size() == 2);
        CHECK(c[0].shortcut == "Ctrl+S");
        CHECK(c[0].first == "file.save");
        CHECK(c[0].second == "script.save");
        CHECK(c[1].shortcut == "F");
        CHECK(c[1].first == "scene.frame");
        CHECK(c[1].second == "scene.focus");
    }
}
