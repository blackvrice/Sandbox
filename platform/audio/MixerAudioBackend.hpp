#pragma once
// 소프트웨어 믹서 오디오 백엔드 (v0.11 NG-17, ADR-0153). docs/07-PLATFORM.md 7장.
//
// 목소리마다 소리 (디코드된 f32 PCM — SoundAsset::samples) 를 선형 보간으로 장치 표본율에 맞춰 (× pitch) 버스 볼륨 ·
// 목소리 볼륨 · 2D 패닝 (positional — 리스너와의 x 차이 · 거리) 을 곱해 더한다. 장치 (IAudioDevice — waveOut ·
// AudioQueue · ALSA) 가 자기 스레드에서 render() 를 부른다 — 모든 공개 메서드는 한 뮤텍스로 (장치 스레드 · 메인
// 스레드). 장치가 없으면 (시험) render() 를 직접 불러 표본을 본다. 반복 아닌 목소리는 끝까지 그린 뒤 update() 가 거둔다
// (NullAudioBackend 와 같은 수명 규칙). 오디오는 표시 쪽만 — 시뮬레이션 · WorldHash 에 닿지 않는다 (04).

#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

#include "platform/common/Audio.hpp"

namespace sbx::platform {

// 장치 스레드가 부른다 — interleaved 를 frames × channels 개 채운다 ([-1, 1])
using AudioRenderFn = std::function<void(f32* interleaved, u32 frames)>;

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    // 장치를 열고 스레드를 시작한다 (실패면 false — 장치 없음 · 권한). 표본율 · 채널은 장치가 받은 대로 맞춘다
    virtual bool start(u32 sampleRate, u16 channels, AudioRenderFn render) = 0;
    virtual void stop() = 0; // 스레드를 멈추고 닫는다 (두 번 불러도 된다)
    [[nodiscard]] virtual std::string_view name() const = 0;
};

// 이 OS 의 기본 출력 장치 (Windows waveOut · macOS AudioQueue · Linux ALSA — libasound 를 실행 중에 연다). 없으면 null
[[nodiscard]] std::unique_ptr<IAudioDevice> createSystemAudioDevice();

inline constexpr f32 kAudioPanRange = 16.f; // positional: 리스너와 x 가 이만큼 떨어지면 한쪽 끝 (월드 단위)
inline constexpr f32 kAudioFalloff = 24.f;  // positional: 볼륨 = 1 / (1 + 거리 / 이것)

class MixerAudioBackend final : public IAudioBackend {
public:
    explicit MixerAudioBackend(std::unique_ptr<IAudioDevice> device = nullptr);
    ~MixerAudioBackend() override;
    MixerAudioBackend(const MixerAudioBackend&) = delete;
    MixerAudioBackend& operator=(const MixerAudioBackend&) = delete;

    bool init(const AudioDesc& desc) override; // 장치가 있으면 연다 — 실패면 false (장치 없이 쓰려면 null 장치)
    void shutdown() override;

    [[nodiscard]] SoundHandle load(const SoundAsset& asset) override; // samples 가 없거나 길이가 맞지 않으면 무효
    void unload(SoundHandle sound) override;

    [[nodiscard]] VoiceId play(SoundHandle sound, const PlayParams& params) override;
    void stop(VoiceId voice) override;
    [[nodiscard]] bool playing(VoiceId voice) const override;

    void setListener(Vec2 position) override;
    void setBusVolume(AudioBus bus, f32 volume) override;
    [[nodiscard]] f32 busVolume(AudioBus bus) const override;

    void update(f32 dtSeconds) override;
    [[nodiscard]] usize activeVoiceCount() const override;

    // 장치 스레드 (또는 시험) — interleaved 를 frames × channels 개 채운다
    void render(f32* interleaved, u32 frames);
    [[nodiscard]] const AudioDesc& desc() const noexcept { return m_desc; }
    [[nodiscard]] std::string_view deviceName() const noexcept;

private:
    struct SoundSlot {
        u32 generation = 0;
        bool live = false;
        u32 sampleRate = 0;
        u16 channels = 1;
        u64 frames = 0;
        std::shared_ptr<const std::vector<f32>> samples;
    };
    struct VoiceSlot {
        u32 generation = 0;
        bool live = false;
        bool done = false; // 끝까지 그렸다 (update 가 거둔다)
        u32 sound = 0;     // m_sounds 자리
        f64 cursor = 0;    // 소리 프레임 (소수)
        PlayParams params;
    };

    [[nodiscard]] const SoundSlot* soundAt(SoundHandle h) const noexcept;
    [[nodiscard]] VoiceSlot* voiceAt(VoiceId v) noexcept;
    [[nodiscard]] const VoiceSlot* voiceAt(VoiceId v) const noexcept;

    std::unique_ptr<IAudioDevice> m_device;
    mutable std::mutex m_mutex;
    bool m_initialized = false;
    AudioDesc m_desc;
    std::vector<SoundSlot> m_sounds;
    std::vector<VoiceSlot> m_voices; // 크기 = maxVoices
    f32 m_bus[kAudioBusCount] = {1.f, 1.f, 1.f, 1.f};
    Vec2 m_listener;
};

} // namespace sbx::platform
