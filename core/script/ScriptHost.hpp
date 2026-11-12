#pragma once
// SandboxScript Host 바인딩 — Host API 표 (script/api) 의 항목마다 엔진 구현을 하나씩. ADR-0057 2 · 4 · 5, ADR-0060,
// docs/19-SANDBOXSCRIPT.md 14장.
//
// 구현 목록 hostBindings() 는 명시 등록이다 (ADR-0011 과 같은 정신). checkHostBindings 가 "표의 Server 도메인 항목마다
// 구현이 정확히 하나, 구현의 서명 = 표" 를 본다 — 단위 시험과 ScriptHost 생성 (Debug) 이 부른다. Client 도메인 항목은
// ClientScriptHost 의 몫이다 (ADR-0067). ScriptHost 는 VM 의 IHost:
// 호출 하나 (beginCall … endCall) 마다 ctx 핸들을 새로 내고, Host 함수는 첫 인자가 지금 호출의 ctx 인지 본다 (지난
// 호출의 ctx · 위조한 핸들 = HostError — ADR-0057 4 "핸들은 호출이 끝나면 무효").
//
// 컴포넌트 항목 (v0.3 M3-T3, ADR-0060) 은 beginCall 에 넘긴 월드를 본다:
//   개체 핸들  호출마다 새 표 (표지 · 호출 번호 · 자리) — 같은 개체 = 같은 핸들, 지난 호출 · 위조한 핸들은 HostError
//   읽기      ctx.read · has · query — 이 호출의 쓰기 버퍼를 먼저 본다 (read-your-writes). 질의 = saveId 오름차순
//   쓰기      ctx.write — 값을 바이트로 검사 (범위 · 유한 · f32 범위 · 개체 참조) 해 호출 단위 버퍼에. endCall(true) 가
//   쓴
//             차례대로 적용하고 endCall(false) (트랩) 는 버린다. 스크립트 컴포넌트만 (엔진 컴포넌트 쓰기는 [계획])

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/ecs/ComponentCatalog.hpp"
#include "core/random/CounterRng.hpp"
#include "core/simulation/ScriptStructural.hpp"
#include "core/simulation/SimConstants.hpp"
#include "script/vm/Interpreter.hpp"

namespace sbx::sim {
class SimulationWorld;
} // namespace sbx::sim

namespace sbx::script {

class ScriptHost;

// 구현 하나 — Host = 구현이 받는 IHost (서버 ScriptHost · 클라이언트 ClientScriptHost)
template <class Host>
struct BasicHostBinding {
    std::string_view id;
    api::AbiType result = api::AbiType::Void;
    std::array<api::AbiType, api::kMaxHostParams + 1> params{}; // 주인 (ctx) 이 첫째
    u8 paramCount = 0;
    vm::HostResult (*impl)(Host& host, std::span<const vm::HostArg> args) = nullptr;
};
using HostImpl = vm::HostResult (*)(ScriptHost& host, std::span<const vm::HostArg> args);
using HostBinding = BasicHostBinding<ScriptHost>;

// 계약 검사가 보는 모양 (구현 함수의 타입과 무관)
struct HostBindingShape {
    std::string_view id;
    api::AbiType result = api::AbiType::Void;
    std::array<api::AbiType, api::kMaxHostParams + 1> params{};
    u8 paramCount = 0;
    bool hasImpl = false;
};
template <class Host>
[[nodiscard]] std::vector<HostBindingShape> hostBindingShapes(std::span<const BasicHostBinding<Host>> bindings) {
    std::vector<HostBindingShape> out;
    out.reserve(bindings.size());
    for (const BasicHostBinding<Host>& b : bindings) {
        out.push_back({b.id, b.result, b.params, b.paramCount, b.impl != nullptr});
    }
    return out;
}
// 도메인 하나의 계약 — 문제마다 한 줄 (비면 통과): 구현 없음 · 둘 · 표에 없는 구현 · 다른 도메인 항목의 구현 ·
// 서명 다름 · 함수 포인터 없음
[[nodiscard]] std::vector<std::string> checkHostBindingShapes(std::span<const HostBindingShape> bindings,
                                                              api::HostDomain domain);

// 엔진의 구현 목록 (core/script/ScriptHost.cpp) — Server 도메인
[[nodiscard]] std::span<const HostBinding> hostBindings();
// 서버 계약 검사 (= checkHostBindingShapes(…, Server))
[[nodiscard]] std::vector<std::string> checkHostBindings(std::span<const HostBinding> bindings);

struct ScriptLogLine {
    sim::Tick tick = 0;
    std::string script;
    std::string text;
};

class ScriptHost final : public vm::IHost {
public:
    static constexpr usize kMaxLogBytes = 1024;       // 한 줄 상한 (넘으면 자른다)
    static constexpr usize kKeptLogLines = 256;       // recentLogs 에 남기는 줄
    static constexpr u32 kMaxLogLinesPerTick = 256;   // 틱마다 ctx.log 줄 상한 (넘으면 버리고 센다 — ADR-0064)
    static constexpr usize kMaxQueryEntities = 65536; // 질의 하나의 개체 상한 (넘으면 HostError)
    static constexpr usize kMaxPendingWrites = 65536; // 호출 하나의 쓰기 (같은 개체 · 컴포넌트는 하나로 친다)
    // 구조 변경 상한 (v0.5 M2-T3, ADR-0080 3) — 넘으면 그 연산을 버리고 센다 (트랩 아님 — spawn 은 null)
    static constexpr u32 kMaxSpawnsPerCall = 64;
    static constexpr u32 kMaxDestroysPerCall = 256;
    static constexpr u32 kMaxAddRemovesPerCall = 256;
    static constexpr u32 kMaxSpawnsPerTick = 1024; // 월드 하나의 모든 스크립트 호출 합

