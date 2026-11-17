#pragma once
// 클라이언트 스크립트 시스템 (v0.3 M5-T3, ADR-0067). docs/19-SANDBOXSCRIPT.md 21장.
//
// 콘텐츠의 팩 바이트코드 (content::ScriptPrograms) 에서 client 시스템 (domain: "client") 만 — 팩마다 인터프리터 하나,
// Host (ClientScriptHost) 는 하나. run 이 프레임마다 (팩 순서, 시스템 id) 차례로 부른다 (v0.10 RT-06-7, ADR-0140):
//   세션의 첫 run   모든 시스템의 onStart → onEvent → (그 프레임의) onUpdate → onLateUpdate
//   그 뒤 run      (이벤트가 있으면 onEvent) → 모든 시스템의 onUpdate → 모든 시스템의 onLateUpdate → 모든 시스템의
//                 onRender (v0.11 RT-06-9 — 그리기 명령만, host().draws())
//   onEvent       복제된 서버 이벤트 (v0.10 RT-06-7 조각 2, ADR-0141) — 시스템 차례 → 구독 종류 차례 → 받은 차례 (틱
//   차례),
//                 시스템마다 프레임에 eventsPerTick 개 · 모두 api::kMaxEventsPerTick (넘으면 건너뛰고 센다 — cut)
//   onAction      게임 키 (frame.input 의 키 — 앱이 UI · 메뉴가 먹은 것을 뺐다) 의 눌림 · 뗌 (v0.10 RT-06-8, ADR-0142)
//   —
//                 onEvent 뒤 · onUpdate 앞, 시스템 차례 → 키 이름 차례, 한 프레임에 눌렀다 떼면 눌림 다음 뗌
//   stop          모든 시스템의 onStop (시작한 세션만 — 한 번). 다음 run 이 새 세션을 연다 (onStart 다시)
// 세션 = 복제 월드 하나 (앱이 정한다 — NetworkSession: 월드가 바뀜 · 콘텐츠가 바뀜 · 접속 끝에 stop). 꺼진 시스템은
// 어느 콜백도 부르지 않는다. 예산: 호출마다 연료 min(kCallFuel, 프레임에 남은 연료), 프레임 연료 kFrameFuel — 다 쓰면
// 그 프레임의 남은 호출은 건너뛴다 ("예산" — 트랩 정책에 넣지 않는다). 트랩 정책: 한 시스템의 호출이 트랩으로
// kDisableAfterTraps 번 이어지면 그 시스템을 끈다 (이 접속 동안 — 상태는 메모리에만, 콘텐츠가 바뀌면 다시 만든다).
// 서버의 ScriptSystemAdapter 와 달리 시뮬레이션 상태가 아니다: 세이브 · 해시 · 서버와 무관하다.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ScriptPrograms.hpp"
#include "core/script/ClientScriptHost.hpp"
#include "core/script/ScriptSystemAdapter.hpp"

namespace sbx::script {

inline constexpr u64 kFrameFuel = 2'000'000; // 한 프레임의 모든 client 시스템 호출 합

// 이번 프레임 (run 하나)
struct ClientScriptStats {
    u32 calls = 0;
    u32 traps = 0;
    u32 budget = 0; // 프레임 연료로 멈춘 호출
    u32 cut = 0;    // 프레임 연료를 다 써 건너뛴 호출
    u64 fuel = 0;
    u64 nanos = 0; // 진단 시계가 있을 때 (ADR-0068)
};

class ClientScriptSystems {
public:
    // programs 의 모듈은 검증된 것 (콘텐츠 로드가 만든 것). client 시스템이 없어도 된다 (run 이 아무것도 하지 않는다)
    explicit ClientScriptSystems(std::shared_ptr<const content::ScriptPrograms> programs);
    ClientScriptSystems(const ClientScriptSystems&) = delete;
    ClientScriptSystems& operator=(const ClientScriptSystems&) = delete;
    ~ClientScriptSystems();

