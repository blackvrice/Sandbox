#pragma once
// 되돌리기 기록 (Phase 12B, docs/10-EDITOR.md 6.2, ADR-0029).
//
//   UndoEntry   한 번의 편집 (배치 한 번 · 붓질 한 번 · 이동 한 번 · Inspector 값 하나 …) = 보낸 명령들 + 역명령들.
//               서버가 받아들인 명령만 담는다 (거절된 것은 기록하지 않는다). 역명령은 보내기 전의 값으로 만든다:
//                 MoveEntity        → MoveEntity{같은 대상, -delta}
//                 CreateEntity      → DeleteEntity{만들어진 netId} (결과가 와야 안다)
//                 DeleteEntity      → 개체마다 CreateEntity{prefab, 알던 컴포넌트 값} — 새 netId 가 생긴다
//                 ChangeComponent   → ChangeComponent{같은 키, 이전 값}
//                 Add/RemoveComponent → Remove/AddComponent{이전 값}
//                 PaintTerrain      → 칸의 이전 머티리얼마다 PaintTerrain
//   IdRemap     다시 만든 개체의 옛 netId → 새 netId (netId 는 재사용되지 않는다 — 기록 속 명령을 보낼 때 바꿔 끼운다).
//
// 역명령도 보통 명령이다 — 서버는 Undo 를 따로 다루지 않는다. 그 사이 다른 사람이 같은 값을 바꿨으면 덮는다
// (last-writer-wins, 10-EDITOR 6.2).

#include <map>
#include <span>
#include <string>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "editor/EditorHost.hpp"

namespace sbx::ecs {
struct ComponentInfo;
}

namespace sbx::editor {

struct UndoCmd {
    cmd::CommandPayload payload;
    // CreateEntity 가 다시 만드는 개체의 옛 netId (결과의 created 와 같은 순서) — 보낸 뒤 IdRemap 에 넣는다
    std::vector<NetEntityId> recreates;
};

struct UndoEntry {
    std::string label;
    std::vector<UndoCmd> forward; // 보낸 순서 (다시 하기)
    std::vector<UndoCmd> inverse; // 되돌릴 때 보낼 순서 (앞 명령의 역이 뒤에)
};

class IdRemap {
public:
    void set(NetEntityId from, NetEntityId to);
    [[nodiscard]] NetEntityId resolve(NetEntityId id) const;
    void apply(cmd::CommandPayload& payload) const; // 대상 netId 를 지금 것으로
    [[nodiscard]] usize size() const noexcept { return m_map.size(); }

private:
    std::map<NetEntityId, NetEntityId> m_map;
};

// JSON 에서 EntityRef 필드를 뺀다 (클라이언트의 EntityId 는 서버에서 뜻이 없다)
[[nodiscard]] ecs::Json withoutEntityRefs(const ecs::Json& value, const ecs::ComponentInfo& info);

// 지운 개체를 다시 만드는 명령: core.prefab 의 Prefab + 알던 컴포넌트 값 (core.prefab · EntityRef 필드 빼고).
// 위치는 core.transform 이 정한다
[[nodiscard]] UndoCmd recreateCommand(NetEntityId id, std::span<const ComponentState> components,
                                      const ecs::ComponentCatalog& catalog);

// 칠하기 전 머티리얼로 되돌리는 명령들 (이전 머티리얼마다 하나, 콘텐츠 순서)
[[nodiscard]] std::vector<UndoCmd> paintInverse(const cmd::PaintTerrain& paint, const world::WorldGrid& grid,
                                                const content::ContentDatabase& content);

} // namespace sbx::editor
