// 인스턴스 스프라이트. docs/06-RENDERING.md 8.1 · 8.3 (WorldSpritePass), ADR-0020.
//   정점 버퍼 없음 — 사각형 꼭짓점은 SV_VertexID (삼각형 띠 4개), 스프라이트마다 인스턴스 정점 하나 (48 바이트,
//   render/renderer/SpriteBatcher.hpp SpriteInstanceGpu 와 같은 배치).
//   카메라는 push constant (월드 → NDC 의 scale · offset). 아틀라스는 Texture2DArray (그룹 2).
#include "common/Common.hlsli"

struct InstanceIn {
    float2 position : SPRITE_POS;  // 월드, 가운데
    float2 size : SPRITE_SIZE;     // 월드
    float rotation : SPRITE_ROT;   // 라디안, 반시계
    uint page : SPRITE_PAGE;       // 아틀라스 층
    float4 uvRect : SPRITE_UV;     // (u0, v0, u1, v1), v0 = 위
    float4 color : SPRITE_COLOR;   // RGBA8 UNORM
    uint flags : SPRITE_FLAGS;     // [계획] 선택 강조 등 — 지금은 읽지 않는다 (뒤집기는 CPU 가 uv 로)
};

struct VSOut {
    float4 position : SV_Position;
    float3 uvw : TEXCOORD0;
    float4 color : COLOR0;
};

struct Push {
    float4 clip; // xy = scale, zw = offset  (ndc = world · scale + offset)
};
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space7);

Texture2DArray atlas : register(t0, space2);
SamplerState atlasSampler : register(s1, space2);

VSOut VSMain(InstanceIn i, uint vertexId : SV_VertexID) {
    // 띠 순서 (0,0) (1,0) (0,1) (1,1) — y 위 기준 반시계 (CCW = 앞면, 06 12장)
    const float2 corner = float2(vertexId & 1, vertexId >> 1);
    const float2 local = (corner - 0.5) * i.size;
    const float2 world = i.position + rotate2d(local, i.rotation);
    VSOut o;
    o.position = float4(world * push.clip.xy + push.clip.zw, 0.0, 1.0);
    // 텍스처 원점은 좌상단: 위(corner.y = 1) → v0
    o.uvw = float3(lerp(i.uvRect.x, i.uvRect.z, corner.x), lerp(i.uvRect.w, i.uvRect.y, corner.y), (float)i.page);
    o.color = i.color;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    return atlas.Sample(atlasSampler, i.uvw) * i.color;
}
