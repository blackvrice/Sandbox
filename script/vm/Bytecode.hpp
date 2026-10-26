#pragma once
// SandboxScript 바이트코드 (.sbc). ADR-0058, docs/19-SANDBOXSCRIPT.md 12장.
//
// 레지스터 기계: 함수마다 레지스터 표 (레지스터마다 타입) 와 8 바이트 고정 명령열. 값은 8 바이트 칸 하나 — 타입 해석은
// 검증기 (Verifier) 가 레지스터 타입으로 보장한다. 모든 레지스터는 프레임 시작에 0 (null · false · 0).
// 매개변수는 레지스터 0 … n-1, 호출 인자는 연속 레지스터 c … c+n-1. 점프 대상 · 32 비트 즉시값은 b | c << 16.
// 파일 (serialize) 은 리틀 엔디안, 머리 "SBXC" · 형식 · Host 표 버전 · 지문 + 표들. 체크섬 없음 (안전은 검증기 —
// ADR-0058 8).

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"
#include "script/api/HostApi.hpp"

namespace sbx::script::bc {

inline constexpr u32 kFormatVersion = 2; // 2 = 컴포넌트 표 · LOADK (v0.3 M3-T3)
inline constexpr u16 kNoReg = 0xFFFF;

// 상한 (믿지 않는 파일 — 읽기와 검증이 둘 다 본다)
inline constexpr u32 kMaxRegisters = 4096;
inline constexpr u32 kMaxParams = 64;
inline constexpr u32 kMaxInstructions = 1u << 20;
inline constexpr u32 kMaxPoolEntries = 0xFFFF; // u16 번호
inline constexpr u32 kMaxFunctions = 0xFFFF;
inline constexpr u32 kMaxTypes = 0xFFFF;
inline constexpr u32 kMaxFields = 256;
inline constexpr u32 kMaxImports = 1024;
inline constexpr u32 kMaxComponents = 1024;
inline constexpr u32 kMaxStringBytes = 1u << 20;

enum class TypeKind : u8 { Void, I32, F64, Bool, String, Opaque, Array, Box, Struct };

struct TypeDesc {
    TypeKind kind = TypeKind::Void;
    u32 elem = 0;                          // Array 원소 · Box 안쪽
    api::AbiType abi = api::AbiType::Void; // Opaque
    std::vector<std::string> fieldNames;   // Struct — 이름 (바이트) 순으로 오름차순 (배치 — ADR-0058 4)
    std::vector<u32> fieldTypes;
};

// 레지스터 칸의 해석
enum class RegClass : u8 { Void, I32, F64, Bool, Ref, Opaque };

enum class Op : u8 {
    NOP,
    MOV,      // a ← b (같은 타입)
    LOADI,    // a(i32) ← 즉시값 b|c<<16
    LOADB,    // a(bool) ← b (0 · 1)
    LOADF,    // a(f64) ← numbers[b]
    LOADS,    // a(string) ← strings[b]
    LOADNULL, // a(참조) ← null
    // i32 (감싼다). REMI 는 0 이면 트랩, IDIV 는 0 이면 트랩 · 0 쪽으로 자름 · -2^31 / -1 = -2^31
    ADDI,
    SUBI,
    MULI,
    REMI,
    IDIV,
    ANDI,
    ORI,
    XORI,
    SHLI,
    SHRI,
    MINI,
    MAXI,
    NEGI,
    NOTI, // a ← -b · ~b
    // f64 (JS 와 같다 — REMF = fmod, POWF = pow)
    ADDF,
    SUBF,
    MULF,
    DIVF,
    REMF,
    POWF,
    NEGF,  // a ← -b
    MATHF, // a ← Math.f(b) 또는 Math.f(b, b+1) — c = kMathFunctions 의 자리 (arity 0 · 2 = 둘)
    // 변환
    I2F,   // a(f64) ← b(i32)
    F2I,   // a(i32) ← ToInt32(b) (JS — NaN · ±Inf → 0, 2^32 로 감싸기)
    F2IDX, // a(i32) ← b(f64) 가 정수 · i32 범위면 그대로, 아니면 트랩 (배열 첨자)
    // 비교 → bool (JS 의 NaN 규칙. > · >= 는 피연산자를 바꿔 LT · LE)
    EQI,
    LTI,
    LEI,
    EQF,
    LTF,
    LEF,
    EQB,
    EQS,
    LTS,
    LES,
    EQR,  // 참조가 같다 (null 끼리도 같다)
    EQO,  // 불투명 핸들이 같다
    NOTB, // a ← !b
    // 문자열 (불변 UTF-8)
    CONCAT, // a ← b + c
    TOSTRI, // a ← String(i32) (JS Number#toString)
    TOSTRF, // a ← String(f64) (JS Number#toString — 가장 짧은 왕복 표기)
    TOSTRB, // a ← "true" · "false"
    STRLEN, // a(i32) ← b.length — JS 와 같은 UTF-16 단위 수 (U+10000 이상은 2)
    // 배열
    NEWARR,  // a ← [] (a 의 타입)
    ARRLEN,  // a(i32) ← b.length
    ARRGET,  // a ← b[c] (범위 밖 · null 이면 트랩)
    ARRSET,  // a[b] ← c (범위 밖이면 트랩 — 늘리기는 ARRPUSH 만)
    ARRPUSH, // a(i32 새 길이 · kNoReg) ← b.push(c)
    // 구조체 · 상자
    NEWOBJ, // a ← { 0 … } (a 의 타입)
    GETF,   // a ← b.필드[c] (null 이면 트랩)
    SETF,   // a.필드[b] ← c
    BOX,    // a(Box X) ← b(X)
    UNBOX,  // a(X) ← b(Box X) (null 이면 트랩)
    // 흐름
    JMP,   // → b|c<<16
    JMPT,  // a 가 참이면 → b|c<<16
    JMPF,  // a 가 거짓이면 → b|c<<16
    CALL,  // a(결과 · kNoReg) ← functions[b](c …)
    CALLH, // a(결과 · kNoReg) ← imports[b](c …) — Host 함수, 주인 (ctx) 이 첫 인자
    RET,   // void 함수
    RETV,  // return a
    TRAP,  // 트랩 (a = TrapCode)
    LOADK, // a(ComponentRef) ← components[b] (v0.3 M3-T3 — 값은 fnv1a64(id) = stableId)
    Count,
};

enum class TrapCode : u16 {
    Unreachable = 1, // 함수 끝에 닿았다 (검사기가 막으므로 생기면 컴파일러 버그)
};

struct Insn {
    Op op = Op::NOP;
    u8 pad = 0; // 0 이어야 한다
    u16 a = 0;
    u16 b = 0;
    u16 c = 0;

