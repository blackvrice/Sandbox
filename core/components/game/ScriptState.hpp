#pragma once
// script.state — 월드 상태 개체의 표시 (v0.10 RT-06-10 조각 2 — docs/22-RUNTIME-V2.md 9장, ADR-0139). 월드에 하나.
//
// 콘텐츠에 defineState 가 있으면 엔진이 단계 16b 앞에서 이 개체를 만들고 (없을 때만) 상태 컴포넌트
// (ComponentFlags::WorldState) 를 기본값으로 붙인다 — 스크립트는 ctx.state · ctx.setState 로 읽고 쓴다. 세이브에서
// 왔으면 그 개체를 그대로 쓰고, 팩이 새로 정의한 상태만 더 붙인다.
//
//   revision  이 표시의 판 (지금 1 — 모양이 바뀌면 올린다)
//
// 엔진이 관리한다 — 사용자 명령 · Prefab · ctx.add 로 붙일 수 없다. Persistent (세이브 · WorldHash · 리플레이) ·
// ServerOnly (표시는 복제하지 않는다). defineState 가 없는 콘텐츠는 개체가 없어 WorldHash · 골든에 닿지 않는다.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"

namespace sbx::comp {

struct ScriptState {
    u32 revision = 1;
};

template <class V>
void reflect(V& v, ScriptState& c) {
    v.field("revision", c.revision);
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::ScriptState, "script.state", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::ServerOnly |
                  sbx::ecs::ComponentFlags::EditorVisible);
