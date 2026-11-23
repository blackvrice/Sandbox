#include "render/graphics_sdk/GraphicsSdkShaders.hpp"

#include <cstring>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "render/graphics_sdk/GraphicsSdkSpirvData.hpp"
#include "render/graphics_sdk/GraphicsSdkTypes.hpp"

namespace sbx::rhi::graphics_sdk {
namespace {

constexpr std::string_view kBasicColorHlsl = R"(
float2 rotate2d(float2 p, float radians) {
    float s, c;
    sincos(radians, s, c);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}
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
cbuffer Push : register(b0, space7) {
    float2 pushOffset;
    float2 pushScale;
    float pushRotation;
    float3 pushPad;
};
VSOut VSMain(VSIn i) {
    VSOut o;
    const float2 p = rotate2d(i.position, pushRotation) * pushScale + pushOffset;
    o.position = float4(p, 0.0, 1.0);
    o.color = i.color;
    return o;
}
float4 PSMain(VSOut i) : SV_Target {
    return i.color * tint;
}
)";

constexpr std::string_view kBasicTextureHlsl = R"(
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
)";

constexpr std::string_view kSpriteHlsl = R"(
float2 rotate2d(float2 p, float radians) {
    float s, c;
    sincos(radians, s, c);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}
struct InstanceIn {
    float2 position : SPRITE_POS;
    float2 size : SPRITE_SIZE;
    float rotation : SPRITE_ROT;
    float page : SPRITE_PAGE;
    float4 uvRect : SPRITE_UV;
    float4 color : SPRITE_COLOR;
    float flags : SPRITE_FLAGS;
};
struct VSOut {
    float4 position : SV_Position;
    float3 uvw : TEXCOORD0;
    float4 color : COLOR0;
};
cbuffer Push : register(b0, space7) {
    float4 clip;
};
Texture2DArray atlas : register(t0, space2);
SamplerState atlasSampler : register(s1, space2);
VSOut VSMain(InstanceIn i, uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1, vertexId >> 1);
    const float2 local = (corner - 0.5) * i.size;
    const float2 world = i.position + rotate2d(local, i.rotation);
    VSOut o;
    o.position = float4(world * clip.xy + clip.zw, 0.0, 1.0);
    const uint pageIdx = asuint(i.page);
    o.uvw = float3(lerp(i.uvRect.x, i.uvRect.z, corner.x),
                   lerp(i.uvRect.w, i.uvRect.y, corner.y),
                   (float)pageIdx);
    o.color = i.color;
    return o;
}
float4 PSMain(VSOut i) : SV_Target {
    return atlas.Sample(atlasSampler, i.uvw) * i.color;
}
)";

constexpr std::string_view kTerrainHlsl = R"(
cbuffer Push : register(b0, space7) {
    float4 clip;
    float2 worldMin;
    float2 worldSize;
    float pixelsPerUnit;
    uint paletteSize;
    float2 pad;
};
Texture2D<float4> tiles : register(t0, space2);
Texture2D<float4> palette : register(t1, space2);
struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};
VSOut VSMain(uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1, vertexId >> 1);
    VSOut o;
    o.world = worldMin + corner * worldSize;
    o.position = float4(o.world * clip.xy + clip.zw, 0.0, 1.0);
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
    const int2 size = int2(worldSize);
    const int2 tile = clamp(int2(floor(i.world - worldMin)), int2(0, 0), size - 1);
    const uint id = (uint)(tiles.Load(int3(tile, 0)).x * 65535.0 + 0.5);
    float4 c = id < paletteSize ? palette.Load(int3((int)id, 0, 0)) : float4(1.0, 0.0, 1.0, 1.0);
    const float grain = smoothstep(4.0, 12.0, pixelsPerUnit);
    const float v = ((float)(hashTile(tile) & 0xFFu) / 255.0 - 0.5) * 0.08 * grain;
    c.rgb *= 1.0 + v;
    return c;
}
)";

