#pragma once
// SandboxScript 인터프리터. ADR-0056 4 · 5, ADR-0058, docs/19-SANDBOXSCRIPT.md 13장.
//
// 검증 (bc::verify) 을 통과한 모듈만 돌린다 — 칸의 해석은 검증기가 보장한 레지스터 타입을 믿는다.
// 호출 하나 (call) 가 실행 단위다: 그 호출의 객체는 아레나에 만들고 다음 call 이 처음에 통째로 버린다 (GC 없음 — 결과의
// 문자열 · 배열은 다음 call 전까지 읽을 수 있다). 실행 중에만 아는 잘못은 트랩: 그 호출을 멈추고 CallResult 로 알린다
// (예외 없음). 예산: 연료 (명령마다 1 + 무거운 명령은 더), 호출 깊이, 레지스터 칸, 아레나 바이트, 배열 길이, 문자열
// 바이트. Host 함수 (CALLH) 는 IHost 로 — 엔진 바인딩은 core/script/ScriptHost, 시험은 가짜 Host.

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "script/vm/Bytecode.hpp"
#include "script/vm/ExecutorKind.hpp"

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

// 트랩 때의 호출 경로 한 칸 (v0.9 M6-T4 조각 3) — pc = 그 함수에서 실행 중이던 명령 (바깥 프레임은 부른 CALL · INTR
// 자리)
struct TrapFrame {
    u32 function = 0;
    u32 pc = 0;
    friend bool operator==(const TrapFrame&, const TrapFrame&) = default;
};
inline constexpr usize kMaxTrapFrames = 16; // 그보다 깊으면 안쪽 15 개 + 맨 바깥 (호출을 시작한 함수)

struct CallResult {
    Trap trap = Trap::None;
    Value value;      // 결과 (void 면 0)
    u32 function = 0; // 트랩이 난 함수 · 자리
    u32 pc = 0;
    // 트랩이면 호출 경로 — 안쪽 (트랩 자리 = function · pc) 부터 바깥 (부른 시스템 콜백) 까지, kMaxTrapFrames 까지.
    // 성공이면 비었다
    std::vector<TrapFrame> stack{};
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
    std::vector<Value> values{}; // ComponentDataOrNull = 펼친 칸 (HostArg::fields 와 같은 순서), EntityList = 원소
    u64 extraFuel = 0;           // 부른 쪽이 더 낼 연료 (질의 — 결과 개체마다)
    std::string text{};          // 결과 String (v0.7 M4-T1 — 인터프리터가 아레나에 문자열로 만든다)
};

// 함수별 프로파일 (v0.9 M6-T4 조각 2 — ADR-0125): 함수에 들어간 수와 자기 연료 (그 함수의 프레임이 맨 위일 때 쓴 연료 —
// 명령 · 내장의 원소 비용 · Host 호출 비용). 호출 하나의 자기 연료 합 = CallResult::fuelUsed (트랩 · 연료 끝 포함).
// 연료는 결정적이라 프로파일도 결정적 — 시간 (벽시계) 은 싣지 않는다
struct FunctionProfile {
    u64 calls = 0;
    u64 selfFuel = 0;
};

class Interpreter;
class CodePatches;

// 디버그 훅 (v0.9 ST-06-7 — docs/27-SCRIPT-IDE.md 6장, ADR-0132): 디버그 세션의 모듈 사본에 놓인 BRK 를 만나면
// 인터프리터가 부른다 (그 명령 전, 연료는 그 명령 몫을 이미 냈다). 훅 안에서는 vm 의 프레임 · 레지스터를 읽기만 한다
// (call 금지 — 같은 호출 안이다). 훅은 CodePatches 의 BRK 를 놓고 · 거둘 수 있다 (단계 실행). 돌아오면 원래 명령을
// 돌린다
class IDebugHook {
public:
    virtual ~IDebugHook() = default;
    virtual void onBreak(const Interpreter& vm, u32 function, u32 pc) = 0;
};

