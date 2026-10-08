#pragma once
// SimCommand 페이로드 ↔ 비트스트림 (Command 메시지의 몸). docs/08-NETWORK.md 5장, docs/09-SERIALIZATION.md 5장.
//
//   종류 태그 u8 (PayloadTag — 값은 바꾸지 않는다, 새 명령은 새 값) + 종류별 필드.
//   엔티티 참조 = NetEntityId varint (03 4장 M2). 좌표 · 속도 = f32. 타일 = zigzag varint.
//   컴포넌트 값 · 패치는 JSON 텍스트 (리플렉션 JsonReader 로 적용 — SimCommand 가 JSON 을 싣는 것과 같다).
//
// 읽기에서 막는 것 = 형식 (03 4.1 의 1단계): 개수 · 길이 상한, 유한한 수, 알려진 태그, 파싱되는 JSON. 대상이 있는지 ·
// 값이 범위 안인지 · 콘텐츠 id 가 있는지는 SimulationWorld 가 적용하면서 본다 (거절 → CommandResult).

#include "core/command/SimCommand.hpp"
#include "network/protocol/BitStream.hpp"

namespace sbx::net {

enum class PayloadTag : u8 {
    CreateEntity = 1,
    DeleteEntity = 2,
    MoveEntity = 3,
    AddComponent = 4,
    RemoveComponent = 5,
    ChangeComponent = 6,
    PaintTerrain = 7,
    PauseSimulation = 20,
    ResumeSimulation = 21,
    StepSimulation = 22,
    SetSimulationSpeed = 23,
};

inline constexpr usize kMaxJsonTextBytes = 16 * 1024; // 컴포넌트 값 하나 · 패치 하나
inline constexpr usize kMaxContentIdBytes = 64;       // prefab · material id

void writePayload(BitWriter& w, const cmd::CommandPayload& payload);
// 형식 오류면 false (r.error() 도 true)
[[nodiscard]] bool readPayload(BitReader& r, cmd::CommandPayload& out);

} // namespace sbx::net
