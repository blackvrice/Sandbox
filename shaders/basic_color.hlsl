// 정점 색 + 상수 버퍼 tint + push constant 변환. sbx_render_tests(triangle · coord_convention · culling)와
// SandboxClient 의 7B 데모가 쓴다.
#include "common/Common.hlsli"

struct VSIn {
    float2 position : POSITION;
    float4 color : COLOR0;
};

struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

cbuffer Frame : register(b0, space0) {
    float4 tint;
};

struct Push {
    float2 offset;   // NDC
    float2 scale;    // NDC (가로세로비 보정은 호출자가)
    float rotation;  // 라디안, CCW
    float3 pad;
};
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space7);

VSOut VSMain(VSIn i) {
    VSOut o;
    const float2 p = rotate2d(i.position, push.rotation) * push.scale + push.offset;
    o.position = float4(p, 0.0, 1.0);
    o.color = i.color;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    return i.color * tint;
}
