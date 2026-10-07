// 텍스처를 붙인 사각형. sbx_render_tests(texture)와 SandboxClient 데모가 쓴다.
#include "common/Common.hlsli"

struct VSIn {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D colorTexture : register(t0, space2);
SamplerState colorSampler : register(s1, space2);

VSOut VSMain(VSIn i) {
    VSOut o;
    o.position = float4(i.position, 0.0, 1.0);
    o.uv = i.uv;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    return colorTexture.Sample(colorSampler, i.uv);
}
