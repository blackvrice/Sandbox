// Linux 오디오 장치 — ALSA (v0.11 NG-17, ADR-0153). libasound.so.2 를 실행 중에 연다 (dlopen — 빌드에 ALSA 개발 파일이
// 필요 없고, 없는 PC · 컨테이너는 장치 없음으로 → 앱이 Null 로). 장치 스레드가 20 ms 씩 render → 16 비트 →
// snd_pcm_writei (막힘 쓰기 — 장치가 속도를 정한다), 밑돌면 snd_pcm_recover. 필요한 함수 여섯만 이름으로 찾는다 (헤더
// 없이 — ABI 는 ALSA 1.x 의 공개 서명).
#include "platform/audio/MixerAudioBackend.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace sbx::platform {
namespace {

using snd_pcm_t = struct _snd_pcm;
using snd_pcm_sframes_t = long;
using snd_pcm_uframes_t = unsigned long;
constexpr int kStreamPlayback = 0; // SND_PCM_STREAM_PLAYBACK
constexpr int kFormatS16Le = 2;    // SND_PCM_FORMAT_S16_LE
constexpr int kAccessRwInterleaved = 3;

using OpenFn = int (*)(snd_pcm_t**, const char*, int, int);
using SetParamsFn = int (*)(snd_pcm_t*, int, int, unsigned int, unsigned int, int, unsigned int);
using WriteFn = snd_pcm_sframes_t (*)(snd_pcm_t*, const void*, snd_pcm_uframes_t);
using RecoverFn = int (*)(snd_pcm_t*, int, int);
using CloseFn = int (*)(snd_pcm_t*);
using DrainFn = int (*)(snd_pcm_t*);

// dlsym 의 void* → 함수 포인터 (객체 · 함수 포인터 사이 reinterpret_cast 는 -Wpedantic 경고 — 바이트로 옮긴다)
template <class F>
F symbol(void* lib, const char* name) {
    static_assert(sizeof(F) == sizeof(void*));
    void* p = dlsym(lib, name);
    F f = nullptr;
    std::memcpy(&f, &p, sizeof f);
    return f;
}

class AlsaDevice final : public IAudioDevice {
public:
    ~AlsaDevice() override {
        stop();
        if (m_lib != nullptr) {
            dlclose(m_lib);
        }
    }

    bool start(u32 sampleRate, u16 channels, AudioRenderFn render) override {
        stop();
        if (m_lib == nullptr) {
            m_lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
            if (m_lib == nullptr) {
                return false;
            }
            m_open = symbol<OpenFn>(m_lib, "snd_pcm_open");
            m_setParams = symbol<SetParamsFn>(m_lib, "snd_pcm_set_params");
            m_write = symbol<WriteFn>(m_lib, "snd_pcm_writei");
            m_recover = symbol<RecoverFn>(m_lib, "snd_pcm_recover");
            m_close = symbol<CloseFn>(m_lib, "snd_pcm_close");
            m_drop = symbol<DrainFn>(m_lib, "snd_pcm_drop");
        }
        if (m_open == nullptr || m_setParams == nullptr || m_write == nullptr || m_recover == nullptr ||
            m_close == nullptr || m_drop == nullptr) {
            return false;
        }
        if (m_open(&m_pcm, "default", kStreamPlayback, 0) < 0) {
            m_pcm = nullptr;
            return false;
        }
        // 소프트 리샘플 켬 · 지연 60 ms
        if (m_setParams(m_pcm, kFormatS16Le, kAccessRwInterleaved, channels, sampleRate, 1, 60'000) < 0) {
            m_close(m_pcm);
            m_pcm = nullptr;
            return false;
        }
        m_render = std::move(render);
        m_channels = channels;
        m_frames = std::max<u32>(sampleRate / 50, 64);
        m_mix.assign(static_cast<usize>(m_frames) * channels, 0.f);
        m_out.assign(m_mix.size(), 0);
        m_running = true;
        m_thread = std::thread([this] { loop(); });
        return true;
    }

    void stop() override {
        if (m_pcm == nullptr) {
            return;
        }
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_drop(m_pcm);
        m_close(m_pcm);
        m_pcm = nullptr;
    }

    [[nodiscard]] std::string_view name() const override { return "ALSA"; }

private:
    void loop() {
        while (m_running) {
            m_render(m_mix.data(), m_frames);
            for (usize k = 0; k < m_mix.size(); ++k) {
                m_out[k] = static_cast<i16>(std::clamp(m_mix[k], -1.f, 1.f) * 32767.f);
            }
            const snd_pcm_sframes_t n = m_write(m_pcm, m_out.data(), m_frames);
            if (n < 0) {
                m_recover(m_pcm, static_cast<int>(n), 1); // 밑돎 · 일시정지 — 다음 쓰기에서 다시
            }
        }
    }

    void* m_lib = nullptr;
    OpenFn m_open = nullptr;
    SetParamsFn m_setParams = nullptr;
    WriteFn m_write = nullptr;
    RecoverFn m_recover = nullptr;
    CloseFn m_close = nullptr;
    DrainFn m_drop = nullptr;
    snd_pcm_t* m_pcm = nullptr;
    AudioRenderFn m_render;
    u16 m_channels = 2;
    u32 m_frames = 0;
    std::vector<f32> m_mix;
    std::vector<i16> m_out;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
    return std::make_unique<AlsaDevice>();
}

} // namespace sbx::platform
