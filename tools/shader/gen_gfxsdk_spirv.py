#!/usr/bin/env python3
"""Graphics SDK Vulkan 백엔드(Spirv.hpp 프로파일: SPIR-V 1.3, main, set 0, no GLSL.std.450)용
Sandbox 내장 셰이더 7종(14개 스테이지) SPIR-V 바이트코드 헤더 생성기.

사용법:
    python tools/shader/gen_gfxsdk_spirv.py --dxc <dxc.exe> --out render/graphics_sdk/GraphicsSdkSpirvData.hpp
    python tools/shader/gen_gfxsdk_spirv.py --stamp-sources --out render/graphics_sdk/GraphicsSdkSpirvData.hpp

원본 = shaders/*.hlsl (ADR-0171). 아래 SHADERS 는 그 원본을 SDK 의 SPIR-V 계약 (set 0 하나 · main · GLSL.std.450 없음 —
SDK-REQ-001) 에 맞게 손으로 옮긴 변형이다. 생성할 때 원본 (include 를 푼 글 · 줄 끝 LF) 의 SHA-256 을 헤더에 적고,
어댑터 시험 (verifyVulkanShaderSources) 이 cmake/SbxEmbedHlsl.cmake 가 같은 방식으로 계산한 지금 값과 견준다 — 원본을
고치면 여기 변형도 고치고 다시 생성해야 시험이 통과한다. --stamp-sources 는 SPIR-V 를 그대로 두고 기록만 새로 쓴다
(변형이 이미 원본과 같은 뜻임을 확인했을 때만).
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import struct
import subprocess
import tempfile

# GLSL.std.450 확장 명령(OpExtInstImport / OpExtInst)을 일절 쓰지 않는 순수 코어 SPIR-V 보조 함수
COMMON_HLSL = r"""
float2 poly_sincos(float a) {
    const float kInvTwoPi = 0.15915494309;
    const float kTwoPi = 6.28318530718;
    float q = a * kInvTwoPi;
    int qi = (int)(q >= 0.0 ? (q + 0.5) : (q - 0.5));
    float x = a - (float)qi * kTwoPi;
    const float kPi = 3.14159265359;
    const float kHalfPi = 1.57079632679;
    float signCos = 1.0;
    if (x > kHalfPi) { x = kPi - x; signCos = -1.0; }
    else if (x < -kHalfPi) { x = -kPi - x; signCos = -1.0; }
    float x2 = x * x;
    float s = x * (1.0 - x2 * (0.16666666667 - x2 * (0.00833333333 - x2 * 0.0001984127)));
    float c = (1.0 - x2 * (0.5 - x2 * (0.04166666667 - x2 * 0.00138888889))) * signCos;
    return float2(s, c);
}
float2 rotate2d(float2 p, float radians) {
    float2 sc = poly_sincos(radians);
    return float2(p.x * sc.y - p.y * sc.x, p.x * sc.x + p.y * sc.y);
}
float my_abs(float x) { return x < 0.0 ? -x : x; }
float2 my_abs2(float2 v) { return float2(my_abs(v.x), my_abs(v.y)); }
float my_min(float a, float b) { return a < b ? a : b; }
float2 my_min2(float2 a, float2 b) { return float2(my_min(a.x, b.x), my_min(a.y, b.y)); }
float my_max(float a, float b) { return a > b ? a : b; }
float my_clamp(float x, float lo, float hi) { return my_min(my_max(x, lo), hi); }
float my_saturate(float x) { return my_clamp(x, 0.0, 1.0); }
int my_clamp_i(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
int2 my_clamp_i2(int2 x, int2 lo, int2 hi) { return int2(my_clamp_i(x.x, lo.x, hi.x), my_clamp_i(x.y, lo.y, hi.y)); }
int my_floor_to_int(float x) {
    int i = (int)x;
    return (float)i > x ? (i - 1) : i;
}
int2 my_floor_to_int2(float2 v) { return int2(my_floor_to_int(v.x), my_floor_to_int(v.y)); }
float my_frac(float x) { return x - (float)my_floor_to_int(x); }
float2 my_frac2(float2 v) { return float2(my_frac(v.x), my_frac(v.y)); }
float my_smoothstep(float a, float b, float x) {
    float t = my_saturate((x - a) / (b - a));
    return t * t * (3.0 - 2.0 * t);
}
float my_lerp(float a, float b, float t) { return a + (b - a) * t; }
float my_sqrt(float x) {
    if (x <= 1e-12) return 0.0;
    uint u = asuint(x);
    u = 0x1fbd1df5u + (u >> 1);
    float y = asfloat(u);
    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);
    return y;
}
"""

# 각 셰이더의 리소스 바인딩 번호는 GraphicsSdkDevice::createGraphicsPipeline 이 생성하는
# gfx::BindingLayout::entries 인덱스(0, 1, 2...) 및 samplers 인덱스(32 + j)와 1:1 일치한다.
SHADERS: list[tuple[str, str, str]] = [
    (
        "kBasicColorVs",
        "vs",
        COMMON_HLSL
        + r"""
