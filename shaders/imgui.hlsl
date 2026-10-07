// Dear ImGui (UIPass). docs/06-RENDERING.md 10장, ADR-0008 · ADR-0023.
//   정점 = ImDrawVert (pos float2 · uv float2 · col RGBA8, 20 바이트), 인덱스 uint16. 좌표는 ImGui 논리 픽셀(왼쪽 위 원점,
//   y 아래) → push constant 의 scale · offset 으로 NDC. 텍스처 하나 (ImTextureData 마다 바인드 그룹 하나).
#include "common/Common.hlsli"

struct VSIn {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0; // RGBA8 UNORM
};

struct Push {
    float4 xform; // ndc = pos · xform.xy + xform.zw
};
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space7);

Texture2D tex : register(t0, space2);
SamplerState texSampler : register(s1, space2);

struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    o.position = float4(i.pos * push.xform.xy + push.xform.zw, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    return i.col * tex.Sample(texSampler, i.uv);
}