    ScriptHost();

    // 호출 하나를 연다 — 돌려준 값을 ctx 인자로 넘긴다. script = 로그에 붙는 이름 ("scripts/a.ts:tick").
    // world 가 없으면 컴포넌트 항목은 HostError. world 는 endCall 까지 살아야 하고, 그동안 구조를 바꾸지 않는다
    // callback = 이 호출의 콜백 이름 (api::kCallbacks — v0.10 RT-06-1 호출 토큰): 그 콜백의 허락 Capability 밖의 Host
    // 함수는 HostError. 표에 없는 이름이면 아무 Host 함수도 부르지 못한다
    vm::Value beginCall(sim::Tick tick, std::string script, sim::SimulationWorld* world = nullptr,
                        std::string_view callback = "onTick");
    [[nodiscard]] std::string_view callback() const noexcept { return m_callback; }
    [[nodiscard]] u32 capabilities() const noexcept { return m_capabilities; }
    // commit = 호출이 트랩 없이 끝났다 → 쓰기 버퍼를 쓴 차례대로 적용, 구조 변경은 월드 큐로 (v0.5 M2-T3 — 단계 끝에
    // 적용). false 면 둘 다 버린다 (ADR-0057 5)
    void endCall(bool commit);

    vm::HostResult call(const api::HostFunction& fn, std::span<const vm::HostArg> args) override;

    [[nodiscard]] const std::vector<ScriptLogLine>& recentLogs() const noexcept { return m_logs; }
    [[nodiscard]] usize pendingWrites() const noexcept { return m_writes.size(); }
    // 틱 상한으로 버린 ctx.log 줄 (처음부터 센 수)
    [[nodiscard]] u64 droppedLogs() const noexcept { return m_droppedLogs; }

    // 열린 호출의 개체 핸들 (시험 · 시스템 단계가 개체를 인자로 넘길 때 — 같은 개체 = 같은 핸들)
    [[nodiscard]] vm::Value entityHandle(ecs::EntityId e);