struct VSIn {
    float2 position : POSITION;
    float4 color : COLOR0;
};
struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};
cbuffer Push : register(b1, space0) {
    float4 p0; // xy = offset, zw = scale
    float4 p1; // x = rotation
};
VSOut main(VSIn i) {
    VSOut o;
    const float2 p = rotate2d(i.position, p1.x) * p0.zw + p0.xy;
    o.position = float4(p, 0.0, 1.0);
    o.color = i.color;
    return o;
}
""",
    ),
    (
        "kBasicColorPs",
        "ps",
        r"""
struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};
cbuffer Frame : register(b0, space0) {
    float4 tint;
};
float4 main(VSOut i) : SV_Target {
    return i.color * tint;
}
""",
    ),
    (
        "kBasicTextureVs",
        "vs",
        r"""
struct VSIn {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};
struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
VSOut main(VSIn i) {
    VSOut o;
    o.position = float4(i.position, 0.0, 1.0);
    o.uv = i.uv;
    return o;
}
""",
    ),
    (
        "kBasicTexturePs",
        "ps",
        r"""
struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Texture2D colorTexture : register(t0, space0);
SamplerState colorSampler : register(s32, space0);
float4 main(VSOut i) : SV_Target {
    return colorTexture.Sample(colorSampler, i.uv);
}
""",
    ),
    (
        "kSpriteVs",
        "vs",
        COMMON_HLSL
        + r"""
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
cbuffer Push : register(b1, space0) {
    float4 clip;
};
VSOut main(InstanceIn i, uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1u, vertexId >> 1u);
    const float2 local = (corner - 0.5) * i.size;
    const float2 world = i.position + rotate2d(local, i.rotation);
    // flags 속성이 최적화로 제거되지 않도록 0이 아닌 극단값에서만 z에 반영(실제 flags 는 0)
    const float zBias = (asuint(i.flags) == 0xFFFFFFFFu) ? 0.0001 : 0.0;
    VSOut o;
    o.position = float4(world * clip.xy + clip.zw, zBias, 1.0);
    const uint pageIdx = asuint(i.page);
    o.uvw = float3(my_lerp(i.uvRect.x, i.uvRect.z, corner.x),
                   my_lerp(i.uvRect.w, i.uvRect.y, corner.y),
                   (float)pageIdx);
    o.color = i.color;
    return o;
}
""",
    ),
    (
        "kSpritePs",
        "ps",
        r"""
struct VSOut {
    float4 position : SV_Position;
    float3 uvw : TEXCOORD0;
    float4 color : COLOR0;
};
Texture2DArray atlas : register(t0, space0);
SamplerState atlasSampler : register(s32, space0);
float4 main(VSOut i) : SV_Target {
    return atlas.Sample(atlasSampler, i.uvw) * i.color;
}
""",
    ),
    (
        "kTerrainVs",
        "vs",
        r"""
