#pragma once
// 클라이언트 스크립트 시스템 (v0.3 M5-T3, ADR-0067). docs/19-SANDBOXSCRIPT.md 21장.
//
// 콘텐츠의 팩 바이트코드 (content::ScriptPrograms) 에서 client 시스템 (domain: "client") 만 — 팩마다 인터프리터 하나,
// Host (ClientScriptHost) 는 하나. run 이 프레임마다 (팩 순서, 시스템 id) 차례로 onUpdate(ctx) 를 부른다.
// 예산: 호출마다 연료 min(kCallFuel, 프레임에 남은 연료), 프레임 연료 kFrameFuel — 다 쓰면 그 프레임의 남은 호출은
// 건너뛴다 ("예산" — 트랩 정책에 넣지 않는다). 트랩 정책: 한 시스템의 호출이 트랩으로 kDisableAfterTraps 번 이어지면
// 그 시스템을 끈다 (이 접속 동안 — 상태는 메모리에만, 콘텐츠가 바뀌면 다시 만든다). 서버의 ScriptSystemAdapter 와 달리
// 시뮬레이션 상태가 아니다: 세이브 · 해시 · 서버와 무관하다.

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
};

class ClientScriptSystems {
public:
    // programs 의 모듈은 검증된 것 (콘텐츠 로드가 만든 것). client 시스템이 없어도 된다 (run 이 아무것도 하지 않는다)
    explicit ClientScriptSystems(std::shared_ptr<const content::ScriptPrograms> programs);
    ClientScriptSystems(const ClientScriptSystems&) = delete;
    ClientScriptSystems& operator=(const ClientScriptSystems&) = delete;
    ~ClientScriptSystems();

    // 프레임 하나 — frame.frame 은 여기서 센다 (넘긴 값은 쓰지 않는다). 표시는 host().labels() · lines()
    void run(ClientScriptFrame frame);

    [[nodiscard]] const ClientScriptStats& lastStats() const noexcept { return m_last; }
    [[nodiscard]] u32 frames() const noexcept { return m_frames; }
    [[nodiscard]] usize systemCount() const noexcept { return m_systems.size(); }
    [[nodiscard]] std::string_view systemId(usize i) const noexcept { return m_systems[i].desc->id; }
    [[nodiscard]] bool disabled(usize i) const noexcept { return m_systems[i].disabled; }
    [[nodiscard]] const ScriptSystemCounters& counters(usize i) const noexcept { return m_systems[i].counters; }
    [[nodiscard]] const ClientScriptHost& host() const noexcept { return m_host; }
    [[nodiscard]] const std::shared_ptr<const content::ScriptPrograms>& programs() const noexcept { return m_programs; }

private:
    struct Entry {
        u32 pack = 0;
        const bc::SystemDesc* desc = nullptr;
        u32 consecutive = 0; // 이어진 트랩
        bool disabled = false;
        ScriptSystemCounters counters;
    };
    void invoke(Entry& s);

    std::shared_ptr<const content::ScriptPrograms> m_programs;
    ClientScriptHost m_host;                             // 인터프리터보다 먼저 만들고 나중에 없앤다
    std::vector<std::unique_ptr<vm::Interpreter>> m_vms; // packs 차례 (client 시스템이 없는 팩은 null)
    std::vector<Entry> m_systems;                        // 실행 순서 (팩 순서, id)
    u64 m_frameFuelLeft = kFrameFuel;
    u32 m_frames = 0;
    bool m_budgetLogged = false;
    ClientScriptStats m_last;
};

// 시스템마다 한 줄 — "클라이언트 스크립트 rpg.hud.names: 호출 10 · 트랩 0 · 예산 0 · 건너뜀 0 · 연료 합 1234 · 최대
// 200" (꺼졌으면 " · 꺼짐")
[[nodiscard]] std::vector<std::string> describeClientScriptSystems(const ClientScriptSystems& systems);

} // namespace sbx::script
