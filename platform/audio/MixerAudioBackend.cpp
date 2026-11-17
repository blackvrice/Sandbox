#include "platform/audio/MixerAudioBackend.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sbx::platform {

namespace {

f32 clamp01(f32 v) noexcept {
    return std::isnan(v) ? 0.f : std::clamp(v, 0.f, 1.f); // NaN 은 clamp 를 그대로 통과한다
}

} // namespace

MixerAudioBackend::MixerAudioBackend(std::unique_ptr<IAudioDevice> device) : m_device(std::move(device)) {}

MixerAudioBackend::~MixerAudioBackend() {
    shutdown();
}

bool MixerAudioBackend::init(const AudioDesc& desc) {
    {
        const std::lock_guard lock(m_mutex);
        m_desc = desc;
        if (m_desc.channels < 1 || m_desc.channels > 2) {
            m_desc.channels = 2;
        }
        m_voices.assign(desc.maxVoices, VoiceSlot{});
        m_initialized = true;
    }
    // 장치 스레드는 뮤텍스 밖에서 연다 (시작하자마자 render 가 뮤텍스를 잡는다)
    if (m_device &&
        !m_device->start(m_desc.sampleRate, m_desc.channels, [this](f32* out, u32 frames) { render(out, frames); })) {
        const std::lock_guard lock(m_mutex);
        m_initialized = false;
        return false;
    }
    return true;
}

void MixerAudioBackend::shutdown() {
    if (m_device) {
        m_device->stop(); // 스레드가 끝난 뒤에 상태를 비운다
    }
    const std::lock_guard lock(m_mutex);
    for (VoiceSlot& v : m_voices) {
        if (v.live) {
            v.live = false;
            ++v.generation;
        }
    }
    for (SoundSlot& s : m_sounds) {
        if (s.live) {
            s.live = false;
            s.samples.reset();
            ++s.generation;
        }
    }
    m_initialized = false;
}

std::string_view MixerAudioBackend::deviceName() const noexcept {
    return m_device ? m_device->name() : std::string_view("없음");
}

SoundHandle MixerAudioBackend::load(const SoundAsset& asset) {
    const std::lock_guard lock(m_mutex);
    if (!m_initialized || asset.sampleRate == 0 || asset.channels < 1 || asset.channels > 2 || !asset.samples ||
        asset.samples->size() != asset.frameCount * asset.channels) {
        return {};
    }
    auto it = std::ranges::find_if(m_sounds, [](const SoundSlot& s) { return !s.live; });
    if (it == m_sounds.end()) {
        m_sounds.push_back({});
        it = m_sounds.end() - 1;
    }
    it->live = true;
    it->sampleRate = asset.sampleRate;
    it->channels = asset.channels;
    it->frames = asset.frameCount;
    it->samples = asset.samples;
    return SoundHandle{static_cast<u32>(it - m_sounds.begin()), it->generation};
}

void MixerAudioBackend::unload(SoundHandle sound) {
    const std::lock_guard lock(m_mutex);
    if (soundAt(sound) == nullptr) {
        return;
    }
    for (VoiceSlot& v : m_voices) {
        if (v.live && v.sound == sound.index) {
            v.live = false;
            ++v.generation;
        }
    }
    SoundSlot& s = m_sounds[sound.index];
    s.live = false;
    s.samples.reset();
    ++s.generation;
}

VoiceId MixerAudioBackend::play(SoundHandle sound, const PlayParams& params) {
    const std::lock_guard lock(m_mutex);
    if (soundAt(sound) == nullptr || !(params.pitch > 0.f) || !std::isfinite(params.pitch)) {
        return {};
    }
    const auto it = std::ranges::find_if(m_voices, [](const VoiceSlot& v) { return !v.live; });
    if (it == m_voices.end()) {
        return {}; // 목소리 상한
    }
    it->live = true;
    it->done = false;
    it->sound = sound.index;
    it->cursor = 0;
    it->params = params;
    it->params.volume = clamp01(params.volume);
    return VoiceId{static_cast<u32>(it - m_voices.begin()), it->generation};
}

void MixerAudioBackend::stop(VoiceId voice) {
    const std::lock_guard lock(m_mutex);
    if (VoiceSlot* v = voiceAt(voice)) {
        v->live = false;
        ++v->generation;
    }
}

bool MixerAudioBackend::playing(VoiceId voice) const {
    const std::lock_guard lock(m_mutex);
    const VoiceSlot* v = voiceAt(voice);
    return v != nullptr && !v->done;
}

void MixerAudioBackend::setListener(Vec2 position) {
    const std::lock_guard lock(m_mutex);
    m_listener = position;
}

void MixerAudioBackend::setBusVolume(AudioBus bus, f32 volume) {
    const auto i = static_cast<usize>(bus);
    if (i < kAudioBusCount) {
        const std::lock_guard lock(m_mutex);
        m_bus[i] = clamp01(volume);
    }
}

f32 MixerAudioBackend::busVolume(AudioBus bus) const {
    const auto i = static_cast<usize>(bus);
    const std::lock_guard lock(m_mutex);
    return i < kAudioBusCount ? m_bus[i] : 0.f;
}