constexpr std::string_view kGridHlsl = R"(
cbuffer Push : register(b0, space7) {
    float4 clip;
    float2 worldMin;
    float2 worldSize;
    float pixelsPerUnit;
    float chunkSize;
    float2 pad;
};
struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};
VSOut VSMain(uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1, vertexId >> 1);
    VSOut o;
    o.world = worldMin + corner * worldSize;
    o.position = float4(o.world * clip.xy + clip.zw, 0.0, 1.0);
    return o;
}
float coverage(float2 rel, float spacing, float widthPx) {
    const float2 d = abs(frac(rel / spacing + 0.5) - 0.5) * spacing * pixelsPerUnit;
    return saturate(widthPx * 0.5 + 0.5 - min(d.x, d.y));
}
float4 PSMain(VSOut i) : SV_Target {
    const float2 rel = i.world - worldMin;
    const float tileA = coverage(rel, 1.0, 1.0) * smoothstep(6.0, 16.0, pixelsPerUnit) * 0.16;
    const float chunkA = coverage(rel, chunkSize, 1.25) * 0.40;
    const float2 edge = min(rel, worldSize - rel) * pixelsPerUnit;
    const float borderA = saturate(1.5 + 0.5 - min(edge.x, edge.y)) * 0.85;
    if (borderA > max(tileA, chunkA)) {
        return float4(1.0, 0.85, 0.35, borderA);
    }
    return float4(0.92, 0.96, 1.0, max(tileA, chunkA));
}
)";

constexpr std::string_view kLinesHlsl = R"(
struct LineIn {
    float2 a : LINE_A;
    float2 b : LINE_B;
    float4 color : LINE_COLOR;
    float width : LINE_WIDTH;
};
cbuffer Push : register(b0, space7) {
    float4 clip;
    float2 viewport;
    float2 pad;
};
struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float across : TEXCOORD0;
    float halfWidth : TEXCOORD1;
};
VSOut VSMain(LineIn i, uint vertexId : SV_VertexID) {
    const float2 halfVp = viewport * 0.5;
    const float2 pa = (i.a * clip.xy + clip.zw) * halfVp;
    const float2 pb = (i.b * clip.xy + clip.zw) * halfVp;
    const float2 d = pb - pa;
    const float len = length(d);
    const float2 dir = len > 1e-4 ? d / len : float2(1.0, 0.0);
    const float2 n = float2(-dir.y, dir.x);
    const float hw = max(i.width, 0.5) * 0.5;
    const float reach = hw + 1.0;
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
)";

