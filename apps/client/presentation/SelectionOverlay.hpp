#pragma once
// 선택 (8B · 10B 부터 netId): 스냅숏에서 고르기 · 외곽선 · 선택한 개체의 디버그 선 · 제목 줄 설명.
// docs/06-RENDERING.md 8.3 (SelectionPass · DebugPass), ADR-0022. Main 스레드에서 스냅숏만 읽는다 (월드를 보지 않는다 —
// T1).
//
//   pickAt   점 아래에서 맨 위에 그려진 개체 (레이어 큰 것 → 같은 레이어면 depth 큰 것). 크기 사각형 안, 회전 무시
//   pickBox  가운데가 사각형 안에 든 개체 전부
//   외곽선   선택한 개체마다 크기보다 조금 큰 회전 사각형 (노랑)
//   디버그   감지 반경(하늘색 원) · 남은 경로(초록 선 + 목표 ×) · 대상(빨강 선) · 속도(흰 화살표)

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "apps/client/presentation/SpriteExtraction.hpp"
#include "render/renderer/DebugDraw.hpp"

namespace sbx::client {

[[nodiscard]] std::optional<NetEntityId> pickAt(const WorldSnapshot& s, Vec2 world);
// 결과는 netId 오름차순
[[nodiscard]] std::vector<NetEntityId> pickBox(const WorldSnapshot& s, render::WorldRect area);

// selection 은 오름차순. 스냅숏에 없는(죽은) id 는 건너뛴다. 그린 수를 돌려준다
usize drawSelectionOutlines(const WorldSnapshot& s, std::span<const NetEntityId> selection, render::DebugDrawList& out);
void drawSelectedDetails(const WorldSnapshot& s, render::DebugDrawList& out);

// 제목 줄 한 토막: 하나면 "선택 eco.rabbit #12 · flee · 에너지 54/100 · 체력 10/10 · 경로 5", 여럿이면 "선택 7"
[[nodiscard]] std::string describeSelection(const WorldSnapshot& s, std::span<const NetEntityId> selection);

// 정렬된 a 에 b 를 합친다 (중복 없이)
void mergeSelection(std::vector<NetEntityId>& a, std::span<const NetEntityId> b);

} // namespace sbx::client
