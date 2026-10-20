// SHA-256 (v0.2 M5-T2 — 게임 패키지): FIPS 180-4 시험 벡터, 나눠 넣어도 같다, 16진 표기.
#include <doctest/doctest.h>

#include <string>

#include "foundation/hash/Sha256.hpp"

using namespace sbx;

TEST_SUITE("foundation") {
    TEST_CASE("sha256: 시험 벡터 (빈 글 · abc · 448 비트 · 백만 a)") {
        CHECK(toHex(sha256(std::string_view(""))) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(toHex(sha256(std::string_view("abc"))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(toHex(sha256(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        CHECK(toHex(sha256(std::string(1'000'000, 'a'))) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    TEST_CASE("sha256: 나눠 넣어도 같다 (블록 경계 · 패딩 경계)") {
        for (const usize n : {0u, 1u, 55u, 56u, 63u, 64u, 65u, 119u, 120u, 1000u}) {
            CAPTURE(n);
            std::string text;
            for (usize i = 0; i < n; ++i) {
                text.push_back(static_cast<char>('a' + i % 26));
            }
            const Sha256Digest whole = sha256(std::string_view(text));
            for (const usize step : {1u, 7u, 64u, 100u}) {
                Sha256 h;
                for (usize i = 0; i < text.size(); i += step) {
                    h.update(std::string_view(text).substr(i, step));
                }
                CHECK(h.finish() == whole);
            }
        }
        CHECK(isSha256Hex(toHex(sha256(std::string_view("x")))));
        CHECK_FALSE(isSha256Hex("ABC"));
        CHECK_FALSE(isSha256Hex(std::string(64, 'G')));
        CHECK_FALSE(isSha256Hex(std::string(63, 'a')));
    }
}