constexpr std::string_view kImguiHlsl = R"(
struct VSIn {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
cbuffer Push : register(b0, space7) {
    float4 xform;
};
Texture2D tex : register(t0, space2);
SamplerState texSampler : register(s1, space2);
struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
VSOut VSMain(VSIn i) {
    VSOut o;
    o.position = float4(i.pos * xform.xy + xform.zw, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
float4 PSMain(VSOut i) : SV_Target {
    return i.col * tex.Sample(texSampler, i.uv);
}
)";

struct BuiltinHlslEntry {
    std::string_view name;
    std::string_view hlsl;
    std::string_view entry;
    gfx::ShaderStage stage;
};

constexpr BuiltinHlslEntry kBuiltinHlsl[] = {
    {"basic_color.vs", kBasicColorHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"basic_color.ps", kBasicColorHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"basic_texture.vs", kBasicTextureHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"basic_texture.ps", kBasicTextureHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"sprite.vs", kSpriteHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"sprite.ps", kSpriteHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"terrain.vs", kTerrainHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"terrain.ps", kTerrainHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"grid.vs", kGridHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"grid.ps", kGridHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"lines.vs", kLinesHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"lines.ps", kLinesHlsl, "PSMain", gfx::ShaderStage::Fragment},
    {"imgui.vs", kImguiHlsl, "VSMain", gfx::ShaderStage::Vertex},
    {"imgui.ps", kImguiHlsl, "PSMain", gfx::ShaderStage::Fragment},
};

template <std::size_t N>
gfx::ShaderBinary makeSpirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    const auto raw = std::as_bytes(std::span{words});
    return {stage, std::vector<std::byte>(raw.begin(), raw.end()), gfx::ShaderFormat::SPIRV};
}

Expected<gfx::ShaderBinary> resolveD3D12Shader(const GraphicsSdkShader& shader) {
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, gfx::ShaderBinary> dxbcCache;

    {
        std::scoped_lock lock(cacheMutex);
        if (const auto it = dxbcCache.find(shader.name); it != dxbcCache.end()) {
            return it->second;
        }
    }

    for (const BuiltinHlslEntry& item : kBuiltinHlsl) {
        if (item.name == shader.name) {
            auto compiled = gfx::compileHlsl(item.hlsl, item.entry, item.stage);
            if (!compiled) {
                return std::unexpected(mapError(compiled.error(), std::format("gfx::compileHlsl({})", shader.name)));
            }
            std::scoped_lock lock(cacheMutex);
            dxbcCache.emplace(shader.name, *compiled);
            return *compiled;
        }
    }

    // 내장 셰이더가 아닌 경우 원본 DXIL 바이트코드를 그대로 전달한다.
    const gfx::ShaderStage stage =
        shader.stage == ShaderStage::Vertex ? gfx::ShaderStage::Vertex : gfx::ShaderStage::Fragment;
    return gfx::ShaderBinary{stage, shader.dxil, gfx::ShaderFormat::DXIL};
}

Expected<gfx::ShaderBinary> resolveVulkanShader(const GraphicsSdkShader& shader) {
    using namespace spirv_data;
    if (shader.name == "basic_color.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kBasicColorVs);
    }
    if (shader.name == "basic_color.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kBasicColorPs);
    }
    if (shader.name == "basic_texture.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kBasicTextureVs);
    }
    if (shader.name == "basic_texture.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kBasicTexturePs);
    }
    if (shader.name == "sprite.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kSpriteVs);
    }
    if (shader.name == "sprite.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kSpritePs);
    }
    if (shader.name == "terrain.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kTerrainVs);
    }
    if (shader.name == "terrain.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kTerrainPs);
    }
    if (shader.name == "grid.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kGridVs);
    }
    if (shader.name == "grid.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kGridPs);
    }
    if (shader.name == "lines.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kLinesVs);
    }
    if (shader.name == "lines.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kLinesPs);
    }
    if (shader.name == "imgui.vs") {
        return makeSpirvBinary(gfx::ShaderStage::Vertex, kImguiVs);
    }
    if (shader.name == "imgui.ps") {
        return makeSpirvBinary(gfx::ShaderStage::Fragment, kImguiPs);
    }

    const gfx::ShaderStage stage =
        shader.stage == ShaderStage::Vertex ? gfx::ShaderStage::Vertex : gfx::ShaderStage::Fragment;
    return gfx::ShaderBinary{stage, shader.spirv, gfx::ShaderFormat::SPIRV};
}

} // namespace

Expected<gfx::ShaderBinary> resolveSdkShader(BackendType backend, const GraphicsSdkShader& shader) {
    if (backend == BackendType::D3D12) {
        return resolveD3D12Shader(shader);
    }
    if (backend == BackendType::Vulkan) {
        return resolveVulkanShader(shader);
    }
    const gfx::ShaderStage stage =
        shader.stage == ShaderStage::Vertex ? gfx::ShaderStage::Vertex : gfx::ShaderStage::Fragment;
    std::vector<std::byte> bytes(shader.msl.size());
    if (!shader.msl.empty()) {
        std::memcpy(bytes.data(), shader.msl.data(), shader.msl.size());
    }
    return gfx::ShaderBinary{stage, std::move(bytes), gfx::ShaderFormat::MSL};
}

} // namespace sbx::rhi::graphics_sdk
