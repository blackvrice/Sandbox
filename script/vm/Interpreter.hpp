#pragma once
// SandboxScript 인터프리터. ADR-0056 4 · 5, ADR-0058, docs/19-SANDBOXSCRIPT.md 13장.
//
// 검증 (bc::verify) 을 통과한 모듈만 돌린다 — 칸의 해석은 검증기가 보장한 레지스터 타입을 믿는다.
// 호출 하나 (call) 가 실행 단위다: 그 호출의 객체는 아레나에 만들고 다음 call 이 처음에 통째로 버린다 (GC 없음 — 결과의
// 문자열 · 배열은 다음 call 전까지 읽을 수 있다). 실행 중에만 아는 잘못은 트랩: 그 호출을 멈추고 CallResult 로 알린다
// (예외 없음). 예산: 연료 (명령마다 1 + 무거운 명령은 더), 호출 깊이, 레지스터 칸, 아레나 바이트, 배열 길이, 문자열
// 바이트. Host 함수 (CALLH) 는 IHost 로 — 엔진 바인딩은 core/script/ScriptHost, 시험은 가짜 Host.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "script/vm/Bytecode.hpp"

namespace sbx::script::vm {

// 8 바이트 칸 (ADR-0058 3)
struct Value {
    u64 bits = 0;

    [[nodiscard]] static constexpr Value i32v(i32 v) noexcept { return {static_cast<u64>(static_cast<u32>(v))}; }
    [[nodiscard]] static Value f64v(f64 v) noexcept;
    [[nodiscard]] static constexpr Value boolean(bool v) noexcept { return {v ? 1u : 0u}; }
    [[nodiscard]] static constexpr Value null() noexcept { return {0}; }
    [[nodiscard]] static constexpr Value opaque(u64 handle) noexcept { return {handle}; }

    [[nodiscard]] constexpr i32 asI32() const noexcept { return static_cast<i32>(static_cast<u32>(bits)); }
    [[nodiscard]] f64 asF64() const noexcept;
    [[nodiscard]] constexpr bool asBool() const noexcept { return bits != 0; }
    [[nodiscard]] constexpr bool isNull() const noexcept { return bits == 0; }
};

enum class Trap : u8 {
    None,
    Unreachable,     // TRAP 명령
    NullReference,   // null 의 필드 · 원소 · 상자
    IndexOutOfRange, // 배열 첨자
    NotAnIndex,      // 정수가 아닌 첨자 (xs[1.5])
    DivideByZero,    // i32 % 0 · idiv(a, 0)
    OutOfFuel,       // 연료 예산
    StackOverflow,   // 호출 깊이 · 레지스터 칸
    OutOfMemory,     // 아레나 · 배열 길이 · 문자열 바이트
    HostError,       // Host 함수가 실패했다 (Host 가 없음 포함)
    BadCall,         // 바깥에서 부른 함수 번호 · 인자 수가 틀렸다
};

[[nodiscard]] std::string_view trapName(Trap t) noexcept;

struct Limits {
    u64 fuel = 1'000'000;
    u32 maxDepth = 128;
    u32 maxStackSlots = 1u << 18;  // 모든 프레임의 레지스터 칸 합
    usize maxHeapBytes = 4u << 20; // 아레나 (객체 머리 + 칸 + 문자열 바이트)
    u32 maxArrayLength = 1u << 20;
    u32 maxStringBytes = 1u << 20;
    u32 maxHostCalls = 1u << 16; // 호출 하나의 Host 함수 호출 (v0.3 M4-T3, ADR-0064 — 넘으면 HostError)
};

struct CallResult {
    Trap trap = Trap::None;
    Value value;      // 결과 (void 면 0)
    u32 function = 0; // 트랩이 난 함수 · 자리
    u32 pc = 0;
    std::string message; // 트랩 설명 (Host 오류의 글 포함)
    u64 fuelUsed = 0;