cbuffer Push : register(b2, space0) {
    float4 clip;
    float4 worldMinSize; // xy = worldMin, zw = worldSize
    float4 params;       // x = pixelsPerUnit, y = asfloat(paletteSize)
};
struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};
VSOut main(uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1u, vertexId >> 1u);
    VSOut o;
    o.world = worldMinSize.xy + corner * worldMinSize.zw;
    o.position = float4(o.world * clip.xy + clip.zw, 0.0, 1.0);
    return o;
}
""",
    ),
    (
        "kTerrainPs",
        "ps",
        COMMON_HLSL
        + r"""
cbuffer Push : register(b2, space0) {
    float4 clip;
    float4 worldMinSize;
    float4 params;
};
Texture2D<float4> tiles : register(t0, space0);
Texture2D<float4> palette : register(t1, space0);
struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};
uint hashTile(int2 p) {
    uint h = (uint)p.x * 0x8da6b343u ^ (uint)p.y * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
float4 main(VSOut i) : SV_Target {
    const int2 size = int2(worldMinSize.zw);
    const int2 tile = my_clamp_i2(my_floor_to_int2(i.world - worldMinSize.xy), int2(0, 0), size - int2(1, 1));
    const float rawId = tiles.Load(int3(tile, 0)).x;
    const uint id = (uint)(rawId * 65535.0 + 0.5);
    const uint palSize = asuint(params.y);
    float4 c = id < palSize ? palette.Load(int3((int)id, 0, 0)) : float4(1.0, 0.0, 1.0, 1.0);
    const float grain = my_smoothstep(4.0, 12.0, params.x);
    const float v = ((float)(hashTile(tile) & 0xFFu) / 255.0 - 0.5) * 0.08 * grain;
    c.rgb *= 1.0 + v;
    return c;
}
""",
    ),
    (
        "kGridVs",
        "vs",
        r"""
cbuffer Push : register(b0, space0) {
    float4 clip;
    float4 worldMinSize;
    float4 params;
};
struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};
VSOut main(uint vertexId : SV_VertexID) {
    const float2 corner = float2(vertexId & 1u, vertexId >> 1u);
    VSOut o;
    o.world = worldMinSize.xy + corner * worldMinSize.zw;
    o.position = float4(o.world * clip.xy + clip.zw, 0.0, 1.0);
    return o;
}
""",
    ),
    (
        "kGridPs",
        "ps",
        COMMON_HLSL
        + r"""
cbuffer Push : register(b0, space0) {
    float4 clip;
    float4 worldMinSize;
    float4 params;
};
struct VSOut {
    float4 position : SV_Position;
    float2 world : TEXCOORD0;
};
float coverage(float2 rel, float spacing, float widthPx, float ppu) {
    const float2 d = my_abs2(my_frac2(rel / spacing + float2(0.5, 0.5)) - float2(0.5, 0.5)) * (spacing * ppu);
    return my_saturate(widthPx * 0.5 + 0.5 - my_min(d.x, d.y));
}
float4 main(VSOut i) : SV_Target {
    const float2 rel = i.world - worldMinSize.xy;
    const float ppu = params.x;
    const float tileA = coverage(rel, 1.0, 1.0, ppu) * my_smoothstep(6.0, 16.0, ppu) * 0.16;
    const float chunkA = coverage(rel, params.y, 1.25, ppu) * 0.40;
    const float2 edge = my_min2(rel, worldMinSize.zw - rel) * ppu;
    const float borderA = my_saturate(1.5 + 0.5 - my_min(edge.x, edge.y)) * 0.85;
    const float maxGrid = my_max(tileA, chunkA);
    if (borderA > maxGrid) {
        return float4(1.0, 0.85, 0.35, borderA);
    }
    return float4(0.92, 0.96, 1.0, maxGrid);
}
""",
    ),
    (
        "kLinesVs",
        "vs",
        COMMON_HLSL
        + r"""
