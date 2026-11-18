#include "foundation/audio/WavDecoder.hpp"

#include <bit>
#include <cstring>
#include <format>

namespace sbx::audio {
namespace {

constexpr u16 kFormatPcm = 1;
constexpr u16 kFormatFloat = 3;
constexpr u16 kFormatExtensible = 0xFFFE;

u16 le16(const u8* p) noexcept {
    return static_cast<u16>(p[0] | (p[1] << 8));
}
u32 le32(const u8* p) noexcept {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

std::unexpected<Error> bad(std::string what) {
    return makeError(ErrorCode::ParseError, std::format("WAV: {}", what), "decodeWav");
}

f32 sampleAt(const u8* p, u16 bits, bool isFloat) noexcept {
    if (isFloat) {
        const f32 v = std::bit_cast<f32>(le32(p));
        return v != v ? 0.f : (v < -1.f ? -1.f : (v > 1.f ? 1.f : v)); // NaN → 0, 범위로 자름
    }
    switch (bits) {
    case 8:
        return (static_cast<f32>(p[0]) - 128.f) / 128.f;
    case 16:
        return static_cast<f32>(static_cast<i16>(le16(p))) / 32768.f;
    case 24: {
        i32 v =
            static_cast<i32>(static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16));
        if ((v & 0x80'0000) != 0) {
            v -= 0x100'0000; // 부호 확장
        }
        return static_cast<f32>(v) / 8388608.f;
    }
    default: // 32
        return static_cast<f32>(static_cast<f64>(static_cast<i32>(le32(p))) / 2147483648.0);
    }
}

} // namespace

Expected<DecodedAudio> decodeWav(std::span<const u8> bytes) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return bad("RIFF WAVE 머리가 아니다");
    }
    bool haveFmt = false;
    u16 format = 0;
    u16 channels = 0;
    u32 rate = 0;
    u16 blockAlign = 0;
    u16 bits = 0;
    const u8* data = nullptr;
    usize dataSize = 0;
    usize at = 12;
    while (at + 8 <= bytes.size()) {
        const u8* chunk = bytes.data() + at;
        const usize size = le32(chunk + 4);
        const usize body = at + 8;
        const usize avail = bytes.size() - body;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16 || size > avail) {
                return bad("fmt 덩어리 크기가 맞지 않다");
            }
            const u8* f = chunk + 8;
            format = le16(f);
            channels = le16(f + 2);
            rate = le32(f + 4);
            blockAlign = le16(f + 12);
            bits = le16(f + 14);
            if (format == kFormatExtensible) {
                if (size < 40) {
                    return bad("WAVE_FORMAT_EXTENSIBLE 의 fmt 가 짧다");
                }
                format = le16(f + 24); // 하위 형식 GUID 의 앞 두 바이트 = PCM 1 · float 3
            }
            haveFmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataSize = size <= avail ? size : avail; // 잘린 파일은 있는 만큼
        }
        const usize step = 8 + size + (size & 1u); // 홀수 덩어리는 채움 한 바이트
        if (size > bytes.size() || step > bytes.size() - at) {
            break;
        }
        at += step;
    }
    if (!haveFmt) {
        return bad("fmt 덩어리가 없다");
    }
    if (data == nullptr) {
        return bad("data 덩어리가 없다");
    }
    const bool isFloat = format == kFormatFloat;
    if (format != kFormatPcm && !isFloat) {
        return makeError(ErrorCode::Unsupported, std::format("WAV: 형식 {} — PCM · float 만", format), "decodeWav");
    }
    if (channels < 1 || channels > 2) {
        return makeError(ErrorCode::Unsupported, std::format("WAV: 채널 {} — 1 · 2 만", channels), "decodeWav");
    }
    if (rate < kMinWavRate || rate > kMaxWavRate) {
        return makeError(ErrorCode::Unsupported,
                         std::format("WAV: 표본율 {} — {} ~ {}", rate, kMinWavRate, kMaxWavRate), "decodeWav");
    }
    if (isFloat ? bits != 32 : (bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        return makeError(ErrorCode::Unsupported, std::format("WAV: 표본 {} 비트", bits), "decodeWav");
    }
    const u16 sampleBytes = static_cast<u16>(bits / 8);
    if (blockAlign != sampleBytes * channels) {
        return bad(std::format("blockAlign {} 이 채널 × 표본 크기 {} 와 다르다", blockAlign, sampleBytes * channels));
    }
    const u64 frames = dataSize / blockAlign;
    if (frames > kMaxWavFrames) {
        return makeError(ErrorCode::OutOfRange, std::format("WAV: {} 프레임 — {} 까지", frames, kMaxWavFrames),
                         "decodeWav");
    }
    DecodedAudio out;
    out.sampleRate = rate;
    out.channels = channels;
    out.samples.resize(static_cast<usize>(frames) * static_cast<usize>(channels));
    const u8* p = data;
    for (usize i = 0; i < out.samples.size(); ++i, p += sampleBytes) {
        out.samples[i] = sampleAt(p, bits, isFloat);
    }
    return out;
}

} // namespace sbx::audio
