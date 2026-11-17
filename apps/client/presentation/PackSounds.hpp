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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "platform/common/Audio.hpp"

namespace sbx::client {

struct SoundEntry {
    platform::SoundHandle handle;
    platform::AudioBus bus = platform::AudioBus::Sfx;
    f32 volume = 1.f;
    std::string origin; // "<팩>/presentation/sounds.json"
};

class SoundLibrary {
public:
    // 올린 소리를 다 내린다
    void clear(platform::IAudioBackend& audio);
    void set(std::string id, SoundEntry entry, platform::IAudioBackend& audio); // 같은 id 는 앞의 것을 내리고 바꾼다
    [[nodiscard]] const SoundEntry* find(std::string_view id) const;
    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }

private:
    std::map<std::string, SoundEntry, std::less<>> m_entries;
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
void playScriptSounds(const SoundLibrary& library, platform::IAudioBackend& audio, std::span<const std::string> ids,
                      SoundPlayStats& stats);

} // namespace sbx::client
