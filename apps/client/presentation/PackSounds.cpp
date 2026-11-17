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

namespace {

std::optional<platform::AudioBus> busOf(std::string_view name) {
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

void playScriptSounds(const SoundLibrary& library, platform::IAudioBackend& audio, std::span<const std::string> ids,
                      SoundPlayStats& stats) {
    for (const std::string& id : ids) {
        const SoundEntry* e = library.find(id);
        if (e == nullptr) {
            ++stats.unknown;
            continue;
        }
        platform::PlayParams p;
        p.volume = e->volume;
        p.bus = e->bus;
        if (audio.play(e->handle, p).valid()) {
            ++stats.played;
        } else {
            ++stats.refused;
        }
    }
}

} // namespace sbx::client
