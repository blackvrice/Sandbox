// 지형 (TerrainPass). docs/06-RENDERING.md 8.3, ADR-0022.
//   월드 타일 격자 전체를 사각형 하나로 그리고, 픽셀마다 그 자리 타일의 머티리얼 번호(R16 UINT 텍스처)를 읽어
//   팔레트(1 행 RGBA8 텍스처)에서 색을 찾는다. 정점 버퍼 없음 (SV_VertexID 삼각형 띠 4개).
//   타일 텍스처: 열 = 월드 x - worldMin.x, 행 = 월드 y - worldMin.y (행 0 이 월드 아래쪽 — 화면에 직접 보이지 않으니
//   텍스처 원점 규약과 무관하게 이 셰이더와 TerrainPass 의 업로드만 같으면 된다).
#include "common/Common.hlsli"

struct Push {
    float4 clip;        // ndc = world · clip.xy + clip.zw
    float2 worldMin;    // 격자 왼쪽 아래 (월드 = 타일 좌표)
    float2 worldSize;   // 타일 수 (가로, 세로)
    float pixelsPerUnit;
    uint paletteSize;
    float2 pad;
};
[[vk::push_constant]] ConstantBuffer<Push> push : register(b0, space7);

Texture2D<uint> tiles : register(t0, space2);
Texture2D<float4> palette : register(t1, space2);

struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID) {
    // 띠 순서 (0,0) (1,0) (0,1) (1,1) — CCW
    const float2 corner = float2(vertexId & 1, vertexId >> 1);
    VSOut o;
    o.world = push.worldMin + corner * push.worldSize;
    o.position = float4(o.world * push.clip.xy + push.clip.zw, 0.0, 1.0);
    return o;
}

uint hashTile(int2 p) {
    uint h = (uint)p.x * 0x8da6b343u ^ (uint)p.y * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float4 PSMain(VSOut i) : SV_Target {
    const int2 size = int2(push.worldSize);
    const int2 tile = clamp(int2(floor(i.world - push.worldMin)), int2(0, 0), size - 1);
    const uint id = tiles.Load(int3(tile, 0));
    float4 c = id < push.paletteSize ? palette.Load(int3(id, 0, 0)) : float4(1.0, 0.0, 1.0, 1.0); // 없는 번호 = 마젠타
    // 타일 결: 타일마다 밝기 ±4 %. 확대했을 때만 (축소하면 픽셀보다 작은 타일이 반짝인다)
    const float grain = smoothstep(4.0, 12.0, push.pixelsPerUnit);
    const float v = ((float)(hashTile(tile) & 0xFFu) / 255.0 - 0.5) * 0.08 * grain;
    c.rgb *= 1.0 + v;
    return c;
}
