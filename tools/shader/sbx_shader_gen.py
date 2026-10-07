#!/usr/bin/env python3
"""셰이더 코드 생성기. docs/06-RENDERING.md 6장, ADR-0019.

DXC 가 만든 단계별 DXIL(.dxil)과 SPIR-V(.spv)를 받아
  <Pascal>Shader.hpp   cbuffer · push constant 구조체(오프셋 static_assert), 바인딩 상수, 접근 함수 선언
  <Pascal>Shader.cpp   바이트코드 배열(실행 파일에 내장 — 경로 문제 없음), 리플렉션 표
  <name>.reflect.json  리플렉션 (06 6.3 형식)
를 쓴다. 리플렉션은 SPIR-V 를 직접 읽는다 (SPIRV-Cross 없이 — 바인딩·구조체 배치·정점 입력만 필요하다).

바인딩 규칙 (06 3.4): register(<b|t|s|u>N, spaceG) → group G(0~3), binding N. 한 그룹 안 binding 은 종류를 가리지 않고 고유.
push constant: [[vk::push_constant]] cbuffer 하나 (D3D12 는 b0, space7 의 루트 상수).

사용:
  sbx_shader_gen.py --name triangle --source shaders/triangle.hlsl --out-dir <dir> \
      --stage vs:VSMain:<a.dxil>:<a.spv> --stage ps:PSMain:<b.dxil>:<b.spv>
  sbx_shader_gen.py --self-test        (내장 SPIR-V 조각으로 파서 시험)
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --- SPIR-V 상수 (SPIR-V 1.6 사양) -------------------------------------------------------------
MAGIC = 0x07230203
OP_NAME, OP_MEMBER_NAME = 5, 6
OP_TYPE_VOID, OP_TYPE_BOOL, OP_TYPE_INT, OP_TYPE_FLOAT = 19, 20, 21, 22
OP_TYPE_VECTOR, OP_TYPE_MATRIX, OP_TYPE_IMAGE, OP_TYPE_SAMPLER = 23, 24, 25, 26
OP_TYPE_SAMPLED_IMAGE, OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT = 27, 28, 29, 30
OP_TYPE_POINTER = 32
OP_CONSTANT = 43
OP_VARIABLE = 59
OP_DECORATE, OP_MEMBER_DECORATE = 71, 72
OP_DECORATE_STRING, OP_MEMBER_DECORATE_STRING = 5632, 5633
OP_ENTRY_POINT = 15

DEC_BLOCK, DEC_BUFFER_BLOCK, DEC_ROW_MAJOR, DEC_COL_MAJOR = 2, 3, 4, 5
DEC_ARRAY_STRIDE, DEC_MATRIX_STRIDE, DEC_BUILTIN = 6, 7, 11
DEC_NON_WRITABLE = 24
DEC_LOCATION, DEC_BINDING, DEC_DESCRIPTOR_SET, DEC_OFFSET = 30, 33, 34, 35
DEC_USER_SEMANTIC = 5635

SC_UNIFORM_CONSTANT, SC_INPUT, SC_UNIFORM, SC_OUTPUT, SC_PUSH_CONSTANT, SC_STORAGE_BUFFER = 0, 1, 2, 3, 9, 12

STAGE_BITS = {"vs": 1, "ps": 2, "cs": 4}
STAGE_ENUM = {"vs": "Vertex", "ps": "Pixel", "cs": "Compute"}
BINDING_TYPES = ["ConstantBuffer", "Texture", "StorageBuffer", "StorageBufferRW", "StorageTexture", "Sampler"]


class GenError(Exception):
    pass


@dataclass
class Module:
    names: dict = field(default_factory=dict)
    member_names: dict = field(default_factory=dict)  # (type, idx) -> name
    decorations: dict = field(default_factory=dict)  # id -> {dec: [operands]}
    member_decorations: dict = field(default_factory=dict)  # (type, idx) -> {dec: [operands]}
    strings: dict = field(default_factory=dict)  # (id, dec) -> str
    types: dict = field(default_factory=dict)  # id -> tuple
    constants: dict = field(default_factory=dict)  # id -> int
    variables: list = field(default_factory=list)  # (id, ptr_type, storage)


def _string(words: list[int]) -> str:
    raw = b"".join(struct.pack("<I", w) for w in words)
    return raw.split(b"\0", 1)[0].decode("utf-8")


def parse_spirv(data: bytes) -> Module:
    if len(data) < 20 or len(data) % 4 != 0:
        raise GenError("SPIR-V 크기가 4 의 배수가 아니다")
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    if words[0] != MAGIC:
        raise GenError("SPIR-V magic 이 아니다")
    m = Module()
    i = 5
    while i < len(words):
        count = words[i] >> 16
        op = words[i] & 0xFFFF
        if count == 0:
            raise GenError(f"잘못된 명령 길이 (word {i})")
        args = words[i + 1:i + count]
        if op == OP_NAME:
            m.names[args[0]] = _string(args[1:])
        elif op == OP_MEMBER_NAME:
            m.member_names[(args[0], args[1])] = _string(args[2:])
        elif op == OP_DECORATE:
            m.decorations.setdefault(args[0], {})[args[1]] = args[2:]
        elif op == OP_MEMBER_DECORATE:
            m.member_decorations.setdefault((args[0], args[1]), {})[args[2]] = args[3:]
        elif op == OP_DECORATE_STRING:
            m.strings[(args[0], args[1])] = _string(args[2:])
        elif op in (OP_TYPE_VOID, OP_TYPE_BOOL):
            m.types[args[0]] = ("void" if op == OP_TYPE_VOID else "bool",)
        elif op == OP_TYPE_INT:
            m.types[args[0]] = ("int", args[1], args[2])
        elif op == OP_TYPE_FLOAT:
            m.types[args[0]] = ("float", args[1])
        elif op == OP_TYPE_VECTOR:
            m.types[args[0]] = ("vector", args[1], args[2])
        elif op == OP_TYPE_MATRIX:
            m.types[args[0]] = ("matrix", args[1], args[2])
        elif op == OP_TYPE_IMAGE:
            # sampled type, Dim, Depth, Arrayed, MS, Sampled, Format
            m.types[args[0]] = ("image", args[2], args[4], args[6])
        elif op == OP_TYPE_SAMPLER:
            m.types[args[0]] = ("sampler",)
        elif op == OP_TYPE_SAMPLED_IMAGE:
            m.types[args[0]] = ("sampled_image", args[1])
        elif op == OP_TYPE_ARRAY:
            m.types[args[0]] = ("array", args[1], args[2])
        elif op == OP_TYPE_RUNTIME_ARRAY:
            m.types[args[0]] = ("runtime_array", args[1])
        elif op == OP_TYPE_STRUCT:
            m.types[args[0]] = ("struct", list(args[1:]))
        elif op == OP_TYPE_POINTER:
            m.types[args[0]] = ("pointer", args[1], args[2])
        elif op == OP_CONSTANT:
            m.constants[args[1]] = args[2]
        elif op == OP_VARIABLE:
            m.variables.append((args[1], args[0], args[2]))
        i += count
    return m


def clean_type_name(name: str) -> str:
    # DXC: "type.Frame", "type.ConstantBuffer.Push", "type.PushConstant.Push" → "Frame", "Push"
    return name.split(".")[-1] if name else name


# --- 구조체 배치 → C++ ----------------------------------------------------------------------------

@dataclass
class CppStruct:
    name: str
    size: int
    lines: list
    asserts: list


class StructEmitter:
    """SPIR-V 구조체(DX 배치, -fvk-use-dx-layout)를 같은 바이트 배치의 C++ 구조체로 옮긴다."""

    def __init__(self, m: Module):
        self.m = m
        self.structs: dict[int, CppStruct] = {}
        self.order: list[int] = []

    def type_size(self, tid: int) -> int:
        t = self.m.types[tid]
        kind = t[0]
        if kind in ("int", "float"):
            return t[1] // 8
        if kind == "bool":
            return 4
        if kind == "vector":
            return self.type_size(t[1]) * t[2]
        if kind == "matrix":
            raise GenError("행렬 크기는 멤버 MatrixStride 로 정한다")
        if kind == "array":
            stride = self.m.decorations.get(tid, {}).get(DEC_ARRAY_STRIDE, [None])[0]
            n = self.m.constants[t[2]]
            if stride is None:
                return self.type_size(t[1]) * n
            return stride * (n - 1) + self.type_size(t[1])
        if kind == "struct":
            return self.emit(tid).size
        raise GenError(f"구조체 멤버로 쓸 수 없는 타입 {kind}")

    def scalar_cpp(self, tid: int) -> str:
        t = self.m.types[tid]
        if t[0] == "float" and t[1] == 32:
            return "f32"
        if t[0] == "int" and t[1] == 32:
            return "i32" if t[2] else "u32"
        if t[0] == "bool":
            return "u32"
        raise GenError(f"지원하지 않는 스칼라 {t}")

    def member_cpp(self, struct_id: int, idx: int, tid: int) -> tuple[str, int]:
        """(C++ 타입, 바이트 크기)"""
        t = self.m.types[tid]
        kind = t[0]
        if kind in ("int", "float", "bool"):
            return self.scalar_cpp(tid), self.type_size(tid)
        if kind == "vector":
            return f"std::array<{self.scalar_cpp(t[1])}, {t[2]}>", self.type_size(tid)
        if kind == "matrix":
            col = self.m.types[t[1]]
            stride = self.m.member_decorations.get((struct_id, idx), {}).get(DEC_MATRIX_STRIDE, [16])[0]
            # DX 배치: 마지막 열/행은 꽉 차고 나머지는 stride 간격
            size = stride * (t[2] - 1) + self.type_size(t[1])
            if size % 4 != 0:
                raise GenError("행렬 크기가 4 의 배수가 아니다")
            if stride != self.type_size(t[1]):
                raise GenError(f"행렬 {col[2]}x{t[2]} 는 stride 패딩이 있어 지원하지 않는다 — float4x4 를 쓰십시오")
            return f"std::array<f32, {size // 4}>", size
        if kind == "array":
            elem = t[1]
            n = self.m.constants[t[2]]
            stride = self.m.decorations.get(tid, {}).get(DEC_ARRAY_STRIDE, [None])[0]
            ecpp, esize = self.member_cpp(struct_id, idx, elem) if self.m.types[elem][0] != "struct" else (
                self.emit(elem).name, self.emit(elem).size)
            if stride is not None and stride != esize:
                raise GenError(f"배열 원소 stride {stride} ≠ 크기 {esize} — float4 · float4x4 · 16 바이트 구조체 배열만")
            return f"std::array<{ecpp}, {n}>", (stride or esize) * n
        if kind == "struct":
            s = self.emit(tid)
            return s.name, s.size
        raise GenError(f"구조체 멤버로 쓸 수 없는 타입 {kind}")

    def emit(self, tid: int) -> CppStruct:
        if tid in self.structs:
            return self.structs[tid]
        t = self.m.types[tid]
        name = clean_type_name(self.m.names.get(tid, f"Struct{tid}"))
        lines = []
        asserts = []
        cursor = 0
        pad = 0
        members = t[1]
        for idx, mtid in enumerate(members):
            mname = self.m.member_names.get((tid, idx), f"m{idx}")
            off = self.m.member_decorations.get((tid, idx), {}).get(DEC_OFFSET, [None])[0]
            if off is None:
                raise GenError(f"{name}.{mname}: Offset 데코레이션이 없다")
            if off < cursor:
                raise GenError(f"{name}.{mname}: 오프셋이 겹친다")
            if off > cursor:
                lines.append(f"    u8 _pad{pad}[{off - cursor}];")
                pad += 1
            cpp, size = self.member_cpp(tid, idx, mtid)
            lines.append(f"    {cpp} {mname}{{}};")
            asserts.append(f"static_assert(offsetof({name}, {mname}) == {off});")
            cursor = off + size
        size = cursor
        s = CppStruct(name, size, lines, asserts)
        self.structs[tid] = s
        self.order.append(tid)
        return s


# --- 리플렉션 --------------------------------------------------------------------------------------

def reflect(m: Module, stage: str) -> dict:
    out = {"bindings": [], "pushConstant": None, "inputs": []}
    for vid, ptr, storage in m.variables:
        ptype = m.types.get(ptr)
        if not ptype or ptype[0] != "pointer":
            continue
        tid = ptype[2]
        t = m.types[tid]
        dec = m.decorations.get(vid, {})
        name = m.names.get(vid, f"var{vid}")
        if storage == SC_PUSH_CONSTANT:
            out["pushConstant"] = {"type": tid, "name": clean_type_name(m.names.get(tid, name))}
            continue
        if storage == SC_INPUT and stage == "vs":
            if DEC_BUILTIN in dec:
                continue
            loc = dec.get(DEC_LOCATION, [None])[0]
            sem = m.strings.get((vid, DEC_USER_SEMANTIC))
            if loc is None or sem is None:
                raise GenError(f"정점 입력 '{name}': Location·semantic 이 없다 (dxc -fspv-reflect)")
            mt = re.match(r"^([A-Za-z_]+?)(\d*)$", sem)
            comps = t[2] if t[0] == "vector" else 1
            out["inputs"].append({"location": loc, "semantic": mt.group(1).upper(),
                                  "semanticIndex": int(mt.group(2) or 0), "components": comps, "name": name})
            continue
        if DEC_DESCRIPTOR_SET not in dec:
            continue
        group = dec[DEC_DESCRIPTOR_SET][0]
        binding = dec[DEC_BINDING][0]
        b = {"group": group, "binding": binding, "name": name, "size": 0, "dim": "None"}
        tdec = m.decorations.get(tid, {})
        if storage == SC_UNIFORM and t[0] == "struct" and DEC_BLOCK in tdec:
            b["type"] = "ConstantBuffer"
            b["struct"] = tid
        elif (storage == SC_UNIFORM and DEC_BUFFER_BLOCK in tdec) or storage == SC_STORAGE_BUFFER:
            # StructuredBuffer: struct { T _m0[]; } — 요소 stride 는 런타임 배열의 ArrayStride
            ra = m.types[t[1][0]]
            stride = m.decorations.get(t[1][0], {}).get(DEC_ARRAY_STRIDE, [0])[0] if ra[0] == "runtime_array" else 0
            readonly = DEC_NON_WRITABLE in m.member_decorations.get((tid, 0), {})
            b["type"] = "StorageBuffer" if readonly else "StorageBufferRW"
            b["size"] = stride
        elif storage == SC_UNIFORM_CONSTANT and t[0] == "image":
            dim, arrayed, sampled = t[1], t[2], t[3]
            b["dim"] = {(1, 0): "Tex2D", (1, 1): "Tex2DArray", (2, 0): "Tex3D", (3, 0): "Cube"}.get((dim, arrayed), "None")
            b["type"] = "StorageTexture" if sampled == 2 else "Texture"
        elif storage == SC_UNIFORM_CONSTANT and t[0] == "sampler":
            b["type"] = "Sampler"
        else:
            raise GenError(f"'{name}': 지원하지 않는 리소스 종류 ({t[0]}, storage {storage})")
        out["bindings"].append(b)
    out["inputs"].sort(key=lambda x: x["location"])
    return out


def check_dxil(data: bytes, what: str) -> None:
    """DXIL 컨테이너가 서명(검증 해시)됐는지. 서명은 dxc 옆의 dxil.dll 이 한다 — 없으면 해시가 0 이고
    실제 D3D12 런타임이 CreateGraphicsPipelineState 에서 거부한다 (WARP·드라이버 모두). 빌드에서 먼저 막는다."""
    if len(data) < 20 or data[:4] != b"DXBC":
        raise GenError(f"{what}: DXIL 컨테이너가 아니다 (DXBC 머리 없음)")
    if data[4:20] == b"\0" * 16:
        raise GenError(f"{what}: DXIL 이 서명되지 않았다 — dxc 와 같은 폴더에 dxil.dll 이 있어야 한다 "
                       "(SBX_DXC 로 지정한 dxc 를 확인하십시오, ADR-0019)")


@dataclass
class Stage:
    key: str
    entry: str
    dxil: bytes
    spirv: bytes
    module: Module
    refl: dict


def merge(stages: list[Stage]) -> tuple[list[dict], dict | None, list[dict], StructEmitter | None, dict]:
    """단계별 리플렉션을 합친다. (bindings, push, inputs, emitter-by-stage-of-push, struct emitters per binding)"""
    merged: dict[tuple[int, int], dict] = {}
    push = None
    inputs: list[dict] = []
    structs: dict[str, tuple[StructEmitter, int]] = {}
    for st in stages:
        em = StructEmitter(st.module)
        for b in st.refl["bindings"]:
            if b["group"] >= 4:
                raise GenError(f"'{b['name']}': space{b['group']} — BindGroup 은 space0~3 (push constant 는 [[vk::push_constant]])")
            key = (b["group"], b["binding"])
            if b["type"] == "ConstantBuffer":
                s = em.emit(b["struct"])
                b = dict(b)
                b["size"] = (s.size + 15) // 16 * 16
                b["structName"] = s.name
                structs.setdefault(s.name, (em, b["struct"]))
            prev = merged.get(key)
            if prev is None:
                nb = {k: v for k, v in b.items() if k != "struct"}
                nb["stages"] = STAGE_BITS[st.key]
                merged[key] = nb
            else:
                if prev["type"] != b["type"] or prev["name"] != b["name"]:
                    raise GenError(f"group {key[0]} binding {key[1]}: '{prev['name']}'({prev['type']}) 와 "
                                   f"'{b['name']}'({b['type']}) 가 겹친다 — 그룹 안 binding 번호는 종류를 가리지 않고 고유해야 한다")
                prev["stages"] |= STAGE_BITS[st.key]
        if st.refl["pushConstant"]:
            pc = st.refl["pushConstant"]
            s = em.emit(pc["type"])
            if push is not None and push["structName"] != s.name:
                raise GenError("단계마다 다른 push constant 구조체")
            size = (s.size + 3) // 4 * 4
            if size > 128:
                raise GenError(f"push constant {size} 바이트 > 128")
            push = {"structName": s.name, "size": size}
            structs.setdefault(s.name, (em, pc["type"]))
        if st.key == "vs":
            inputs = st.refl["inputs"]
    # 같은 그룹 안에서 binding 중복 검사는 key 로 이미 됐다. 정렬: group, binding
    bindings = sorted(merged.values(), key=lambda b: (b["group"], b["binding"]))
    return bindings, push, inputs, None, structs


# --- 출력 ------------------------------------------------------------------------------------------

def pascal(name: str) -> str:
    return "".join(p[:1].upper() + p[1:] for p in re.split(r"[_\-\s]+", name) if p)


def byte_array(name: str, data: bytes) -> str:
    out = [f"alignas(4) constexpr unsigned char {name}[{max(len(data), 1)}] = {{"]
    line = []
    for i, b in enumerate(data):
        line.append(f"0x{b:02x}")
        if len(line) == 24:
            out.append("    " + ",".join(line) + ",")
            line = []
    if line:
        out.append("    " + ",".join(line) + ",")
    if not data:
        out.append("    0")
    out.append("};")
    return "\n".join(out)


def generate(name: str, source: str, stages: list[Stage], out_dir: Path) -> list[Path]:
    bindings, push, inputs, _, structs = merge(stages)
    pas = pascal(name)
    ns = f"sbx::render::shaders::{name}"

    # 구조체 (의존 순서: 각 emitter 의 order — 중첩 구조체가 먼저)
    emitted: list[str] = []
    seen: set[str] = set()
    struct_blocks: list[str] = []
    for sname, (em, tid) in structs.items():
        for t in em.order:
            s = em.structs[t]
            if s.name in seen:
                continue
            seen.add(s.name)
            emitted.append(s.name)
            total = (s.size + 15) // 16 * 16 if s.name != (push or {}).get("structName") else (s.size + 3) // 4 * 4
            lines = list(s.lines)
            if total > s.size:
                lines.append(f"    u8 _tail[{total - s.size}];")
            block = [f"struct {s.name} {{"] + lines + ["};"]
            block += s.asserts
            block.append(f"static_assert(sizeof({s.name}) == {total});")
            struct_blocks.append("\n".join(block))

    consts = []
    for b in bindings:
        ident = pascal(b["name"])
        consts.append(f"inline constexpr u32 k{ident}Group = {b['group']};")
        consts.append(f"inline constexpr u32 k{ident}Binding = {b['binding']};")

    stage_decls = "\n".join(f"[[nodiscard]] rhi::ShaderBytecode {st.key}(); // {st.entry}" for st in stages)
    hpp = f"""#pragma once
