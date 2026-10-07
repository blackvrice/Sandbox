// 두께 있는 선 (SelectionPass · DebugPass). docs/06-RENDERING.md 8.3, ADR-0022.
//   선마다 인스턴스 하나 (24 바이트, render/renderer/DebugDraw.hpp LineInstanceGpu). 정점 버퍼 없음 — 꼭짓점 4개를
//   화면 픽셀 공간에서 선 방향 · 법선으로 넓혀 만든다 → 줌과 무관한 두께. 가장자리 1 px 은 알파로 부드럽게.
#include "common/Common.hlsli"

struct LineIn {
    float2 a : LINE_A;          // 월드
    float2 b : LINE_B;
    float4 color : LINE_COLOR;  // RGBA8 UNORM
    float width : LINE_WIDTH;   // 화면 픽셀
};

struct Push {
    float4 clip;      // ndc = world · clip.xy + clip.zw
    float2 viewport;  // 픽셀
    float2 pad;
};
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space7);

struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float across : TEXCOORD0;    // 선 가운데에서 픽셀 거리 (부호 있음)
    float halfWidth : TEXCOORD1;
};

VSOut VSMain(LineIn i, uint vertexId : SV_VertexID) {
    const float2 halfVp = push.viewport * 0.5;
    // 픽셀 공간 (y 위 — NDC 와 같은 방향이라 법선 방향이 뒤집히지 않는다)
    const float2 pa = (i.a * push.clip.xy + push.clip.zw) * halfVp;
    const float2 pb = (i.b * push.clip.xy + push.clip.zw) * halfVp;
    const float2 d = pb - pa;
    const float len = length(d);
    const float2 dir = len > 1e-4 ? d / len : float2(1.0, 0.0);
    const float2 n = float2(-dir.y, dir.x);
    const float hw = max(i.width, 0.5) * 0.5;
    const float reach = hw + 1.0; // 부드러운 가장자리 몫

    // 띠 순서 (0,0) (1,0) (0,1) (1,1): x = 끝(a/b), y = 법선 쪽(-/+). 끝은 반 픽셀 늘려 이음매를 메운다
    const float2 corner = float2(vertexId & 1, vertexId >> 1);
    const float2 along = corner.x > 0.5 ? pb + dir * 0.5 : pa - dir * 0.5;
    const float side = (corner.y * 2.0 - 1.0) * reach;
    VSOut o;
    o.position = float4((along + n * side) / halfVp, 0.0, 1.0);
    o.color = i.color;
    o.across = side;
    o.halfWidth = hw;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    const float a = saturate(i.halfWidth + 0.5 - abs(i.across));
    return float4(i.color.rgb, i.color.a * a);
}
