#pragma once
// 게임 프로젝트 · 패키지의 소리 (v0.11 NG-17 — docs/26-ASSET-PIPELINE.md · 11-CONTENT-SCHEMA, ADR-0153).
//
//   팩마다 presentation/sounds.json (팩 로드 순 — 같은 id 는 뒤가 이긴다):
//     { "sounds": { "<id>": { "file": "asset:<id>" | "assets/…wav", "bus": "sfx" | "ui" | "ambient", "volume": 0 ~ 1 }
//     } }
//   file 은 프로젝트 에셋 (머티리얼의 sprite 와 같은 규칙 — 옮겨도 id 그대로), WAV (foundation/audio/WavDecoder) 를
//   디코드해 오디오 백엔드에 올린다. 스크립트의 client.playSound(id) 가 그 id 를 부르면 재생 (playScriptSounds). 모르는
//   id · 못 읽은 파일 · 틀린 값은 경고 (소리 없이 계속). 오디오는 표시 쪽만 — 시뮬레이션과 무관.

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/script/ClientScriptHost.hpp"
#include "platform/common/Audio.hpp"

namespace sbx::client {

struct SoundEntry {
    platform::SoundHandle handle;
    platform::AudioBus bus = platform::AudioBus::Sfx;
    f32 volume = 1.f;
    bool loop = false;  // "loop" (v0.12 RT-06-15) — 멈출 때까지 (stopSound)
    f32 pitch = 1.f;    // "pitch" 0.25 ~ 4
    std::string origin; // "<팩>/presentation/sounds.json"
};

class SoundLibrary {
public:
    // 올린 소리를 다 내린다
    void clear(platform::IAudioBackend& audio);
    void set(std::string id, SoundEntry entry, platform::IAudioBackend& audio); // 같은 id 는 앞의 것을 내리고 바꾼다
    [[nodiscard]] const SoundEntry* find(std::string_view id) const;
    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }
    // 그 id 로 낸 목소리 (stopSound 가 멈춘다 — v0.12 RT-06-15). 끝난 것은 다음 기록 때 거둔다
    void trackVoice(std::string_view id, platform::VoiceId voice, const platform::IAudioBackend& audio);
    [[nodiscard]] std::vector<platform::VoiceId> takeVoices(std::string_view id);

private:
    std::map<std::string, SoundEntry, std::less<>> m_entries;
    std::map<std::string, std::vector<platform::VoiceId>, std::less<>> m_voices;
};

struct PackSoundsResult {
    usize packs = 0;  // sounds.json 이 있던 팩
    usize sounds = 0; // 올린 소리
    std::vector<std::string> warnings;
};

// projectDir 가 비면 (원격 서버 — 프로젝트 파일이 없다) "asset:" · "assets/" 는 찾지 못함 (경고)
[[nodiscard]] PackSoundsResult loadPackSounds(SoundLibrary& library, platform::IAudioBackend& audio,
                                              const content::ContentDatabase& content,
                                              const std::filesystem::path& projectDir);

struct SoundPlayStats {
    u64 played = 0;
    u64 unknown = 0; // 표에 없는 id
    u64 refused = 0; // 백엔드가 받지 않았다 (목소리 상한)
};

// client.playSound 의 id 들을 부른 차례로 재생한다
void playScriptSounds(SoundLibrary& library, platform::IAudioBackend& audio, std::span<const std::string> ids,
                      SoundPlayStats& stats);
// 하나 — at 이 있으면 위치 소리 (월드 좌표 — 리스너 기준 패닝 · 감쇠, v0.12 RT-06-15)
void playScriptSound(SoundLibrary& library, platform::IAudioBackend& audio, std::string_view id,
                     const std::optional<Vec2>& at, SoundPlayStats& stats);
// 그 id 로 낸 목소리를 모두 멈춘다. 멈춘 수
usize stopScriptSounds(SoundLibrary& library, platform::IAudioBackend& audio, std::string_view id);
// client 스크립트의 소리 줄을 부른 차례로 (재생 · 위치 소리 · 멈춤 · 버스 볼륨 — v0.12 RT-06-15)
void applyScriptSounds(SoundLibrary& library, platform::IAudioBackend& audio,
                       std::span<const script::ClientScriptSound> sounds, SoundPlayStats& stats);
// "master" · "sfx" · "ambient" · "ui" (sounds.json 의 bus 는 master 를 쓰지 않는다)
[[nodiscard]] std::optional<platform::AudioBus> audioBusOf(std::string_view name);

} // namespace sbx::client
