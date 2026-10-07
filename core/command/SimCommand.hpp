#pragma once
// 월드를 바꾸는 유일한 입력. docs/03-SIMULATION.md 4장.
//
// M1 값 타입 variant — 할당·가상 함수 없이 복사·비교·직렬화한다.
// M2 엔티티 참조는 NetEntityId (클라이언트는 서버 EntityId 를 모른다).
// M3 선택·카메라·UI 는 명령이 아니다.
// M4 executeTick 은 서버가 정한다.
// M5 적용 순서 = (executeTick, issuer, sequence). 도착 순서가 아니다.
// M6 실패는 조용히 무시하지 않고 CommandResult{Rejected} 로 알린다.
//
// 컴포넌트 값은 JSON 으로 싣는다 (리플렉션 JsonReader 로 적용). 와이어 포맷(비트스트림)은 Phase 9.
// Phase 3 범위의 명령만 있다. PaintTerrain(4)·ChangeRule/ChangeBehavior/CreatePrefab/PlayerAction(5, 12) 은 해당 Phase
// 에서 추가한다.

#include <string>
#include <variant>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/ecs/Component.hpp"
#include "core/serialization/JsonVisitor.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/math/Vec2.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::cmd {

using ClientId = u16; // 0 = 서버/시스템

inline constexpr ClientId kServerIssuer = 0;

struct CommandHeader {
    sim::Tick executeTick = 0;
    ClientId issuer = kServerIssuer;
    u32 sequence = 0; // issuer 별 단조 증가
};

struct ComponentValue {
    ecs::StableId stableId = 0;
    ecs::Json value = ecs::Json::object(); // 없는 필드는 기본값
};

// --- 편집 -------------------------------------------------------------------
// prefab 이 있으면 Prefab 의 컴포넌트·태그로 만들고 components 로 덮어쓴다 (키 단위 병합, 11-CONTENT-SCHEMA 2장).
// 위치 우선순위: components 의 core.transform > position > Prefab 의 core.transform (P4).
struct CreateEntity {
    Vec2 position{};
    std::vector<ComponentValue> components;
    std::string prefab = {}; // 콘텐츠 Prefab id. 비우면 Prefab 없이
};
struct DeleteEntity {
    std::vector<NetEntityId> targets;
};
struct MoveEntity {
    std::vector<NetEntityId> targets;
    Vec2 value{};
    bool absolute = false; // false = value 만큼 이동, true = value 위치로
};
struct AddComponent {
    NetEntityId target = kInvalidNetEntityId;
    ComponentValue component;
};
struct RemoveComponent {
    NetEntityId target = kInvalidNetEntityId;
    ecs::StableId stableId = 0;
};
// patch 의 키만 바꾼다 (나머지 필드 유지). 모르는 키·타입 오류·범위 밖이면 거절되고 아무것도 바뀌지 않는다.
struct ChangeComponent {
    NetEntityId target = kInvalidNetEntityId;
    ecs::StableId stableId = 0;
    ecs::Json patch = ecs::Json::object();
};

// 지형 칠하기 (05-WORLD 3.3). cells 가 비어 있지 않으면 그 타일들(전부 경계 안이어야 한다),
// 비어 있으면 center 중심 브러시(경계 밖 부분은 잘린다). 타일 수 상한 4096.
enum class BrushShape : u8 {
    Square = 0, // |dx| ≤ r, |dy| ≤ r
    Circle = 1, // dx² + dy² ≤ r²
};
struct PaintTerrain {
    std::string materialId; // 콘텐츠 id ("core.water"). 인덱스가 아니다 — 인덱스는 프로세스마다 다를 수 있다
    std::vector<Vec2i> cells;
    Vec2i center{};
    BrushShape shape = BrushShape::Circle;
    u32 radius = 0; // 0 = center 한 칸, 최대 31
};

// --- 실행 제어 ---------------------------------------------------------------
struct PauseSimulation {};
struct ResumeSimulation {};
struct StepSimulation {
    u32 ticks = 1; // 일시정지 상태에서 진행할 틱 수
};
struct SetSimulationSpeed {
    f32 speed = 1.0f; // 0.25 ~ 8. 틱 간격(페이싱)만 바꾼다 — dt 는 영원히 상수
};

using CommandPayload =
    std::variant<CreateEntity, DeleteEntity, MoveEntity, AddComponent, RemoveComponent, ChangeComponent, PaintTerrain,
                 PauseSimulation, ResumeSimulation, StepSimulation, SetSimulationSpeed>;

struct SimCommand {
    CommandHeader header;
    CommandPayload payload;
};

[[nodiscard]] std::string_view commandName(const CommandPayload& payload) noexcept;

struct CommandResult {
    ClientId issuer = kServerIssuer;
    u32 sequence = 0;
    sim::Tick appliedTick = 0;
    bool accepted = false;
    Error error{};                    // 거절 사유
    std::vector<NetEntityId> created; // CreateEntity 가 만든 엔티티
};

} // namespace sbx::cmd