    [[nodiscard]] bool ok() const noexcept { return trap == Trap::None; }
};

// Host 함수에 넘기는 인자 — 문자열은 내용으로 (VM 의 참조를 밖으로 내보내지 않는다 — ADR-0057 4).
// ComponentData (구조체 T) 는 칸 목록으로 펼친다: 필드 이름 순, 중첩 구조체 (Vec2) 는 그 자리에서 펼치고, 상자 (Entity
// | null) 는 null = 0 · 아니면 안쪽 값 (ADR-0060)
struct HostArg {
    api::AbiType type = api::AbiType::Void;
    Value value;
    std::string_view text;         // String 일 때만
    std::span<const Value> fields; // ComponentData 일 때만
};
struct HostResult {
    bool ok = true;
    Value value; // ComponentDataOrNull 이면 0 = null, 아니면 1 (칸은 values)
    std::string error;
    std::vector<Value> values; // ComponentDataOrNull = 펼친 칸 (HostArg::fields 와 같은 순서), EntityList = 원소
    u64 extraFuel = 0;         // 부른 쪽이 더 낼 연료 (질의 — 결과 개체마다)
};

class IHost {
public:
    virtual ~IHost() = default;
    virtual HostResult call(const api::HostFunction& fn, std::span<const HostArg> args) = 0;
};

class Interpreter {
public:
    // module 은 검증된 것이어야 한다 (Debug 에서 다시 검증한다). module · host 는 이 객체보다 오래 살아야 한다
    Interpreter(const bc::Module& module, IHost* host, Limits limits = {});

    // 함수 하나를 부른다 — 인자는 기본 타입 · 불투명 값 (참조는 넘길 수 없다: 이 호출의 아레나가 새로 시작한다)
    CallResult call(u32 function, std::span<const Value> args);
    CallResult call(std::string_view module, std::string_view exportName, std::span<const Value> args);
    // 구조체 매개변수를 받는 함수 (시스템의 onEvent — ADR-0063): 구조체 자리의 args 값은 쓰지 않고, 구조체 매개변수마다
    // structFields 에서 칸을 차례로 읽어 이 호출의 아레나에 만든다 (필드 이름 순 — Host 결과와 같은 잎 규칙). 칸이
    // 모자라거나 남거나 모양이 다르면 BadCall
    CallResult call(u32 function, std::span<const Value> args, std::span<const Value> structFields);

    // 마지막 call 의 아레나 읽기 (다음 call 전까지)
    [[nodiscard]] std::string_view stringOf(Value v) const;
    [[nodiscard]] usize arrayLength(Value v) const;
    [[nodiscard]] Value arrayAt(Value v, usize i) const;
    [[nodiscard]] Value fieldOf(Value v, usize slot) const;
    [[nodiscard]] Value unbox(Value v) const;

    [[nodiscard]] usize heapBytes() const noexcept { return m_heapBytes; }
    [[nodiscard]] const Limits& limits() const noexcept { return m_limits; }
    void setLimits(const Limits& l) noexcept { m_limits = l; }

private:
    struct Object {
        enum class Kind : u8 { String, Array, Struct, Box } kind = Kind::String;
        std::string text;
        std::vector<Value> slots;
    };
    struct Frame {
        u32 function = 0;
        u32 pc = 0;
        u32 base = 0;
        u16 resultReg = bc::kNoReg; // 부른 쪽 프레임의 결과 레지스터
    };

    CallResult run(u32 function, std::span<const Value> args, const std::span<const Value>* structFields = nullptr);
    bool alloc(Object o, usize bytes, Value& out);
    // Host 칸 ↔ 구조체 (ADR-0060). 잎은 i32 · f64 · bool · 불투명 · 상자 (불투명) 와 중첩 구조체 (깊이 4) 만 — 그 밖의
    // 모양 (문자열 · 배열 · 재귀) 은 BadShape: 믿지 않는 바이트코드가 Host 값을 참조로 읽게 하지 않는다
    enum class Marshal : u8 { Ok, BadShape, OutOfMemory };
    Marshal buildStruct(u32 type, std::span<const Value> values, usize& pos, u32 depth, Value& out);
    [[nodiscard]] bool flattenStruct(u32 type, Value v, std::vector<Value>& out, u32 depth) const;
    [[nodiscard]] const Object* object(Value v) const;
    Object* object(Value v);

    const bc::Module& M;
    IHost* m_host;
    Limits m_limits;
    std::vector<u32> m_importToHost; // import 자리 → api::kHostFunctions 자리
    std::vector<u64> m_componentIds; // components 자리 → stableId (fnv1a64(id))
    std::vector<Object> m_objects;
    usize m_heapBytes = 0;
    std::vector<Value> m_regs;
    std::vector<Frame> m_frames;
    std::vector<HostArg> m_hostArgs;
    std::vector<Value> m_hostFields;
};

} // namespace sbx::script::vm
