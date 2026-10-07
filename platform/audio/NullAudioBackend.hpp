#pragma once
// 소리를 내지 않는 오디오 백엔드. 핸들·목소리 수명·버스 볼륨 규칙은 실제 백엔드와 같다 —
// 오디오 장치가 없는 CI·서버 PC·--headless 에서 쓰고, MiniaudioBackend 의 동작 기준이 된다.

#include <vector>

#include "platform/common/Audio.hpp"

namespace sbx::platform {

class NullAudioBackend final : public IAudioBackend {
public:
    bool init(const AudioDesc& desc) override;
    void shutdown() override;

    [[nodiscard]] SoundHandle load(const SoundAsset& asset) override;
    void unload(SoundHandle sound) override;

    [[nodiscard]] VoiceId play(SoundHandle sound, const PlayParams& params) override;
    void stop(VoiceId voice) override;
    [[nodiscard]] bool playing(VoiceId voice) const override;

    void setListener(Vec2 position) override { m_listener = position; }
    void setBusVolume(AudioBus bus, f32 volume) override;
    [[nodiscard]] f32 busVolume(AudioBus bus) const override;

    void update(f32 dtSeconds) override;

    [[nodiscard]] usize activeVoiceCount() const override;

    [[nodiscard]] Vec2 listener() const noexcept { return m_listener; }

private:
    struct SoundSlot {
        u32 generation = 0;
        bool live = false;
        f64 seconds = 0; // 길이
    };
    struct VoiceSlot {
        u32 generation = 0;
        bool live = false;
        SoundHandle sound;
        f64 remaining = 0; // 남은 재생 시간 (pitch 반영)
        bool loop = false;
    };

    [[nodiscard]] const SoundSlot* soundAt(SoundHandle h) const noexcept;
    [[nodiscard]] const VoiceSlot* voiceAt(VoiceId v) const noexcept;
    void endVoice(usize index) noexcept;

    bool m_initialized = false;
    AudioDesc m_desc;
    std::vector<SoundSlot> m_sounds;
    std::vector<VoiceSlot> m_voices; // 크기 = maxVoices
    f32 m_bus[kAudioBusCount] = {1.f, 1.f, 1.f, 1.f};
    Vec2 m_listener;
};

} // namespace sbx::platform