// 멈춘 동안의 프레임 한 칸 — 0 = 맨 바깥 (부른 콜백). pc = 맨 위는 지금 명령, 바깥은 부른 CALL · INTR 자리
struct FrameInfo {
    u32 function = 0;
    u32 pc = 0;
    u32 base = 0;
};

class IHost {
public:
    virtual ~IHost() = default;
    virtual HostResult call(const api::HostFunction& fn, std::span<const HostArg> args) = 0;
};

// 스크립트 실행 경계 (v0.9 M6-T4 조각 4 — ADR-0081 1, ADR-0127). 모듈 하나를 받아 함수를 부른다 — 결과 · 트랩 · 연료 ·
// 메모리 · 함수별 프로파일. 지금 구현은 Interpreter 하나 (makeScriptExecutor — script/vm/ScriptExecutor). JIT/AOT
// 실행자는 같은 계약 (server: 결과 · 트랩 · 연료 · 호출 경로가 비트 단위로 Interpreter 와 같다 — describeCallDifference
// 가 빈 글) 을 지켜야 들어온다. 엔진 (core/script) 은 이 인터페이스만 쥔다
class IScriptExecutor {
public:
    virtual ~IScriptExecutor() = default;
    [[nodiscard]] virtual ExecutorKind kind() const noexcept = 0;
    // 기본 · 불투명 인자의 호출 (Interpreter::call 과 같은 뜻)
    virtual CallResult call(u32 function, std::span<const Value> args) = 0;
    // 구조체 매개변수 (onEvent 의 GameEvent) — Interpreter::call 의 칸 · 글 규칙
    virtual CallResult call(u32 function, std::span<const Value> args, std::span<const Value> structFields,
                            std::span<const std::string> structTexts) = 0;
    [[nodiscard]] virtual const Limits& limits() const noexcept = 0;
    virtual void setLimits(const Limits& l) noexcept = 0;
    // 마지막 호출이 쓴 아레나 바이트
    [[nodiscard]] virtual usize heapBytes() const noexcept = 0;
    // 함수 번호마다 쌓인 프로파일 (ADR-0125) · 0 으로
    [[nodiscard]] virtual const std::vector<FunctionProfile>& profile() const noexcept = 0;
    virtual void resetProfile() = 0;
};

class Interpreter final : public IScriptExecutor {
public:
    // module 은 검증된 것이어야 한다 (Debug 에서 다시 검증한다). module · host 는 이 객체보다 오래 살아야 한다
    Interpreter(const bc::Module& module, IHost* host, Limits limits = {});

    // 함수 하나를 부른다 — 인자는 기본 타입 · 불투명 값 (참조는 넘길 수 없다: 이 호출의 아레나가 새로 시작한다)
    [[nodiscard]] ExecutorKind kind() const noexcept override { return ExecutorKind::Interpreter; }
    CallResult call(u32 function, std::span<const Value> args) override;
    CallResult call(std::string_view module, std::string_view exportName, std::span<const Value> args);
    // 구조체 매개변수를 받는 함수 (시스템의 onEvent — ADR-0063): 구조체 자리의 args 값은 쓰지 않고, 구조체 매개변수마다
    // structFields 에서 칸을 차례로 읽어 이 호출의 아레나에 만든다 (필드 이름 순 — Host 결과와 같은 잎 규칙). 칸이
    // 모자라거나 남거나 모양이 다르면 BadCall. 잎이 string 이면 칸 = structTexts 의 자리 (i32), 잎이 불투명 값의
    // 배열이면 칸 = 개수 n 다음 원소 n 칸 (GameEvent 의 action · targets — v0.5 M2-T5 (2))
    CallResult call(u32 function, std::span<const Value> args, std::span<const Value> structFields,
                    std::span<const std::string> structTexts = {}) override;

    // 마지막 call 의 아레나 읽기 (다음 call 전까지)
    [[nodiscard]] std::string_view stringOf(Value v) const;
    [[nodiscard]] usize arrayLength(Value v) const;
    [[nodiscard]] Value arrayAt(Value v, usize i) const;
    [[nodiscard]] Value fieldOf(Value v, usize slot) const;
    [[nodiscard]] Value unbox(Value v) const;

