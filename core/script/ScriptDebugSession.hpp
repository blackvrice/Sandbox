#pragma once
// 스크립트 재생 디버그 세션 (v0.9 ST-06-7 조각 2 — docs/27-SCRIPT-IDE.md 6장 방식 B, ADR-0133). 리플레이를 디버그
// 스레드에서 다시 돌리며 서버 스크립트 콜백 안에서 멈춘다 — 라이브 서버를 멈추지 않는다. 재생은 결정적이라 버그가
// 그대로 나오고, 디버거는 읽기만 하므로 재생 결과 (리플레이의 해시 기록 · 끝 해시) 가 디버거 없을 때와 같다 (시험).
//
// 팩마다 vm::Debugger 하나 — ScriptSystemAdapter::debugModule 의 사본에 BRK. 중단점 = (팩, 파일, 줄). stopAtTick 이면
// 그 틱의 첫 스크립트 줄에서 멈춘다 (armStep). 멈추면 디버그 스레드는 훅 안에서 기다리고 (그 콜백 · 그 틱의 중간), UI
// 스레드는 스냅숏 (프레임 · 지역 · 감시 — 멈출 때 디버그 스레드가 글로 만든 것) 만 읽는다. 멈춘 동안의 요청 (감시 식 ·
// 중단점 바꾸기) 은 디버그 스레드로 넘겨 그 스레드가 VM 을 읽는다 — VM 을 두 스레드가 만지지 않는다.
// 클라이언트 시스템 (onUpdate) 은 디버그하지 않는다 ([계획 v0.7] — 27 6장).
// 세션이 Running · Paused 인 동안 그 월드는 디버그 스레드의 것이다 — 다른 스레드가 읽거나 돌리지 않는다.
//
// 상태: Ready (시작 전 — 중단점 · 감시를 놓는다) → Running → Paused ↔ Running → Finished (끝 · abort) | Failed (재생
// 오류).

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/replay/Replay.hpp"
#include "script/vm/Debug.hpp"

namespace sbx::script {

struct DebugFrameView {
    std::string function; // 짧은 이름 ("onTick" · "heal")
    std::string site;     // "scripts/regen.ts:12:5 (heal)" (bc::describeSite)
    u32 line = 0;
    std::vector<vm::DebugVariable> locals;
};

struct DebugSnapshot {
    u64 stop = 0;       // 몇 번째 멈춤 (1 부터)
    u64 call = 0;       // 재생 call (ReplayPlayer::call — 그 call 의 tick() 안)
    sim::Tick tick = 0; // 월드의 currentTick (그 틱 안)
    std::string pack;
    vm::StopReason reason = vm::StopReason::Breakpoint;
    std::string file;
    u32 line = 0;
    u32 column = 0;
    std::vector<DebugFrameView> frames;   // 안쪽 (멈춘 함수) 부터 — 맨 끝이 엔진이 부른 콜백
    std::vector<vm::WatchResult> watches; // 감시 식 (안쪽 프레임에서)
};

enum class DebugSessionState : u8 { Ready, Running, Paused, Finished, Failed };
[[nodiscard]] std::string_view debugSessionStateName(DebugSessionState s) noexcept;

class ScriptDebugSession {
public:
    // world = replay 의 시작 세이브를 로드한 월드 (아직 tick() 하지 않은 상태). world · replay 는 세션보다 오래 산다.
    // 서버 스크립트 시스템이 없거나 리플레이가 시작 월드와 맞지 않으면 오류
    [[nodiscard]] static Expected<std::unique_ptr<ScriptDebugSession>> create(sim::SimulationWorld& world,
                                                                              const replay::Replay& replay);
    ScriptDebugSession(const ScriptDebugSession&) = delete;
    ScriptDebugSession& operator=(const ScriptDebugSession&) = delete;
    ~ScriptDebugSession(); // 끝나지 않았으면 abort 하고 디버그 스레드를 기다린다

