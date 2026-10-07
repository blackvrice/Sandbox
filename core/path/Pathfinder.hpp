#pragma once
// 격자 A*. docs/03-SIMULATION.md 7.3.
//
//   8방향, 코너 컷 금지(대각선은 양옆 직교 타일이 모두 열려 있어야), 옥타일 휴리스틱 × 최소 비용.
//   간선 비용: 직교 = 도착 타일 moveCost × 10, 대각 = × 14 (정수 — 동점 처리가 부동소수에 흔들리지 않게).
//   open set 은 (f, h, 삽입 순번) 전순서 → 결과가 힙 구현·스레드와 무관하다.
//   확장 상한(maxExpansions)에 닿거나 목표에 닿을 수 없으면 h 가 가장 작은 타일까지의 부분 경로를 돌려준다.
//
// 결과 경유점: A* 타일 경로를 직선 검사(tileLineClear)로 줄인 타일 중심들 + 마지막에 목표 점(닿을 수 있으면).
// kMaxWaypoints 를 넘으면 앞쪽만 남기고 partial — 끝에 닿은 이동 쪽이 다시 요청한다.
//
// 스레드: 순수 함수. 작업 버퍼는 스레드마다 하나 (thread_local).

#include "core/components/ai/Ai.hpp"
#include "core/path/PathGrid.hpp"

namespace sbx::path {

struct PathQuery {
    Vec2 start{};
    Vec2 goal{};
    u32 maxExpansions = 4096;
};

struct PathResult {
    bool found = false;   // 목표 타일에 닿았다
    bool partial = false; // 목표까지 가지 못했거나 경유점이 잘렸다
    SmallVector<Vec2, comp::kMaxWaypoints> waypoints;
    u32 expanded = 0; // 진단용
};

[[nodiscard]] PathResult findPath(const PathGridSnapshot& grid, const PathQuery& query);

} // namespace sbx::path
