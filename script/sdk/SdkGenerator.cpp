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
    case api::AbiType::Component:
        return "ComponentRef<T>";
    case api::AbiType::ComponentData:
        return "T";
    case api::AbiType::ComponentDataOrNull:
        return "T | null";
    case api::AbiType::EntityList:
        return "Entity[]";
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
    // 컴포넌트 정의 (ADR-0059) — 검사기는 defineComponent 를 실행하지 않고 상수 인자를 스키마로 읽는다 (19 15장)
    out += "\n";
    out += "  /** 2 차원 값 — vec2 필드의 값 (f32 둘로 저장) */\n";
    out += "  export interface Vec2 {\n";
    out += "    x: number;\n";
    out += "    y: number;\n";
    out += "  }\n";
    out += "  /** 필드 정의 — field.* 가 만든다 (defineComponent 의 fields 안에서만) */\n";
    out += "  export interface Field<T> {\n";
    out += "    readonly __sandboxField: T;\n";
    out += "  }\n";
    out += "  /** 필드 범위 — 기본값 · 쓰기 · 세이브 · Inspector 가 이 안이다 */\n";
    out += "  export interface FieldRange {\n";
    out += "    min: number;\n";
    out += "    max: number;\n";
    out += "  }\n";
    out += "  /** 필드 종류 7 — 인자는 모두 상수 */\n";
    out += "  export namespace field {\n";
    out += "    function i32(defaultValue?: i32, range?: FieldRange): Field<i32>;\n";
    out += "    /** f32 로 저장 — 읽으면 number, 쓰면 f32 로 반올림 */\n";
    out += "    function f32(defaultValue?: number, range?: FieldRange): Field<number>;\n";
    out += "    function f64(defaultValue?: number, range?: FieldRange): Field<number>;\n";
    out += "    function bool(defaultValue?: boolean): Field<boolean>;\n";
    out += "    function vec2(x?: number, y?: number): Field<Vec2>;\n";
    out += "    /** 개체 참조 (saveId 로 저장 — 세이브 · 로드 뒤에도 같은 개체), 기본 null */\n";
    out += "    function entity(): Field<Entity | null>;\n";
    out += "    /** enum 필드 — 인자 = 기본 멤버 (E.A) */\n";
    out += "    function enumOf<E>(defaultValue: E): Field<E>;\n";
    out += "  }\n";
    out += "  /** defineComponent 의 정의 — 최상위 const 의 초기값으로만 */\n";
    out += "  export interface ComponentDefinition<F> {\n";
    out += "    /** \"이름공간.이름\" (엔진 이름공간 core · game · … 은 안 된다) */\n";
    out += "    id: string;\n";
    out += "    /** 1 ~ 65535 (기본 1) — 모양을 바꾸면 올린다 */\n";
    out += "    version?: i32;\n";
    out += "    fields: F;\n";
    out += "    /** 세이브 · WorldHash (기본 true) */\n";
    out += "    persistent?: boolean;\n";
    out += "    /** 클라이언트로 복제 (기본 true, serverOnly 면 false) */\n";
    out += "    replicated?: boolean;\n";
    out += "    /** Inspector (기본 true) */\n";
    out += "    editorVisible?: boolean;\n";
    out += "    /** 서버에만 (기본 false) */\n";
    out += "    serverOnly?: boolean;\n";
    out += "  }\n";
    out += "  /** 컴포넌트 참조 — ctx.read · ctx.query 의 인자 (v0.3 M3-T3) */\n";
    out += "  export interface ComponentRef<T> {\n";
    out += "    readonly __sandboxComponent: T;\n";
    out += "  }\n";
    out += "  /** 스크립트 컴포넌트 — 필드는 이름 순으로 저장된다 (선언 순서를 바꿔도 같은 컴포넌트) */\n";
    out += "  export function defineComponent<F extends { [name: string]: Field<unknown> }>(\n";
    out += "    definition: ComponentDefinition<F>,\n";
    out += "  ): ComponentRef<{ [K in keyof F]: F[K] extends Field<infer T> ? T : never }>;\n";
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
            // 컴포넌트 항목은 제네릭 — T 는 ComponentRef<T> 인자가 정한다 (ADR-0060)
            out += std::format("    {}{}({}): {};\n", f.member, api::isComponentGeneric(f) ? "<T>" : "", params,
                               tsType(f.result));
        }
        out += "  }\n";
    }
    out += "}\n";
    return out;
}

} // namespace sbx::script::sdk
