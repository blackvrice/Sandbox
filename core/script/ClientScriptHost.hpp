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
#include "foundation/math/Vec2.hpp"

namespace sbx::script {

class ClientScriptHost;
using ClientHostBinding = BasicHostBinding<ClientScriptHost>;

// 엔진의 클라이언트 구현 목록 (core/script/ClientScriptHost.cpp) — Client 도메인
[[nodiscard]] std::span<const ClientHostBinding> clientHostBindings();
// 클라이언트 계약 검사 (= checkHostBindingShapes(…, Client))
[[nodiscard]] std::vector<std::string> checkClientHostBindings(std::span<const ClientHostBinding> bindings);

// 이번 프레임의 입력 (v0.5 M2-T6) — 게임 키 ("play.<이름>" 의 <이름>, 이름 정렬) · 커서의 월드 자리. Application 이
// 만든다
struct ClientScriptKey {
    std::string name;
    bool down = false;
    bool pressed = false;
    bool released = false;
};
struct ClientScriptInput {
    std::vector<ClientScriptKey> keys; // name 오름차순 (찾기는 이분)
    Vec2 pointer{};
    bool hasPointer = false;
};
// 스크립트가 내보낸 것 (부른 차례) — 트랩난 호출의 것은 버린다
struct ClientScriptSend {
    std::string system;
    std::string signal;
    i32 value = 0;
};
struct ClientScriptSound {
    std::string system;
    std::string id;
};
// 게임 UI (v0.7 M3-T4) — var 바인딩 값 (수 · 글자) 또는 문서 보이기
struct ClientScriptUi {
    enum class Kind : u8 { Number, Text, Show };
    Kind kind = Kind::Number;
    std::string key; // var 키 · 문서 id
    f64 number = 0;
    std::string text;
    bool visible = false;
};

// 프레임 하나에 스크립트가 보는 것 — registry · catalog 는 그 프레임의 호출이 끝날 때까지 살아야 한다
struct ClientScriptFrame {
    const ecs::Registry* registry = nullptr;
    const ecs::ComponentCatalog* catalog = nullptr;
    u64 tick = 0;                             // 복제 월드의 서버 틱
    u32 frame = 0;                            // 몇 번째 프레임인가 (1 부터)
    f64 dt = 0;                               // 앞 프레임 뒤 지난 실제 시간 (초)
    const ClientScriptInput* input = nullptr; // 없으면 키는 모두 false · 커서 (0, 0) (v0.5 M2-T6)
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
    static constexpr usize kMaxSendsPerFrame = 8;     // client.send (v0.5 M2-T6 — 서버의 perTick · 속도 제한과 따로)
    static constexpr usize kMaxSoundsPerFrame = 32;   // client.playSound
    static constexpr usize kMaxUiPerFrame = 64;       // client.setUi · setUiText · showUi (v0.7 M3-T4)

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
    // 이번 프레임에 내보낸 신호 · 소리 (부른 차례), 상한으로 버린 수 (처음부터)
    [[nodiscard]] const std::vector<ClientScriptSend>& sends() const noexcept { return m_sends; }
    [[nodiscard]] const std::vector<ClientScriptSound>& sounds() const noexcept { return m_sounds; }
    [[nodiscard]] u64 droppedOutputs() const noexcept { return m_droppedOutputs; }
    // 이번 프레임의 게임 UI 값 · 보이기 (부른 차례 — 트랩난 호출의 것은 버렸다)
    [[nodiscard]] const std::vector<ClientScriptUi>& uiOutputs() const noexcept { return m_ui; }

    // 바인딩이 쓰는 지금 호출의 상태 · 구현 (core/script/ClientScriptHost.cpp 의 표가 부른다)
    [[nodiscard]] const ClientScriptFrame& frame() const noexcept { return m_frame; }
    vm::HostResult read(std::span<const vm::HostArg> args);
    vm::HostResult has(std::span<const vm::HostArg> args);
    vm::HostResult query(std::span<const vm::HostArg> args);
    vm::HostResult label(std::span<const vm::HostArg> args);
    vm::HostResult print(std::span<const vm::HostArg> args);
    vm::HostResult key(std::span<const vm::HostArg> args, int which); // 0 down · 1 pressed · 2 released
    vm::HostResult send(std::span<const vm::HostArg> args);
    vm::HostResult playSound(std::span<const vm::HostArg> args);
    vm::HostResult ui(std::span<const vm::HostArg> args, ClientScriptUi::Kind kind);

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
    usize m_sendsAtCall = 0;
    usize m_soundsAtCall = 0;
    usize m_uiAtCall = 0;
    std::vector<ecs::EntityId> m_handles; // 핸들 자리 → 개체 (호출마다 새로)
    std::map<u64, u32> m_handleOf;        // EntityId.raw → 자리 (찾기만)
    std::vector<ClientScriptLabel> m_labels;
    std::vector<ClientScriptLine> m_lines;
    u64 m_dropped = 0;
    std::vector<ClientScriptSend> m_sends;
    std::vector<ClientScriptSound> m_sounds;
    std::vector<ClientScriptUi> m_ui;
    u64 m_droppedOutputs = 0;
};

} // namespace sbx::script
