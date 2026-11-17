// 오디오 출력 (v0.11 NG-17, ADR-0153): WAV 디코더 (PCM 8 · 16 · 24 · 32 · float · EXTENSIBLE · 홀수 덩어리 · 잘린 data,
// 거절 — 머리 · 채널 · 표본율 · 비트 · blockAlign), 소프트웨어 믹서 (표본 · 볼륨 · 버스 · 마스터 · 패닝 · 거리 · pitch
// 리샘플 · 반복 · 끝 → update 가 거둔다 · 목소리 상한 · unload · 자르기), 팩 sounds.json (asset: · assets/ · 경고),
// client.playSound → 재생 (이벤트 → 소리), 장치 없는 앱은 Null. 실제 장치 출력은 수동 QA (세 OS).
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "apps/client/presentation/PackSounds.hpp"
#include "core/asset/AssetDatabase.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ClientScriptSystems.hpp"
#include "foundation/audio/WavDecoder.hpp"
#include "platform/audio/MixerAudioBackend.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "script/lang/Codegen.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

void put16(std::vector<u8>& b, u16 v) {
    b.push_back(static_cast<u8>(v));
    b.push_back(static_cast<u8>(v >> 8));
}
void put32(std::vector<u8>& b, u32 v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<u8>(v >> (8 * i)));
    }
}
void tag(std::vector<u8>& b, const char* t) {
    b.insert(b.end(), t, t + 4);
}

// WAV 하나 — data 는 이미 인코딩한 바이트
std::vector<u8> wav(u16 format, u16 channels, u32 rate, u16 bits, const std::vector<u8>& data, bool extensible = false,
                    bool oddChunk = false) {
    std::vector<u8> b;
    tag(b, "RIFF");
    put32(b, 0); // 크기 — 디코더는 보지 않는다
    tag(b, "WAVE");
    if (oddChunk) { // 모르는 덩어리 (홀수 크기 — 채움 한 바이트)
        tag(b, "LIST");
        put32(b, 3);
        b.insert(b.end(), {1, 2, 3, 0});
    }
    tag(b, "fmt ");
    put32(b, extensible ? 40 : 16);
    put16(b, extensible ? 0xFFFE : format);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * channels * (bits / 8));
    put16(b, static_cast<u16>(channels * (bits / 8)));
    put16(b, bits);
    if (extensible) {
        put16(b, 22);
        put16(b, bits);
        put32(b, 3);
        put16(b, format); // 하위 형식 GUID 의 앞 두 바이트
        b.insert(b.end(), 14, 0);
    }
    tag(b, "data");
    put32(b, static_cast<u32>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

std::vector<u8> pcm16(const std::vector<i16>& v) {
    std::vector<u8> b;
    for (const i16 s : v) {
        put16(b, static_cast<u16>(s));
    }
    return b;
}

// 장치 없는 믹서 (48 kHz 스테레오) 와 소리 하나
struct Mix {
    platform::MixerAudioBackend mixer;
    Mix() { REQUIRE(mixer.init({48000, 2, 4})); }
    platform::SoundHandle load(std::vector<f32> samples, u16 channels = 1, u32 rate = 48000) {
        platform::SoundAsset a;
        a.sampleRate = rate;
        a.channels = channels;
        a.frameCount = samples.size() / channels;
        a.samples = std::make_shared<const std::vector<f32>>(std::move(samples));
        return mixer.load(a);
    }
    std::vector<f32> render(u32 frames) {
        std::vector<f32> out(static_cast<usize>(frames) * 2, 9.f);
        mixer.render(out.data(), frames);
        return out;
    }
};

} // namespace

