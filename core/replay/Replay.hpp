#pragma once
// 리플레이 기록·재생. docs/09-SERIALIZATION.md 4장, docs/04-DETERMINISM.md D3.
//
// 리플레이 = 시작 세이브 + 그 뒤에 **적용된** 명령 + 주기적 WorldHash.
//   기록  ReplayRecorder 가 월드의 명령 관찰자(setCommandObserver)로 붙어 수락된 명령을 받는다.
//         tick() 호출마다 afterTick() — 호출 번호(call)가 명령의 위치다. 일시정지 편집 단계는 틱 번호가 같아도
//         호출이 다르므로 순서·구간이 그대로 보존된다 (문서의 editSequence 역할; editSequence 는 진단용으로 함께
//         남긴다).
//  재생  playReplay: 시작 세이브를 로드한 월드에 call 마다 그 call 의 명령을 넣고 tick() 한 번, Hash 레코드마다 비교.
//         재생 명령의 헤더는 (executeTick 0, issuer 0, 기록 순번) — 그 call 에서 기록 순서대로 바로 적용된다.
//
// 엔티티 참조는 saveId 로 기록하고 재생 때 그 월드의 NetEntityId 로 바꾼다 (netId 는 로드 때 다시 매겨진다).
// 같은 tick() 호출 안에서 만든 엔티티를 같은 호출의 명령이 가리키는 경우는 없다 — 명령을 내는 쪽(클라·에디터)은
// 생성 결과를 다음 틱에야 받는다.
//
// 파일 (replay.sbxr) = JSON Lines. 1행 header, 이어서 레코드, 마지막 행 end.
//   {"magic":"SBXR","replayVersion":1,"simVersion":3,"contentHash":"0x…","packs":["eco"],"startWorld":"start",
//    "startWorldHash":"0x…","startTick":0,"hashInterval":30}
//   {"k":"cmd","call":c,"tick":t,"editSeq":e,"c":{…CommandJson…}}
//   {"k":"hash","call":c,"tick":t,"hash":"0x…"}
//   {"k":"end","calls":n,"endTick":t,"finalHash":"0x…"}
// (문서 초안의 바이너리 명령은 와이어 포맷(Phase 9)과 함께 — 그때 replayVersion 2.)

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/simulation/SimulationWorld.hpp"

namespace sbx::replay {

inline constexpr u32 kReplayVersion = 1;

struct ReplayHeader {
    u32 replayVersion = kReplayVersion;
    u32 simVersion = 0;
    u64 contentHash = 0;
    std::vector<std::string> packs; // 재생할 때 읽을 콘텐츠 팩 (기록한 월드의 팩, 로드 순서)
    std::string startWorld; // 시작 세이브 폴더 — 리플레이 파일이 있는 폴더 기준 상대 경로
    u64 startWorldHash = 0;
    sim::Tick startTick = 0;
    u32 hashInterval = 30;
};

struct ReplayCommand {
    u64 call = 0;
    sim::Tick tick = 0;
    u64 editSequence = 0;
    ecs::Json payload; // CommandJson, 엔티티 참조 = saveId
};

struct ReplayHash {
    u64 call = 0;
    sim::Tick tick = 0;
    u64 hash = 0;
};

struct Replay {
    ReplayHeader header;
    std::vector<ReplayCommand> commands; // call 오름차순 (같은 call 안에서는 적용 순서)
    std::vector<ReplayHash> hashes;      // call 오름차순
    u64 calls = 0;                       // 기록한 tick() 호출 수
    sim::Tick endTick = 0;
    u64 finalHash = 0;
};

class ReplayRecorder {
public:
    // world 에 명령 관찰자를 붙인다. header 의 simVersion·contentHash·startTick 은 world 에서 채운다.
    ReplayRecorder(sim::SimulationWorld& world, ReplayHeader header);
    ReplayRecorder(const ReplayRecorder&) = delete;
    ReplayRecorder& operator=(const ReplayRecorder&) = delete;
    ReplayRecorder(ReplayRecorder&&) = delete;
    ReplayRecorder& operator=(ReplayRecorder&&) = delete;
    ~ReplayRecorder(); // 관찰자를 뗀다

    // world.tick() 한 번 뒤에 부른다. 틱이 올랐고 (tick − startTick) 가 hashInterval 의 배수면 Hash 레코드.
    [[nodiscard]] Expected<void> afterTick();
    // 끝 레코드(마지막 틱·해시)를 채운다. 그 뒤로는 기록하지 않는다.
    [[nodiscard]] Expected<void> finish();
    [[nodiscard]] const Replay& replay() const noexcept { return m_replay; }

private:
    sim::SimulationWorld& m_world;
    Replay m_replay;
    sim::Tick m_lastTick = 0;
    bool m_finished = false;
};

[[nodiscard]] Expected<void> writeReplay(const Replay& replay, const std::filesystem::path& file);
[[nodiscard]] Expected<Replay> readReplay(const std::filesystem::path& file);

struct PlaybackMismatch {
    u64 call = 0;
    sim::Tick tick = 0;
    u64 expected = 0;
    u64 actual = 0;
};

struct PlaybackResult {
    u64 calls = 0;
    u64 hashesCompared = 0;
    std::optional<PlaybackMismatch> mismatch; // 첫 불일치에서 멈춘다
    bool simVersionDiffers = false;    // 불일치가 있으면 "규칙 차이"(true) / "버그"(false) 로 분류
    std::vector<std::string> warnings; // 재생 중 거절된 명령 등
};

// world = replay.header.startWorld 를 로드한 월드 (아직 tick() 하지 않은 상태).
// afterCall(call) 은 tick() 마다 불린다 (진단·나란히 비교용).
[[nodiscard]] Expected<PlaybackResult> playReplay(sim::SimulationWorld& world, const Replay& replay,
                                                  const std::function<void(u64 call)>& afterCall = {});

} // namespace sbx::replay