struct LineIn {
    float2 a : LINE_A;
    float2 b : LINE_B;
    float4 color : LINE_COLOR;
    float width : LINE_WIDTH;
};
cbuffer Push : register(b0, space0) {
    float4 clip;
    float4 viewportPad;
};
struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float across : TEXCOORD0;
    float halfWidth : TEXCOORD1;
};
VSOut main(LineIn i, uint vertexId : SV_VertexID) {
    const float2 halfVp = viewportPad.xy * 0.5;
    const float2 pa = (i.a * clip.xy + clip.zw) * halfVp;
    const float2 pb = (i.b * clip.xy + clip.zw) * halfVp;
    const float2 d = pb - pa;
    const float len = my_sqrt(d.x * d.x + d.y * d.y);
    const float2 dir = len > 1e-4 ? d / len : float2(1.0, 0.0);
    const float2 n = float2(-dir.y, dir.x);
    const float hw = my_max(i.width, 0.5) * 0.5;
    const float reach = hw + 1.0;
    const float2 corner = float2(vertexId & 1u, vertexId >> 1u);
    const float2 along = corner.x > 0.5 ? pb + dir * 0.5 : pa - dir * 0.5;
    const float side = (corner.y * 2.0 - 1.0) * reach;
    VSOut o;
    o.position = float4((along + n * side) / halfVp, 0.0, 1.0);
    o.color = i.color;
    o.across = side;
    o.halfWidth = hw;
    return o;
}
""",
    ),
    (
        "kLinesPs",
        "ps",
        COMMON_HLSL
        + r"""
struct VSOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float across : TEXCOORD0;
    float halfWidth : TEXCOORD1;
};
float4 main(VSOut i) : SV_Target {
    const float a = my_saturate(i.halfWidth + 0.5 - my_abs(i.across));
    return float4(i.color.rgb, i.color.a * a);
}
""",
    ),
    (
        "kImguiVs",
        "vs",
        r"""
