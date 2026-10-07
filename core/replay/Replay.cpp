#include "core/replay/Replay.hpp"

#include <format>
#include <sstream>

#include "core/command/CommandJson.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/simulation/SimVersion.hpp"
#include "foundation/io/FileIo.hpp"

// 이 파일 안에서만 쓰는 Expected 전파 (WorldSave.cpp 와 같은 모양)
#define SBX_TRY_ASSIGN(var, expr)                                                                                      \
    auto var##_r = (expr);                                                                                             \
    if (!var##_r) {                                                                                                    \
        return std::unexpected(var##_r.error());                                                                       \
    }                                                                                                                  \
    auto var = std::move(*var##_r)

namespace sbx::replay {
namespace {

using ecs::Json;

Expected<u64> hashField(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) {
        return makeError(ErrorCode::ParseError, std::format("'{}' 는 \"0x…\" 문자열", key));
    }
    return parseHash(j[key].get<std::string>());
}

Expected<u64> uintField(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number_unsigned()) {
        return makeError(ErrorCode::ParseError, std::format("'{}' 는 0 이상의 정수", key));
    }
    return j[key].get<u64>();
}

} // namespace

// --- 기록 ------------------------------------------------------------------------------------------------------------
ReplayRecorder::ReplayRecorder(sim::SimulationWorld& world, ReplayHeader header) : m_world(world) {
    m_replay.header = std::move(header);
    m_replay.header.replayVersion = kReplayVersion;
    m_replay.header.simVersion = sim::kSimVersion;
    m_replay.header.contentHash = world.content().contentHash();
    m_replay.header.packs.clear();
    for (const auto& pk : world.content().packs()) {
        m_replay.header.packs.push_back(pk.id);
    }
    m_replay.header.startTick = world.currentTick();
    if (m_replay.header.hashInterval == 0) {
        m_replay.header.hashInterval = 30;
    }
    m_lastTick = world.currentTick();
    m_world.setCommandObserver([this](const Json& payload) {
        if (!m_finished) {
            m_replay.commands.push_back(
                ReplayCommand{m_replay.calls, m_world.currentTick(), m_world.clock().editSequence(), payload});
        }
    });
}

ReplayRecorder::~ReplayRecorder() {
    m_world.setCommandObserver({});
}

Expected<void> ReplayRecorder::afterTick() {
    if (m_finished) {
        return makeError(ErrorCode::InvalidArgument, "finish() 뒤에는 기록할 수 없다");
    }
    const u64 call = m_replay.calls++;
    const sim::Tick t = m_world.currentTick();
    if (t != m_lastTick) {
        m_lastTick = t;
        if ((t - m_replay.header.startTick) % m_replay.header.hashInterval == 0) {
            SBX_TRY_ASSIGN(h, m_world.worldHash());
            m_replay.hashes.push_back(ReplayHash{call, t, h});
        }
    }
    return {};
}

Expected<void> ReplayRecorder::finish() {
    if (!m_finished) {
        SBX_TRY_ASSIGN(h, m_world.worldHash());
        m_replay.endTick = m_world.currentTick();
        m_replay.finalHash = h;
        m_finished = true;
    }
    return {};
}

// --- 파일 ------------------------------------------------------------------------------------------------------------
Expected<void> writeReplay(const Replay& r, const std::filesystem::path& file) {
    std::string out;
    const ReplayHeader& h = r.header;
    out += Json{{"magic", "SBXR"},
                {"replayVersion", h.replayVersion},
                {"simVersion", h.simVersion},
                {"contentHash", formatHash(h.contentHash)},
                {"packs", h.packs},
                {"startWorld", h.startWorld},
                {"startWorldHash", formatHash(h.startWorldHash)},
                {"startTick", h.startTick},
                {"hashInterval", h.hashInterval}}
               .dump();
    out += '\n';
    // call 순서로 명령과 해시를 섞어 쓴다 (같은 call 이면 명령 먼저 — 해시는 그 call 이 끝난 뒤의 값)
    usize ci = 0;
    usize hi = 0;
    while (ci < r.commands.size() || hi < r.hashes.size()) {
        const bool takeCmd =
            hi >= r.hashes.size() || (ci < r.commands.size() && r.commands[ci].call <= r.hashes[hi].call);
        if (takeCmd) {
            const ReplayCommand& c = r.commands[ci++];
            out += Json{{"k", "cmd"}, {"call", c.call}, {"tick", c.tick}, {"editSeq", c.editSequence}, {"c", c.payload}}
                       .dump();
        } else {
            const ReplayHash& x = r.hashes[hi++];
            out += Json{{"k", "hash"}, {"call", x.call}, {"tick", x.tick}, {"hash", formatHash(x.hash)}}.dump();
        }
        out += '\n';
    }
    out +=
        Json{{"k", "end"}, {"calls", r.calls}, {"endTick", r.endTick}, {"finalHash", formatHash(r.finalHash)}}.dump();
    out += '\n';
    return io::writeFileAtomic(file, out);
}

Expected<Replay> readReplay(const std::filesystem::path& file) {
    SBX_TRY_ASSIGN(text, io::readFile(file));
    std::istringstream in(text);
    std::string line;
    Replay r;
    usize lineNo = 0;
    bool haveHeader = false;
    bool haveEnd = false;
    const auto fail = [&](std::string_view what) {
        return makeError(ErrorCode::ParseError, std::format("{}:{}: {}", file.string(), lineNo, what));
    };
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const Json j = Json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            return fail("JSON 객체가 아니다");
        }
        if (!haveHeader) {
            if (!j.contains("magic") || j["magic"] != "SBXR") {
                return fail("리플레이 파일이 아니다 (magic SBXR 없음)");
            }
            auto ver = uintField(j, "replayVersion");
            auto simv = uintField(j, "simVersion");
            auto ch = hashField(j, "contentHash");
            auto swh = hashField(j, "startWorldHash");
            auto st = uintField(j, "startTick");
            auto hi = uintField(j, "hashInterval");
            if (!ver || !simv || !ch || !swh || !st || !hi || !j.contains("startWorld") ||
                !j["startWorld"].is_string()) {
                return fail("머리 필드가 빠졌거나 형식이 틀렸다");
            }
            if (*ver != kReplayVersion) {
                return fail(std::format("replayVersion {} 은 읽을 수 없다 (지원: {})", *ver, kReplayVersion));
            }
            std::vector<std::string> packs;
            if (j.contains("packs")) {
                if (!j["packs"].is_array()) {
                    return fail("'packs' 는 문자열 배열");
                }
                for (const Json& pk : j["packs"]) {
                    if (!pk.is_string()) {
                        return fail("'packs' 는 문자열 배열");
                    }
                    packs.push_back(pk.get<std::string>());
                }
            }
            r.header = ReplayHeader{static_cast<u32>(*ver),
                                    static_cast<u32>(*simv),
                                    *ch,
                                    std::move(packs),
                                    j["startWorld"].get<std::string>(),
                                    *swh,
                                    *st,
                                    static_cast<u32>(*hi)};
            haveHeader = true;
            continue;
        }
        if (haveEnd) {
            return fail("end 뒤에 레코드가 있다");
        }
        if (!j.contains("k") || !j["k"].is_string()) {
            return fail("레코드에 'k' 가 없다");
        }
        const std::string k = j["k"].get<std::string>();
        if (k == "cmd") {
            auto call = uintField(j, "call");
            auto tick = uintField(j, "tick");
            auto es = uintField(j, "editSeq");
            if (!call || !tick || !es || !j.contains("c") || !j["c"].is_object()) {
                return fail("cmd 레코드 형식");
            }
            if (!r.commands.empty() && *call < r.commands.back().call) {
                return fail("cmd 레코드의 call 이 거꾸로 간다");
            }
            r.commands.push_back(ReplayCommand{*call, *tick, *es, j["c"]});
        } else if (k == "hash") {
            auto call = uintField(j, "call");
            auto tick = uintField(j, "tick");
            auto h = hashField(j, "hash");
            if (!call || !tick || !h) {
                return fail("hash 레코드 형식");
            }
            r.hashes.push_back(ReplayHash{*call, *tick, *h});
        } else if (k == "end") {
            auto calls = uintField(j, "calls");
            auto endTick = uintField(j, "endTick");
            auto fh = hashField(j, "finalHash");
            if (!calls || !endTick || !fh) {
                return fail("end 레코드 형식");
            }
            r.calls = *calls;
            r.endTick = *endTick;
            r.finalHash = *fh;
            haveEnd = true;
        } else {
            return fail(std::format("모르는 레코드 '{}'", k));
        }
    }
    if (!haveHeader || !haveEnd) {
        return makeError(ErrorCode::ParseError,
                         std::format("{}: 머리 또는 end 레코드가 없다 (잘린 파일?)", file.string()));
    }
    return r;
}