// 생성 파일 — 고치지 마십시오. 원본: {source} (tools/shader/sbx_shader_gen.py, ADR-0019)
#include <array>
#include <cstddef>

#include "foundation/types/Types.hpp"
#include "render/rhi/ShaderTypes.hpp"

namespace {ns} {{

{chr(10).join(struct_blocks)}

{chr(10).join(consts)}
inline constexpr u32 kPushConstantBytes = {push["size"] if push else 0};

[[nodiscard]] const rhi::ShaderReflection& reflection();
{stage_decls}

}} // namespace {ns}
"""

    arrays = []
    funcs = []
    for st in stages:
        arrays.append(byte_array(f"k{st.key.upper()}Dxil", st.dxil))
        arrays.append(byte_array(f"k{st.key.upper()}Spirv", st.spirv))
        funcs.append(f"""rhi::ShaderBytecode {st.key}() {{
    return {{rhi::ShaderStage::{STAGE_ENUM[st.key]}, "{st.entry}",
            {{reinterpret_cast<const std::byte*>(k{st.key.upper()}Dxil), {len(st.dxil)}}},
            {{reinterpret_cast<const std::byte*>(k{st.key.upper()}Spirv), {len(st.spirv)}}}, "{name}.{st.key}"}};
}}""")
    binds = ",\n".join(
        f'    rhi::ReflectedBinding{{{b["group"]}, {b["binding"]}, rhi::BindingType::{b["type"]}, {b["stages"]}, '
        f'{b["size"]}, rhi::TextureDim::{b["dim"]}, "{b["name"]}"}}'
        for b in bindings)
    ins = ",\n".join(
        f'    rhi::ReflectedVertexInput{{{i["location"]}, "{i["semantic"]}", {i["semanticIndex"]}, {i["components"]}}}'
        for i in inputs)
    cpp = f"""// 생성 파일 — 고치지 마십시오. 원본: {source} (tools/shader/sbx_shader_gen.py)
