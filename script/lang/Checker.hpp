#pragma once
// SandboxScript 이름 · 타입 검사기. docs/19-SANDBOXSCRIPT.md 5 · 10 · 11장, ADR-0056.
//
// 입력은 팩 하나의 스크립트 파일 전부 (경로 = 팩 기준 상대 "scripts/a.ts"). 차례:
//   1 파일마다 파싱 (경로 정렬 순 — 04 4.5)
//   2 import 그래프: "./b" · "../c" 를 같은 팩 안의 파일로 (확장자 없이), "sandbox" = SDK. 없는 파일 · 순환 = S416
//   3 모듈마다 (의존 먼저 — 위상 순서): 최상위 이름 → import 묶기 → interface · type · enum → 함수 서명 → 최상위 const
//   4 함수 몸 (반환 타입이 없는 함수는 처음 부를 때 먼저 검사해 추론 — 재귀면 S421)
// 결과 Program 은 마디마다 타입 · 가리키는 것 (NodeSema) 과 상수값을 남긴다 — IR (v0.3 M1-T3) 이 읽는다.
// 오류는 모은다. 이미 보고한 자리는 Error 타입이 되어 같은 오류가 이어서 나지 않는다.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "script/api/ComponentSchema.hpp"
#include "script/api/MathBuiltins.hpp"
#include "script/api/SystemEvents.hpp"
#include "script/lang/Ast.hpp"
#include "script/lang/Types.hpp"

namespace sbx::script {

struct SourceFile {
    std::string path; // 팩 기준 상대, '/' 구분 ("scripts/mana.ts"). 의존 팩의 파일은 "@<팩>/scripts/mana.ts" (아래)
    std::string text;
};

// 팩 사이 import (v0.4 M4-T1, ADR-0075 — 정적 링크). import { X } from "@<팩>/<경로>" 는 그 팩의
// "@<팩>/scripts/<경로>.ts" 를 가리킨다 — 그 파일은 files 에 "@<팩>/…" 경로로 함께 들어온다. 팩마다 "@" 로 가져올 수
// 있는 팩 (그 팩의 직접 의존 — pack.json dependencies · requires). 이 프로그램의 팩은 pack = "". 의존 팩의 모듈은 이
// 팩의 모듈에서 (import 를 따라) 닿는 것만 검사 · 코드 생성에 들어가고, 그 안의 defineSystem 은 시스템 표에 넣지 않는다
// (그 팩이 돌린다). defineComponent 는 타입 · 컴포넌트 표에는 쓰되 등록은 그 팩의 몫이다 (부르는 쪽이 모듈 경로의 '@'
// 로 가린다)
struct PackImports {
    std::string pack;
    std::vector<std::string> allowed;
};

// 경로가 의존 팩의 파일이면 그 팩 id ("@inv/scripts/a.ts" → "inv"), 아니면 빈 문자열
[[nodiscard]] std::string_view foreignPackOf(std::string_view path) noexcept;

// 컴파일 때 아는 값 (상수식 — 최상위 const · enum 값 · i32 자리의 정수 리터럴)
struct ConstValue {
    enum class Kind : u8 { None, Number, I32, Bool, String };
    Kind kind = Kind::None;
    f64 number = 0.0;
    i32 int32 = 0;
    bool boolean = false;
    std::string string;

