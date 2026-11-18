#include "apps/client/presentation/PackSounds.hpp"

#include <format>
#include <memory>

#include <nlohmann/json.hpp>

#include "core/asset/AssetDatabase.hpp"
#include "core/project/GameProject.hpp"
#include "foundation/audio/WavDecoder.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::client {

void SoundLibrary::clear(platform::IAudioBackend& audio) {
    for (auto& [id, e] : m_entries) {
        audio.unload(e.handle);
    }
    m_entries.clear();
}

void SoundLibrary::set(std::string id, SoundEntry entry, platform::IAudioBackend& audio) {
    if (const auto it = m_entries.find(id); it != m_entries.end()) {
        audio.unload(it->second.handle);
        it->second = std::move(entry);
        return;
    }
    m_entries.emplace(std::move(id), std::move(entry));
}

const SoundEntry* SoundLibrary::find(std::string_view id) const {
    const auto it = m_entries.find(id);
    return it != m_entries.end() ? &it->second : nullptr;
}

void SoundLibrary::trackVoice(std::string_view id, platform::VoiceId voice, const platform::IAudioBackend& audio) {
    auto it = m_voices.find(id);
    if (it == m_voices.end()) {
        it = m_voices.emplace(std::string(id), std::vector<platform::VoiceId>{}).first;
    }
    std::erase_if(it->second, [&](platform::VoiceId v) { return !audio.playing(v); });
    it->second.push_back(voice);
}

std::vector<platform::VoiceId> SoundLibrary::takeVoices(std::string_view id) {
    const auto it = m_voices.find(id);
    if (it == m_voices.end()) {
        return {};
    }
    std::vector<platform::VoiceId> out = std::move(it->second);
    m_voices.erase(it);
    return out;
}

std::optional<platform::AudioBus> audioBusOf(std::string_view name) {
    if (name == "master") {
        return platform::AudioBus::Master;
    }
    if (name == "sfx") {
        return platform::AudioBus::Sfx;
    }
    if (name == "ui") {
        return platform::AudioBus::Ui;
    }
    if (name == "ambient") {
        return platform::AudioBus::Ambient;
    }
    return std::nullopt;
}

namespace {

std::optional<platform::AudioBus> busOf(std::string_view name) {
    const auto bus = audioBusOf(name);
    return bus && *bus != platform::AudioBus::Master ? bus : std::nullopt; // 소리는 master 에 직접 두지 않는다
}

} // namespace

