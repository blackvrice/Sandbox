#include "platform/audio/NullAudioBackend.hpp"

#include <algorithm>
#include <cmath>

namespace sbx::platform {

bool NullAudioBackend::init(const AudioDesc& desc) {
    m_desc = desc;
    m_voices.assign(desc.maxVoices, VoiceSlot{});
    m_initialized = true;
    return true;
}

void NullAudioBackend::shutdown() {
    for (usize i = 0; i < m_voices.size(); ++i) {
        endVoice(i);
    }
    for (SoundSlot& s : m_sounds) {
        if (s.live) {
            s.live = false;
            ++s.generation;
        }
    }
    m_initialized = false;
}

SoundHandle NullAudioBackend::load(const SoundAsset& asset) {
    if (!m_initialized || asset.sampleRate == 0 || asset.channels == 0) {
        return {};
    }
    const f64 seconds = static_cast<f64>(asset.frameCount) / static_cast<f64>(asset.sampleRate);
    auto it = std::ranges::find_if(m_sounds, [](const SoundSlot& s) { return !s.live; });
    if (it == m_sounds.end()) {
        m_sounds.push_back({});
        it = m_sounds.end() - 1;
    }
    it->live = true;
    it->seconds = seconds;
    return SoundHandle{static_cast<u32>(it - m_sounds.begin()), it->generation};
}

void NullAudioBackend::unload(SoundHandle sound) {
    if (soundAt(sound) == nullptr) {
        return;
    }
    for (usize i = 0; i < m_voices.size(); ++i) {
        if (m_voices[i].live && m_voices[i].sound == sound) {
            endVoice(i);
        }
    }
    SoundSlot& s = m_sounds[sound.index];
    s.live = false;
    ++s.generation;
}

VoiceId NullAudioBackend::play(SoundHandle sound, const PlayParams& params) {
    const SoundSlot* s = soundAt(sound);
    if (s == nullptr || !(params.pitch > 0.f)) {
        return {};
    }
    const auto it = std::ranges::find_if(m_voices, [](const VoiceSlot& v) { return !v.live; });
    if (it == m_voices.end()) {
        return {}; // 목소리 상한
    }
    it->live = true;
    it->sound = sound;
    it->loop = params.loop;
    it->remaining = s->seconds / static_cast<f64>(params.pitch);
    return VoiceId{static_cast<u32>(it - m_voices.begin()), it->generation};
}

void NullAudioBackend::stop(VoiceId voice) {
    if (voiceAt(voice) != nullptr) {
        endVoice(voice.index);
    }
}

bool NullAudioBackend::playing(VoiceId voice) const {
    return voiceAt(voice) != nullptr;
}

void NullAudioBackend::setBusVolume(AudioBus bus, f32 volume) {
    const auto i = static_cast<usize>(bus);
    if (i < kAudioBusCount) {
        m_bus[i] = std::isnan(volume) ? 0.f : std::clamp(volume, 0.f, 1.f); // NaN 은 clamp 를 그대로 통과한다
    }
}

f32 NullAudioBackend::busVolume(AudioBus bus) const {
    const auto i = static_cast<usize>(bus);
    return i < kAudioBusCount ? m_bus[i] : 0.f;
}

void NullAudioBackend::update(f32 dtSeconds) {
    if (!(dtSeconds > 0.f)) {
        return;
    }
    for (usize i = 0; i < m_voices.size(); ++i) {
        VoiceSlot& v = m_voices[i];
        if (!v.live || v.loop) {
            continue;
        }
        v.remaining -= static_cast<f64>(dtSeconds);
        if (v.remaining <= 0) {
            endVoice(i);
        }
    }
}

usize NullAudioBackend::activeVoiceCount() const {
    return static_cast<usize>(std::ranges::count_if(m_voices, [](const VoiceSlot& v) { return v.live; }));
}

const NullAudioBackend::SoundSlot* NullAudioBackend::soundAt(SoundHandle h) const noexcept {
    if (!h.valid() || h.index >= m_sounds.size()) {
        return nullptr;
    }
    const SoundSlot& s = m_sounds[h.index];
    return s.live && s.generation == h.generation ? &s : nullptr;
}

const NullAudioBackend::VoiceSlot* NullAudioBackend::voiceAt(VoiceId v) const noexcept {
    if (!v.valid() || v.index >= m_voices.size()) {
        return nullptr;
    }
    const VoiceSlot& s = m_voices[v.index];
    return s.live && s.generation == v.generation ? &s : nullptr;
}

void NullAudioBackend::endVoice(usize index) noexcept {
    VoiceSlot& v = m_voices[index];
    if (v.live) {
        v.live = false;
        ++v.generation;
    }
}

} // namespace sbx::platform
