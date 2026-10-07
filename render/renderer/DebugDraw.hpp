#pragma once
// 월드 좌표 선 목록 — DebugPass(경로 · 감지 반경 · 청크 경계)와 SelectionPass(선택 외곽선 · 박스)가 같이 쓴다.
// docs/06-RENDERING.md 8.1 · 8.3, ADR-0022.
//
// 선 하나 = GPU 인스턴스 하나 (shaders/lines.hlsl): 두 끝점(월드) · 색 · 두께(화면 픽셀). 셰이더가 화면에서 두께만큼
// 사각형으로 넓히고 가장자리를 1 픽셀 부드럽게 한다 — 줌과 무관하게 같은 두께. 원 · 사각형 · 화살표는 선 여럿.
// 프레임마다 다시 채운다 (RenderWorld::reset). 순수 CPU — SandboxTests 가 본다.

#include <span>
#include <vector>

#include "foundation/math/Vec2.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::render {

struct LineDraw {
    Vec2 a{};
    Vec2 b{};
    u32 color = 0xFFFF'FFFFu; // RGBA8
    f32 width = 1.5f;         // 화면 픽셀
};

// GPU 인스턴스 (shaders/lines.hlsl 의 LineIn 과 같은 배치, 24 바이트)
struct LineInstanceGpu {
    f32 a[2];
    f32 b[2];
    u32 color;
    f32 width;
};
static_assert(sizeof(LineInstanceGpu) == 24);

class DebugDrawList {
public:
    void line(Vec2 a, Vec2 b, u32 color, f32 width = 1.5f) { m_lines.push_back({a, b, color, width}); }
    // 축 정렬 사각형 (min · max 순서 무관)
    void rect(Vec2 a, Vec2 b, u32 color, f32 width = 1.5f);
    // 회전한 사각형 (가운데 · 크기 · 라디안)
    void box(Vec2 center, Vec2 size, f32 rotation, u32 color, f32 width = 1.5f);
    // 원: 선분 segments 개 (3 이상)
    void circle(Vec2 center, f32 radius, u32 color, f32 width = 1.5f, u32 segments = 32);
    // a → b 화살표. 머리 길이는 월드 단위
    void arrow(Vec2 a, Vec2 b, u32 color, f32 head, f32 width = 1.5f);
    // 이어진 선 (점 n 개 → 선 n-1 개)
    void polyline(std::span<const Vec2> points, u32 color, f32 width = 1.5f);

    void clear() noexcept { m_lines.clear(); }
    [[nodiscard]] bool empty() const noexcept { return m_lines.empty(); }
    [[nodiscard]] std::span<const LineDraw> lines() const noexcept { return m_lines; }

private:
    std::vector<LineDraw> m_lines;
};

} // namespace sbx::render