TEST_SUITE("platform") {
    TEST_CASE("WAV (v0.11 NG-17): PCM 8 · 16 · 24 · 32 · float · EXTENSIBLE · 홀수 덩어리 · 잘린 data") {
        auto m16 = audio::decodeWav(wav(1, 1, 22050, 16, pcm16({0, 16384, -32768, 32767})));
        REQUIRE_MESSAGE(m16.has_value(), m16.error().describe());
        CHECK(m16->sampleRate == 22050);
        CHECK(m16->channels == 1);
        REQUIRE(m16->frames() == 4);
        CHECK(m16->samples[1] == doctest::Approx(0.5f));
        CHECK(m16->samples[2] == doctest::Approx(-1.f));
        CHECK(m16->seconds() == doctest::Approx(4.0 / 22050.0));

        auto s8 = audio::decodeWav(wav(1, 2, 8000, 8, {128, 255, 0, 128}, false, true));
        REQUIRE(s8.has_value());
        CHECK(s8->frames() == 2);
        CHECK(s8->samples[0] == doctest::Approx(0.f));
        CHECK(s8->samples[2] == doctest::Approx(-1.f));

        auto s24 = audio::decodeWav(wav(1, 1, 48000, 24, {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0}));
        REQUIRE(s24.has_value());
        CHECK(s24->samples[0] == doctest::Approx(0.5f));
        CHECK(s24->samples[1] == doctest::Approx(-0.5f));

        std::vector<u8> f32data(8);
        const f32 a = 0.25f;
        const f32 nan = std::nanf("");
        std::memcpy(f32data.data(), &a, 4);
        std::memcpy(f32data.data() + 4, &nan, 4);
        auto fl = audio::decodeWav(wav(3, 1, 44100, 32, f32data, true));
        REQUIRE_MESSAGE(fl.has_value(), fl.error().describe());
        CHECK(fl->samples[0] == doctest::Approx(0.25f));
        CHECK(fl->samples[1] == 0.f); // NaN → 0

        std::vector<u8> cut = wav(1, 1, 48000, 16, pcm16({100, 200, 300}));
        cut.resize(cut.size() - 3); // 마지막 표본 하나 반 — 온전한 프레임만
        auto c = audio::decodeWav(cut);
        REQUIRE(c.has_value());
        CHECK(c->frames() == 1);
    }

    TEST_CASE("WAV (v0.11 NG-17): 거절 — 머리 · fmt · data · 채널 · 표본율 · 비트 · 형식 · blockAlign") {
        CHECK_FALSE(audio::decodeWav(std::vector<u8>{1, 2, 3}).has_value());
        CHECK_FALSE(audio::decodeWav(wav(1, 3, 48000, 16, pcm16({0, 0, 0}))).has_value());
        CHECK_FALSE(audio::decodeWav(wav(1, 1, 4000, 16, pcm16({0}))).has_value());
        CHECK_FALSE(audio::decodeWav(wav(1, 1, 48000, 12, pcm16({0}))).has_value());
        CHECK_FALSE(audio::decodeWav(wav(2, 1, 48000, 16, pcm16({0}))).has_value()); // ADPCM
        std::vector<u8> noData = wav(1, 1, 48000, 16, {});
        noData.resize(noData.size() - 8); // data 덩어리 머리를 뺀다
        CHECK_FALSE(audio::decodeWav(noData).has_value());
        std::vector<u8> badAlign = wav(1, 2, 48000, 16, pcm16({0, 0}));
        badAlign[32] = 3; // blockAlign
        CHECK_FALSE(audio::decodeWav(badAlign).has_value());
    }

    TEST_CASE("믹서 (v0.11 NG-17): 표본 · 볼륨 · 버스 · 마스터 · 반복 · 끝 → update · 목소리 상한 · unload · 자르기") {
        Mix m;
        const platform::SoundHandle s = m.load({0.5f, 0.5f, 0.5f, 0.5f});
        REQUIRE(s.valid());
        const platform::VoiceId v = m.mixer.play(s, {});
        REQUIRE(v.valid());
        std::vector<f32> out = m.render(6);
        CHECK(out[0] == doctest::Approx(0.5f));
        CHECK(out[1] == doctest::Approx(0.5f)); // 모노 → 두 채널
        CHECK(out[8] == 0.f);                   // 4 프레임 뒤는 조용
        CHECK_FALSE(m.mixer.playing(v));        // 끝까지 그렸다
        CHECK(m.mixer.activeVoiceCount() == 0);
        m.mixer.update(0.1f); // 거둔다
        CHECK_FALSE(m.mixer.playing(v));

        platform::PlayParams p;
        p.volume = 0.5f;
        p.bus = platform::AudioBus::Ui;
        p.loop = true;
        m.mixer.setBusVolume(platform::AudioBus::Ui, 0.5f);
        m.mixer.setBusVolume(platform::AudioBus::Master, 0.5f);
        const platform::VoiceId loop = m.mixer.play(s, p);
        out = m.render(10);
        CHECK(out[18] == doctest::Approx(0.0625f)); // 0.5 × 0.5 × 0.5 × 0.5, 반복이라 끝나지 않는다
        CHECK(m.mixer.playing(loop));
        m.mixer.stop(loop);
        CHECK_FALSE(m.mixer.playing(loop));
        m.mixer.setBusVolume(platform::AudioBus::Master, 1.f);

        // 자르기 · 목소리 상한 (4)
        const platform::SoundHandle loud = m.load({1.f, 1.f});
        for (int i = 0; i < 4; ++i) {
            CHECK(m.mixer.play(loud, {}).valid());
        }
        CHECK_FALSE(m.mixer.play(loud, {}).valid());
        out = m.render(1);
        CHECK(out[0] == 1.f);
        // unload 는 그 소리의 목소리를 멈춘다
        m.mixer.unload(loud);
        CHECK(m.mixer.activeVoiceCount() == 0);
        CHECK_FALSE(m.mixer.play(loud, {}).valid());
        CHECK_FALSE(m.load({0.f, 0.f, 0.f}, 2).valid()); // 길이가 채널과 맞지 않는다
    }

    TEST_CASE("믹서 (v0.11 NG-17): 2D 패닝 · 거리 · pitch · 표본율 리샘플 · 스테레오 소리") {
        Mix m;
        const platform::SoundHandle s = m.load(std::vector<f32>(100, 1.f));
        platform::PlayParams p;
        p.positional = true;
        p.position = Vec2{100.f, 0.f}; // 오른쪽 끝
        m.mixer.setListener(Vec2{});
        (void)m.mixer.play(s, p);
        std::vector<f32> out = m.render(1);
        CHECK(out[0] == doctest::Approx(0.f).epsilon(1e-6));
        const f32 far = 1.f / (1.f + 100.f / platform::kAudioFalloff);
        CHECK(out[1] == doctest::Approx(std::sqrt(2.f) * far));

        // 24 kHz 소리 → 48 kHz: 한 표본이 두 프레임 (선형 보간) — 0, 0.5, 1
        Mix r;
        const platform::SoundHandle ramp = r.load({0.f, 1.f}, 1, 24000);
        (void)r.mixer.play(ramp, {});
        out = r.render(4);
        CHECK(out[0] == doctest::Approx(0.f));
        CHECK(out[2] == doctest::Approx(0.5f));
        CHECK(out[4] == doctest::Approx(1.f));
        // pitch 2 — 같은 소리가 두 배 빠르다
        Mix q;
        const platform::SoundHandle steps = q.load({0.f, 0.25f, 0.5f, 0.75f});
        platform::PlayParams fast;
        fast.pitch = 2.f;
        (void)q.mixer.play(steps, fast);
        out = q.render(2);
        CHECK(out[2] == doctest::Approx(0.5f));
        CHECK_FALSE(q.mixer.play(steps, platform::PlayParams{1.f, 0.f}).valid()); // pitch 0 거절
        // 스테레오 소리는 채널 그대로
        Mix st;
        (void)st.mixer.play(st.load({0.2f, -0.2f}, 2), {});
        out = st.render(1);
        CHECK(out[0] == doctest::Approx(0.2f));
        CHECK(out[1] == doctest::Approx(-0.2f));
    }

    TEST_CASE("팩 sounds.json (v0.11 NG-17): asset: · assets/ · 경고, client.playSound → 재생 (이벤트 → 소리)") {
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "audio-output" / "game";
        std::error_code ec;
        fs::remove_all(root, ec);
        const auto put = [&](const std::string& rel, const std::vector<u8>& bytes) {
            fs::create_directories((root / rel).parent_path());
            std::ofstream(root / rel, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        };
        const auto text = [&](const std::string& rel, std::string_view t) {
            put(rel, std::vector<u8>(t.begin(), t.end()));
        };
        put("assets/sfx/hit.wav", wav(1, 1, 48000, 16, pcm16({16384, 16384, 16384})));
        put("assets/sfx/coin.wav", wav(1, 1, 48000, 16, pcm16({8192})));
        put("assets/sfx/broken.wav", {1, 2, 3});
        text("content/t/pack.json", R"({"id": "t", "version": "1"})");
        REQUIRE(asset::importAssets(root).has_value());
        const asset::AssetScan scan = asset::scanProjectAssets(root);
        const asset::AssetEntry* coin = nullptr;
        for (const asset::AssetEntry& e : scan.assets) {
            if (e.path == "assets/sfx/coin.wav") {
                coin = &e;
            }
        }
        REQUIRE(coin != nullptr);
        text("content/t/presentation/sounds.json",
             std::string(R"({"sounds": {"hit": {"file": "assets/sfx/hit.wav", "bus": "sfx", "volume": 0.5}, )") +
                 R"("coin": {"file": "asset:)" + coin->id + R"(", "bus": "ui"}, )" +
                 R"("broken": {"file": "assets/sfx/broken.wav"}, "nope": {"file": "asset:0000"}, )" +
                 R"("loud": {"file": "assets/sfx/hit.wav", "volume": 3}, "away": {"file": "../x.wav"}}})");

        ecs::ComponentCatalog cat;
        REQUIRE(comp::registerCoreComponents(cat).has_value());
        content::ContentLoadResult loaded =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, cat);
        REQUIRE(loaded.ok());
        Mix m;
        client::SoundLibrary lib;
        const client::PackSoundsResult r = client::loadPackSounds(lib, m.mixer, loaded.db, root);
        CHECK(r.packs == 1);
        CHECK(r.sounds == 2);
        CHECK(r.warnings.size() == 4); // broken · nope · loud · away
        REQUIRE(lib.find("hit") != nullptr);
        CHECK(lib.find("coin")->bus == platform::AudioBus::Ui);
        CHECK(lib.find("hit")->volume == doctest::Approx(0.5f));

        // client 스크립트의 playSound → 이벤트 → 재생 (모르는 id 는 센다)
        script::DiagnosticList d;
        const std::vector<script::SourceFile> files = {
            {"scripts/fx.ts", "import { defineSystem } from \"sandbox\"\nexport default defineSystem({ id: \"t.fx\", "
                              "domain: \"client\", "
                              "onUpdate(ctx) { ctx.playSound(\"hit\"); ctx.playSound(\"missing\") } })\n"}};
        std::optional<script::bc::Module> mod = script::compileScripts(files, d);
        REQUIRE_MESSAGE(mod.has_value(), d.describeAll());
        auto programs = std::make_shared<content::ScriptPrograms>();
        content::ScriptPackProgram pack;
        pack.pack = "t";
        pack.folder = "content/t";
        pack.module = std::move(*mod);
        programs->packs.push_back(std::move(pack));
        script::ClientScriptSystems sys(programs);
        sys.run({});
        std::vector<std::string> ids;
        for (const script::ClientScriptSound& s : sys.host().sounds()) {
            ids.push_back(s.id);
        }
        client::SoundPlayStats stats;
        client::playScriptSounds(lib, m.mixer, ids, stats);
        CHECK(stats.played == 1);
        CHECK(stats.unknown == 1);
        const std::vector<f32> out = m.render(2);
        CHECK(out[0] == doctest::Approx(0.25f)); // 0.5 × 볼륨 0.5
        lib.clear(m.mixer);
        CHECK(lib.size() == 0);
        CHECK(m.mixer.activeVoiceCount() == 0);

        // 장치 없는 앱 (헤드리스) 은 Null — 길이만, 같은 표가 올라간다
        platform::NullAudioBackend null;
        REQUIRE(null.init({}));
        client::SoundLibrary nl;
        CHECK(client::loadPackSounds(nl, null, loaded.db, root).sounds == 2);
        client::SoundPlayStats ns;
        client::playScriptSounds(nl, null, ids, ns);
        CHECK(ns.played == 1);
    }
}
