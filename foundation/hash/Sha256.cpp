#include "foundation/hash/Sha256.hpp"

#include <algorithm>
#include <cstring>

namespace sbx {
namespace {

constexpr std::array<u32, 64> kK{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr u32 rotr(u32 x, u32 n) noexcept {
    return (x >> n) | (x << (32u - n));
}

} // namespace

Sha256::Sha256() noexcept
    : m_state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u} {}

void Sha256::block(const u8* p) noexcept {
    std::array<u32, 64> w{};
    for (usize i = 0; i < 16; ++i) { // 큰 끝 (big-endian)
        w[i] = (u32{p[i * 4]} << 24) | (u32{p[i * 4 + 1]} << 16) | (u32{p[i * 4 + 2]} << 8) | u32{p[i * 4 + 3]};
    }
    for (usize i = 16; i < 64; ++i) {
        const u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    u32 e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
    for (usize i = 0; i < 64; ++i) {
        const u32 s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const u32 ch = (e & f) ^ (~e & g);
        const u32 t1 = h + s1 + ch + kK[i] + w[i];
        const u32 s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const u32 maj = (a & b) ^ (a & c) ^ (b & c);
        const u32 t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::update(std::span<const u8> bytes) noexcept {
    m_length += bytes.size();
    usize i = 0;
    if (m_buffered > 0) {
        const usize take = std::min(bytes.size(), usize{64} - m_buffered);
        std::memcpy(m_buffer.data() + m_buffered, bytes.data(), take);
        m_buffered += take;
        i = take;
        if (m_buffered < 64) {
            return;
        }
        block(m_buffer.data());
        m_buffered = 0;
    }
    for (; i + 64 <= bytes.size(); i += 64) {
        block(bytes.data() + i);
    }
    const usize rest = bytes.size() - i;
    if (rest > 0) {
        std::memcpy(m_buffer.data(), bytes.data() + i, rest);
        m_buffered = rest;
    }
}

void Sha256::update(std::string_view text) noexcept {
    update(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
}

Sha256Digest Sha256::finish() noexcept {
    const u64 bits = m_length * 8u;
    const std::array<u8, 1> one{0x80};
    update(one);
    const std::array<u8, 64> zeros{};
    const usize pad = m_buffered <= 56 ? 56 - m_buffered : 120 - m_buffered;
    update(std::span<const u8>(zeros.data(), pad));
    std::array<u8, 8> len{};
    for (usize i = 0; i < 8; ++i) {
        len[i] = static_cast<u8>(bits >> (56 - 8 * i));
    }
    update(len);
    Sha256Digest out{};
    for (usize i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<u8>(m_state[i] >> 24);
        out[i * 4 + 1] = static_cast<u8>(m_state[i] >> 16);
        out[i * 4 + 2] = static_cast<u8>(m_state[i] >> 8);
        out[i * 4 + 3] = static_cast<u8>(m_state[i]);
    }
    return out;
}

Sha256Digest sha256(std::span<const u8> bytes) noexcept {
    Sha256 h;
    h.update(bytes);
    return h.finish();
}

Sha256Digest sha256(std::string_view text) noexcept {
    Sha256 h;
    h.update(text);
    return h.finish();
}

std::string toHex(const Sha256Digest& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (const u8 b : d) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0F]);
    }
    return s;
}

bool isSha256Hex(std::string_view text) noexcept {
    if (text.size() != 64) {
        return false;
    }
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace sbx