    // 중단점 — Ready · Paused 에서만 (Running 이면 놓지 않는다: 0 · false). 놓인 BRK 수 (0 = 그 줄에 코드 없음 · 팩
    // 없음)
    usize setBreakpoint(std::string_view pack, std::string_view file, u32 line, std::string log = {});
    bool clearBreakpoint(std::string_view pack, std::string_view file, u32 line);
    // 감시 식 — 멈출 때마다 안쪽 프레임에서. Paused 면 지금 스냅숏도 다시 구한다
    void setWatches(std::vector<std::string> exprs);
    // 이 틱의 첫 스크립트 줄에서 멈춘다 (Ready 에서)
    void setStopAtTick(std::optional<sim::Tick> tick);

    void start(); // Ready → Running
    // Running 이 아니게 될 때까지 (최대 timeout) — 그때의 상태
    DebugSessionState wait(std::chrono::milliseconds timeout);
    [[nodiscard]] DebugSessionState state() const;
    [[nodiscard]] std::optional<DebugSnapshot> snapshot() const; // Paused 일 때
    // Paused → Running (Continue · Into · Over · Out). Paused 가 아니면 아무 일 없음
    void resume(vm::StepAction action);
    // Paused 에서 그 프레임 (0 = 안쪽) 의 감시 식 하나
    [[nodiscard]] vm::WatchResult evaluate(usize frame, std::string expr);
    // 멈춤을 풀고 더 멈추지 않으며 다음 call 경계에서 재생을 그친다 → Finished (aborted)
    void abort();

    [[nodiscard]] bool aborted() const;
    [[nodiscard]] replay::PlaybackResult result() const;  // Finished 뒤 (그 전에는 지금까지)
    [[nodiscard]] u64 finalHash() const;                  // Finished 뒤의 월드 해시 (0 = 아직)
    [[nodiscard]] std::string error() const;              // Failed 의 이유
    [[nodiscard]] std::vector<std::string> logs() const;  // logpoint 글 (멈출 때 · 끝날 때 모은다)
    [[nodiscard]] std::vector<std::string> packs() const; // 디버그할 수 있는 팩 (서버 스크립트가 있는 것)

private:
    struct PackDebugger {
        std::string pack;
        usize index = 0; // ScriptPrograms::packs 의 자리
        std::unique_ptr<vm::Debugger> debugger;
    };
    ScriptDebugSession(sim::SimulationWorld& world, const replay::Replay& replay) : m_world(world), m_replay(replay) {}

    void run();                                                // 디버그 스레드
    vm::StepAction onStop(const vm::DebugStop& s, usize pack); // 디버그 스레드 (훅 안)
    // Paused 에서 디버그 스레드에 일을 넘기고 끝날 때까지 기다린다 (Paused 가 아니면 false — 일을 하지 않았다)
    bool onDebugThread(const std::function<void(const vm::Interpreter&)>& task);
    PackDebugger* find(std::string_view pack);
    void collectLogs(); // 디버그 스레드 · 잠금 안

    sim::SimulationWorld& m_world;
    const replay::Replay& m_replay;
    std::unique_ptr<replay::ReplayPlayer> m_player;
    std::vector<PackDebugger> m_debuggers;
    std::thread m_thread;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    DebugSessionState m_state = DebugSessionState::Ready;
    std::optional<DebugSnapshot> m_snapshot;
    std::vector<std::string> m_watches;
    std::optional<sim::Tick> m_stopAtTick;
    std::optional<vm::StepAction> m_resume;                           // UI → 디버그 스레드
    std::vector<std::function<void(const vm::Interpreter&)>> m_tasks; // UI → 디버그 스레드 (멈춘 동안)
    u64 m_tasksDone = 0;
    u64 m_tasksPosted = 0;
    bool m_abort = false;
    u64 m_stops = 0;
    u64 m_call = 0;
    replay::PlaybackResult m_result;
    u64 m_finalHash = 0;
    std::string m_error;
    std::vector<std::string> m_logs;
};

} // namespace sbx::script
