#include "network/protocol/BitStream.hpp"

#include <bit>

#include "foundation/assert/Assert.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::net {

namespace {

// 엄격한 UTF-8 검사 (decodeNext 는 잘못된 바이트를 U+FFFD 로 바꾸므로, 진짜 U+FFFD 바이트열인지 다시 본다)
bool validUtf8(std::string_view s) {
    usize pos = 0;
    while (pos < s.size()) {
        const usize start = pos;
        if (utf8::decodeNext(s, pos) == utf8::kReplacement && s.substr(start, pos - start) != "\xEF\xBF\xBD") {
            return false;
        }
    }
    return true;
}

} // namespace

void BitWriter::writeBits(u64 value, u32 bits) {
    SBX_ASSERT(bits >= 1 && bits <= 64, "writeBits: 1 ~ 64 비트");
    if (bits < 64) {
        value &= (u64{1} << bits) - 1;
    }
    while (bits > 0) {
        const usize byteIndex = m_bits / 8;
        const u32 bitInByte = static_cast<u32>(m_bits % 8);
        if (byteIndex == m_data.size()) {
            m_data.push_back(std::byte{0});
        }
        const u32 take = std::min(bits, 8 - bitInByte);
        const u64 chunk = value & ((u64{1} << take) - 1);
        m_data[byteIndex] |= static_cast<std::byte>(chunk << bitInByte);
        value >>= take;
        bits -= take;
        m_bits += take;
    }
}

void BitWriter::writeVarU(u64 value) {
    while (value >= 0x80) {
        writeBits((value & 0x7F) | 0x80, 8);
        value >>= 7;
    }
    writeBits(value, 8);
}

void BitWriter::writeVarI(i64 value) {
    const u64 zigzag = (static_cast<u64>(value) << 1) ^ static_cast<u64>(value >> 63);
    writeVarU(zigzag);
}

void BitWriter::writeF32(f32 value) {
    writeBits(std::bit_cast<u32>(value), 32);
}

void BitWriter::writeString(std::string_view text) {
    writeVarU(text.size());
    for (const char c : text) {
        writeBits(static_cast<u8>(c), 8);
    }
}

void BitWriter::writeBytes(std::span<const std::byte> bytes) {
    writeVarU(bytes.size());
    for (const std::byte b : bytes) {
        writeBits(static_cast<u8>(b), 8);
    }
}

void BitWriter::alignToByte() {
    m_bits = (m_bits + 7) / 8 * 8;
}

std::vector<std::byte> BitWriter::take() && {
    m_bits = 0;
    return std::move(m_data);
}

u64 BitReader::readBits(u32 bits) {
    SBX_ASSERT(bits >= 1 && bits <= 64, "readBits: 1 ~ 64 비트");
    if (m_error || m_data.size() * 8 - m_pos < bits) {
        m_error = true;
        return 0;
    }
    u64 value = 0;
    u32 got = 0;
    while (got < bits) {
        const usize byteIndex = m_pos / 8;
        const u32 bitInByte = static_cast<u32>(m_pos % 8);
        const u32 take = std::min(bits - got, 8 - bitInByte);
        const u64 chunk = (static_cast<u64>(m_data[byteIndex]) >> bitInByte) & ((u64{1} << take) - 1);
        value |= chunk << got;
        got += take;
        m_pos += take;
    }
    return value;
}

u64 BitReader::readVarU() {
    u64 value = 0;
    for (u32 shift = 0; shift < 70; shift += 7) {
        const u64 byte = readBits(8);
        if (m_error) {
            return 0;
        }
        // 10번째 바이트(shift 63)에는 1비트만 남는다
        if (shift == 63 && (byte & 0x7E) != 0) {
            break;
        }
        value |= (byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
    }
    m_error = true; // 너무 길다
    return 0;
}

u64 BitReader::readVarU(u64 max) {
    const u64 v = readVarU();
    if (v > max) {
        m_error = true;
        return 0;
    }
    return v;
}

i64 BitReader::readVarI() {
    const u64 z = readVarU();
    return static_cast<i64>(z >> 1) ^ -static_cast<i64>(z & 1);
}

f32 BitReader::readF32() {
    return std::bit_cast<f32>(static_cast<u32>(readBits(32)));
}

std::string BitReader::readString(usize maxBytes) {
    const u64 len = readVarU(maxBytes);
    if (m_error || remainingBits() < len * 8) {
        m_error = true;
        return {};
    }
    std::string s;
    s.resize(static_cast<usize>(len));
    for (auto& c : s) {
        c = static_cast<char>(readBits(8));
    }
    if (!validUtf8(s)) {
        m_error = true;
        return {};
    }
    return s;
}

std::vector<std::byte> BitReader::readBytes(usize maxBytes) {
    const u64 len = readVarU(maxBytes);
    if (m_error || remainingBits() < len * 8) {
        m_error = true;
        return {};
    }
    std::vector<std::byte> out(static_cast<usize>(len));
    for (auto& b : out) {
        b = static_cast<std::byte>(readBits(8));
    }
    return out;
}

void BitReader::alignToByte() {
    if (!m_error) {
        m_pos = std::min((m_pos + 7) / 8 * 8, m_data.size() * 8);
    }
}

bool BitReader::atEnd() const noexcept {
    if (m_error) {
        return false;
    }
    const usize remaining = m_data.size() * 8 - m_pos;
    if (remaining >= 8) {
        return false;
    }
    if (remaining == 0) {
        return true;
    }
    // 마지막 바이트의 남은 비트는 0 이어야 한다
    const auto last = static_cast<u8>(m_data.back());
    return (last >> (8 - remaining)) == 0;
}

} // namespace sbx::net
