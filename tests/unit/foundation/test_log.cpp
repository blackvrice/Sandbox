#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "foundation/log/Log.hpp"

namespace {
struct Captured {
    sbx::log::Level level;
    std::string category;
    std::string message;
};

// 싱크와 레벨을 바꾸고, 범위를 벗어나면 기본값으로 되돌린다.
struct ScopedCapture {
    std::vector<Captured> lines;
    sbx::log::Level previousLevel = sbx::log::level();
    ScopedCapture() {
        sbx::log::setSink([this](sbx::log::Level l, std::string_view c, std::string_view m) {
            lines.push_back({l, std::string(c), std::string(m)});
        });
    }
    ~ScopedCapture() {
        sbx::log::setSink(nullptr);
        sbx::log::setLevel(previousLevel);
    }
};
} // namespace

TEST_SUITE("foundation") {

    TEST_CASE("log: formats message and passes category") {
        ScopedCapture cap;
        sbx::log::setLevel(sbx::log::Level::Info);
        sbx::log::info("net", "client {} connected from {}", 7, "127.0.0.1");
        REQUIRE(cap.lines.size() == 1);
        CHECK(cap.lines[0].level == sbx::log::Level::Info);
        CHECK(cap.lines[0].category == "net");
        CHECK(cap.lines[0].message == "client 7 connected from 127.0.0.1");
    }

    TEST_CASE("log: messages below level are dropped") {
        ScopedCapture cap;
        sbx::log::setLevel(sbx::log::Level::Warn);
        sbx::log::debug("core", "hidden");
        sbx::log::info("core", "hidden");
        sbx::log::warn("core", "shown");
        sbx::log::error("core", "shown");
        CHECK(cap.lines.size() == 2);
    }

    TEST_CASE("log: Off disables everything") {
        ScopedCapture cap;
        sbx::log::setLevel(sbx::log::Level::Off);
        sbx::log::error("core", "hidden");
        CHECK(cap.lines.empty());
    }

} // TEST_SUITE