struct VSIn {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
cbuffer Push : register(b1, space0) {
    float4 xform;
};
struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
VSOut main(VSIn i) {
    VSOut o;
    o.position = float4(i.pos * xform.xy + xform.zw, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
""",
    ),
    (
        "kImguiPs",
        "ps",
        r"""
struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
Texture2D tex : register(t0, space0);
SamplerState texSampler : register(s32, space0);
float4 main(VSOut i) : SV_Target {
    return i.col * tex.Sample(texSampler, i.uv);
}
""",
    ),
]


CANONICAL = ["basic_color", "basic_texture", "sprite", "terrain", "grid", "lines", "imgui"]
SHADERS_DIR = pathlib.Path(__file__).resolve().parents[2] / "shaders"
_INCLUDE = re.compile(r'#include "(common/[A-Za-z0-9_]+\.hlsli)"')


def canonical_sha256(name: str) -> str:
    """cmake/SbxEmbedHlsl.cmake 와 같은 글의 SHA-256 — 줄 끝 LF, `#include "common/…"` 줄을 그 파일 내용으로."""
    text = (SHADERS_DIR / f"{name}.hlsl").read_bytes().decode("utf-8").replace("\r\n", "\n")
    for rel in sorted(set(_INCLUDE.findall(text))):
        inc = (SHADERS_DIR / rel).read_bytes().decode("utf-8").replace("\r\n", "\n")
        text = text.replace(f'#include "{rel}"', inc)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def pascal(name: str) -> str:
    return "".join(p[:1].upper() + p[1:] for p in name.split("_"))


def source_stamp_lines() -> list[str]:
    out = ["// 이 SPIR-V 를 만든 shaders/*.hlsl 원본의 SHA-256 (ADR-0171 — verifyVulkanShaderSources 가 지금 원본과 견준다)"]
    for name in CANONICAL:
        out.append(f'inline constexpr std::string_view k{pascal(name)}SourceSha256 = "{canonical_sha256(name)}";')
    out.append("")
    return out


def stamp_only(out: pathlib.Path) -> None:
    text = out.read_text(encoding="utf-8")
    kept = [l for l in text.split("\n") if "SourceSha256 = " not in l and "원본의 SHA-256 (ADR-0171" not in l]
    if "#include <string_view>" not in kept:
        kept.insert(kept.index("#include <cstdint>") + 1, "#include <string_view>")
    end = len(kept) - 1 - kept[::-1].index("} // namespace sbx::rhi::graphics_sdk::spirv_data")
    # 앞의 빈 줄 하나는 남긴다
    while end > 0 and kept[end - 1] == "" and kept[end - 2] == "":
        del kept[end - 1]
        end -= 1
    kept[end:end] = source_stamp_lines()
    out.write_text("\n".join(kept), encoding="utf-8", newline="\n")
    print(f"stamped {len(CANONICAL)} source hashes into {out}")


def verify_spirv(name: str, words: tuple[int, ...]) -> None:
    if words[0] != 0x07230203 or words[1] != 0x00010300:
        raise RuntimeError(f"{name}: invalid SPIR-V 1.3 header ({hex(words[0])}, {hex(words[1])})")
    i = 5
    while i < len(words):
        n = words[i] >> 16
        op = words[i] & 0xFFFF
        p = words[i + 1 : i + n]
        if op in (10, 11, 12):
            raise RuntimeError(f"{name}: forbidden SPIR-V extension/import op={op} args={p}")
        if op == 17 and p[0] not in (1, 43):
            raise RuntimeError(f"{name}: unsupported capability {p[0]}")
        i += n


def compile_one(dxc: pathlib.Path, name: str, stage: str, source: str) -> tuple[int, ...]:
    with tempfile.TemporaryDirectory() as tmp:
        src_path = pathlib.Path(tmp) / f"{name}.hlsl"
        spv_path = pathlib.Path(tmp) / f"{name}.spv"
        src_path.write_text(source, encoding="utf-8")
        subprocess.run(
            [
                str(dxc),
                "-T",
                f"{stage}_6_0",
                "-E",
                "main",
                "-O3",
                "-Qstrip_debug",
                "-spirv",
                "-fspv-target-env=vulkan1.1",
                "-Fo",
                str(spv_path),
                str(src_path),
            ],
            check=True,
        )
        data = spv_path.read_bytes()
        words_list = list(struct.unpack(f"<{len(data) // 4}I", data))
        # DXC 는 Texture2D / Texture2DArray 의 OpTypeImage Depth 피연산자를 2(unknown)로 내보내지만,
        # GraphicsSDK Spirv.hpp 는 Depth=0(non-depth)만 허용하므로 0으로 정규화한다.
        i = 5
        while i < len(words_list):
            n = words_list[i] >> 16
            op = words_list[i] & 0xFFFF
            if op == 25 and n == 9 and words_list[i + 4] == 2:
                words_list[i + 4] = 0
            i += n
        words = tuple(words_list)
        verify_spirv(name, words)
        return words


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dxc", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--stamp-sources", action="store_true", help="SPIR-V 는 그대로, 원본 SHA-256 기록만 새로 쓴다")
    args = parser.parse_args()
    if args.stamp_sources:
        stamp_only(args.out)
        return
    if args.dxc is None:
        parser.error("--dxc 가 필요하다 (--stamp-sources 가 아니면)")

    lines: list[str] = [
        "#pragma once",
        "// 자동 생성 파일 — tools/shader/gen_gfxsdk_spirv.py",
        "// Graphics SDK Vulkan 백엔드(Spirv.hpp 프로파일: SPIR-V 1.3, entry=main, set=0, no GLSL.std.450)용",
        "// Sandbox 내장 셰이더 7종(14개 스테이지) SPIR-V 바이트코드.",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace sbx::rhi::graphics_sdk::spirv_data {",
        "",
    ]

    for name, stage, source in SHADERS:
        words = compile_one(args.dxc, name, stage, source)
        lines.append(f"inline constexpr std::array<std::uint32_t, {len(words)}> {name} = {{")
        for idx in range(0, len(words), 8):
            chunk = ", ".join(f"0x{w:08x}u" for w in words[idx : idx + 8])
            lines.append(f"    {chunk},")
        lines.append("};")
        lines.append("")
        print(f"generated {name}: {len(words)} words ({len(words) * 4} bytes)")

    lines.extend(source_stamp_lines())
    lines.append("} // namespace sbx::rhi::graphics_sdk::spirv_data")
    lines.append("")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
