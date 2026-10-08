#pragma once
// 서버가 받은 명령을 시뮬레이션에 넣기 전에 거르는 곳. docs/03-SIMULATION.md 4.1, docs/10-EDITOR.md 7장, ADR-0024.
//
//   1 형식      CommandCodec 이 메시지를 읽을 때 (개수 · 길이 · 유한한 수 · JSON)
//   2 순서      sequence 는 클라이언트마다 단조 증가 (같거나 작으면 거절)
//   3 속도 제한  클라이언트마다 토큰 버킷 — 초당 ratePerSecond, 몰아서 burst 까지 (03 4.1 기본 120)
//   4 권한      역할 × 명령 종류 (10 7장 표). 판단은 여기 한 곳 — 클라 UI 의 버튼 숨김은 편의일 뿐
//   (5 대상 · 콘텐츠 · 값 은 SimulationWorld 가 적용하면서 본다 — 거절은 CommandResult 로 같은 길)
//
// Net IO 스레드에서 돈다 (월드를 보지 않는다 — T1). 시간은 호출자가 준다 (초).

#include "core/command/SimCommand.hpp"
#include "network/protocol/Messages.hpp"

namespace sbx::net {

// 권한 표의 열
enum class CommandClass : u8 {
    PlayerAction = 0,      // [계획 Phase 12] — 아직 그런 명령이 없다
    EntityEdit = 1,        // 엔티티 · 컴포넌트 편집
    TerrainEdit = 2,       // 지형 칠하기
    ContentEdit = 3,       // Rule · Behavior · Prefab [계획 Phase 12]
    SimulationControl = 4, // 일시정지 · 재개 · 한 틱 · 속도
};

[[nodiscard]] CommandClass classifyCommand(const cmd::CommandPayload& payload) noexcept;
[[nodiscard]] bool roleAllows(Role role, CommandClass c) noexcept;

struct CommandValidatorDesc {
    f64 ratePerSecond = 120;
    f64 burst = 120;
};

// 클라이언트 한 명의 검사 상태 (Net IO 스레드가 클라이언트마다 하나)
struct ClientCommandState {
    u32 lastSequence = 0;
    f64 tokens = -1; // 음수 = 아직 첫 명령 전 (버킷을 가득 채워 시작)
    f64 lastRefillSeconds = 0;
};

class CommandValidator {
public:
    explicit CommandValidator(CommandValidatorDesc desc = {}) : m_desc(desc) {}

    // 통과하면 state 를 갱신한다. 거절이어도 sequence 는 소비한다 (같은 번호를 다시 보내지 못하게)
    [[nodiscard]] Expected<void> check(ClientCommandState& state, Role role, u32 sequence,
                                       const cmd::CommandPayload& payload, f64 nowSeconds) const;

private:
    CommandValidatorDesc m_desc;
};

} // namespace sbx::net