PackSoundsResult loadPackSounds(SoundLibrary& library, platform::IAudioBackend& audio,
                                const content::ContentDatabase& content, const std::filesystem::path& projectDir) {
    PackSoundsResult r;
    std::optional<asset::AssetScan> scan;
    for (const content::PackInfo& p : content.packs()) {
        if (p.sounds.empty()) {
            continue;
        }
        const std::string origin = std::format("{}/presentation/sounds.json", p.id);
        const nlohmann::json doc = nlohmann::json::parse(p.sounds, nullptr, false);
        if (doc.is_discarded() || !doc.is_object() || !doc.contains("sounds") || !doc["sounds"].is_object()) {
            r.warnings.push_back(
                std::format("{}: {{ \"sounds\": {{ \"<id>\": {{ \"file\": … }} }} }} 가 아니다", origin));
            continue;
        }
        ++r.packs;
        for (const auto& [id, spec] : doc["sounds"].items()) {
            const auto warn = [&](std::string what) {
                r.warnings.push_back(std::format("{} '{}': {}", origin, id, what));
            };
            if (!spec.is_object() || !spec.contains("file") || !spec["file"].is_string()) {
                warn("file (문자열) 이 필요하다");
                continue;
            }
            SoundEntry e;
            e.origin = origin;
            if (const auto b = spec.find("bus"); b != spec.end()) {
                const auto bus = b->is_string() ? busOf(b->get<std::string>()) : std::nullopt;
                if (!bus) {
                    warn("bus 는 \"sfx\" · \"ui\" · \"ambient\"");
                    continue;
                }
                e.bus = *bus;
            }
            if (const auto v = spec.find("volume"); v != spec.end()) {
                if (!v->is_number() || !(v->get<f64>() >= 0.0 && v->get<f64>() <= 1.0)) {
                    warn("volume 은 0 ~ 1");
                    continue;
                }
                e.volume = static_cast<f32>(v->get<f64>());
            }
            if (const auto l = spec.find("loop"); l != spec.end()) { // v0.12 RT-06-15
                if (!l->is_boolean()) {
                    warn("loop 는 true · false");
                    continue;
                }
                e.loop = l->get<bool>();
            }
            if (const auto pt = spec.find("pitch"); pt != spec.end()) {
                if (!pt->is_number() || !(pt->get<f64>() >= 0.25 && pt->get<f64>() <= 4.0)) {
                    warn("pitch 는 0.25 ~ 4");
                    continue;
                }
                e.pitch = static_cast<f32>(pt->get<f64>());
            }
            // 파일 — 머티리얼 sprite 와 같은 규칙 (프로젝트 에셋만)
            const std::string file = spec["file"].get<std::string>();
            std::filesystem::path path;
            if (projectDir.empty()) {
                warn(std::format("'{}' — 프로젝트가 없는 접속에서는 소리 파일을 찾지 못한다", file));
                continue;
            }
            if (file.starts_with("asset:")) {
                if (!scan) {
                    scan = asset::scanProjectAssets(projectDir);
                }
                const asset::AssetEntry* a = scan->find(std::string_view(file).substr(6));
                if (a == nullptr) {
                    warn(std::format("에셋 id '{}' 가 없다", file.substr(6)));
                    continue;
                }
                path = projectDir / std::filesystem::path(a->path);
            } else if (file.starts_with(std::string(asset::kAssetDir) + "/") && !project::checkRelativePath(file)) {
                path = projectDir / std::filesystem::path(file);
            } else {
                warn(std::format("'{}' — \"asset:<id>\" 또는 \"assets/…\" 만", file));
                continue;
            }
            const auto bytes = io::readFile(path);
            if (!bytes) {
                warn(bytes.error().describe());
                continue;
            }
            auto decoded = audio::decodeWav(std::span(reinterpret_cast<const u8*>(bytes->data()), bytes->size()));
            if (!decoded) {
                warn(decoded.error().describe());
                continue;
            }
            platform::SoundAsset sa;
            sa.name = id;
            sa.sampleRate = decoded->sampleRate;
            sa.channels = decoded->channels;
            sa.frameCount = decoded->frames();
            sa.samples = std::make_shared<const std::vector<f32>>(std::move(decoded->samples));
            e.handle = audio.load(sa);
            if (!e.handle.valid()) {
                warn("오디오 백엔드가 받지 않았다");
                continue;
            }
            library.set(id, std::move(e), audio);
            ++r.sounds;
        }
    }
    return r;
}

void playScriptSounds(SoundLibrary& library, platform::IAudioBackend& audio, std::span<const std::string> ids,
                      SoundPlayStats& stats) {
    for (const std::string& id : ids) {
        playScriptSound(library, audio, id, std::nullopt, stats);
    }
}

void playScriptSound(SoundLibrary& library, platform::IAudioBackend& audio, std::string_view id,
                     const std::optional<Vec2>& at, SoundPlayStats& stats) {
    const SoundEntry* e = library.find(id);
    if (e == nullptr) {
        ++stats.unknown;
        return;
    }
    platform::PlayParams p;
    p.volume = e->volume;
    p.bus = e->bus;
    p.loop = e->loop;
    p.pitch = e->pitch;
    if (at) {
        p.positional = true;
        p.position = *at;
    }
    const platform::VoiceId v = audio.play(e->handle, p);
    if (v.valid()) {
        ++stats.played;
        library.trackVoice(id, v, audio);
    } else {
        ++stats.refused;
    }
}

void applyScriptSounds(SoundLibrary& library, platform::IAudioBackend& audio,
                       std::span<const script::ClientScriptSound> sounds, SoundPlayStats& stats) {
    for (const script::ClientScriptSound& s : sounds) {
        switch (s.op) {
        case script::ClientScriptSound::Op::Play: // 모르는 id 는 센다 (v0.11 NG-17)
            playScriptSound(library, audio, s.id, s.positional ? std::optional<Vec2>(s.position) : std::nullopt, stats);
            break;
        case script::ClientScriptSound::Op::Stop:
            (void)stopScriptSounds(library, audio, s.id);
            break;
        case script::ClientScriptSound::Op::Volume:
            if (const auto bus = audioBusOf(s.id)) {
                audio.setBusVolume(*bus, s.volume);
            }
            break;
        }
    }
}

usize stopScriptSounds(SoundLibrary& library, platform::IAudioBackend& audio, std::string_view id) {
    usize n = 0;
    for (const platform::VoiceId v : library.takeVoices(id)) {
        if (audio.playing(v)) {
            audio.stop(v);
            ++n;
        }
    }
    return n;
}

} // namespace sbx::client
