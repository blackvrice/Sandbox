#include "script/sdk/SdkGenerator.hpp"

#include <format>

#include "script/api/HostApi.hpp"

namespace sbx::script::sdk {

namespace {

std::string_view tsType(api::AbiType t) {
    switch (t) {
    case api::AbiType::Void:
        return "void";
    case api::AbiType::Bool:
        return "boolean";
    case api::AbiType::I32:
        return "i32";
    case api::AbiType::Number:
        return "number";
    case api::AbiType::String:
        return "string";
    case api::AbiType::Entity:
        return "Entity";
    case api::AbiType::ServerContext:
        return "ServerContext";
    }
    return "never";
}

} // namespace

std::string generateSandboxDts() {
    std::string out;
    out +=
        "// SandboxScript SDK — 생성된 파일 (sbx sdk). 손으로 고치지 않는다 — script/api 의 표를 고치고 다시 만든다.\n";
    out += std::format("// Host API {} · 지문 {:016x} — docs/19-SANDBOXSCRIPT.md\n", api::kHostApiVersion,
                       api::hostApiFingerprint());
    out += "declare module \"sandbox\" {\n";
    // SDK 내장 — 검사기 (script/lang/Checker) 의 "sandbox" 이름과 같다
    out += "  /** 32 비트 정수 — 산술은 감싼다 (2147483647 + 1 = -2147483648). TS 에서는 number 와 같다 */\n";
    out += "  export type i32 = number;\n";
    out += "  /** JS ToInt32 — NaN · ±Infinity → 0, 0 쪽으로 자르고 2^32 로 감싼다 */\n";
    out += "  export function toI32(x: number): i32;\n";
    out += "  /** 정수 나눗셈 (0 쪽으로 자름) — b 가 0 이면 트랩 */\n";
    out += "  export function idiv(a: i32, b: i32): i32;\n";
    for (const api::OpaqueTypeInfo& o : api::kOpaqueTypes) {
        out += "\n";
        out += std::format("  /** {} */\n", o.doc);
        out += std::format("  export interface {} {{\n", o.name);
        out += std::format("    readonly __sandboxOpaque: \"{}\";\n", o.name); // 리터럴로 만들 수 없게 (불투명)
        for (const api::HostFunction& f : api::kHostFunctions) {
            if (f.owner != o.abi) {
                continue;
            }
            out += std::format("    /** {} — {} */\n", f.doc, f.id);
            if (f.property) {
                out += std::format("    readonly {}: {};\n", f.member, tsType(f.result));
                continue;
            }
            std::string params;
            for (u8 i = 0; i < f.paramCount; ++i) {
                const std::string_view name = f.paramNames[i].empty() ? std::string_view("arg") : f.paramNames[i];
                params += std::format("{}{}: {}", i > 0 ? ", " : "", name, tsType(f.params[i]));
            }
            out += std::format("    {}({}): {};\n", f.member, params, tsType(f.result));
        }
        out += "  }\n";
    }
    out += "}\n";
    return out;
}

} // namespace sbx::script::sdk
