// macOS 오디오 장치 — AudioToolbox AudioQueue (v0.11 NG-17, ADR-0153). 버퍼 셋 (각 20 ms, f32 엇갈림) 을 큐에 넣고, 다
// 쓴 버퍼가 돌아오면 (AudioQueue 의 내부 스레드 콜백) render 로 채워 다시 넣는다. CoreAudio 의 AUHAL 보다 지연이 크지만
// (약 60 ms) 설정이 단순하다 — 더 낮은 지연은 [계획].
#include "platform/audio/MixerAudioBackend.hpp"

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <atomic>

namespace sbx::platform {
namespace {

constexpr usize kBuffers = 3;

class AudioQueueDevice final : public IAudioDevice {
public:
    ~AudioQueueDevice() override { stop(); }

    bool start(u32 sampleRate, u16 channels, AudioRenderFn render) override {
        stop();
        m_render = std::move(render);
        m_channels = channels;
        m_frames = std::max<u32>(sampleRate / 50, 64);
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = static_cast<Float64>(sampleRate);
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
        fmt.mChannelsPerFrame = channels;
        fmt.mBitsPerChannel = 32;
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerFrame = static_cast<UInt32>(channels * sizeof(f32));
        fmt.mBytesPerPacket = fmt.mBytesPerFrame;
        // 콜백은 AudioQueue 의 내부 스레드에서 (run loop 를 주지 않는다)
        if (AudioQueueNewOutput(&fmt, &AudioQueueDevice::callback, this, nullptr, nullptr, 0, &m_queue) != noErr) {
            m_queue = nullptr;
            return false;
        }
        m_running = true;
        for (AudioQueueBufferRef& b : m_buffers) {
            if (AudioQueueAllocateBuffer(m_queue, m_frames * fmt.mBytesPerFrame, &b) != noErr) {
                stop();
                return false;
            }
            fill(b);
        }
        if (AudioQueueStart(m_queue, nullptr) != noErr) {
            stop();
            return false;
        }
        return true;
    }

    void stop() override {
        if (m_queue == nullptr) {
            return;
        }
        m_running = false;
        AudioQueueStop(m_queue, true); // 바로 (콜백이 끝난 뒤 돌아온다)
        AudioQueueDispose(m_queue, true);
        m_queue = nullptr;
        m_buffers.fill(nullptr);
    }

    [[nodiscard]] std::string_view name() const override { return "AudioQueue"; }

private:
    static void callback(void* user, AudioQueueRef /*queue*/, AudioQueueBufferRef buffer) {
        auto* self = static_cast<AudioQueueDevice*>(user);
        if (self->m_running) {
            self->fill(buffer);
        }
    }

    void fill(AudioQueueBufferRef b) {
        m_render(static_cast<f32*>(b->mAudioData), m_frames);
        b->mAudioDataByteSize = static_cast<UInt32>(m_frames * m_channels * sizeof(f32));
        AudioQueueEnqueueBuffer(m_queue, b, 0, nullptr);
    }

    AudioRenderFn m_render;
    u16 m_channels = 2;
    u32 m_frames = 0;
    AudioQueueRef m_queue = nullptr;
    std::array<AudioQueueBufferRef, kBuffers> m_buffers{};
    std::atomic<bool> m_running{false};
};

} // namespace

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
    return std::make_unique<AudioQueueDevice>();
}

} // namespace sbx::platform
