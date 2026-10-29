#include "script/sdk/SdkGenerator.hpp"

#include <algorithm>
#include <format>
#include <vector>

#include "script/api/EngineComponents.hpp"
#include "script/api/HostApi.hpp"
#include "script/api/SystemEvents.hpp"

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
    case api::AbiType::ClientContext:
        return "ClientContext";
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
    // 엔진 컴포넌트 (v0.5 M2-T1, ADR-0080 1) — 접근 표 그대로, 읽기만 (쓰기 규칙은 M2-T2)
    for (const api::EngineComponentAccess& e : api::kEngineComponents) {
        out += std::format("  /** {} — 엔진 컴포넌트 {} (읽기 {}) */\n", e.doc, e.id, api::engineReadName(e.read));
        out += std::format("  export interface {} {{\n", e.scriptName);
        for (const api::EngineFieldAccess& f : e.fields) {
            std::string_view t = "number";
            switch (f.kind) {
            case api::ComponentFieldKind::I32:
            case api::ComponentFieldKind::Enum:
                t = "i32";
                break;
            case api::ComponentFieldKind::Bool:
                t = "boolean";
                break;
            case api::ComponentFieldKind::Vec2:
                t = "Vec2";
                break;
            case api::ComponentFieldKind::Entity:
                t = "Entity | null";
                break;
            case api::ComponentFieldKind::F32:
            case api::ComponentFieldKind::F64:
                break;
            }
            out += std::format("    /** {} */\n    readonly {}: {};\n", f.doc, f.name, t);
        }
        out += "  }\n";
        out += std::format("  export const {}: ComponentRef<{}>;\n", e.scriptName, e.scriptName);
    }
    // 시스템 정의 (ADR-0062 · 0063) — export default 의 식으로만, 값은 상수 (19 17 · 18장)
    out += "  /** 시스템 이벤트 종류 (onEvent 의 e.kind) */\n";
    out += "  export const enum GameEventKind {\n";
    for (const api::GameEventKindInfo& k : api::kGameEventKinds) {
        out += std::format("    {} = {},\n", k.member, static_cast<int>(k.kind));
    }
    out += "  }\n";
    out += "  /** onEvent 의 이벤트 (단계 17d — 그 틱에 일어난 일) */\n";
    out += "  export interface GameEvent {\n";
    for (const api::GameEventFieldInfo& f : api::kGameEventFields) {
        std::string_view t = "i32";
        switch (f.type) {
        case api::GameEventField::EntityOrNull:
            t = "Entity | null";
            break;
        case api::GameEventField::Kind:
            t = "GameEventKind";
            break;
        case api::GameEventField::I32:
            t = "i32";
            break;
        case api::GameEventField::Number:
            t = "number";
            break;
        }
        out += std::format("    /** {} */\n", f.doc);
        out += std::format("    readonly {}: {};\n", f.name, t);
    }
    out += "  }\n";
    out += "  /** 태그 거르기 — 콘텐츠 태그 이름 (Rule · 트리거와 같은 식) */\n";
    out += "  export interface TagFilter {\n";
    out += "    all?: string[];\n";
    out += "    any?: string[];\n";
    out += "    none?: string[];\n";
    out += "  }\n";
    out += "  /** 구독 — 종류마다 하나 (값은 상수) */\n";
    out += "  export interface SystemEvents {\n";
    out += "    /** 그 틱에 죽은 개체 (태그는 틱 처음의 것) */\n";
    out += "    death?: { tags?: TagFilter };\n";
    out += "    /** 그 틱에 생겨 살아 있는 개체 */\n";
    out += "    spawn?: { tags?: TagFilter };\n";
    out += "    /** 틱 처음에 사각형 밖 (또는 없었다), 지금 안 */\n";
    out += "    areaEnter?: { min: [number, number]; max: [number, number]; tags?: TagFilter };\n";
    out += "    /** 점수 칸이 바뀌었다 (게임 세션이 있을 때) */\n";
    out += "    score?: {};\n";
    out += "  }\n";
    out += "  /** defineSystem 의 정의 — export default defineSystem({ … }) 으로만 (파일 하나에 하나) */\n";
    out += "  export interface SystemDefinition {\n";
    out += "    /** \"이름공간.이름\" — 팩들 사이에 하나. 실행 순서 = (팩 순서, id) */\n";
    out += "    id: string;\n";
    out += "    /** \"server\" (기본 — 서버 시뮬레이션) 또는 \"client\" (클라이언트 화면 — onUpdate 만, ADR-0067) */\n";
    out += "    domain?: \"server\" | \"client\";\n";
    out += "    /** onTick 의 주기 — 틱 번호 % n == 0 인 틱 (1 ~ 65535, 기본 1) */\n";
    out += "    intervalTicks?: i32;\n";
    out += "    /** 한 틱에 받는 onEvent 수 (1 ~ 4096, 기본 256 — 넘으면 건너뛴다) */\n";
    out += "    eventsPerTick?: i32;\n";
    out += "    events?: SystemEvents;\n";
    out += "    /** 단계 16b — Native System 뒤, 구조 변경 · 게임 규칙 · 트리거 앞. 쓰기는 호출이 끝나면 적용 */\n";
    out += "    onTick?(ctx: ServerContext): void;\n";
    out += "    /** 단계 17d — 게임이 Running 이 된 틱 (세션이 없으면 틱 1) */\n";
    out += "    onStart?(ctx: ServerContext): void;\n";
    out += "    /** 단계 17d — 게임이 Finished 가 된 틱 (세션이 없으면 없다) */\n";
    out += "    onStop?(ctx: ServerContext): void;\n";
    out += "    /** 단계 17d — events 의 구독마다, 그 틱에 일어난 차례로 */\n";
    out += "    onEvent?(ctx: ServerContext, e: GameEvent): void;\n";
    out += "    /** client 시스템만 — 클라이언트의 프레임마다 (복제 월드를 읽고 화면에 표시, 시뮬레이션과 무관) */\n";
    out += "    onUpdate?(ctx: ClientContext): void;\n";
    out += "  }\n";
    out += "  /** 스크립트 시스템 (서버 · 클라이언트) */\n";
    out += "  export function defineSystem(definition: SystemDefinition): SystemDefinition;\n";
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

