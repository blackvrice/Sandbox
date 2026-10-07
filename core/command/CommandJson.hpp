#pragma once
// SimCommand 페이로드 ↔ JSON. 리플레이(docs/09-SERIALIZATION.md 4장)가 쓴다. 와이어 포맷(비트스트림)은 Phase 9.
//
// 엔티티 참조(NetEntityId)는 그대로 쓰지 않고 호출자가 준 함수로 바꾼다 — 리플레이는 saveId 로 기록한다.
// NetEntityId 는 세션 값이라 세이브를 로드한 월드에서 달라지기 때문이다 (03 0장: 로드 시 netId 재부여).
//
// 형식 (op 으로 구분)
//   {"op":"create", "position":[x,y], "prefab":"", "components":[{"id":"0x<stableId>","value":{…}}]}
//   {"op":"delete", "targets":[ref…]}            {"op":"move", "targets":[ref…], "value":[x,y], "absolute":b}
//   {"op":"add", "target":ref, "component":{"id","value"}}   {"op":"remove", "target":ref, "id":"0x…"}
//   {"op":"change", "target":ref, "id":"0x…", "patch":{…}}
//   {"op":"paint", "material":"", "cells":[[x,y]…], "center":[x,y], "shape":0|1, "radius":r}
//   {"op":"pause"} {"op":"resume"} {"op":"step","ticks":n} {"op":"speed","speed":f}
// f32 는 JSON 수(double)로 왕복해도 같은 값이다.

#include <functional>

#include "core/command/SimCommand.hpp"

namespace sbx::cmd {

using RefWriter = std::function<u64(NetEntityId)>;
using RefReader = std::function<NetEntityId(u64)>;

[[nodiscard]] ecs::Json payloadToJson(const CommandPayload& payload, const RefWriter& ref);
[[nodiscard]] Expected<CommandPayload> payloadFromJson(const ecs::Json& j, const RefReader& ref);

} // namespace sbx::cmd