#include "render/generated/{pas}Shader.hpp"

namespace {ns} {{
namespace {{

{chr(10).join(arrays)}

constexpr rhi::ReflectedBinding kBindings[] = {{
{binds if binds else "    rhi::ReflectedBinding{}"}
}};
constexpr rhi::ReflectedVertexInput kInputs[] = {{
{ins if ins else "    rhi::ReflectedVertexInput{}"}
}};

}} // namespace

const rhi::ShaderReflection& reflection() {{
    static const rhi::ShaderReflection r{{std::span<const rhi::ReflectedBinding>(kBindings, {len(bindings)}),
                                         std::span<const rhi::ReflectedVertexInput>(kInputs, {len(inputs)}),
                                         {push["size"] if push else 0}}};
    return r;
}}

{chr(10).join(funcs)}

}} // namespace {ns}
"""
    refl_json = {
        "name": name,
        "source": source,
        "stages": [st.key for st in stages],
        "bindGroups": [
            {"group": g, "bindings": [
                {k: v for k, v in b.items() if k in ("name", "type", "binding", "size", "dim", "stages", "structName")}
                for b in bindings if b["group"] == g]}
            for g in sorted({b["group"] for b in bindings})],
        "pushConstants": {"size": push["size"], "struct": push["structName"]} if push else None,
        "vertexInputs": [{k: v for k, v in i.items()} for i in inputs],
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = [out_dir / f"{pas}Shader.hpp", out_dir / f"{pas}Shader.cpp", out_dir / f"{name}.reflect.json"]
    contents = [hpp, cpp, json.dumps(refl_json, indent=2, ensure_ascii=False) + "\n"]
    for p, c in zip(paths, contents):
        # 내용이 같으면 쓰지 않는다 — 불필요한 재컴파일을 막는다
        if not p.exists() or p.read_text(encoding="utf-8") != c:
            p.write_text(c, encoding="utf-8", newline="\n")
    return paths


# --- 자체 시험 -------------------------------------------------------------------------------------

def _asm(instrs: list[tuple[int, list[int]]], bound: int = 64) -> bytes:
    words = [MAGIC, 0x00010600, 0, bound, 0]
    for op, args in instrs:
        words.append(((len(args) + 1) << 16) | op)
        words += args
    return struct.pack(f"<{len(words)}I", *words)


def _str_words(s: str) -> list[int]:
    b = s.encode() + b"\0"
    b += b"\0" * (-len(b) % 4)
    return list(struct.unpack(f"<{len(b) // 4}I", b))


def self_test() -> int:
    # cbuffer Frame { float4x4 m; float4 tint; } : group 0 binding 0
    # Texture2D tex : group 2 binding 0 · SamplerState s : group 2 binding 1 · push { float2 off; float scale; }
    F, V4, M44, ST, PST, VAR = 1, 2, 3, 4, 5, 6
    IMG, PIMG, VIMG, SMP, PSMP, VSMP = 7, 8, 9, 10, 11, 12
    PUSH, PPUSH, VPUSH, V2 = 13, 14, 15, 16
    ins = [
        (OP_NAME, [ST] + _str_words("type.Frame")),
        (OP_MEMBER_NAME, [ST, 0] + _str_words("transform")),
        (OP_MEMBER_NAME, [ST, 1] + _str_words("tint")),
        (OP_NAME, [VAR] + _str_words("Frame")),
        (OP_NAME, [VIMG] + _str_words("tex")),
        (OP_NAME, [VSMP] + _str_words("samp")),
        (OP_NAME, [PUSH] + _str_words("type.PushConstant.Push")),
        (OP_MEMBER_NAME, [PUSH, 0] + _str_words("offset")),
        (OP_MEMBER_NAME, [PUSH, 1] + _str_words("scale")),
        (OP_DECORATE, [ST, DEC_BLOCK]),
        (OP_MEMBER_DECORATE, [ST, 0, DEC_OFFSET, 0]),
        (OP_MEMBER_DECORATE, [ST, 0, DEC_MATRIX_STRIDE, 16]),
        (OP_MEMBER_DECORATE, [ST, 1, DEC_OFFSET, 64]),
        (OP_DECORATE, [VAR, DEC_DESCRIPTOR_SET, 0]),
        (OP_DECORATE, [VAR, DEC_BINDING, 0]),
        (OP_DECORATE, [VIMG, DEC_DESCRIPTOR_SET, 2]),
        (OP_DECORATE, [VIMG, DEC_BINDING, 0]),
        (OP_DECORATE, [VSMP, DEC_DESCRIPTOR_SET, 2]),
        (OP_DECORATE, [VSMP, DEC_BINDING, 1]),
        (OP_DECORATE, [PUSH, DEC_BLOCK]),
        (OP_MEMBER_DECORATE, [PUSH, 0, DEC_OFFSET, 0]),
        (OP_MEMBER_DECORATE, [PUSH, 1, DEC_OFFSET, 8]),
        (OP_TYPE_FLOAT, [F, 32]),
        (OP_TYPE_VECTOR, [V4, F, 4]),
        (OP_TYPE_VECTOR, [V2, F, 2]),
        (OP_TYPE_MATRIX, [M44, V4, 4]),
        (OP_TYPE_STRUCT, [ST, M44, V4]),
        (OP_TYPE_POINTER, [PST, SC_UNIFORM, ST]),
        (OP_VARIABLE, [PST, VAR, SC_UNIFORM]),
        (OP_TYPE_IMAGE, [IMG, F, 1, 0, 0, 0, 1, 0]),
        (OP_TYPE_POINTER, [PIMG, SC_UNIFORM_CONSTANT, IMG]),
        (OP_VARIABLE, [PIMG, VIMG, SC_UNIFORM_CONSTANT]),
        (OP_TYPE_SAMPLER, [SMP]),
        (OP_TYPE_POINTER, [PSMP, SC_UNIFORM_CONSTANT, SMP]),
        (OP_VARIABLE, [PSMP, VSMP, SC_UNIFORM_CONSTANT]),
        (OP_TYPE_STRUCT, [PUSH, V2, F]),
        (OP_TYPE_POINTER, [PPUSH, SC_PUSH_CONSTANT, PUSH]),
        (OP_VARIABLE, [PPUSH, VPUSH, SC_PUSH_CONSTANT]),
    ]
    m = parse_spirv(_asm(ins))
    r = reflect(m, "ps")
    st = Stage("ps", "PSMain", b"\x01\x02", _asm(ins), m, r)
    bindings, push, inputs, _, structs = merge([st])
    assert [(b["group"], b["binding"], b["type"]) for b in bindings] == [
        (0, 0, "ConstantBuffer"), (2, 0, "Texture"), (2, 1, "Sampler")], bindings
    assert bindings[0]["size"] == 80 and bindings[0]["structName"] == "Frame", bindings[0]
    assert bindings[1]["dim"] == "Tex2D"
    assert push == {"structName": "Push", "size": 12}, push
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        paths = generate("selftest", "selftest.hlsl", [st], Path(d))
        hpp = paths[0].read_text(encoding="utf-8")
        assert "std::array<f32, 16> transform{};" in hpp, hpp
        assert "static_assert(offsetof(Frame, tint) == 64);" in hpp
        assert "static_assert(sizeof(Push) == 12);" in hpp
        assert "kPushConstantBytes = 12" in hpp
    # 같은 binding 에 다른 종류 → 오류
    bad = [ins[i] for i in range(len(ins))]
    bad = [(op, a) if not (op == OP_DECORATE and a[0] == VSMP and a[1] == DEC_BINDING) else (op, [VSMP, DEC_BINDING, 0])
           for op, a in bad]
    bad = [(op, a) if not (op == OP_DECORATE and a[0] == VSMP and a[1] == DEC_DESCRIPTOR_SET) else
           (op, [VSMP, DEC_DESCRIPTOR_SET, 2]) for op, a in bad]
    mb = parse_spirv(_asm(bad))
    try:
        merge([Stage("ps", "PSMain", b"", b"", mb, reflect(mb, "ps"))])
        raise AssertionError("겹치는 binding 을 놓쳤다")
    except GenError as e:
        assert "겹친다" in str(e)
    # DXIL 서명 검사
    check_dxil(b"DXBC" + bytes(range(1, 17)) + b"\0" * 8, "signed")
    for blob, why in ((b"DXBC" + b"\0" * 24, "서명되지"), (b"XXXX" + b"\1" * 24, "컨테이너가 아니다")):
        try:
            check_dxil(blob, "t")
            raise AssertionError(f"DXIL 검사가 '{why}' 를 놓쳤다")
        except GenError as e:
            assert why in str(e), e
    print("sbx_shader_gen self-test OK")
    return 0


def main(argv: list[str]) -> int:
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8")
        except AttributeError:
            pass
    ap = argparse.ArgumentParser(description="DXIL + SPIR-V → 내장 바이트코드 · 리플렉션 · cbuffer 헤더")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--name")
    ap.add_argument("--source", default="")
    ap.add_argument("--out-dir")
    ap.add_argument("--stage", action="append", default=[], help="key:entry:dxil:spirv (key = vs|ps|cs)")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.name or not a.out_dir or not a.stage:
        ap.error("--name, --out-dir, --stage 가 필요하다")
    stages = []
    try:
        for spec in a.stage:
            # Windows 경로의 드라이브 문자(C:)가 있어 앞 두 칸만 ':' 로 자른다
            key, entry, rest = spec.split(":", 2)
            m = re.match(r"^(.*\.dxil):(.*)$", rest)
            if not m or key not in STAGE_BITS:
                raise GenError(f"--stage 형식: key:entry:<file.dxil>:<file.spv> ('{spec}')")
            dxil = Path(m.group(1)).read_bytes()
            check_dxil(dxil, m.group(1))
            spirv = Path(m.group(2)).read_bytes()
            mod = parse_spirv(spirv)
            stages.append(Stage(key, entry, dxil, spirv, mod, reflect(mod, key)))
        generate(a.name, a.source, stages, Path(a.out_dir))
    except (GenError, OSError, KeyError) as e:
        print(f"sbx_shader_gen: {a.source or a.name}: 오류: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