    // 바인딩이 쓰는 지금 호출의 상태 · 구현 (core/script/ScriptHost.cpp 의 표가 부른다)
    [[nodiscard]] sim::Tick tick() const noexcept { return m_tick; }
    void log(std::string_view text);
    vm::HostResult read(std::span<const vm::HostArg> args);
    vm::HostResult has(std::span<const vm::HostArg> args);
    vm::HostResult query(std::span<const vm::HostArg> args);
    vm::HostResult write(std::span<const vm::HostArg> args);
    // 구조 변경 (v0.5 M2-T3)
    vm::HostResult spawn(std::span<const vm::HostArg> args);
    vm::HostResult destroy(std::span<const vm::HostArg> args);
    vm::HostResult add(std::span<const vm::HostArg> args);
    vm::HostResult remove(std::span<const vm::HostArg> args);
    // 시간 · 난수 · 반경 질의 (v0.5 M2-T4)
    static constexpr usize kMaxNearEntities = 1024;
    static constexpr f64 kMaxNearRadius = 128.0;
    vm::HostResult time(std::span<const vm::HostArg> args);
    // 플레이어 표 (v0.5 M2-T5 (4a), ADR-0093) — SimulationWorld::players() 를 id 순으로
    vm::HostResult setOwner(std::span<const vm::HostArg> args);
    vm::HostResult playerCount(std::span<const vm::HostArg> args);
    vm::HostResult playerId(std::span<const vm::HostArg> args);
    vm::HostResult playerRole(std::span<const vm::HostArg> args);
    vm::HostResult random(std::span<const vm::HostArg> args);
    vm::HostResult randomInt(std::span<const vm::HostArg> args);
    vm::HostResult near(std::span<const vm::HostArg> args, const ecs::ComponentInfo* with);
    // 엔진 질의 · 경로 요청 (v0.5 M8-T8 — 19 25.7)
    static constexpr f64 kMaxBoxExtent = 256.0;
    static constexpr usize kMaxPathOpsPerCall = 256;
    vm::HostResult nearest(std::span<const vm::HostArg> args, bool many);
    vm::HostResult inBox(std::span<const vm::HostArg> args);
    vm::HostResult requestPath(std::span<const vm::HostArg> args);
    vm::HostResult pathState(std::span<const vm::HostArg> args);
    vm::HostResult pathPoints(std::span<const vm::HostArg> args);
    vm::HostResult cancelPath(std::span<const vm::HostArg> args);
    // 충돌 질의 (v0.5 M8-T8 (2)) — core.collider 원
    static constexpr f64 kMaxRayLength = 256.0;
    vm::HostResult raycast(std::span<const vm::HostArg> args);
    vm::HostResult overlap(std::span<const vm::HostArg> args);
    vm::HostResult nearWithArgs(std::span<const vm::HostArg> args);

private:
    struct PendingWrite {
        ecs::EntityId entity;
        const ecs::ComponentInfo* info = nullptr;
        std::vector<std::byte> bytes;
    };
    // 핸들 → 개체 (지난 호출 · 위조 · 죽은 개체면 kNullEntity)
    [[nodiscard]] ecs::EntityId entityOf(vm::Value handle) const;
    // 컴포넌트 참조 → 스크립트 컴포넌트 정보 (없으면 error 를 채우고 nullptr)
    const ecs::ComponentInfo* componentOf(vm::Value ref, std::string& error) const;
    // 지금 값 (쓰기 버퍼 → 풀). 없으면 nullptr
    [[nodiscard]] const std::byte* currentBytes(ecs::EntityId e, const ecs::ComponentInfo& info) const;
    // 스크립트 컴포넌트 값의 칸 → 배치 바이트 (범위 · 유한 · 핸들 검사). 문제면 그 설명
    [[nodiscard]] std::optional<std::string> encodeDynamic(const ecs::ComponentInfo& info, std::byte* bytes,
                                                           std::span<const vm::Value> v) const;
    // 이 호출에서 spawn 한 개체의 핸들 (읽기 null · add · destroy 의 대상) — 번호는 1 부터. 다른 핸들이면 0
    [[nodiscard]] vm::Value newEntityHandle(u32 newIndex) const;
    [[nodiscard]] u32 newIndexOf(vm::Value handle) const;

    std::vector<const HostBinding*> m_byHostIndex; // api::kHostFunctions 자리 → 구현
    u64 m_nextToken = 1;
    u64 m_token = 0;        // 0 = 열린 호출 없음
    std::string m_callback; // 이 호출의 콜백 (호출 토큰 — RT-06-1)
    u32 m_capabilities = 0; // 그 콜백의 허락 Capability
    sim::Tick m_tick = 0;
    std::string m_script;
    std::vector<ScriptLogLine> m_logs;
    sim::Tick m_logTick = 0;
    u32 m_logLinesThisTick = 0;
    u64 m_droppedLogs = 0;
    sim::SimulationWorld* m_world = nullptr;
    std::vector<ecs::EntityId> m_handles;           // 핸들 자리 → 개체 (호출마다 새로)
    std::map<u64, u32> m_handleOf;                  // EntityId.raw → 자리 (찾기만)
    std::vector<PendingWrite> m_writes;             // 쓴 차례
    std::map<std::pair<u64, u64>, usize> m_writeOf; // (EntityId.raw, stableId) → m_writes 자리 (찾기만)
    // 경로 요청 · 취소 (v0.5 M8-T8) — 호출 끝 (commit) 에 ai.path 로, 트랩이면 버린다
    struct PathOp {
        ecs::EntityId entity{};
        bool cancel = false;
        Vec2 goal{};
    };
    std::vector<PathOp> m_pathOps;
    // 구조 변경 (v0.5 M2-T3) — 이 호출의 것
    sim::ScriptStructuralBatch m_ops;
    u32 m_newEntities = 0; // spawn 한 수 (newIndex 의 끝)
    u32 m_destroysThisCall = 0;
    u32 m_addRemovesThisCall = 0;
    u64 m_droppedThisCall = 0; // 상한 · 잘못된 연산으로 버린 수 (commit 이면 월드 통계에)
    // ctx.random 흐름 (v0.5 M2-T4) — 틱마다 (시스템 id 해시 → 흐름). 같은 시스템의 호출들이 한 틱에 이어 쓴다
    std::map<u64, rnd::CounterRng> m_streams;
    sim::Tick m_streamTick = 0;
    [[nodiscard]] rnd::CounterRng* stream();
};

} // namespace sbx::script
