#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "network/protocol/BitStream.hpp"

using namespace sbx;
using namespace sbx::net;

TEST_SUITE("network") {

    TEST_CASE("bitstream: mixed widths round-trip, byte-aligned ints are little-endian bytes") {
        BitWriter w;
        w.writeBits(0b101, 3);
        w.writeBool(true);
        w.writeBits(0xABCD, 16);
        w.writeU64(0x0123'4567'89AB'CDEFull);
        w.writeF32(-1.5f);
        w.writeVarU(300);
        w.writeVarI(-2);
        w.writeString("한글 ok");
        BitReader r(w.bytes());
        CHECK(r.readBits(3) == 0b101);
        CHECK(r.readBool());
        CHECK(r.readBits(16) == 0xABCD);
        CHECK(r.readU64() == 0x0123'4567'89AB'CDEFull);
        CHECK(r.readF32() == -1.5f);
        CHECK(r.readVarU() == 300);
        CHECK(r.readVarI() == -2);
        CHECK(r.readString(64) == "한글 ok");
        CHECK_FALSE(r.error());
        CHECK(r.atEnd());

        BitWriter le;
        le.writeU32(0x11223344u);
        REQUIRE(le.bytes().size() == 4);
        CHECK(le.bytes()[0] == std::byte{0x44});
        CHECK(le.bytes()[3] == std::byte{0x11});
    }

    TEST_CASE("bitstream: varint boundaries and zigzag") {
        const u64 values[] = {0, 1, 127, 128, 16383, 16384, 0xFFFF'FFFFull, std::numeric_limits<u64>::max()};
        for (const u64 v : values) {
            BitWriter w;
            w.writeVarU(v);
            BitReader r(w.bytes());
            CHECK(r.readVarU() == v);
            CHECK(r.atEnd());
        }
        BitWriter w;
        w.writeVarU(127);
        w.writeVarU(128);
        CHECK(w.byteCount() == 3); // 1 + 2 바이트
        const i64 signedValues[] = {
            0, -1, 1, -64, 64, std::numeric_limits<i64>::min(), std::numeric_limits<i64>::max()};
        for (const i64 v : signedValues) {
            BitWriter s;
            s.writeVarI(v);
            BitReader r(s.bytes());
            CHECK(r.readVarI() == v);
        }
        BitWriter small;
        small.writeVarI(-1);
        CHECK(small.byteCount() == 1); // zigzag: -1 → 1
    }

    TEST_CASE("bitstream: reading past the end, over a cap, or a too-long varint sets error and returns zeros") {
        BitWriter w;
        w.writeU8(7);
        BitReader r(w.bytes());
        CHECK(r.readU8() == 7);
        CHECK(r.readU8() == 0);
        CHECK(r.error());
        CHECK(r.readU64() == 0); // 오류 뒤에는 모두 0

        BitWriter s;
        s.writeString("abcdef");
        BitReader capped(s.bytes());
        CHECK(capped.readString(5).empty());
        CHECK(capped.error());

        BitWriter cnt;
        cnt.writeVarU(1000);
        BitReader limited(cnt.bytes());
        CHECK(limited.readVarU(999) == 0);
        CHECK(limited.error());

        // 길이가 남은 바이트보다 크다
        BitWriter lie;
        lie.writeVarU(50);
        lie.writeU8('x');
        BitReader liar(lie.bytes());
        CHECK(liar.readString(100).empty());
        CHECK(liar.error());

        // 11 바이트 varint
        std::vector<std::byte> tooLong(11, std::byte{0x80});
        tooLong.back() = std::byte{0x01};
        BitReader vr(tooLong);
        CHECK(vr.readVarU() == 0);
        CHECK(vr.error());
        // 10번째 바이트에 1 비트 넘게
        std::vector<std::byte> overflow(10, std::byte{0xFF});
        overflow.back() = std::byte{0x02};
        BitReader ov(overflow);
        CHECK(ov.readVarU() == 0);
        CHECK(ov.error());
    }

    TEST_CASE("bitstream: invalid UTF-8 strings are errors, a real U+FFFD is fine") {
        BitWriter w;
        w.writeString(std::string("a\xFF", 2));
        BitReader r(w.bytes());
        CHECK(r.readString(16).empty());
        CHECK(r.error());

        BitWriter ok;
        ok.writeString("\xEF\xBF\xBD");
        BitReader r2(ok.bytes());
        CHECK(r2.readString(16) == "\xEF\xBF\xBD");
        CHECK_FALSE(r2.error());
    }

    TEST_CASE("bitstream: atEnd accepts only zero padding in the last byte") {
        BitWriter w;
        w.writeBits(1, 3);
        BitReader r(w.bytes());
        (void)r.readBits(3);
        CHECK(r.atEnd());

        const std::byte junk[] = {std::byte{0xF1}};
        BitReader j(junk);
        (void)j.readBits(3);
        CHECK_FALSE(j.atEnd());
    }

    TEST_CASE("bitstream: bytes and NaN pass through as bits") {
        BitWriter w;
        const std::byte payload[] = {std::byte{1}, std::byte{2}, std::byte{255}};
        w.writeBits(1, 1); // 정렬되지 않은 위치에서도
        w.writeBytes(payload);
        w.writeF32(std::numeric_limits<f32>::quiet_NaN());
        BitReader r(w.bytes());
        (void)r.readBits(1);
        const auto back = r.readBytes(8);
        REQUIRE(back.size() == 3);
        CHECK(back[2] == std::byte{255});
        CHECK(std::isnan(r.readF32()));
    }

} // TEST_SUITE
