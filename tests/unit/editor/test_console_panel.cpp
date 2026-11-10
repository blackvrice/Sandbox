// Studio 콘솔 (v0.8 UI-3 — editor/ui/ConsolePanel, foundation log::recentTotal): 줄의 수준, 보일 줄 (지우기 뒤 · 수준 ·
// 글자), 누계는 고리가 차도 오른다
#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "editor/ui/ConsolePanel.hpp"
#include "foundation/log/Log.hpp"

using namespace sbx;
using namespace sbx::editor;

TEST_SUITE("editor") {
    TEST_CASE("console (v0.8 UI-3): 수준 · 보일 줄 · 지우기 · 누계") {
        CHECK(consoleLineLevel("[info] [studio] 빌드를 끝냈다") == 0);
        CHECK(consoleLineLevel("[warn] [studio] A8") == 1);
        CHECK(consoleLineLevel("[error] [client] 실패") == 2);
        CHECK(consoleLineLevel("info 처럼 보이는 줄") == 0);

        ConsoleState st;
        st.lines = {"[info] [a] one", "[warn] [b] two", "[error] [c] three", "[info] [d] four"};
        // total 10 → lines[0] = 6 번째 줄
        CHECK(consoleVisible(st, 10) == std::vector<usize>{0, 1, 2, 3});
        st.level = 1;
        CHECK(consoleVisible(st, 10) == std::vector<usize>{1, 2});
        st.level = 2;
        CHECK(consoleVisible(st, 10) == std::vector<usize>{2});
        st.level = 0;
        std::snprintf(st.filter.data(), st.filter.size(), "%s", "[d]");
        CHECK(consoleVisible(st, 10) == std::vector<usize>{3});
        st.filter[0] = '\0';
        st.clearedAt = 8; // 6 · 7 번째 줄 (lines 0 · 1) 은 지운 뒤가 아니다
        CHECK(consoleVisible(st, 10) == std::vector<usize>{2, 3});
        st.clearedAt = 10;
        CHECK(consoleVisible(st, 10).empty());

        log::setSink([](log::Level, std::string_view, std::string_view) {}); // 출력은 버린다 (test_diag 와 같다)
        const u64 before = log::recentTotal();
        for (usize i = 0; i < log::kRecentLines + 5; ++i) {
            log::write(log::Level::Info, "test", "고리를 채운다");
        }
        CHECK(log::recentTotal() == before + log::kRecentLines + 5); // 고리가 차도 누계는 오른다
        CHECK(log::recentLines().size() == log::kRecentLines);
        log::setSink(nullptr);
    }
}
