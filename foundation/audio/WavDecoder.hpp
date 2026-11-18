#pragma once
// WAV (RIFF WAVE) 디코더 (v0.11 NG-17 — 오디오 출력, ADR-0153). 소리 에셋 · 오디오 믹서가 같이 쓴다 — 순수 (파일 · 장치
// 없음).
//
//   형식   PCM 정수 8 · 16 · 24 · 32 비트, IEEE float 32 비트 (WAVE_FORMAT_EXTENSIBLE 의 같은 하위 형식 포함)
//   채널   1 · 2 (그 밖은 Unsupported), 표본율 8,000 ~ 192,000
//   결과   채널을 엇갈린 f32 [-1, 1] (8 비트는 부호 없는 128 가운데). 길이 상한 kMaxWavFrames (넘으면 OutOfRange)
// 믿지 않는 파일 — 덩어리 크기 · 정렬 · 남는 바이트를 모두 본다 (잘린 data 덩어리는 있는 만큼만, 홀수 덩어리는 한
// 바이트 채움).

#include <span>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::audio {

inline constexpr u64 kMaxWavFrames = 48'000ull * 60 * 10; // 10 분 (48 kHz) — 그보다 긴 소리는 스트리밍 [계획]
inline constexpr u32 kMinWavRate = 8'000;
inline constexpr u32 kMaxWavRate = 192'000;

struct DecodedAudio {
    u32 sampleRate = 0;
    u16 channels = 0;
    std::vector<f32> samples; // 엇갈린 표본 — 크기 = frames × channels

    [[nodiscard]] u64 frames() const noexcept {
        return channels == 0 ? 0 : samples.size() / static_cast<usize>(channels);
    }
    [[nodiscard]] f64 seconds() const noexcept {
        return sampleRate == 0 ? 0.0 : static_cast<f64>(frames()) / static_cast<f64>(sampleRate);
    }
};

[[nodiscard]] Expected<DecodedAudio> decodeWav(std::span<const u8> bytes);

} // namespace sbx::audio
