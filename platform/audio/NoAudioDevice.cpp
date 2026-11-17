// 출력 장치가 없는 플랫폼 (stub · 창 없는 Linux 빌드) — createSystemAudioDevice 는 null (앱이 Null 백엔드로). v0.11
// NG-17.
#include "platform/audio/MixerAudioBackend.hpp"

namespace sbx::platform {

std::unique_ptr<IAudioDevice> createSystemAudioDevice() {
    return nullptr;
}

} // namespace sbx::platform
