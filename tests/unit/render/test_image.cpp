// CPU 이미지: PNG 왕복 · 비교 · 차이 그림. 기준 이미지 테스트(sbx_render_tests)의 바탕. docs/06-RENDERING.md 14장.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "foundation/io/FileIo.hpp"
#include "render/asset/Image.hpp"

using namespace sbx;
using namespace sbx::render;

TEST_SUITE("render") {

    TEST_CASE("image: PNG save/load round trip keeps every RGBA byte (including a Korean path)") {
        Image img = Image::filled(5, 3, 10, 20, 30, 40);
        img.setPixel(0, 0, 255, 0, 0);
        img.setPixel(4, 2, 0, 0, 255, 128);
        const auto dir = std::filesystem::temp_directory_path() /
                         std::filesystem::path(u8"sbx_image_테스트"); // u8: Windows 에서도 한글 경로
        const auto made = io::createDirectories(dir);
        REQUIRE_MESSAGE(made.has_value(), (made ? std::string() : made.error().describe()));
        const auto path = dir / std::filesystem::path(u8"왕복.png");
        REQUIRE(savePng(path, img).has_value());
        auto back = loadPng(path);
        REQUIRE(back.has_value());
        CHECK(back->width == 5);
        CHECK(back->height == 3);
        CHECK(back->rgba == img.rgba);
        CHECK(back->pixel(4, 2)[3] == 128);
        std::filesystem::remove_all(dir);

        CHECK_FALSE(loadPng(dir / std::filesystem::path(u8"없음.png")).has_value());
        CHECK_FALSE(savePng(path, Image{}).has_value());
    }

    TEST_CASE("image: garbage bytes are a parse error, not a crash") {
        const auto path = std::filesystem::temp_directory_path() / "sbx_not_png.png";
        REQUIRE(io::writeFileAtomic(path, "this is not a png").has_value());
        auto r = loadPng(path);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::ParseError);
        std::filesystem::remove(path);
    }

    TEST_CASE("image: a header claiming a huge size is refused before decode (v0.6 P7 — decompression bomb)") {
        const auto dir = std::filesystem::temp_directory_path() / "sbx_png_p7";
        REQUIRE(io::createDirectories(dir).has_value());
        // PNG 머리 + IHDR + 빈 IDAT (stb 의 머리 훑기는 첫 IDAT 에서 멈춘다) — 16000 × 16000 RGBA 라고 적는다.
        // 디코드하면 1 GB — stb 자신은 1 GiB 넘는 것만 막는다
        const auto be32 = [](u32 v) {
            return std::string{static_cast<char>(v >> 24), static_cast<char>((v >> 16) & 0xFF),
                               static_cast<char>((v >> 8) & 0xFF), static_cast<char>(v & 0xFF)};
        };
        std::string bomb("\x89PNG\r\n\x1a\n", 8);
        bomb += be32(13) + "IHDR" + be32(16000) + be32(16000) + std::string("\x08\x06\x00\x00\x00", 5) + be32(0) +
                be32(0) + "IDAT" + be32(0);
        REQUIRE(io::writeFileAtomic(dir / "bomb.png", bomb).has_value());
        auto r = loadPng(dir / "bomb.png");
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::OutOfRange);
        CHECK(r.error().message.find("16000×16000") != std::string::npos);
        // 경계: 4096 은 받고 4097 은 거절 (진짜 PNG — 한 줄짜리라 작다)
        REQUIRE(savePng(dir / "edge.png", Image::filled(kMaxImageDimension, 1, 1, 2, 3)).has_value());
        auto edge = loadPng(dir / "edge.png");
        REQUIRE(edge.has_value());
        CHECK(edge->width == kMaxImageDimension);
        REQUIRE(savePng(dir / "over.png", Image::filled(kMaxImageDimension + 1, 1, 1, 2, 3)).has_value());
        CHECK(loadPng(dir / "over.png").error().code == ErrorCode::OutOfRange);
        // 파일 크기 상한 — 읽기 전에
        {
            std::ofstream big(dir / "big.png", std::ios::binary | std::ios::trunc);
            big.seekp(static_cast<std::streamoff>(kMaxPngFileBytes));
            big.put('\0');
        }
        CHECK(loadPng(dir / "big.png").error().code == ErrorCode::OutOfRange);
        std::filesystem::remove_all(dir);
    }

    TEST_CASE("image compare: tolerance, size mismatch, diff picture") {
        const Image a = Image::filled(4, 4, 100, 100, 100);
        Image b = a;
        b.setPixel(1, 1, 101, 100, 100); // 1 차이
        b.setPixel(2, 2, 100, 140, 100); // 40 차이
        const ImageDiff d = compareImages(a, b, 1);
        CHECK(d.sameSize);
        CHECK(d.maxChannelDiff == 40);
        CHECK(d.pixelsOverTolerance == 1);
        CHECK(d.totalPixels == 16);
        CHECK(compareImages(a, a, 0).pixelsOverTolerance == 0);
        CHECK_FALSE(compareImages(a, Image::filled(4, 5, 0, 0, 0), 0).sameSize);

        const Image diff = diffImage(a, b, 1);
        CHECK(diff.pixel(2, 2)[0] == 255);
        CHECK(diff.pixel(2, 2)[1] == 0);
        CHECK(diff.pixel(1, 1)[0] == 25); // 허용 범위 안 → 어둡게 비친 기대값
        const Image sized = diffImage(a, Image::filled(5, 4, 100, 100, 100), 0);
        CHECK(sized.width == 5);
        CHECK(sized.pixel(4, 0)[2] == 255); // 넘치는 열은 자홍
    }
}
