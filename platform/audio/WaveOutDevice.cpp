// Windows 오디오 장치 — waveOut (winmm, v0.11 NG-17, ADR-0153). 버퍼 셋 (각 20 ms) 을 돌려 쓰며 장치 스레드가 다 쓴
// 버퍼 (WHDR_DONE) 를 render 로 채워 다시 넣는다 (CALLBACK_EVENT). 16 비트 PCM 으로 바꿔 보낸다. WASAPI 보다 지연이
// 크지만 (약 60 ms) 모든 Windows 에 있고 COM 이 필요 없다 — 더 낮은 지연은 [계획].
#include "platform/audio/MixerAudioBackend.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h 다음 (mmsystem 은 windows.h 의 형식을 쓴다 — clang-format 이 차례를 바꾸지 않게 따로 둔다)
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace sbx::platform {
namespace {

constexpr usize kBuffers = 3;

class WaveOutDevice final : public IAudioDevice {
public:
    ~WaveOutDevice() override { stop(); }

    bool start(u32 sampleRate, u16 channels, AudioRenderFn render) override {
        stop();
        m_render = std::move(render);
        m_channels = channels;
        m_frames = std::max<u32>(sampleRate / 50, 64); // 20 ms
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = channels;
        fmt.nSamplesPerSec = sampleRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = static_cast<WORD>(channels * 2);
        fmt.nAvgBytesPerSec = sampleRate * fmt.nBlockAlign;
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (m_event == nullptr) {
            return false;
        }
        if (waveOutOpen(&m_out, WAVE_MAPPER, &fmt, reinterpret_cast<DWORD_PTR>(m_event), 0, CALLBACK_EVENT) !=
            MMSYSERR_NOERROR) {
            CloseHandle(m_event);
            m_event = nullptr;
            m_out = nullptr;
            return false;
        }
        m_mix.assign(static_cast<usize>(m_frames) * channels, 0.f);
        for (usize i = 0; i < kBuffers; ++i) {
            m_pcm[i].assign(static_cast<usize>(m_frames) * channels, 0);
            m_headers[i] = WAVEHDR{};
            m_headers[i].lpData = reinterpret_cast<LPSTR>(m_pcm[i].data());
            m_headers[i].dwBufferLength = static_cast<DWORD>(m_pcm[i].size() * sizeof(i16));
            waveOutPrepareHeader(m_out, &m_headers[i], sizeof(WAVEHDR));
        }
        m_running = true;
        for (usize i = 0; i < kBuffers; ++i) {
            fill(i); // 처음 셋을 채워 넣는다 — 그 뒤는 장치 스레드가
        }
        m_thread = std::thread([this] { loop(); });
        return true;
    }

    void stop() override {
        if (m_out == nullptr) {
            return;
        }
        m_running = false;
        SetEvent(m_event);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        waveOutReset(m_out); // 남은 버퍼를 돌려받는다 (WHDR_DONE)
        for (WAVEHDR& h : m_headers) {
            waveOutUnprepareHeader(m_out, &h, sizeof(WAVEHDR));
        }
        waveOutClose(m_out);
        CloseHandle(m_event);
        m_out = nullptr;
        m_event = nullptr;
    }

    [[nodiscard]] std::string_view name() const override { return "waveOut"; }

private:
    void fill(usize i) {
        m_render(m_mix.data(), m_frames);
        for (usize k = 0; k < m_mix.size(); ++k) {
            m_pcm[i][k] = static_cast<i16>(std::clamp(m_mix[k], -1.f, 1.f) * 32767.f);
        }
        m_headers[i].dwFlags &= ~static_cast<DWORD>(WHDR_DONE);
        waveOutWrite(m_out, &m_headers[i], sizeof(WAVEHDR));
    }

    void loop() {
        while (m_running) {
            WaitForSingleObject(m_event, 100);
            for (usize i = 0; i < kBuffers && m_running; ++i) {
                if ((m_headers[i].dwFlags & WHDR_DONE) != 0) {
                    fill(i);
                }
            }
        }
    }

    AudioRenderFn m_render;
    u16 m_channels = 2;
    u32 m_frames = 0;
    HWAVEOUT m_out = nullptr;
    HANDLE m_event = nullptr;
    std::array<WAVEHDR, kBuffers> m_headers{};
    std::array<std::vector<i16>, kBuffers> m_pcm;
    std::vector<f32> m_mix;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
    return std::make_unique<WaveOutDevice>();
}

} // namespace sbx::platform