// --- 재생 ------------------------------------------------------------------------------------------------------------
Expected<PlaybackResult> playReplay(sim::SimulationWorld& world, const Replay& r,
                                    const std::function<void(u64 call)>& afterCall) {
    PlaybackResult result;
    result.simVersionDiffers = r.header.simVersion != sim::kSimVersion;
    if (r.header.contentHash != world.content().contentHash()) {
        result.warnings.push_back(std::format("콘텐츠가 기록 당시와 다르다 (contentHash {} → {})",
                                              formatHash(r.header.contentHash),
                                              formatHash(world.content().contentHash())));
    }
    if (world.currentTick() != r.header.startTick) {
        return makeError(ErrorCode::ValidationFailed, std::format("시작 틱이 다르다: 월드 {} · 리플레이 {}",
                                                                  world.currentTick(), r.header.startTick));
    }
    SBX_TRY_ASSIGN(startHash, world.worldHash());
    if (startHash != r.header.startWorldHash) {
        return makeError(ErrorCode::ValidationFailed,
                         std::format("시작 월드의 해시 {} ≠ 기록 {} (다른 세이브?)", formatHash(startHash),
                                     formatHash(r.header.startWorldHash)));
    }

    const auto toNet = [&world](u64 saveId) -> NetEntityId {
        const ecs::EntityId e = world.resolveSave(saveId);
        const comp::NetIdentity* n = e.valid() ? world.registry().tryRead<comp::NetIdentity>(e) : nullptr;
        return n != nullptr ? n->netId : kInvalidNetEntityId;
    };

    usize ci = 0;
    usize hi = 0;
    u32 seq = 0;
    for (u64 call = 0; call < r.calls; ++call) {
        for (; ci < r.commands.size() && r.commands[ci].call == call; ++ci) {
            auto payload = cmd::payloadFromJson(r.commands[ci].payload, toNet);
            if (!payload) {
                return makeError(ErrorCode::ParseError,
                                 std::format("call {} 의 명령을 읽을 수 없다: {}", call, payload.error().describe()));
            }
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{0, cmd::kServerIssuer, seq++}, std::move(*payload)});
        }
        world.tick();
        result.calls = call + 1;
        for (const cmd::CommandResult& res : world.lastResults()) {
            if (!res.accepted) {
                result.warnings.push_back(std::format("call {} tick {}: 기록 때 수락된 명령이 거절됐다 — {}", call,
                                                      world.currentTick(), res.error.describe()));
            }
        }
        if (afterCall) {
            afterCall(call);
        }
        for (; hi < r.hashes.size() && r.hashes[hi].call == call; ++hi) {
            SBX_TRY_ASSIGN(h, world.worldHash());
            ++result.hashesCompared;
            if (h != r.hashes[hi].hash || world.currentTick() != r.hashes[hi].tick) {
                result.mismatch = PlaybackMismatch{call, world.currentTick(), r.hashes[hi].hash, h};
                return result;
            }
        }
    }
    SBX_TRY_ASSIGN(finalHash, world.worldHash());
    ++result.hashesCompared;
    if (finalHash != r.finalHash || world.currentTick() != r.endTick) {
        result.mismatch = PlaybackMismatch{r.calls, world.currentTick(), r.finalHash, finalHash};
    }
    return result;
}

} // namespace sbx::replay

#undef SBX_TRY_ASSIGN