void MixerAudioBackend::update(f32 /*dtSeconds*/) {
    // 끝은 render 가 표본으로 정한다 (시간이 아니라) — 여기서는 거두기만
    const std::lock_guard lock(m_mutex);
    for (VoiceSlot& v : m_voices) {
        if (v.live && v.done) {
            v.live = false;
            ++v.generation;
        }
    }
}

usize MixerAudioBackend::activeVoiceCount() const {
    const std::lock_guard lock(m_mutex);
    return static_cast<usize>(std::ranges::count_if(m_voices, [](const VoiceSlot& v) { return v.live && !v.done; }));
}

void MixerAudioBackend::render(f32* out, u32 frames) {
    const std::lock_guard lock(m_mutex);
    const u16 channels = m_desc.channels;
    std::fill(out, out + static_cast<usize>(frames) * channels, 0.f);
    if (!m_initialized || m_desc.sampleRate == 0) {
        return;
    }
    const f32 master = m_bus[static_cast<usize>(AudioBus::Master)];
    for (VoiceSlot& v : m_voices) {
        if (!v.live || v.done) {
            continue;
        }
        const SoundSlot& s = m_sounds[v.sound];
        if (!s.live || !s.samples || s.frames == 0) {
            v.done = true;
            continue;
        }
        const usize bus = static_cast<usize>(v.params.bus) < kAudioBusCount ? static_cast<usize>(v.params.bus) : 1;
        f32 gain = v.params.volume * m_bus[bus] * (bus == 0 ? 1.f : master);
        // 2D: 리스너와의 x 차이로 패닝 (상수 파워), 거리로 줄인다. 아니면 가운데 그대로
        f32 left = 1.f;
        f32 right = 1.f;
        if (v.params.positional) {
            const Vec2 d = v.params.position - m_listener;
            const f32 pan = std::clamp(d.x / kAudioPanRange, -1.f, 1.f);
            const f32 angle = (pan + 1.f) * 0.25f * std::numbers::pi_v<f32>;
            left = std::cos(angle) * std::numbers::sqrt2_v<f32>;
            right = std::sin(angle) * std::numbers::sqrt2_v<f32>;
            gain /= 1.f + std::sqrt(d.lengthSquared()) / kAudioFalloff;
        }
        const std::vector<f32>& pcm = *s.samples;
        const f64 step =
            static_cast<f64>(s.sampleRate) / static_cast<f64>(m_desc.sampleRate) * static_cast<f64>(v.params.pitch);
        for (u32 f = 0; f < frames; ++f) {
            if (v.cursor >= static_cast<f64>(s.frames)) {
                if (!v.params.loop) {
                    v.done = true;
                    break;
                }
                v.cursor = std::fmod(v.cursor, static_cast<f64>(s.frames));
            }
            const u64 i0 = static_cast<u64>(v.cursor);
            const u64 i1 = i0 + 1 < s.frames ? i0 + 1 : (v.params.loop ? 0 : i0);
            const f32 t = static_cast<f32>(v.cursor - static_cast<f64>(i0));
            f32 l = 0.f;
            f32 r = 0.f;
            if (s.channels == 1) {
                l = r = pcm[i0] + (pcm[i1] - pcm[i0]) * t;
            } else {
                l = pcm[i0 * 2] + (pcm[i1 * 2] - pcm[i0 * 2]) * t;
                r = pcm[i0 * 2 + 1] + (pcm[i1 * 2 + 1] - pcm[i0 * 2 + 1]) * t;
            }
            if (channels == 1) {
                out[f] += (l * left + r * right) * 0.5f * gain;
            } else {
                out[f * 2] += l * left * gain;
                out[f * 2 + 1] += r * right * gain;
            }
            v.cursor += step;
        }
    }
    for (usize i = 0; i < static_cast<usize>(frames) * channels; ++i) {
        out[i] = std::clamp(out[i], -1.f, 1.f);
    }
}

const MixerAudioBackend::SoundSlot* MixerAudioBackend::soundAt(SoundHandle h) const noexcept {
    if (!h.valid() || h.index >= m_sounds.size()) {
        return nullptr;
    }
    const SoundSlot& s = m_sounds[h.index];
    return s.live && s.generation == h.generation ? &s : nullptr;
}

MixerAudioBackend::VoiceSlot* MixerAudioBackend::voiceAt(VoiceId v) noexcept {
    if (!v.valid() || v.index >= m_voices.size()) {
        return nullptr;
    }
    VoiceSlot& s = m_voices[v.index];
    return s.live && s.generation == v.generation ? &s : nullptr;
}

const MixerAudioBackend::VoiceSlot* MixerAudioBackend::voiceAt(VoiceId v) const noexcept {
    if (!v.valid() || v.index >= m_voices.size()) {
        return nullptr;
    }
    const VoiceSlot& s = m_voices[v.index];
    return s.live && s.generation == v.generation ? &s : nullptr;
}

} // namespace sbx::platform