    [[nodiscard]] usize heapBytes() const noexcept override { return m_heapBytes; }
    // 함수 번호마다 쌓인 프로파일 (call 을 넘어 쌓인다 — resetProfile 로 0)
    [[nodiscard]] const std::vector<FunctionProfile>& profile() const noexcept override { return m_profile; }
    void resetProfile() override;
    [[nodiscard]] const Limits& limits() const noexcept override { return m_limits; }
    void setLimits(const Limits& l) noexcept override { m_limits = l; }

    // --- 디버그 (v0.9 ST-06-7 — ADR-0132) ---
    // BRK 를 만나면 hook 을 부르고 patches 의 원래 명령을 돌린다. 둘 다 nullptr 면 뗀다 (BRK 를 만나면 트랩). 디버거가
    // 없을 때 실행 루프의 비용은 0 이다 — BRK 는 switch 의 칸 하나 (명령마다 검사가 없다)
    void attachDebugger(IDebugHook* hook, const CodePatches* patches) noexcept {
        m_debugHook = hook;
        m_patches = patches;
    }
    [[nodiscard]] const bc::Module& module() const noexcept { return M; }
    // 훅 안에서만 뜻이 있다 (실행 중의 프레임)
    [[nodiscard]] usize frameCount() const noexcept { return m_frames.size(); }
    [[nodiscard]] FrameInfo frameAt(usize i) const;
    [[nodiscard]] Value regAt(usize frame, u16 reg) const;
    // Map · Set 의 살아 있는 항목 수, 컬렉션 · 배열의 원소 수, 문자열 바이트 (그 밖은 0)
    [[nodiscard]] usize containerSize(Value v) const;

private:
    // Map · Set (v0.5 M8-T4) — 넣은 차례의 항목 (지운 것은 alive = 0 으로 남는다 — 반복 커서가 그대로) + 선형 탐사 색인
    // (0 = 빈 칸, 아니면 항목 + 1). 해시는 고정 (keyHash — 주소 · 씨앗 없음)
    struct HashData {
        std::vector<Value> keys;
        std::vector<Value> values; // Map 만
        std::vector<u8> alive;
        std::vector<u32> index; // 크기 2^k
        usize live = 0;
        usize used = 0; // 색인에서 차 있는 칸
        bc::TypeKind keyKind = bc::TypeKind::I32;
    };
    // "sandbox" 컬렉션 (v0.5 M8-T5) — sub = CollKind. Stack · 힙 · PriorityQueue = buf 를 배열 · 이진 힙으로 (cap =
    // 논리 용량), Queue · Deque = buf 가 고리 (크기 2^k, head · count), PriorityQueue 는 prio · order (같은 우선순위 =
    // 넣은 차례), BitSet = buf 가 64 비트 낱말 (bits = 크기)
    struct SeqData {
        std::vector<Value> buf;
        std::vector<f64> prio;
        std::vector<u64> order;
        u64 nextOrder = 0;
        usize head = 0;
        usize count = 0;
        usize cap = 0;
        usize bits = 0;
        u8 sub = 0;
        // Graph (v0.5 M8-T7) — count = 노드 수, adj = 노드마다 (끝, 가중치) 넣은 차례, edges = 칸 수 (무방향은 둘)
        std::vector<std::vector<std::pair<u32, f64>>> adj;
        usize edges = 0;
        bool directed = false;
    };
    struct Object {
        enum class Kind : u8 { String, Array, Struct, Box, Map, Set, Coll } kind = Kind::String;
        std::string text;
        std::vector<Value> slots;
        std::unique_ptr<HashData> hash; // Map · Set 만
        std::unique_ptr<SeqData> seq;   // 컬렉션만
    };
    struct Frame {
        u32 function = 0;
        u32 pc = 0;
        u32 base = 0;
        u16 resultReg = bc::kNoReg; // 부른 쪽 프레임의 결과 레지스터
    };