    // 프레임 하나 — frame.frame 은 여기서 센다 (넘긴 값은 쓰지 않는다). 표시는 host().labels() · lines(). events = 이
    // 프레임에 닿은 복제 이벤트 (받은 차례)
    void run(ClientScriptFrame frame, std::span<const ClientScriptEvent> events = {});
    // 세션 끝 — 시작한 세션이면 onStop 을 부른다 (아니면 아무것도 하지 않는다). frame.registry 는 null 이어도 된다
    // (월드가 이미 없다 — 질의는 빈 목록). frame.frame 은 마지막 프레임 번호, 연료는 프레임 하나 몫
    void stop(ClientScriptFrame frame);
    [[nodiscard]] bool started() const noexcept { return m_started; }
    [[nodiscard]] u32 sessions() const noexcept { return m_sessions; } // 연 세션 수 (onStart 를 부른 run)

    [[nodiscard]] const ClientScriptStats& lastStats() const noexcept { return m_last; }
    [[nodiscard]] u32 frames() const noexcept { return m_frames; }
    [[nodiscard]] usize systemCount() const noexcept { return m_systems.size(); }
    [[nodiscard]] std::string_view systemId(usize i) const noexcept { return m_systems[i].desc->id; }
    [[nodiscard]] bool disabled(usize i) const noexcept { return m_systems[i].disabled; }
    [[nodiscard]] const ScriptSystemCounters& counters(usize i) const noexcept { return m_systems[i].counters; }
    // 지난 run (· stop) 에서 그 시스템이 쓴 연료 · 시간 — 콜백들의 합 (부르지 않았으면 0)
    [[nodiscard]] u64 lastFuel(usize i) const noexcept { return m_systems[i].lastFuel; }
    [[nodiscard]] u64 lastNanos(usize i) const noexcept { return m_systems[i].lastNanos; }
    // 함수별 프로파일 (v0.9 M6-T4 — 만든 뒤 누적)
    [[nodiscard]] std::vector<ScriptFunctionRow> functionProfile(usize maxRows, u64* totalFuel = nullptr) const {
        return m_programs ? collectFunctionProfile(*m_programs, m_vms, maxRows, totalFuel)
                          : std::vector<ScriptFunctionRow>{};
    }
    // 진단 시계 (null = 재지 않는다 — ScriptClock.hpp)
    void setClock(ScriptClock clock) noexcept { m_clock = clock; }
    [[nodiscard]] bool timed() const noexcept { return m_clock != nullptr; }
    [[nodiscard]] const ClientScriptHost& host() const noexcept { return m_host; }
    [[nodiscard]] const std::shared_ptr<const content::ScriptPrograms>& programs() const noexcept { return m_programs; }

private:
    struct Entry {
        u32 pack = 0;
        u32 index = 0; // 팩 모듈의 시스템 자리 (구독 표 — ScriptPackProgram::events)
        const bc::SystemDesc* desc = nullptr;
        u32 consecutive = 0; // 이어진 트랩
        bool disabled = false;
        ScriptSystemCounters counters;
        u64 lastFuel = 0;
        u64 lastNanos = 0;
    };
    enum class Callback : u8 { Start, Update, LateUpdate, Stop, Event, Action, Render };
    struct ActionValues {
        std::string_view name;
        bool pressed = false;
        Vec2 pos{};
    };
    void invoke(Entry& s, Callback which, const ClientScriptEvent* ev = nullptr, const ActionValues* act = nullptr);
    void deliverEvents(std::span<const ClientScriptEvent> events);
    void deliverActions(const ClientScriptInput* input);

    std::shared_ptr<const content::ScriptPrograms> m_programs;
    ScriptClock m_clock = nullptr;
    ClientScriptHost m_host;                                 // 인터프리터보다 먼저 만들고 나중에 없앤다
    std::vector<std::unique_ptr<vm::IScriptExecutor>> m_vms; // packs 차례 (client 시스템이 없는 팩은 null)
    std::vector<Entry> m_systems;                            // 실행 순서 (팩 순서, id)
    u64 m_frameFuelLeft = kFrameFuel;
    u32 m_frames = 0;
    u32 m_sessions = 0;
    bool m_started = false;
    bool m_budgetLogged = false;
    ClientScriptStats m_last;
    std::vector<vm::Value> m_fields; // onEvent 의 GameEvent 칸 (호출마다 다시)
    std::vector<std::string> m_texts;
};

// 시스템마다 한 줄 — "클라이언트 스크립트 rpg.hud.names: 호출 10 · 트랩 0 · 예산 0 · 건너뜀 0 · 연료 합 1234 · 최대
// 200" (꺼졌으면 " · 꺼짐")
[[nodiscard]] std::vector<std::string> describeClientScriptSystems(const ClientScriptSystems& systems);

} // namespace sbx::script