std::string generateTsconfig(std::span<const TsPackPath> packs) {
    // 보수적인 설정 — 모듈 해석은 상대 경로 (확장자 없이) 와 sandbox (선언 파일의 declare module) 만 쓴다.
    // 다른 팩은 "@<팩>/…" — paths 가 그 팩의 scripts/ 로 (ADR-0075)
    std::vector<const TsPackPath*> sorted;
    for (const TsPackPath& p : packs) {
        sorted.push_back(&p);
    }
    std::sort(sorted.begin(), sorted.end(), [](const TsPackPath* a, const TsPackPath* b) { return a->pack < b->pack; });
    std::string out = R"({
  "compilerOptions": {
    "target": "ES2020",
    "module": "ES2020",
    "moduleResolution": "node",
    "lib": ["ES2020"],
    "types": [],
    "strict": true,
    "noEmit": true)";
    if (!sorted.empty()) {
        out += ",\n    \"baseUrl\": \".\",\n    \"paths\": {";
        for (usize i = 0; i < sorted.size(); ++i) {
            out += std::format("{}\n      \"@{}/*\": [\"{}/scripts/*\"]", i == 0 ? "" : ",", sorted[i]->pack,
                               sorted[i]->dir);
        }
        out += "\n    }";
    }
    out += "\n  },\n  \"include\": [\"sdk/sandbox.d.ts\", \"content/*/scripts/**/*.ts\"";
    if (!sorted.empty()) {
        out += std::format(", \"{}\"", kInstalledScriptsInclude);
    }
    out += "]\n}\n";
    return out;
}

} // namespace sbx::script::sdk