    [[nodiscard]] u32 wide() const noexcept { return static_cast<u32>(b) | (static_cast<u32>(c) << 16); }
};
static_assert(sizeof(Insn) == 8);

// Host 함수 import — 이름으로 부른다 (ADR-0057 2). params 의 첫째는 주인 타입 (ctx)
struct HostImport {
    std::string id;
    std::vector<api::AbiType> params;
    api::AbiType result = api::AbiType::Void;
};

struct Function {
    std::string name;        // "scripts/a.ts:clamp" (진단 · 역어셈블)
    std::vector<u32> params; // 타입 번호
    u32 result = 0;          // 타입 번호 (Void 됨)
    std::vector<u32> regs;   // 레지스터마다 타입 — 앞 params.size() 개는 매개변수와 같다
    std::vector<Insn> code;
};

struct Export {
    std::string module; // "scripts/a.ts"
    std::string name;
    u32 function = 0;
};

struct Module {
    u32 format = kFormatVersion;
    u32 hostApiVersion = api::kHostApiVersion;
    u64 hostApiFingerprint = 0;
    std::vector<TypeDesc> types;
    std::vector<std::string> strings;
    std::vector<f64> numbers;
    std::vector<HostImport> imports;
    std::vector<std::string> components; // 스크립트가 쓰는 컴포넌트 id ("rpg.mana") — LOADK 가 가리킨다
    std::vector<Function> functions;
    std::vector<Export> exports;

    [[nodiscard]] const Export* findExport(std::string_view module, std::string_view name) const noexcept;
};

[[nodiscard]] std::string_view opName(Op op) noexcept;
[[nodiscard]] RegClass regClass(const Module& m, u32 type) noexcept;
[[nodiscard]] std::string typeName(const Module& m, u32 type);

[[nodiscard]] std::vector<u8> serialize(const Module& m);
// 바이트열 → 모듈 (모양 · 상한만 본다 — 뜻은 verify). 남는 바이트 · 잘림 · 상한 넘음은 ParseError
[[nodiscard]] Expected<Module> deserialize(std::span<const u8> bytes);
// 사람이 읽는 목록 (시험의 기준)
[[nodiscard]] std::string disassemble(const Module& m);

} // namespace sbx::script::bc
