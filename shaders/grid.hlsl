// 격자 (GridPass). docs/06-RENDERING.md 8.3, ADR-0022.
//   월드 격자 위에 사각형 하나를 알파로 덮고, 픽셀마다 가장 가까운 격자선까지의 화면 거리로 선을 그린다
//   (선마다 정점이 없다 — 줌과 무관하게 1 픽셀 선, 가장자리 부드럽게).
//   타일 선: 확대(타일이 6 px 이상)할수록 진해진다. 청크 선 · 월드 경계: 늘 보인다.
#include "common/Common.hlsli"

struct Push {
    float4 clip;        // ndc = world · clip.xy + clip.zw
    float2 worldMin;    // 격자 왼쪽 아래 (월드)
    float2 worldSize;
    float pixelsPerUnit;
    float chunkSize;    // 타일
    float2 pad;
};
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space7);

struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1, vertexId >> 1);
    VSOut o;
    o.world = push.worldMin + corner * push.worldSize;
    o.position = float4(o.world * push.clip.xy + push.clip.zw, 0.0, 1.0);
    return o;
}

// spacing 간격 격자선까지의 화면 거리(px)로 만든 덮임 (선 두께 widthPx, 가장자리 1 px)
float coverage(float2 rel, float spacing, float widthPx) {
    const float2 d = abs(frac(rel / spacing + 0.5) - 0.5) * spacing * push.pixelsPerUnit;
    return saturate(widthPx * 0.5 + 0.5 - min(d.x, d.y));
}

float4 PSMain(VSOut i) : SV_Target {
    const float2 rel = i.world - push.worldMin;
    const float tileA = coverage(rel, 1.0, 1.0) * smoothstep(6.0, 16.0, push.pixelsPerUnit) * 0.16;
    const float chunkA = coverage(rel, push.chunkSize, 1.25) * 0.40;
    // 월드 경계: 가장자리까지 화면 거리
    const float2 edge = min(rel, push.worldSize - rel) * push.pixelsPerUnit;
    const float borderA = saturate(1.5 + 0.5 - min(edge.x, edge.y)) * 0.85;
    if (borderA > max(tileA, chunkA)) {
        return float4(1.0, 0.85, 0.35, borderA);
    }
    return float4(0.92, 0.96, 1.0, max(tileA, chunkA));
}
