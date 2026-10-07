#pragma once
// 오디오 백엔드. docs/07-PLATFORM.md 7장.
// 구현 순서: NullAudioBackend (Phase 6) → MiniaudioBackend (Phase 8 이후).
// 경로: Server EventStream → 복제 Event → Client AudioExtraction → AudioEvent 큐 → Audio 스레드 → IAudioBackend.
// 백엔드 메서드는 한 스레드(Audio 스레드, 지금은 메인)에서만 부른다.

#include <string>

#include "foundation/handle/Handle.hpp"
#include "foundation/math/Vec2.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::platform {

using SoundHandle = Handle<struct SoundTag>;
using VoiceId = Handle<struct VoiceTag>;

enum class AudioBus : u8 { Master = 0, Sfx, Ambient, Ui, Count };
inline constexpr usize kAudioBusCount = static_cast<usize>(AudioBus::Count);

struct AudioDesc {
    u32 sampleRate = 48000;
    u16 channels = 2;
    u16 maxVoices = 64; // 넘으면 play 가 무효 VoiceId 를 돌려준다
};

// 디코드된 PCM 의 설명. 표본 데이터는 Phase 8 의 AssetManager 가 들고 온다 — Null 백엔드는 길이만 쓴다.
struct SoundAsset {
    std::string name;
    u32 sampleRate = 48000;
    u16 channels = 1;
    u64 frameCount = 0; // 채널당 표본 수. 길이(초) = frameCount / sampleRate
};

struct PlayParams {
    f32 volume = 1.f; // 0..1 (버스 볼륨과 곱한다)
    f32 pitch = 1.f;  // 재생 속도 배율 (> 0)
    Vec2 position;    // 월드 좌표 — 리스너 기준 2D 패닝
    bool positional = false;
    bool loop = false;
    AudioBus bus = AudioBus::Sfx;
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual bool init(const AudioDesc& desc) = 0;
    virtual void shutdown() = 0;

    [[nodiscard]] virtual SoundHandle load(const SoundAsset& asset) = 0;
    virtual void unload(SoundHandle sound) = 0; // 그 소리를 내는 목소리도 멈춘다

    // 무효 핸들·목소리 상한 초과면 무효 VoiceId
    [[nodiscard]] virtual VoiceId play(SoundHandle sound, const PlayParams& params) = 0;
    virtual void stop(VoiceId voice) = 0; // 이미 끝난 목소리면 아무 일 없음
    [[nodiscard]] virtual bool playing(VoiceId voice) const = 0;

    virtual void setListener(Vec2 position) = 0;
    virtual void setBusVolume(AudioBus bus, f32 volume) = 0; // 0..1 로 자른다
    [[nodiscard]] virtual f32 busVolume(AudioBus bus) const = 0;

    // dtSeconds 만큼 진행한다: 끝난(반복 아님) 목소리를 거둔다. 실제 출력은 백엔드 스레드가 한다.
    virtual void update(f32 dtSeconds) = 0;

    [[nodiscard]] virtual usize activeVoiceCount() const = 0;
};

} // namespace sbx::platform
