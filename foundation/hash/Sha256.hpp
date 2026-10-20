#pragma once
// SHA-256 (FIPS 180-4) — 게임 패키지의 파일 · 다이제스트 (v0.2 M5-T2, ADR-0054, docs/09-SERIALIZATION.md 9장).
// 보안 경계가 아니라 무결성 · 동일성 확인용이다 (서명은 [계획] — v0.6 Hub). 시뮬레이션은 쓰지 않는다 (지문은 FNV-1a).
//
//   Sha256 h;  h.update(bytes);  h.update(more);  const Sha256Digest d = h.finish();   toHex(d) = 소문자 64 글자

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx {

using Sha256Digest = std::array<u8, 32>;

class Sha256 {
public:
    Sha256() noexcept;
    void update(std::span<const u8> bytes) noexcept;
    void update(std::string_view text) noexcept;
    // 끝낸다 — 그 뒤 update · finish 는 쓰지 않는다 (새로 만든다)
    [[nodiscard]] Sha256Digest finish() noexcept;

private:
    void block(const u8* p) noexcept;
    std::array<u32, 8> m_state{};
    std::array<u8, 64> m_buffer{};
    usize m_buffered = 0;
    u64 m_length = 0; // 바이트
};

[[nodiscard]] Sha256Digest sha256(std::span<const u8> bytes) noexcept;
[[nodiscard]] Sha256Digest sha256(std::string_view text) noexcept;
[[nodiscard]] std::string toHex(const Sha256Digest& d);
// 64 글자 소문자 16진 문자열인가
[[nodiscard]] bool isSha256Hex(std::string_view text) noexcept;

} // namespace sbx
