#pragma once
// game.camera_start — 클라이언트 카메라의 처음 자리 (v0.2 M3-T3, ADR-0049). 에디터로 월드에 놓는다 (core.transform 이
// 가운데). 클라이언트는 월드를 받을 때마다 (처음 · 불러오기 · 재시작 — worldGeneration) 이 개체가 보이면 카메라를 그
// 위치 · 높이로 맞추고, Home (camera.reset) 도 여기로 간다 (없으면 월드 전체에 맞춘다).
// 시뮬레이션은 읽지 않는다 — 카메라는 클라이언트 로컬이다 (10 E3). 월드에 둘 이상이면 netId 가 가장 작은 것.
//
//   height   화면 세로에 보이는 월드 단위 (2 ~ 512) — 창 크기 · DPI 와 무관한 줌

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::comp {

struct CameraStart {
    f32 height = 24.f;
};

template <class V>
void reflect(V& v, CameraStart& c) {
    v.field("height", c.height, ecs::Hint::None, ecs::FieldMeta::range(2, 512));
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::CameraStart, "game.camera_start", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::Replicated |
                  sbx::ecs::ComponentFlags::EditorVisible);