    CallResult run(u32 function, std::span<const Value> args, const std::span<const Value>* structFields = nullptr,
                   std::span<const std::string> structTexts = {});
    // 실행 루프 — 프레임 깊이가 stopDepth 로 돌아올 때까지 (그 RET 의 값 = out). 트랩이면 m_res 를 채우고 false.
    // run 은 0 으로, 콜백 (invoke — v0.5 M8-T3) 은 그 때의 깊이로 부른다 (같은 연료 · 깊이 · 칸 예산)
    bool loop(usize stopDepth, Value& retOut);
    bool enterFrame(u32 function, u16 resultReg);
    // 함수 fn 을 args 로 끝까지 (내장의 콜백). 트랩이면 m_res 가 채워지고 false
    bool invoke(u32 fn, std::span<const Value> args, Value& out);
    bool alloc(Object o, usize bytes, Value& out);
    bool newString(std::string s, Value& out);
    bool newArray(std::vector<Value> slots, Value& out);
    // INTR (ADR-0085 — script/vm/Intrinsics). 실패면 trap · msg 를 채우고 false. fuel = run 의 남은 연료 (원소 ·
    // 비교마다 여기서 뺀다)
    bool intrinsic(const bc::Insn& in, const bc::Function& fn, Value* R, u64& fuel, Trap& trap, std::string& msg);
    // Map · Set 내장 (v0.5 M8-T4)
    bool collectionIntrinsic(const bc::Insn& in, const bc::Function& fn, Value* R, u64& fuel, Trap& trap,
                             std::string& msg);
    bool seqIntrinsic(const bc::Insn& in, const bc::Function& fn, Value* R, u64& fuel, Trap& trap, std::string& msg);
    bool graphIntrinsic(const bc::Insn& in, const bc::Function& fn, Value* R, u64& fuel, Trap& trap, std::string& msg);
    // 수학 · vec2 (v0.5 M8-T6 — 받는 쪽이 없다)
    bool valueIntrinsic(const bc::Insn& in, const bc::Function& fn, Value* R, u64& fuel, Trap& trap, std::string& msg);
    [[nodiscard]] u64 keyHash(bc::TypeKind kind, Value v) const;
    [[nodiscard]] bool keyEqual(bc::TypeKind kind, Value a, Value b) const;
    // 배열 원소 같음 — indexOf 는 === (NaN 은 무엇과도 다르다), includes 는 SameValueZero (NaN = NaN)
    [[nodiscard]] bool elemEqual(u32 elemType, Value a, Value b, bool sameValueZero) const;
    // Host 칸 ↔ 구조체 (ADR-0060). 잎은 i32 · f64 · bool · 불투명 · 상자 (불투명) 와 중첩 구조체 (깊이 4) 만 — 그 밖의
    // 모양 (문자열 · 배열 · 재귀) 은 BadShape: 믿지 않는 바이트코드가 Host 값을 참조로 읽게 하지 않는다
    enum class Marshal : u8 { Ok, BadShape, OutOfMemory };
    Marshal buildStruct(u32 type, std::span<const Value> values, std::span<const std::string> texts, usize& pos,
                        u32 depth, Value& out);
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
    // 호출 하나의 실행 상태 (loop 가 중첩되어도 하나 — 콜백은 같은 예산)
    CallResult m_res;
    u64 m_budget = 0;
    u64 m_fuel = 0;
    u32 m_hostCalls = 0;
    // 프로파일: 프레임이 바뀔 때만 (들어감 · RET · 호출 끝) 그때까지 쓴 연료를 맨 위 프레임의 함수에 — 실행 루프는
    // 그대로
    std::vector<FunctionProfile> m_profile;
    u64 m_profileMark = 0; // 마지막으로 나눈 때의 m_fuel
    void chargeTop() noexcept;
    // 디버그 (BRK)
    IDebugHook* m_debugHook = nullptr;
    const CodePatches* m_patches = nullptr;
    bc::Insn m_brkInsn{}; // BRK 자리의 원래 명령 (훅이 그 자리를 거둬도 이 프레임은 이것을 돌린다)
    // 트랩 자리 (topPc = 맨 위 프레임의 pc) 로 m_res.stack 을 채운다 — 바깥 프레임의 pc 는 돌아갈 자리 - 1 (부른 명령)
    void captureStack(u32 topPc);
};

} // namespace sbx::script::vm
