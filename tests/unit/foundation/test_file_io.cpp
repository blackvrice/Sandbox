// 파일 이름 바꾸기 다시 하기 (foundation/io renameRetrying — createProject · 패키지 풀기 · 원자적 쓰기의 rename). Windows 는 폴더 안의
// 파일이 (FILE_SHARE_DELETE 없이) 열려 있으면 폴더 rename 이 ERROR_ACCESS_DENIED — 백신 · 색인기가 방금 쓴 파일을 잠깐 여는 것과 같다.
// 잠깐 잡혔다 풀리면 성공, 다른 오류는 바로 돌려준다
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <thread>

#include "foundation/io/FileIo.hpp"

using namespace sbx;
namespace fs = std::filesystem;

TEST_CASE("io: renameRetrying — 잠깐 잡힌 폴더는 풀린 뒤 바꾼다 · 다른 오류는 바로") {
    std::random_device rd;
    const fs::path root = fs::temp_directory_path() / std::format("sbx_rename_{:08x}", rd());
    const fs::path from = root / "staging";
    const fs::path to = root / "Project";
    fs::create_directories(from);
    std::ofstream(from / "a.json", std::ios::binary) << "{}";

    {
        auto held = std::make_unique<std::ifstream>(from / "a.json", std::ios::binary); // 백신처럼 잠깐 연다
        REQUIRE(held->is_open());
        std::thread release([&held] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            held.reset();
        });
        const std::error_code ec = io::renameRetrying(from, to);
        release.join();
        CHECK_FALSE(ec);
    }
    CHECK(fs::exists(to / "a.json"));
    CHECK_FALSE(fs::exists(from));

    // 없는 원본 — 다시 하지 않고 바로 (다시 하면 2 초)
    const auto t0 = std::chrono::steady_clock::now();
    const std::error_code missing = io::renameRetrying(root / "nothing", root / "x");
    CHECK(missing);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1000));

    std::error_code ec;
    fs::remove_all(root, ec);
}