    [[nodiscard]] bool has() const noexcept { return kind != Kind::None; }
    [[nodiscard]] f64 asNumber() const noexcept { return kind == Kind::I32 ? static_cast<f64>(int32) : number; }
};

using SymbolId = u32;
inline constexpr SymbolId kNoSymbol = 0xFFFF'FFFFu;

enum class SymbolKind : u8 {
    Local,       // let · const (함수 안)
    Param,       // 매개변수
    ModuleConst, // 최상위 const (상수식)
    Function,    // 최상위 function
    Type,        // interface · type · SDK 타입 (타입 이름공간만)
    Enum,        // enum (타입 + 값 이름공간)
    Builtin,     // SDK · 전역 내장 (toI32 · idiv · Math · NaN · Infinity · defineComponent · field)
    Component,   // 최상위 const = defineComponent(…) (function = Program::components 의 자리)
};

// 내장 (Symbol::builtin, NodeSema::refIndex)
enum class Builtin : u32 {
    None = 0,
    ToI32,
    Idiv,
    MathNamespace,
    NaN,
    Infinity,
    DefineComponent,        // defineComponent — 최상위 const 의 초기값으로만 (ADR-0059 1)
    FieldNamespace,         // field.i32 · f32 · … — defineComponent 의 fields 안에서만
    DefineSystem,           // defineSystem — export default 의 식으로만 (ADR-0062)
    MathFunctionBase = 100, // + kMathFunctions 의 자리
    MathConstantBase = 200, // + kMathConstants 의 자리
};

struct Symbol {
    SymbolKind kind = SymbolKind::Local;
    std::string name;
    TypeId type = TypeTable::kError;
    u32 module = 0;
    NodeId decl = kNoNode;
    bool mutableVar = false;    // let · 매개변수
    bool maybeAssigned = false; // 선언 뒤에 대입이 있을 수 있다 — null 좁히기에서 뺀다
    ConstValue value;           // const 의 상수값 (있으면)
    u32 function = 0;           // Function: Program::functions 의 자리 · Component: Program::components 의 자리
    Builtin builtin = Builtin::None;
};

struct FunctionInfo {
    std::string name;
    u32 module = 0;
    NodeId decl = kNoNode;
    std::vector<TypeId> params;
    std::vector<SymbolId> paramSymbols;
    TypeId result = TypeTable::kError;
    bool resultDeclared = false;
    enum class State : u8 { Pending, Checking, Done } state = State::Pending;
};

// 마디가 가리키는 것
enum class RefKind : u8 {
    None,
    Symbol,       // Ident · 호출 대상 → refIndex = SymbolId
    Field,        // Member → 구조체 필드 자리
    HostFunction, // Member (속성) · 호출 대상 → api::kHostFunctions 의 자리
    Builtin,      // Member · 호출 대상 → Builtin 값
    EnumMember,   // Member (E.A) → 멤버 자리
    ArrayLength,  // xs.length
    StringLength, // s.length
    ArrayPush,    // xs.push(…) 의 대상
};

struct NodeSema {
    TypeId type = TypeTable::kError;
    RefKind ref = RefKind::None;
    u32 refIndex = 0;
};

struct ModuleSema {
    std::string path;
    Ast ast;
    bool parsed = false; // 파싱 오류가 없다 (있으면 검사하지 않는다 — 이 모듈을 가져오는 이름은 Error 타입)
    std::vector<NodeSema> nodes;
    std::vector<ConstValue> consts; // 마디마다 상수값 (없으면 None)
    std::vector<std::pair<std::string, SymbolId>> exportValues;
    std::vector<std::pair<std::string, SymbolId>> exportTypes;
};

// defineComponent 하나 (ADR-0059). schema 의 필드는 이름 순, dataType = 값 구조체 (필드도 이름 순 — ctx.read 의 결과,
// ADR-0060)
struct ComponentDecl {
    api::ComponentSchema schema;
    SymbolId symbol = kNoSymbol;
    TypeId dataType = TypeTable::kError;
    u32 module = 0;
    NodeId decl = kNoNode;
};

inline constexpr u32 kMaxSystemIntervalTicks = 65535; // bc::kMaxIntervalTicks 와 같다

// events 의 구독 하나 (ADR-0063) — 태그는 이름 (콘텐츠 로드가 푼다), 사각형은 f32 로 반올림한 값
struct SystemEventDecl {
    u8 kind = 0; // api::GameEventKind
    f64 minX = 0, minY = 0, maxX = 0, maxY = 0;
    std::vector<std::string> all, any, none;
};

// export default defineSystem({ … }) 하나 (ADR-0062 · 0063 · 0067). 콜백 = Program::functions 의 자리 (메서드가 함수
// 하나), 없으면 api::kNoCallback. client 시스템은 onUpdate 만, server 시스템은 onUpdate 가 없다
struct SystemDecl {
    std::string id;
    std::string module; // "scripts/regen.ts"
    u32 line = 0;
    u32 column = 0;
    api::HostDomain domain = api::HostDomain::Server;
    u32 onUpdate = api::kNoCallback; // client — (ClientContext?) → void, 매 프레임
    u32 intervalTicks = 1;
    u32 onTick = api::kNoCallback;
    u32 onStart = api::kNoCallback;
    u32 onStop = api::kNoCallback;
    u32 onEvent = api::kNoCallback;
    u32 eventsPerTick = 0;               // onEvent 가 있으면 1 ~ api::kMaxEventsPerTick (기본 256)
    std::vector<SystemEventDecl> events; // 종류 차례
    NodeId decl = kNoNode;
};

struct Program {
    TypeTable types;
    std::vector<Symbol> symbols;
    std::vector<FunctionInfo> functions;
    std::vector<ComponentDecl> components; // 정의 차례 (모듈 위상 순서 → 소스 순서). 오류 있는 정의는 없다
    std::vector<SystemDecl> systems;       // 정의 차례 (모듈 위상 순서). 오류 있는 정의는 없다
    std::vector<ModuleSema> modules;       // 경로 정렬 순
    std::vector<u32> order;                // 위상 순서 (의존이 먼저)

    [[nodiscard]] const ModuleSema* module(std::string_view path) const noexcept;
    [[nodiscard]] SymbolId exportedValue(std::string_view path, std::string_view name) const noexcept;
};

[[nodiscard]] Program checkProgram(std::span<const SourceFile> files, DiagnosticList& diags,
                                   std::span<const PackImports> imports = {});

// import 경로 풀기: from = "scripts/x/a.ts", spec = "../b" → "scripts/b.ts". 팩 밖 · 빈 마디 · 확장자 있음이면 빈
// 문자열
[[nodiscard]] std::string resolveImportPath(std::string_view from, std::string_view spec);

} // namespace sbx::script
