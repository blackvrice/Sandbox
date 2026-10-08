#pragma once
// 와이어 비트스트림. docs/09-SERIALIZATION.md 5장.
//
//   비트 순서   바이트 안에서 낮은 비트부터 채운다 (값도 낮은 비트부터). 결과적으로 바이트 정렬된 고정 폭 정수는
//               리틀 엔디안 바이트열과 같다.
//   varint      LEB128 — 7비트씩, 이어짐 비트 0x80. 부호 있는 값은 zigzag. 최대 10바이트 (u64)
//   f32         IEEE 754 비트 그대로 32비트. 와이어 쪽 양자화는 Phase 10 (09 5장)
//   문자열 · 바이트열  varint 길이 + 바이트. 읽을 때 상한을 반드시 준다
//
// 읽기 오류 (경계 초과 · 상한 초과 · varint 너무 김 · 잘못된 UTF-8): error() 가 true 가 되고 그 뒤 모든 읽기는 0 · 빈
// 값을 돌려준다. 예외를 던지지 않는다 — 호출자는 메시지 하나를 다 읽은 뒤 한 번 검사하고 버린다 (08 11장).

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::net {

class BitWriter {
public:
    void writeBits(u64 value, u32 bits); // bits 1 ~ 64, 위쪽 비트는 무시
    void writeBool(bool value) { writeBits(value ? 1 : 0, 1); }
    void writeU8(u8 value) { writeBits(value, 8); }
    void writeU16(u16 value) { writeBits(value, 16); }
    void writeU32(u32 value) { writeBits(value, 32); }
    void writeU64(u64 value) { writeBits(value, 64); }
    void writeVarU(u64 value);
    void writeVarI(i64 value);
    void writeF32(f32 value);
    void writeString(std::string_view text); // 상한은 쓰는 쪽이 지킨다 (읽는 쪽이 검사)
    void writeBytes(std::span<const std::byte> bytes);
    // 다음 바이트 경계까지 0 비트
    void alignToByte();

    [[nodiscard]] usize bitCount() const noexcept { return m_bits; }
    [[nodiscard]] usize byteCount() const noexcept { return (m_bits + 7) / 8; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return m_data; }
    [[nodiscard]] std::vector<std::byte> take() &&;

private:
    std::vector<std::byte> m_data;
    usize m_bits = 0;
};

class BitReader {
public:
    explicit BitReader(std::span<const std::byte> data) noexcept : m_data(data) {}

    [[nodiscard]] u64 readBits(u32 bits);
    [[nodiscard]] bool readBool() { return readBits(1) != 0; }
    [[nodiscard]] u8 readU8() { return static_cast<u8>(readBits(8)); }
    [[nodiscard]] u16 readU16() { return static_cast<u16>(readBits(16)); }
    [[nodiscard]] u32 readU32() { return static_cast<u32>(readBits(32)); }
    [[nodiscard]] u64 readU64() { return readBits(64); }
    [[nodiscard]] u64 readVarU();
    // max 를 넘으면 오류 (예: 개수 · 길이 · 열거 값)
    [[nodiscard]] u64 readVarU(u64 max);
    [[nodiscard]] i64 readVarI();
    [[nodiscard]] f32 readF32();
    // 바이트 길이가 maxBytes 를 넘거나 UTF-8 이 아니면 오류
    [[nodiscard]] std::string readString(usize maxBytes);
    [[nodiscard]] std::vector<std::byte> readBytes(usize maxBytes);
    void alignToByte();

    // 잘못된 값을 발견한 호출자가 오류로 표시한다 (범위 밖 열거 값 등)
    void fail() noexcept { m_error = true; }
    [[nodiscard]] bool error() const noexcept { return m_error; }
    [[nodiscard]] usize remainingBits() const noexcept { return m_error ? 0 : m_data.size() * 8 - m_pos; }
    // 메시지 끝: 남은 비트가 마지막 바이트의 0 채움뿐인가
    [[nodiscard]] bool atEnd() const noexcept;

private:
    std::span<const std::byte> m_data;
    usize m_pos = 0;
    bool m_error = false;
};

} // namespace sbx::net
