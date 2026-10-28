#pragma once
// 자동 플레이 스크립트 (v0.2 M6-T1, ADR-0055, docs/13-TESTING.md 5장) — 게임을 사람 없이 끝까지 돌린다.
//
//   { "format": 1, "name": "토끼 지키기 — 자동 플레이", "maxTicks": 6000,
//     "steps": [ {"at": 1,   "command": {"op": "game", "action": "start"}},
//                {"at": 200, "command": {"op": "action", "action": "eco.feed", "position": [3, 4]}} ],
//     "expect": {"state": "Finished", "result": "win", "team": 0, "rule": 3, "tick": 3692,
//                "hash": {"Windows-x86_64-MSVC-19.44": "0x…"}} }
//
//   at       러너 단계 번호 (1 부터 — ScenarioRunner::step 의 횟수. Lobby · 일시정지의 편집 단계도 센다). 오름차순,
//   같은
//            번호는 적힌 순서로. 명령은 리플레이 JSON (core/command/CommandJson — 개체 참조 없는 것만)
//   command  서버가 그 단계 직전에 넣는다 (issuer = kServerIssuer). 명령 단계의 결과 (거절 포함) 는 결정적이다
//   maxTicks 시작한 뒤 월드 틱이 이만큼 지나면 끝 (게임이 끝나지 않아도 — 0 이면 끝날 때까지)
//   expect   (선택) 끝난 뒤 맞아야 할 것 — 다르면 실패 (헤드리스 서버 · 로컬 플레이 · 시험이 같은 값을 본다).
//            hash 는 골든처럼 툴체인 키별 (ADR-0012 — 보장은 같은 바이너리) — 이 툴체인 항목이 없으면 해시는 보지
//            않는다
//            ("0x…" 문자열 하나면 모든 툴체인)
//
// 같은 월드 + 같은 스크립트 = 같은 결과: 헤드리스 서버 (SandboxServer --script) 와 로컬 플레이 (SandboxClient --script)
// 는 같은 ScenarioRunner 를 같은 순서로 단계 짓는다 (스크립트가 ScenarioRunner 의 시나리오로 들어간다).

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "core/scenarios/Scenario.hpp"

namespace sbx::scenario {

inline constexpr u32 kPlayScriptFormat = 1;
inline constexpr usize kMaxScriptSteps = 10'000;

struct PlayExpectation {
    std::optional<std::string> state;  // "Lobby" · "Starting" · "Running" · "Finished"
    std::optional<std::string> result; // "none" · "win" · "lose" · "draw"
    std::optional<u32> team;
    std::optional<u32> rule;
    std::optional<sim::Tick> tick;
    std::optional<u64> hash; // 이 툴체인 (build::kToolchainKey) 의 것 — 없으면 보지 않는다
};

struct PlayScript {
    std::string name;
    struct Step {
        u64 at = 0;
        cmd::CommandPayload command;
    };
    std::vector<Step> steps; // at 오름차순
    sim::Tick maxTicks = 0;
    PlayExpectation expect;
};

[[nodiscard]] Expected<PlayScript> parsePlayScript(std::string_view text, std::string_view source);
[[nodiscard]] Expected<PlayScript> loadPlayScript(const std::filesystem::path& file);

// 러너의 시나리오를 스크립트로 바꾼다 (지금 단계 번호에서 다시 센다 — 바꾼 뒤 첫 step 이 1)
void attachPlayScript(ScenarioRunner& runner, PlayScript script);

// 끝났나: 게임 세션이 Finished (자동 재시작이 없는 모드) 이거나, 시작 틱 + maxTicks 에 닿았다
[[nodiscard]] bool playFinished(const sim::SimulationWorld& world, sim::Tick startTick, const PlayScript& script);

// 한 줄 요약 — "게임 Finished · win · team 0 · rule 3 · 점수 2 · tick 3692 · hash 0x…" (점수 = 공용 칸) (게임 세션이
// 없으면 "게임 없음 · tick …")
[[nodiscard]] std::string describePlayResult(const sim::SimulationWorld& world);
// expect 와 다른 것 (맞으면 빈 목록)
[[nodiscard]] std::vector<std::string> checkPlayExpectation(const sim::SimulationWorld& world,
                                                            const PlayExpectation& expect);

// 시험 · 도구용: 같은 프로세스에서 러너를 끝날 때까지 (maxSteps 단계 안에) — 끝나지 않으면 OutOfRange
[[nodiscard]] Expected<void> runPlayScript(ScenarioRunner& runner, const PlayScript& script, u64 maxSteps);

// 게임 프로젝트의 자동 플레이 스크립트 (v0.4 M4-T2 — sbx test): <프로젝트>/playthrough.json (있으면) 다음
// <프로젝트>/tests/ 의 *.json (이름 순, 하위 폴더는 보지 않는다)
[[nodiscard]] std::vector<std::filesystem::path> projectPlayScripts(const std::filesystem::path& projectDir);
// runPlayScript 의 단계 상한 — maxTicks 가 있으면 그 두 배 + 1,000 (Lobby · 편집 단계 몫), 없으면 2,000,000
[[nodiscard]] u64 playStepLimit(const PlayScript& script) noexcept;

} // namespace sbx::scenario
