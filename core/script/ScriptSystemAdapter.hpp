#pragma once
// 스크립트 시스템 단계 16b · 17d (v0.3 M4-T1 · T2, ADR-0062 · 0063). docs/19-SANDBOXSCRIPT.md 17 · 18장, 03-SIMULATION
// 2장.
//
// 콘텐츠의 팩 바이트코드 (content::ScriptPrograms) 마다 인터프리터 하나, Host (ScriptHost) 는 하나.
//   16b run           (팩 순서, 시스템 id) 차례로 이번 틱에 돌 시스템 (틱 번호 % intervalTicks == 0) 의 onTick
//   17d runCallbacks  ① onStart (시작한 틱) ② onEvent (시스템 차례 → 구독 종류 차례 → 그 틱에 일어난 차례) ③ onStop
//   (끝난 틱)
//                     — 재료 (ScriptEventFacts) 는 SimulationWorld 가 그 틱 안에서 모은다 (단계 0 스냅숏 · EventStream)
// 호출 하나 = beginCall(틱, 시스템 id, 월드) → Interpreter::call → endCall(트랩 아님). 앞 호출의 쓰기는 바로 적용되어
// 뒤 호출이 본다. 트랩이면 그 호출의 쓰기를 버리고 다음 호출로 — 로그는 시스템마다 처음 한 줄 (끄는 정책 · 예산은 [계획
// M4-T3]). onEvent 예산: 시스템마다 eventsPerTick, 틱 전체 api::kMaxEventsPerTick — 넘은 이벤트는 건너뛰고 센다 (cut).
// 시뮬레이션 상태가 없다 (주기 · 시작 · 끝은 틱 번호 · 세션으로만) — 트랩 수 · 통계는 진단이고 세이브 · WorldHash 에
// 없다.

#include <memory>
#include <string_view>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/script/ScriptHost.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::script {

struct ScriptSystemStats {
    u32 calls = 0; // 이번 틱에 부른 콜백 (16b + 17d)
    u32 traps = 0;
    u32 cut = 0; // 예산으로 건너뛴 onEvent
    u64 fuel = 0;
};

// 단계 17d 의 재료 (ADR-0063 3) — 구독이 없는 종류는 비어 있어도 된다
struct ScriptEventFacts {
    struct Death {
        SaveId saveId = kInvalidSaveId;
        bool hasPos = false; // 틱 처음의 스냅숏에 있었다 (그 틱에 생겨 죽었으면 없다)
        Vec2 pos{};
        content::TagSet tags; // 스냅숏의 태그
        i32 cause = 0;        // sim::DeathCause
    };
    struct Present {
        ecs::EntityId entity{};
        SaveId saveId = kInvalidSaveId;
        Vec2 pos{};
        content::TagSet tags;
        bool before = false; // 틱 처음의 스냅숏에 있었다
        Vec2 beforePos{};
    };
    struct Score {
        i32 slot = 0;
        i32 before = 0;
        i32 after = 0;
    };
    bool starting = false;
    bool stopping = false;
    std::vector<Death> deaths;    // 발생 순 (Died)
    std::vector<Present> spawns;  // 생성 순, 17d 에 살아 있는 것
    std::vector<Present> current; // 지금 개체 — saveId 순 (areaEnter)
    std::vector<Score> scores;    // 바뀐 칸 — 칸 순
};

class ScriptSystemAdapter {
public:
    // programs 의 모듈은 검증된 것 (콘텐츠 로드가 만든 것)
    explicit ScriptSystemAdapter(std::shared_ptr<const content::ScriptPrograms> programs);
    ScriptSystemAdapter(const ScriptSystemAdapter&) = delete;
    ScriptSystemAdapter& operator=(const ScriptSystemAdapter&) = delete;
    ~ScriptSystemAdapter();

    // 단계 16b — world 는 System 이 도는 틱 (구조 잠금 밖, ECB 적용 전). 이번 틱의 통계를 새로 센다
    void run(sim::SimulationWorld& world, sim::Tick tick);
    // 단계 17d — 17c 다음 (통계는 run 에 더한다)
    void runCallbacks(sim::SimulationWorld& world, sim::Tick tick, const ScriptEventFacts& facts);

    // onStart · onStop · onEvent 가 있는 시스템이 있다 (17d 가 필요하다)
    [[nodiscard]] bool hasCallbacks() const noexcept { return m_hasCallbacks; }
    // 이 종류를 구독한 시스템이 있다
    [[nodiscard]] bool subscribes(api::GameEventKind kind) const noexcept {
        return (m_subscribed & (1u << static_cast<u32>(kind))) != 0;
    }

    [[nodiscard]] const ScriptSystemStats& lastStats() const noexcept { return m_last; }
    // 실행 순서의 시스템 (시험 · 진단)
    [[nodiscard]] usize systemCount() const noexcept { return m_systems.size(); }
    [[nodiscard]] std::string_view systemId(usize i) const noexcept { return m_systems[i].desc->id; }
    [[nodiscard]] u32 trapCount(usize i) const noexcept { return m_systems[i].traps; } // 처음부터 센 수
    [[nodiscard]] const ScriptHost& host() const noexcept { return m_host; }

private:
    struct Entry {
        u32 pack = 0;
        u32 index = 0; // 모듈의 systems 자리
        const bc::SystemDesc* desc = nullptr;
        u32 traps = 0;
    };
    struct EventValues {
        api::GameEventKind kind = api::GameEventKind::Death;
        ecs::EntityId entity{};
        f64 x = 0;
        f64 y = 0;
        i32 team = 0;
        i32 value = 0;
        i32 previous = 0;
    };
    // 콜백 하나를 부른다 (ev 가 있으면 onEvent 의 e)
    void invoke(sim::SimulationWorld& world, sim::Tick tick, Entry& s, u32 fn, std::string_view callback,
                const EventValues* ev);

    std::shared_ptr<const content::ScriptPrograms> m_programs;
    ScriptHost m_host;                                   // 인터프리터보다 먼저 만들고 나중에 없앤다
    std::vector<std::unique_ptr<vm::Interpreter>> m_vms; // packs 차례
    std::vector<Entry> m_systems;                        // 실행 순서 (팩 순서, id)
    bool m_hasCallbacks = false;
    u32 m_subscribed = 0; // 1 << GameEventKind
    std::vector<vm::Value> m_fields;
    ScriptSystemStats m_last;
};

} // namespace sbx::script
