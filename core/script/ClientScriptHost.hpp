#pragma once
// 클라이언트 스크립트 Host (v0.3 M5-T3, ADR-0067). docs/19-SANDBOXSCRIPT.md 21장.
//
// client 시스템 (defineSystem({ domain: "client", onUpdate(ctx) { … } })) 의 ctx = ClientContext. 보는 것은 복제 월드
// (클라이언트의 ecs::Registry — 읽기만) 와 그 카탈로그, 하는 것은 화면 표시 (개체 위 글자 · "스크립트" 칸의 줄) 뿐이다.
// 시뮬레이션 · 서버로 가는 것은 없다 — 명령 보내기 · 입력은 [계획]. 그래서 결정론 규칙 (04) 밖이다: 프레임 수 · dt 는
// 표시용이고, 결과는 그 클라이언트 화면에만 남는다.
//
//   ctx 핸들   호출마다 새로 (지난 호출 · 위조 = HostError — 서버 ScriptHost 와 같은 규칙)
//   개체 핸들  호출마다 새 표. 질의 = netId 오름차순 (comp::NetIdentity — 없는 개체는 빠진다)
//   읽기      client.read · has · query — 스크립트 컴포넌트만. 개체 참조 필드는 null [계획]
//   표시      client.label · print — 줄마다 kMaxTextBytes 바이트 (UTF-8 글자 가운데에서 자르지 않는다), 프레임마다
//             kMaxLabels · kMaxLines 까지 (넘으면 버리고 센다). 트랩난 호출의 표시는 버린다

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/ecs/ComponentCatalog.hpp"
#include "core/ecs/Registry.hpp"
#include "core/script/ScriptHost.hpp"

namespace sbx::script {

class ClientScriptHost;
using ClientHostBinding = BasicHostBinding<ClientScriptHost>;

// 엔진의 클라이언트 구현 목록 (core/script/ClientScriptHost.cpp) — Client 도메인
[[nodiscard]] std::span<const ClientHostBinding> clientHostBindings();
// 클라이언트 계약 검사 (= checkHostBindingShapes(…, Client))
[[nodiscard]] std::vector<std::string> checkClientHostBindings(std::span<const ClientHostBinding> bindings);

// 프레임 하나에 스크립트가 보는 것 — registry · catalog 는 그 프레임의 호출이 끝날 때까지 살아야 한다
struct ClientScriptFrame {
    const ecs::Registry* registry = nullptr;
    const ecs::ComponentCatalog* catalog = nullptr;
    u64 tick = 0;  // 복제 월드의 서버 틱
    u32 frame = 0; // 몇 번째 프레임인가 (1 부터)
    f64 dt = 0;    // 앞 프레임 뒤 지난 실제 시간 (초)
};

struct ClientScriptLabel {
    ecs::EntityId entity; // 복제 월드의 개체
    std::string text;
};
struct ClientScriptLine {
    std::string system; // 쓴 시스템 id
    std::string text;
};

class ClientScriptHost final : public vm::IHost {
public:
    static constexpr usize kMaxTextBytes = 256;       // label · print 한 줄
    static constexpr usize kMaxLabels = 1024;         // 프레임마다
    static constexpr usize kMaxLines = 64;            // 프레임마다
    static constexpr usize kMaxQueryEntities = 65536; // 질의 하나 (넘으면 HostError)

    ClientScriptHost();

    // 프레임을 연다 — 지난 프레임의 표시를 비운다
    void beginFrame(const ClientScriptFrame& frame);
    // 호출 하나를 연다 — 돌려준 값을 ctx 인자로 넘긴다
    vm::Value beginCall(std::string system);
    // ok = 트랩 없이 끝났다. false 면 이 호출의 표시를 버린다
    void endCall(bool ok);

    vm::HostResult call(const api::HostFunction& fn, std::span<const vm::HostArg> args) override;

    // 이번 프레임의 표시 (부른 차례)
    [[nodiscard]] const std::vector<ClientScriptLabel>& labels() const noexcept { return m_labels; }
    [[nodiscard]] const std::vector<ClientScriptLine>& lines() const noexcept { return m_lines; }
    // 프레임 상한으로 버린 label · print (처음부터 센 수)
    [[nodiscard]] u64 droppedDisplay() const noexcept { return m_dropped; }

    // 바인딩이 쓰는 지금 호출의 상태 · 구현 (core/script/ClientScriptHost.cpp 의 표가 부른다)
    [[nodiscard]] const ClientScriptFrame& frame() const noexcept { return m_frame; }
    vm::HostResult read(std::span<const vm::HostArg> args);
    vm::HostResult has(std::span<const vm::HostArg> args);
    vm::HostResult query(std::span<const vm::HostArg> args);
    vm::HostResult label(std::span<const vm::HostArg> args);
    vm::HostResult print(std::span<const vm::HostArg> args);

private:
    [[nodiscard]] vm::Value entityHandle(ecs::EntityId e);
    [[nodiscard]] ecs::EntityId entityOf(vm::Value handle) const;
    const ecs::ComponentInfo* componentOf(vm::Value ref, std::string& error) const;

    std::vector<const ClientHostBinding*> m_byHostIndex; // api::kHostFunctions 자리 → 구현
    ClientScriptFrame m_frame;
    u64 m_nextToken = 1;
    u64 m_token = 0; // 0 = 열린 호출 없음
    std::string m_system;
    usize m_labelsAtCall = 0;
    usize m_linesAtCall = 0;
    std::vector<ecs::EntityId> m_handles; // 핸들 자리 → 개체 (호출마다 새로)
    std::map<u64, u32> m_handleOf;        // EntityId.raw → 자리 (찾기만)
    std::vector<ClientScriptLabel> m_labels;
    std::vector<ClientScriptLine> m_lines;
    u64 m_dropped = 0;
};

} // namespace sbx::script
