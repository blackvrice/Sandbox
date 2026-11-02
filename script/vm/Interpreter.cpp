#include "script/vm/Interpreter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>

#include "foundation/assert/Assert.hpp"
#include "foundation/hash/Fnv1a.hpp"
#include "script/api/MathBuiltins.hpp"
#include "script/vm/Intrinsics.hpp"
#include "script/vm/Numbers.hpp"
#include "script/vm/Verifier.hpp"

namespace sbx::script::vm {

namespace {

// 참조 칸: 0 = null, 1 … = 아레나 객체 (자리 + 1), 최상위 비트 = 상수 문자열 (자리)
constexpr u32 kConstStringBit = 0x8000'0000u;

constexpr usize kObjectHeaderBytes = 32; // 아레나 계산용 어림 (객체 머리)

} // namespace

Value Value::f64v(f64 v) noexcept {
    return {std::bit_cast<u64>(v)};
}
f64 Value::asF64() const noexcept {
    return std::bit_cast<f64>(bits);
}

std::string_view trapName(Trap t) noexcept {
    switch (t) {
    case Trap::None:
        return "None";
    case Trap::Unreachable:
        return "Unreachable";
    case Trap::NullReference:
        return "NullReference";
    case Trap::IndexOutOfRange:
        return "IndexOutOfRange";
    case Trap::NotAnIndex:
        return "NotAnIndex";
    case Trap::DivideByZero:
        return "DivideByZero";
    case Trap::OutOfFuel:
        return "OutOfFuel";
    case Trap::StackOverflow:
        return "StackOverflow";
    case Trap::OutOfMemory:
        return "OutOfMemory";
    case Trap::HostError:
        return "HostError";
    case Trap::BadCall:
        return "BadCall";
    }
    return "?";
}

Interpreter::Interpreter(const bc::Module& module, IHost* host, Limits limits)
    : M(module), m_host(host), m_limits(limits) {
    SBX_ASSERT(bc::verify(module).ok(), "Interpreter 는 검증된 모듈만 받는다 (bc::verify · loadModule)");
    for (const bc::HostImport& im : M.imports) {
        const api::HostFunction* h = api::findHostFunction(im.id);
        m_importToHost.push_back(h == nullptr ? 0 : static_cast<u32>(api::hostFunctionIndex(*h)));
    }
    for (const std::string& id : M.components) {
        m_componentIds.push_back(fnv1a64(id));
    }
}

const Interpreter::Object* Interpreter::object(Value v) const {
    const u32 h = static_cast<u32>(v.bits);
    if (h == 0 || (h & kConstStringBit) != 0 || h > m_objects.size()) {
        return nullptr;
    }
    return &m_objects[h - 1];
}
Interpreter::Object* Interpreter::object(Value v) {
    return const_cast<Object*>(static_cast<const Interpreter*>(this)->object(v));
}

std::string_view Interpreter::stringOf(Value v) const {
    const u32 h = static_cast<u32>(v.bits);
    if ((h & kConstStringBit) != 0) {
        const u32 k = h & ~kConstStringBit;
        return k < M.strings.size() ? std::string_view(M.strings[k]) : std::string_view{};
    }
    const Object* o = object(v);
    return o == nullptr ? std::string_view{} : std::string_view(o->text);
}
usize Interpreter::arrayLength(Value v) const {
    const Object* o = object(v);
    return o == nullptr ? 0 : o->slots.size();
}
Value Interpreter::arrayAt(Value v, usize i) const {
    const Object* o = object(v);
    return o == nullptr || i >= o->slots.size() ? Value{} : o->slots[i];
}
Value Interpreter::fieldOf(Value v, usize slot) const {
    return arrayAt(v, slot);
}
Value Interpreter::unbox(Value v) const {
    return arrayAt(v, 0);
}

bool Interpreter::alloc(Object o, usize bytes, Value& out) {
    if (m_heapBytes + bytes > m_limits.maxHeapBytes || m_objects.size() >= kConstStringBit - 1) {
        return false;
    }
    m_heapBytes += bytes;
    m_objects.push_back(std::move(o));
    out = Value{static_cast<u64>(m_objects.size())};
    return true;
}

bool Interpreter::newString(std::string s, Value& out) {
    if (s.size() > m_limits.maxStringBytes) {
        return false;
    }
    const usize bytes = kObjectHeaderBytes + s.size();
    Object o;
    o.kind = Object::Kind::String;
    o.text = std::move(s);
    return alloc(std::move(o), bytes, out);
}

bool Interpreter::newArray(std::vector<Value> slots, Value& out) {
    if (slots.size() > m_limits.maxArrayLength) {
        return false;
    }
    const usize bytes = kObjectHeaderBytes + slots.size() * 8;
    Object o;
    o.kind = Object::Kind::Array;
    o.slots = std::move(slots);
    return alloc(std::move(o), bytes, out);
}

namespace {
constexpr u32 kMaxMarshalDepth = 4;
} // namespace

Interpreter::Marshal Interpreter::buildStruct(u32 type, std::span<const Value> values, usize& pos, u32 depth,
                                              Value& out) {
    const bc::TypeDesc& t = M.types[type];
    if (t.kind != bc::TypeKind::Struct || depth > kMaxMarshalDepth) {
        return Marshal::BadShape;
    }
    // 안쪽 객체를 먼저 만든다 (m_objects 가 자라도 칸 값만 들고 있다)
    std::vector<Value> slots(t.fieldTypes.size());
    for (usize i = 0; i < t.fieldTypes.size(); ++i) {
        const bc::TypeDesc& f = M.types[t.fieldTypes[i]];
        switch (f.kind) {
        case bc::TypeKind::I32:
        case bc::TypeKind::F64:
        case bc::TypeKind::Bool:
        case bc::TypeKind::Opaque:
            if (pos >= values.size()) {
                return Marshal::BadShape;
            }
            slots[i] = values[pos++];
            break;
        case bc::TypeKind::Box: {
            if (M.types[f.elem].kind != bc::TypeKind::Opaque || pos >= values.size()) {
                return Marshal::BadShape;
            }
            const Value v = values[pos++];
            if (!v.isNull()) {
                Object o;
                o.kind = Object::Kind::Box;
                o.slots.push_back(v);
                if (!alloc(std::move(o), kObjectHeaderBytes + 8, slots[i])) {
                    return Marshal::OutOfMemory;
                }
            }
            break;
        }
        case bc::TypeKind::Struct:
            if (const Marshal m = buildStruct(t.fieldTypes[i], values, pos, depth + 1, slots[i]); m != Marshal::Ok) {
                return m;
            }
            break;
        default:
            return Marshal::BadShape;
        }
    }
    Object o;
    o.kind = Object::Kind::Struct;
    o.slots = std::move(slots);
    const usize bytes = kObjectHeaderBytes + o.slots.size() * 8;
    return alloc(std::move(o), bytes, out) ? Marshal::Ok : Marshal::OutOfMemory;
}

bool Interpreter::flattenStruct(u32 type, Value v, std::vector<Value>& out, u32 depth) const {
    const bc::TypeDesc& t = M.types[type];
    const Object* o = object(v);
    if (t.kind != bc::TypeKind::Struct || depth > kMaxMarshalDepth || o == nullptr || o->kind != Object::Kind::Struct ||
        o->slots.size() != t.fieldTypes.size()) {
        return false;
    }
    for (usize i = 0; i < t.fieldTypes.size(); ++i) {
        const bc::TypeDesc& f = M.types[t.fieldTypes[i]];
        const Value s = o->slots[i];
        switch (f.kind) {
        case bc::TypeKind::I32:
        case bc::TypeKind::F64:
        case bc::TypeKind::Bool:
        case bc::TypeKind::Opaque:
            out.push_back(s);
            break;
        case bc::TypeKind::Box: {
            if (M.types[f.elem].kind != bc::TypeKind::Opaque) {
                return false;
            }
            const Object* b = object(s);
            out.push_back(b == nullptr ? Value::null() : b->slots[0]);
            break;
        }
        case bc::TypeKind::Struct:
            if (!flattenStruct(t.fieldTypes[i], s, out, depth + 1)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

CallResult Interpreter::call(u32 function, std::span<const Value> args) {
    return run(function, args);
}

CallResult Interpreter::call(u32 function, std::span<const Value> args, std::span<const Value> structFields) {
    return run(function, args, &structFields);
}

CallResult Interpreter::call(std::string_view module, std::string_view exportName, std::span<const Value> args) {
    const bc::Export* e = M.findExport(module, exportName);
    if (e == nullptr) {
        CallResult r;
        r.trap = Trap::BadCall;
        r.message = std::format("export {}:{} 가 없다", module, exportName);
        return r;
    }
    return run(e->function, args);
}

CallResult Interpreter::run(u32 entry, std::span<const Value> args, const std::span<const Value>* structFields) {
    // 새 호출 = 새 아레나 (ADR-0056 4)
    m_objects.clear();
    m_heapBytes = 0;
    m_regs.clear();
    m_frames.clear();
    m_regs.reserve(m_limits.maxStackSlots); // 프레임 칸의 포인터가 호출 동안 그대로이도록 (재할당 없음)

    CallResult res;
    if (entry >= M.functions.size() || args.size() != M.functions[entry].params.size()) {
        res.trap = Trap::BadCall;
        res.message = entry >= M.functions.size() ? std::format("함수 {} 가 없다", entry)
                                                  : std::format("인자 {} 개 — 함수는 {} 개를 받는다", args.size(),
                                                                M.functions[entry].params.size());
        return res;
    }

    if (!enterFrame(entry, bc::kNoReg)) {
        res.trap = Trap::StackOverflow;
        res.message = "첫 프레임이 레지스터 칸 예산을 넘는다";
        return res;
    }
    for (usize i = 0; i < args.size(); ++i) {
        m_regs[i] = args[i];
    }
    if (structFields != nullptr) {
        // 구조체 매개변수 — 새 아레나에 만든다 (프레임은 이미 잡았다: 객체는 m_objects 에, 칸에는 참조만)
        usize pos = 0;
        const bc::Function& fn = M.functions[entry];
        for (usize i = 0; i < fn.params.size(); ++i) {
            if (M.types[fn.params[i]].kind != bc::TypeKind::Struct) {
                continue;
            }
            const Marshal m = buildStruct(fn.params[i], *structFields, pos, 0, m_regs[i]);
            if (m != Marshal::Ok) {
                res.trap = m == Marshal::OutOfMemory ? Trap::OutOfMemory : Trap::BadCall;
                res.message = std::format("매개변수 {} 의 구조체를 만들 수 없다 (칸 모양이 다르다)", i);
                return res;
            }
        }
        if (pos != structFields->size()) {
            res.trap = Trap::BadCall;
            res.message = std::format("구조체 칸 {} 개 — 매개변수는 {} 개를 쓴다", structFields->size(), pos);
            return res;
        }
    }

    m_res = CallResult{};
    m_budget = m_limits.fuel;
    m_fuel = m_budget;
    m_hostCalls = 0;
    Value out;
    if (loop(0, out)) {
        m_res.value = out;
        m_res.fuelUsed = m_budget - m_fuel;
    }
    return m_res;
}

bool Interpreter::enterFrame(u32 f, u16 resultReg) {
    const bc::Function& fn = M.functions[f];
    if (m_frames.size() >= m_limits.maxDepth || m_regs.size() + fn.regs.size() > m_limits.maxStackSlots) {
        return false;
    }
    m_frames.push_back({f, 0, static_cast<u32>(m_regs.size()), resultReg});
    m_regs.resize(m_regs.size() + fn.regs.size(), Value{});
    return true;
}

bool Interpreter::invoke(u32 fn, std::span<const Value> args, Value& out) {
    const usize depth = m_frames.size();
    if (!enterFrame(fn, bc::kNoReg)) {
        m_res.trap = Trap::StackOverflow;
        m_res.function = m_frames.back().function;
        m_res.message = std::format("호출 깊이 {} · 레지스터 칸 예산을 넘는다 (콜백)", m_frames.size());
        m_res.fuelUsed = m_budget - m_fuel;
        return false;
    }
    Value* regs = m_regs.data() + m_frames.back().base;
    for (usize i = 0; i < args.size(); ++i) {
        regs[i] = args[i];
    }
    return loop(depth, out);
}

bool Interpreter::loop(usize stopDepth, Value& retOut) {
    // m_regs 는 run 이 maxStackSlots 로 잡아 두었다 — 중첩된 루프가 프레임을 더해도 R 은 그대로 (재할당 없음)
    const bc::Function* F = &M.functions[m_frames.back().function];
    Value* R = m_regs.data() + m_frames.back().base;
    u32 pc = m_frames.back().pc;
    u64& fuel = m_fuel;
    const u64 budget = m_budget;
    u32& hostCalls = m_hostCalls;

    auto trap = [&](Trap t, std::string msg) {
        m_res.trap = t;
        m_res.function = m_frames.back().function;
        m_res.pc = pc;
        m_res.message = std::move(msg);
        m_res.fuelUsed = budget - fuel;
        return false;
    };
    auto str = [&](Value v) { return stringOf(v); };
    auto spend = [&](u64 extra) {
        if (fuel < extra) {
            fuel = 0;
            return false;
        }
        fuel -= extra;
        return true;
    };

    using bc::Op;
    while (true) {
        if (fuel == 0) {
            return trap(Trap::OutOfFuel, std::format("연료 {} 를 다 썼다", budget));
        }
        --fuel;
        const bc::Insn& in = F->code[pc];
        switch (in.op) {
        case Op::NOP:
            break;
        case Op::MOV:
            R[in.a] = R[in.b];
            break;
        case Op::LOADI:
            R[in.a] = Value::i32v(static_cast<i32>(in.wide()));
            break;
        case Op::LOADB:
            R[in.a] = Value::boolean(in.b != 0);
            break;
        case Op::LOADF:
            R[in.a] = Value::f64v(M.numbers[in.b]);
            break;
        case Op::LOADS:
            R[in.a] = Value{static_cast<u64>(kConstStringBit | in.b)};
            break;
        case Op::LOADNULL:
            R[in.a] = Value::null();
            break;

        // i32 — 감싸기는 u32 로 계산해 정의된 동작으로
        case Op::ADDI:
            R[in.a] =
                Value::i32v(static_cast<i32>(static_cast<u32>(R[in.b].asI32()) + static_cast<u32>(R[in.c].asI32())));
            break;
        case Op::SUBI:
            R[in.a] =
                Value::i32v(static_cast<i32>(static_cast<u32>(R[in.b].asI32()) - static_cast<u32>(R[in.c].asI32())));
            break;
        case Op::MULI:
            R[in.a] =
                Value::i32v(static_cast<i32>(static_cast<u32>(R[in.b].asI32()) * static_cast<u32>(R[in.c].asI32())));
            break;
        case Op::REMI: {
            const i32 b = R[in.c].asI32();
            if (b == 0) {
                return trap(Trap::DivideByZero, "i32 % 0");
            }
            R[in.a] = Value::i32v(b == -1 ? 0 : R[in.b].asI32() % b);
            break;
        }
        case Op::IDIV: {
            const i32 a = R[in.b].asI32();
            const i32 b = R[in.c].asI32();
            if (b == 0) {
                return trap(Trap::DivideByZero, "idiv(a, 0)");
            }
            R[in.a] = Value::i32v(b == -1 ? static_cast<i32>(0u - static_cast<u32>(a)) : a / b); // -2^31 / -1 = -2^31
            break;
        }
        case Op::ANDI:
            R[in.a] = Value::i32v(R[in.b].asI32() & R[in.c].asI32());
            break;
        case Op::ORI:
            R[in.a] = Value::i32v(R[in.b].asI32() | R[in.c].asI32());
            break;
        case Op::XORI:
            R[in.a] = Value::i32v(R[in.b].asI32() ^ R[in.c].asI32());
            break;
        case Op::SHLI:
            R[in.a] = Value::i32v(
                static_cast<i32>(static_cast<u32>(R[in.b].asI32()) << (static_cast<u32>(R[in.c].asI32()) & 31u)));
            break;
        case Op::SHRI:
            R[in.a] = Value::i32v(R[in.b].asI32() >> (static_cast<u32>(R[in.c].asI32()) & 31u));
            break;
        case Op::MINI:
            R[in.a] = Value::i32v(std::min(R[in.b].asI32(), R[in.c].asI32()));
            break;
        case Op::MAXI:
            R[in.a] = Value::i32v(std::max(R[in.b].asI32(), R[in.c].asI32()));
            break;
        case Op::NEGI:
            R[in.a] = Value::i32v(static_cast<i32>(0u - static_cast<u32>(R[in.b].asI32())));
            break;
        case Op::NOTI:
            R[in.a] = Value::i32v(~R[in.b].asI32());
            break;

        // f64 — /fp:precise · -ffp-contract=off (sbx_simulation_flags) 로 한 번에 한 연산
        case Op::ADDF:
            R[in.a] = Value::f64v(R[in.b].asF64() + R[in.c].asF64());
            break;
        case Op::SUBF:
            R[in.a] = Value::f64v(R[in.b].asF64() - R[in.c].asF64());
            break;
        case Op::MULF:
            R[in.a] = Value::f64v(R[in.b].asF64() * R[in.c].asF64());
            break;
        case Op::DIVF:
            R[in.a] = Value::f64v(R[in.b].asF64() / R[in.c].asF64());
            break;
        case Op::REMF:
            R[in.a] = Value::f64v(std::fmod(R[in.b].asF64(), R[in.c].asF64()));
            break;
        case Op::POWF:
            R[in.a] = Value::f64v(jsPow(R[in.b].asF64(), R[in.c].asF64()));
            break;
        case Op::NEGF:
            R[in.a] = Value::f64v(-R[in.b].asF64());
            break;
        case Op::MATHF: {
            const bool binary = kMathFunctions[in.c].arity != 1;
            R[in.a] = Value::f64v(mathCall(in.c, R[in.b].asF64(), binary ? R[in.b + 1].asF64() : 0.0));
            break;
        }
        case Op::I2F:
            R[in.a] = Value::f64v(static_cast<f64>(R[in.b].asI32()));
            break;
        case Op::F2I:
            R[in.a] = Value::i32v(toInt32(R[in.b].asF64()));
            break;
        case Op::F2IDX: {
            i32 idx = 0;
            if (!toIndex(R[in.b].asF64(), idx)) {
                return trap(Trap::NotAnIndex, std::format("첨자 {} 가 정수가 아니다", numberToString(R[in.b].asF64())));
            }
            R[in.a] = Value::i32v(idx);
            break;
        }

        case Op::EQI:
            R[in.a] = Value::boolean(R[in.b].asI32() == R[in.c].asI32());
            break;
        case Op::LTI:
            R[in.a] = Value::boolean(R[in.b].asI32() < R[in.c].asI32());
            break;
        case Op::LEI:
            R[in.a] = Value::boolean(R[in.b].asI32() <= R[in.c].asI32());
            break;
        case Op::EQF:
            R[in.a] = Value::boolean(R[in.b].asF64() == R[in.c].asF64());
            break;
        case Op::LTF:
            R[in.a] = Value::boolean(R[in.b].asF64() < R[in.c].asF64());
            break;
        case Op::LEF:
            R[in.a] = Value::boolean(R[in.b].asF64() <= R[in.c].asF64());
            break;
        case Op::EQB:
            R[in.a] = Value::boolean(R[in.b].asBool() == R[in.c].asBool());
            break;
        case Op::EQS:
            R[in.a] = Value::boolean(str(R[in.b]) == str(R[in.c]));
            break;
        case Op::LTS:
            R[in.a] = Value::boolean(str(R[in.b]) < str(R[in.c]));
            break; // UTF-8 바이트 순 (19 5장)
        case Op::LES:
            R[in.a] = Value::boolean(str(R[in.b]) <= str(R[in.c]));
            break;
        case Op::EQR:
        case Op::EQO:
            R[in.a] = Value::boolean(R[in.b].bits == R[in.c].bits);
            break;
        case Op::NOTB:
            R[in.a] = Value::boolean(!R[in.b].asBool());
            break;

        case Op::CONCAT: {
            const std::string_view a = str(R[in.b]);
            const std::string_view b = str(R[in.c]);
            if (!spend((a.size() + b.size()) / 32)) {
                return trap(Trap::OutOfFuel, "연료를 다 썼다 (문자열 잇기)");
            }
            std::string s;
            s.reserve(a.size() + b.size());
            s.append(a);
            s.append(b); // 새 객체를 만들기 전에 복사한다 (아레나가 자라면 a · b 가 가리키던 곳이 움직인다)
            Value out;
            if (!newString(std::move(s), out)) {
                return trap(Trap::OutOfMemory, "문자열 · 아레나 예산을 넘는다");
            }
            R[in.a] = out;
            break;
        }
        case Op::TOSTRI:
        case Op::TOSTRF:
        case Op::TOSTRB: {
            std::string s = in.op == Op::TOSTRI   ? int32ToString(R[in.b].asI32())
                            : in.op == Op::TOSTRF ? numberToString(R[in.b].asF64())
                                                  : std::string(R[in.b].asBool() ? "true" : "false");
            Value out;
            if (!newString(std::move(s), out)) {
                return trap(Trap::OutOfMemory, "아레나 예산을 넘는다");
            }
            R[in.a] = out;
            break;
        }
        case Op::STRLEN:
            R[in.a] = Value::i32v(utf16Length(str(R[in.b])));
            break;

        case Op::NEWARR: {
            Object o;
            o.kind = Object::Kind::Array;
            Value out;
            if (!alloc(std::move(o), kObjectHeaderBytes, out)) {
                return trap(Trap::OutOfMemory, "아레나 예산을 넘는다");
            }
            R[in.a] = out;
            break;
        }
        case Op::ARRLEN: {
            const Object* o = object(R[in.b]);
            if (o == nullptr) {
                return trap(Trap::NullReference, "null 의 length");
            }
            R[in.a] = Value::i32v(static_cast<i32>(o->slots.size()));
            break;
        }
        case Op::ARRGET:
        case Op::ARRSET: {
            Object* o = object(R[in.op == Op::ARRGET ? in.b : in.a]);
            if (o == nullptr) {
                return trap(Trap::NullReference, "null 의 원소");
            }
            const i32 i = R[in.op == Op::ARRGET ? in.c : in.b].asI32();
            if (i < 0 || static_cast<usize>(i) >= o->slots.size()) {
                return trap(Trap::IndexOutOfRange, std::format("첨자 {} — 길이 {}", i, o->slots.size()));
            }
            if (in.op == Op::ARRGET) {
                R[in.a] = o->slots[static_cast<usize>(i)];
            } else {
                o->slots[static_cast<usize>(i)] = R[in.c];
            }
            break;
        }
        case Op::ARRPUSH: {
            Object* o = object(R[in.b]);
            if (o == nullptr) {
                return trap(Trap::NullReference, "null 의 push");
            }
            if (o->slots.size() >= m_limits.maxArrayLength || m_heapBytes + 8 > m_limits.maxHeapBytes) {
                return trap(Trap::OutOfMemory, std::format("배열 길이 · 아레나 예산을 넘는다 ({})", o->slots.size()));
            }
            m_heapBytes += 8;
            o->slots.push_back(R[in.c]);
            if (in.a != bc::kNoReg) {
                R[in.a] = Value::i32v(static_cast<i32>(o->slots.size()));
            }
            break;
        }
        case Op::NEWOBJ: {
            const usize n = M.types[F->regs[in.a]].fieldTypes.size();
            Object o;
            o.kind = Object::Kind::Struct;
            o.slots.resize(n);
            Value out;
            if (!alloc(std::move(o), kObjectHeaderBytes + n * 8, out)) {
                return trap(Trap::OutOfMemory, "아레나 예산을 넘는다");
            }
            R[in.a] = out;
            break;
        }
        case Op::GETF: {
            const Object* o = object(R[in.b]);
            if (o == nullptr) {
                return trap(Trap::NullReference, "null 의 필드");
            }
            R[in.a] = o->slots[in.c];
            break;
        }
        case Op::SETF: {
            Object* o = object(R[in.a]);
            if (o == nullptr) {
                return trap(Trap::NullReference, "null 의 필드");
            }
            o->slots[in.b] = R[in.c];
            break;
        }
        case Op::BOX: {
            Object o;
            o.kind = Object::Kind::Box;
            o.slots.push_back(R[in.b]);
            Value out;
            if (!alloc(std::move(o), kObjectHeaderBytes + 8, out)) {
                return trap(Trap::OutOfMemory, "아레나 예산을 넘는다");
            }
            R[in.a] = out;
            break;
        }
        case Op::UNBOX: {
            const Object* o = object(R[in.b]);
            if (o == nullptr) {
                return trap(Trap::NullReference, "null 상자를 열었다");
            }
            R[in.a] = o->slots[0];
            break;
        }

        case Op::JMP:
            pc = in.wide();
            continue;
        case Op::JMPT:
            if (R[in.a].asBool()) {
                pc = in.wide();
                continue;
            }
            break;
        case Op::JMPF:
            if (!R[in.a].asBool()) {
                pc = in.wide();
                continue;
            }
            break;
        case Op::CALL: {
            const u32 callee = in.b;
            const usize argc = M.functions[callee].params.size();
            m_frames.back().pc = pc + 1;
            const u32 argBase = m_frames.back().base + in.c;
            if (!enterFrame(callee, in.a)) {
                return trap(Trap::StackOverflow,
                            std::format("호출 깊이 {} · 레지스터 칸 예산을 넘는다", m_frames.size()));
            }
            Value* calleeRegs = m_regs.data() + m_frames.back().base;
            for (usize i = 0; i < argc; ++i) {
                calleeRegs[i] = m_regs[argBase + i];
            }
            F = &M.functions[callee];
            R = calleeRegs;
            pc = 0;
            continue;
        }
        case Op::CALLH: {
            const api::HostFunction& h = api::kHostFunctions[m_importToHost[in.b]];
            if (++hostCalls > m_limits.maxHostCalls) {
                return trap(Trap::HostError,
                            std::format("Host 호출이 {} 번을 넘었다 ({})", m_limits.maxHostCalls, h.id));
            }
            if (!spend(h.fuelBase)) {
                return trap(Trap::OutOfFuel, std::format("연료를 다 썼다 ({})", h.id));
            }
            if (m_host == nullptr) {
                return trap(Trap::HostError, std::format("Host 가 없어 {} 를 부를 수 없다", h.id));
            }
            const bc::HostImport& im = M.imports[in.b];
            m_hostArgs.clear();
            m_hostFields.clear();
            usize fieldsArg = im.params.size();
            for (usize i = 0; i < im.params.size(); ++i) {
                HostArg a;
                a.type = im.params[i];
                a.value = R[in.c + i];
                if (a.type == api::AbiType::String) {
                    a.text = str(a.value);
                } else if (a.type == api::AbiType::ComponentData) {
                    if (!flattenStruct(F->regs[in.c + i], a.value, m_hostFields, 0)) {
                        return trap(Trap::HostError,
                                    std::format("{}: 값의 모양이 컴포넌트가 아니다 (null 포함)", h.id));
                    }
                    fieldsArg = i;
                }
                m_hostArgs.push_back(a);
            }
            if (fieldsArg < m_hostArgs.size()) {
                m_hostArgs[fieldsArg].fields = m_hostFields; // 다 펼친 뒤에 (벡터가 자라는 동안 가리키지 않게)
            }
            const HostResult hr = m_host->call(h, m_hostArgs);
            if (!hr.ok) {
                return trap(Trap::HostError, std::format("{}: {}", h.id, hr.error));
            }
            if (!spend(hr.extraFuel)) {
                return trap(Trap::OutOfFuel, std::format("연료를 다 썼다 ({})", h.id));
            }
            if (in.a == bc::kNoReg) {
                break;
            }
            if (im.result == api::AbiType::ComponentDataOrNull) {
                Value out = Value::null();
                if (!hr.value.isNull()) {
                    usize pos = 0;
                    const Marshal m = buildStruct(F->regs[in.a], hr.values, pos, 0, out);
                    if (m == Marshal::OutOfMemory) {
                        return trap(Trap::OutOfMemory, "아레나 예산을 넘는다");
                    }
                    if (m != Marshal::Ok || pos != hr.values.size()) {
                        return trap(Trap::HostError, std::format("{}: 결과의 모양이 레지스터 타입과 다르다", h.id));
                    }
                }
                R[in.a] = out;
            } else if (im.result == api::AbiType::EntityOrNull) {
                // Entity | null — 레지스터는 상자 (null 이면 null, 아니면 핸들을 담은 상자 — BOX 와 같은 모양)
                Value out = Value::null();
                if (!hr.value.isNull()) {
                    Object o;
                    o.kind = Object::Kind::Box;
                    o.slots.push_back(hr.value);
                    if (!alloc(std::move(o), kObjectHeaderBytes + 8, out)) {
                        return trap(Trap::OutOfMemory, "아레나 예산을 넘는다");
                    }
                }
                R[in.a] = out;
            } else if (im.result == api::AbiType::EntityList) {
                if (hr.values.size() > m_limits.maxArrayLength) {
                    return trap(Trap::OutOfMemory, "배열 길이 예산을 넘는다");
                }
                Object o;
                o.kind = Object::Kind::Array;
                o.slots = hr.values;
                Value out;
                if (!alloc(std::move(o), kObjectHeaderBytes + hr.values.size() * 8, out)) {
                    return trap(Trap::OutOfMemory, "아레나 예산을 넘는다");
                }
                R[in.a] = out;
            } else {
                R[in.a] = hr.value;
            }
            break;
        }
        case Op::RET:
        case Op::RETV: {
            const Value result = in.op == Op::RETV ? R[in.a] : Value{};
            const Frame done = m_frames.back();
            m_frames.pop_back();
            m_regs.resize(done.base);
            if (m_frames.size() == stopDepth) {
                retOut = result;
                return true;
            }
            const Frame& caller = m_frames.back();
            F = &M.functions[caller.function];
            R = m_regs.data() + caller.base;
            pc = caller.pc;
            if (done.resultReg != bc::kNoReg) {
                R[done.resultReg] = result;
            }
            continue;
        }
        case Op::TRAP:
            return trap(Trap::Unreachable, std::format("TRAP {}", in.a));
        case Op::LOADK:
            R[in.a] = Value::opaque(m_componentIds[in.b]);
            break;
        case Op::INTR: {
            Trap t = Trap::None;
            std::string msg;
            if (!intrinsic(in, *F, R, fuel, t, msg)) {
                // Trap::None = 콜백 안의 트랩 (m_res 에 그 자리가 이미 있다)
                return t == Trap::None ? false : trap(t, std::move(msg));
            }
            break;
        }
        case Op::LOADFN:
            R[in.a] = Value{static_cast<u64>(in.b) + 1}; // 0 = 없음 (검증기가 서명을 봤다)
            break;
        case Op::Count:
            return trap(Trap::Unreachable, "모르는 명령");
        }
        ++pc;
    }
}

namespace {

// JS 의 상대 첨자 (ToIntegerOrInfinity → 음수는 끝에서 셈 → [0, len] 로 자름). NaN = 0
usize relIndex(f64 v, usize len) {
    if (std::isnan(v)) {
        return 0;
    }
    const f64 t = std::trunc(v);
    const f64 n = static_cast<f64>(len);
    if (t < 0) {
        const f64 r = n + t;
        return r <= 0 ? 0 : static_cast<usize>(r);
    }
    return t >= n ? len : static_cast<usize>(t);
}

// 정렬의 연료 — 비교 횟수의 위 한계 n · ⌈log2 n⌉ (실제 비교 수는 표준 라이브러리마다 달라 쓰지 않는다 — 결정론)
u64 sortFuel(usize n) {
    u64 log = 0;
    while ((usize{1} << log) < n) {
        ++log;
    }
    return static_cast<u64>(n) * log;
}

} // namespace

bool Interpreter::elemEqual(u32 elemType, Value a, Value b, bool sameValueZero) const {
    const bc::TypeDesc& t = M.types[elemType];
    switch (t.kind) {
    case bc::TypeKind::F64: {
        const f64 x = a.asF64();
        const f64 y = b.asF64();
        return x == y || (sameValueZero && std::isnan(x) && std::isnan(y));
    }
    case bc::TypeKind::String:
        return stringOf(a) == stringOf(b);
    case bc::TypeKind::Box:
        // T | null 원소 (상자) — 둘 다 null 이면 같다, 아니면 안쪽 값으로
        if (a.isNull() || b.isNull()) {
            return a.isNull() && b.isNull();
        }
        return elemEqual(t.elem, unbox(a), unbox(b), sameValueZero);
    default:
        // i32 · bool · 불투명 = 값, 배열 · 구조체 = 같은 객체 (===)
        return a.bits == b.bits;
    }
}

bool Interpreter::intrinsic(const bc::Insn& in, const bc::Function& fn, Value* R, u64& fuel, Trap& trap,
                            std::string& msg) {
    if (isCollectionIntrinsic(in.c)) {
        return collectionIntrinsic(in, fn, R, fuel, trap, msg);
    }
    const Intrinsic& k = kIntrinsics[in.c];
    const auto id = static_cast<IntrinsicId>(in.c);
    auto fail = [&](Trap t, std::string m) {
        trap = t;
        msg = std::format("{}: {}", k.name, std::move(m));
        return false;
    };
    auto spend = [&](u64 extra) {
        if (fuel < extra) {
            fuel = 0;
            return false;
        }
        fuel -= extra;
        return true;
    };
    if (!spend(k.fuelBase)) {
        return fail(Trap::OutOfFuel, "연료를 다 썼다");
    }
    const Value* args = R + in.b;
    // 배열 받는 쪽 (모든 v0.5 M8-T2 내장 — 검증기가 인자 0 = 배열을 보장한다)
    const u32 arrayType = fn.regs[in.b];
    const u32 elemType = M.types[arrayType].elem;
    if (object(args[0]) == nullptr) {
        return fail(Trap::NullReference, "null 의 메서드");
    }
    auto arr = [&]() -> Object& { return *object(args[0]); }; // alloc 뒤에는 다시 찾는다 (m_objects 가 자란다)
    const usize len = arr().slots.size();
    auto result = [&](Value v) {
        if (in.a != bc::kNoReg) {
            R[in.a] = v;
        }
    };
    // 원소 | undefined 결과 — 상자 레지스터면 상자로 싼다 (없음 = null)
    auto optResult = [&](bool present, Value v) {
        if (in.a == bc::kNoReg) {
            return true;
        }
        if (!present) {
            R[in.a] = Value::null();
            return true;
        }
        if (M.types[fn.regs[in.a]].kind != bc::TypeKind::Box) {
            R[in.a] = v;
            return true;
        }
        Object o;
        o.kind = Object::Kind::Box;
        o.slots.push_back(v);
        Value out;
        if (!alloc(std::move(o), kObjectHeaderBytes + 8, out)) {
            return false;
        }
        R[in.a] = out;
        return true;
    };
    auto grow = [&](usize by) {
        return len + by <= m_limits.maxArrayLength && m_heapBytes + by * 8 <= m_limits.maxHeapBytes;
    };
    // 콜백 (v0.5 M8-T3) — 함수 참조 값 = 함수 번호 + 1 (0 = 없음). 그 함수의 매개변수 수만큼 넘긴다: lead (원소 ·
    // 누적 · 비교 짝) 다음에 자리가 남으면 첨자 (그 매개변수의 타입 — i32 · f64). 콜백 안의 트랩은 m_res 에 그 자리가
    // 남고 trap = None 으로 돌아간다 (부른 쪽이 덮어쓰지 않는다)
    auto callback = [&](Value fnv, std::span<const Value> lead, usize index, Value& cbOut) {
        if (fnv.bits == 0 || fnv.bits > M.functions.size()) {
            return fail(Trap::NullReference, "콜백이 없다");
        }
        const u32 f = static_cast<u32>(fnv.bits - 1);
        const bc::Function& cf = M.functions[f];
        std::array<Value, 3> a{};
        usize n = 0;
        for (const Value v : lead) {
            if (n < cf.params.size() && n < a.size()) {
                a[n++] = v;
            }
        }
        if (n < cf.params.size() && n < a.size()) {
            a[n] = M.types[cf.params[n]].kind == bc::TypeKind::F64 ? Value::f64v(static_cast<f64>(index))
                                                                   : Value::i32v(static_cast<i32>(index));
            ++n;
        }
        if (!invoke(f, std::span<const Value>(a.data(), n), cbOut)) {
            trap = Trap::None;
            return false;
        }
        return true;
    };

    switch (id) {
    case IntrinsicId::ArrayPop:
    case IntrinsicId::ArrayShift: {
        if (len == 0) {
            return optResult(false, {}) || fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        Value v;
        if (id == IntrinsicId::ArrayPop) {
            v = arr().slots.back();
            arr().slots.pop_back();
        } else {
            if (!spend(len)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            v = arr().slots.front();
            arr().slots.erase(arr().slots.begin());
        }
        return optResult(true, v) || fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
    }
    case IntrinsicId::ArrayUnshift: {
        if (!grow(1)) {
            return fail(Trap::OutOfMemory, std::format("배열 길이 · 아레나 예산을 넘는다 ({})", len));
        }
        if (!spend(len)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        m_heapBytes += 8;
        arr().slots.insert(arr().slots.begin(), args[1]);
        result(Value::i32v(static_cast<i32>(len + 1)));
        return true;
    }
    case IntrinsicId::ArraySlice: {
        const usize from = relIndex(args[1].asF64(), len);
        const usize to = relIndex(args[2].asF64(), len);
        const usize n = to > from ? to - from : 0;
        if (!spend(n)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        std::vector<Value> slots(arr().slots.begin() + static_cast<std::ptrdiff_t>(from),
                                 arr().slots.begin() + static_cast<std::ptrdiff_t>(from + n));
        Value out;
        if (!newArray(std::move(slots), out)) {
            return fail(Trap::OutOfMemory, "배열 길이 · 아레나 예산을 넘는다");
        }
        result(out);
        return true;
    }
    case IntrinsicId::ArrayConcat: {
        const Object* other = object(args[1]);
        if (other == nullptr) {
            return fail(Trap::NullReference, "null 을 잇는다");
        }
        const usize n = len + other->slots.size();
        if (n > m_limits.maxArrayLength) {
            return fail(Trap::OutOfMemory, std::format("배열 길이 예산을 넘는다 ({})", n));
        }
        if (!spend(n)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        std::vector<Value> slots;
        slots.reserve(n);
        slots.insert(slots.end(), arr().slots.begin(), arr().slots.end());
        slots.insert(slots.end(), other->slots.begin(), other->slots.end());
        Value out;
        if (!newArray(std::move(slots), out)) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        result(out);
        return true;
    }
    case IntrinsicId::ArrayReverse: {
        if (!spend(len)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        std::reverse(arr().slots.begin(), arr().slots.end());
        result(args[0]);
        return true;
    }
    case IntrinsicId::ArrayFill: {
        const usize from = relIndex(args[2].asF64(), len);
        const usize to = relIndex(args[3].asF64(), len);
        if (!spend(to > from ? to - from : 0)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        for (usize i = from; i < to; ++i) {
            arr().slots[i] = args[1];
        }
        result(args[0]);
        return true;
    }
    case IntrinsicId::ArrayCopyWithin: {
        const usize target = relIndex(args[1].asF64(), len);
        const usize from = relIndex(args[2].asF64(), len);
        const usize to = relIndex(args[3].asF64(), len);
        const usize n = std::min(to > from ? to - from : 0, len - target);
        if (!spend(n)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        // 겹쳐도 원래 값을 옮긴다 (JS — 방향을 골라 복사하는 것과 같은 결과)
        const std::vector<Value> part(arr().slots.begin() + static_cast<std::ptrdiff_t>(from),
                                      arr().slots.begin() + static_cast<std::ptrdiff_t>(from + n));
        std::copy(part.begin(), part.end(), arr().slots.begin() + static_cast<std::ptrdiff_t>(target));
        result(args[0]);
        return true;
    }
    case IntrinsicId::ArraySplice: {
        const usize start = relIndex(args[1].asF64(), len);
        const f64 dc = args[2].asF64();
        // deleteCount: ToIntegerOrInfinity 를 [0, len - start] 로 (NaN = 0)
        const f64 dct = std::isnan(dc) ? 0.0 : std::trunc(dc);
        const usize room = len - start;
        usize n = 0;
        if (dct > 0) {
            n = dct >= static_cast<f64>(room) ? room : static_cast<usize>(dct);
        }
        if (!spend(len)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        std::vector<Value> removed(arr().slots.begin() + static_cast<std::ptrdiff_t>(start),
                                   arr().slots.begin() + static_cast<std::ptrdiff_t>(start + n));
        Value out;
        if (!newArray(std::move(removed), out)) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        auto& slots = arr().slots;
        slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(start),
                    slots.begin() + static_cast<std::ptrdiff_t>(start + n));
        result(out);
        return true;
    }
    case IntrinsicId::ArrayIndexOf:
    case IntrinsicId::ArrayIncludes: {
        const usize from = relIndex(args[2].asF64(), len);
        const bool svz = id == IntrinsicId::ArrayIncludes;
        i32 found = -1;
        for (usize i = from; i < len; ++i) {
            if (!spend(1)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            if (elemEqual(elemType, arr().slots[i], args[1], svz)) {
                found = static_cast<i32>(i);
                break;
            }
        }
        result(svz ? Value::boolean(found >= 0) : Value::i32v(found));
        return true;
    }
    case IntrinsicId::ArrayLastIndexOf: {
        // fromIndex: 기본 (+Infinity) = len - 1, 음수는 끝에서, -Infinity 면 없음
        const f64 f = args[2].asF64();
        const f64 t = std::isnan(f) ? 0.0 : std::trunc(f);
        const f64 n = static_cast<f64>(len);
        const f64 start = t >= 0 ? std::min(t, n - 1) : n + t;
        i32 found = -1;
        for (f64 i = start; i >= 0; i -= 1) {
            if (!spend(1)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            if (elemEqual(elemType, arr().slots[static_cast<usize>(i)], args[1], false)) {
                found = static_cast<i32>(i);
                break;
            }
        }
        result(Value::i32v(found));
        return true;
    }
    case IntrinsicId::ArrayJoin: {
        const std::string sep(stringOf(args[1])); // 아레나가 자라기 전에 복사
        const bc::TypeKind ek = M.types[elemType].kind;
        std::string s;
        for (usize i = 0; i < len; ++i) {
            if (i > 0) {
                s += sep;
            }
            const Value v = arr().slots[i];
            switch (ek) {
            case bc::TypeKind::I32:
                s += int32ToString(v.asI32());
                break;
            case bc::TypeKind::F64:
                s += numberToString(v.asF64());
                break;
            case bc::TypeKind::Bool:
                s += v.asBool() ? "true" : "false";
                break;
            default:
                s += stringOf(v);
                break;
            }
            if (s.size() > m_limits.maxStringBytes) {
                return fail(Trap::OutOfMemory, "문자열 예산을 넘는다");
            }
            if (!spend(1 + s.size() / 1024)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
        }
        Value out;
        if (!newString(std::move(s), out)) {
            return fail(Trap::OutOfMemory, "문자열 · 아레나 예산을 넘는다");
        }
        result(out);
        return true;
    }
    case IntrinsicId::ArraySortBy: {
        // 안정 정렬 — 길이 16 덩어리의 삽입 정렬 + 아래에서 위로 병합 (고정 알고리즘: 같은 입력 · 같은 비교자 = 같은
        // 비교 차례). 비교자 결과 < 0 이면 앞, NaN = 0 (JS). 복사본을 정렬해 다시 쓴다 (비교자가 배열을 바꿔도 결정적)
        const Value fnv = args[1];
        const bool f64Result = M.types[M.types[fn.regs[in.b + 1u]].elem].kind == bc::TypeKind::F64;
        std::vector<Value> a = arr().slots;
        auto cmp = [&](Value x, Value y, f64& r) {
            if (!spend(1)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            const std::array<Value, 2> lead{x, y};
            Value o;
            if (!callback(fnv, lead, 0, o)) {
                return false;
            }
            r = f64Result ? o.asF64() : static_cast<f64>(o.asI32());
            if (std::isnan(r)) {
                r = 0.0;
            }
            return true;
        };
        constexpr usize kRun = 16;
        const usize n = a.size();
        for (usize lo = 0; lo < n; lo += kRun) {
            const usize hi = std::min(lo + kRun, n);
            for (usize i = lo + 1; i < hi; ++i) {
                const Value key = a[i];
                usize j = i;
                while (j > lo) {
                    f64 r = 0;
                    if (!cmp(a[j - 1], key, r)) {
                        return false;
                    }
                    if (r <= 0) {
                        break;
                    }
                    a[j] = a[j - 1];
                    --j;
                }
                a[j] = key;
            }
        }
        std::vector<Value> tmp(n);
        for (usize width = kRun; width < n; width *= 2) {
            for (usize lo = 0; lo < n; lo += 2 * width) {
                const usize mid = std::min(lo + width, n);
                const usize hi = std::min(lo + 2 * width, n);
                usize i = lo;
                usize j = mid;
                usize w = lo;
                while (i < mid && j < hi) {
                    f64 r = 0;
                    if (!cmp(a[i], a[j], r)) {
                        return false;
                    }
                    tmp[w++] = r <= 0 ? a[i++] : a[j++]; // 같으면 왼쪽 먼저 (안정)
                }
                while (i < mid) {
                    tmp[w++] = a[i++];
                }
                while (j < hi) {
                    tmp[w++] = a[j++];
                }
            }
            a.swap(tmp);
        }
        auto& slots = arr().slots;
        std::copy_n(a.begin(), std::min(slots.size(), a.size()), slots.begin());
        result(args[0]);
        return true;
    }
    case IntrinsicId::ArrayFind:
    case IntrinsicId::ArrayFindIndex:
    case IntrinsicId::ArraySome:
    case IntrinsicId::ArrayEvery:
    case IntrinsicId::ArrayFilter:
    case IntrinsicId::ArrayMap:
    case IntrinsicId::ArrayForEach: {
        // 처음 길이까지, 콜백이 배열을 줄였으면 거기서 멈춘다 (빈 칸이 없는 배열 — JS 의 HasProperty 건너뛰기와 같다)
        std::vector<Value> kept;
        for (usize i = 0; i < len && i < arr().slots.size(); ++i) {
            if (!spend(1)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            const Value v = arr().slots[i];
            const std::array<Value, 1> lead{v};
            Value o;
            if (!callback(args[1], lead, i, o)) {
                return false;
            }
            switch (id) {
            case IntrinsicId::ArrayFind:
                if (o.asBool()) {
                    return optResult(true, v) || fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
                }
                break;
            case IntrinsicId::ArrayFindIndex:
            case IntrinsicId::ArraySome:
                if (o.asBool()) {
                    result(id == IntrinsicId::ArraySome ? Value::boolean(true) : Value::i32v(static_cast<i32>(i)));
                    return true;
                }
                break;
            case IntrinsicId::ArrayEvery:
                if (!o.asBool()) {
                    result(Value::boolean(false));
                    return true;
                }
                break;
            case IntrinsicId::ArrayFilter:
                if (o.asBool()) {
                    kept.push_back(v);
                }
                break;
            case IntrinsicId::ArrayMap:
                kept.push_back(o);
                break;
            default:
                break;
            }
        }
        switch (id) {
        case IntrinsicId::ArrayFind:
            return optResult(false, {}) || fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        case IntrinsicId::ArrayFindIndex:
            result(Value::i32v(-1));
            return true;
        case IntrinsicId::ArraySome:
            result(Value::boolean(false));
            return true;
        case IntrinsicId::ArrayEvery:
            result(Value::boolean(true));
            return true;
        case IntrinsicId::ArrayFilter:
        case IntrinsicId::ArrayMap: {
            Value out;
            if (!newArray(std::move(kept), out)) {
                return fail(Trap::OutOfMemory, "배열 길이 · 아레나 예산을 넘는다");
            }
            result(out);
            return true;
        }
        default:
            return true; // forEach
        }
    }
    case IntrinsicId::ArrayReduce: {
        Value acc = args[2];
        for (usize i = 0; i < len && i < arr().slots.size(); ++i) {
            if (!spend(1)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            const std::array<Value, 2> lead{acc, arr().slots[i]};
            Value o;
            if (!callback(args[1], lead, i, o)) {
                return false;
            }
            acc = o;
        }
        result(acc);
        return true;
    }
    case IntrinsicId::ArraySortStrings: {
        if (!spend(sortFuel(len))) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        // 안정 정렬 — 결과는 비교 순서 (UTF-8 바이트 = 코드 포인트, `<` 와 같다) 로 하나로 정해진다
        std::stable_sort(arr().slots.begin(), arr().slots.end(),
                         [&](Value a, Value b) { return stringOf(a) < stringOf(b); });
        result(args[0]);
        return true;
    }
    }
    return fail(Trap::Unreachable, "모르는 내장");
}

namespace {

// 고정 섞기 (splitmix64 의 끝 — 주소 · 무작위 씨앗 없음, ADR-0085 7)
u64 mix64(u64 x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

} // namespace

u64 Interpreter::keyHash(bc::TypeKind kind, Value v) const {
    switch (kind) {
    case bc::TypeKind::F64: {
        const f64 f = v.asF64();
        // SameValueZero — -0 = +0, NaN 은 모두 같다
        const u64 bits = f == 0.0 ? 0u : (std::isnan(f) ? 0x7FF8'0000'0000'0000ULL : v.bits);
        return mix64(bits);
    }
    case bc::TypeKind::String:
        return fnv1a64(stringOf(v));
    default:
        return mix64(v.bits & 0xFFFF'FFFFu); // i32 · bool
    }
}

bool Interpreter::keyEqual(bc::TypeKind kind, Value a, Value b) const {
    switch (kind) {
    case bc::TypeKind::F64: {
        const f64 x = a.asF64();
        const f64 y = b.asF64();
        return x == y || (std::isnan(x) && std::isnan(y));
    }
    case bc::TypeKind::String:
        return stringOf(a) == stringOf(b);
    default:
        return static_cast<u32>(a.bits) == static_cast<u32>(b.bits);
    }
}

bool Interpreter::collectionIntrinsic(const bc::Insn& in, const bc::Function& fn, Value* R, u64& fuel, Trap& trap,
                                      std::string& msg) {
    const Intrinsic& k = kIntrinsics[in.c];
    const auto id = static_cast<IntrinsicId>(in.c);
    auto fail = [&](Trap t, std::string m) {
        trap = t;
        msg = std::format("{}: {}", k.name, std::move(m));
        return false;
    };
    auto spend = [&](u64 extra) {
        if (fuel < extra) {
            fuel = 0;
            return false;
        }
        fuel -= extra;
        return true;
    };
    auto result = [&](Value v) {
        if (in.a != bc::kNoReg) {
            R[in.a] = v;
        }
    };
    if (!spend(k.fuelBase)) {
        return fail(Trap::OutOfFuel, "연료를 다 썼다");
    }
    // 새 컬렉션 (Map.new · Set.new · Set.from · 집합 연산의 결과) — 키 종류는 결과 레지스터의 타입
    auto newCollection = [&](bool isMap, bc::TypeKind keyKind, Value& out) {
        Object o;
        o.kind = isMap ? Object::Kind::Map : Object::Kind::Set;
        o.hash = std::make_unique<HashData>();
        o.hash->keyKind = keyKind;
        return alloc(std::move(o), kObjectHeaderBytes + 64, out);
    };
    auto keyKindOf = [&](u32 collType) { return M.types[M.types[collType].elem].kind; };

    if (id == IntrinsicId::MapNew || id == IntrinsicId::SetNew) {
        Value out;
        if (!newCollection(id == IntrinsicId::MapNew, keyKindOf(fn.regs[in.a]), out)) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        result(out);
        return true;
    }
    const Value* args = R + in.b;
    if (object(args[0]) == nullptr) {
        return fail(Trap::NullReference, "null 의 메서드");
    }
    auto coll = [&](Value v) -> HashData& { return *object(v)->hash; }; // alloc 뒤에는 다시 찾는다

    // 찾기 — 선형 탐사. 지운 항목도 자기 키의 칸을 지킨다 (같은 키를 다시 넣으면 그 칸을 다시 쓴다 — 키마다 칸 하나)
    struct Found {
        usize slot = 0;
        i64 entry = -1; // 살아 있는 항목 (없으면 -1)
        bool occupied = false;
    };
    auto find = [&](HashData& h, Value key, Found& f) {
        if (h.index.empty()) {
            return true;
        }
        const usize mask = h.index.size() - 1;
        usize i = static_cast<usize>(keyHash(h.keyKind, key)) & mask;
        while (h.index[i] != 0) {
            if (!spend(1)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            const u32 e = h.index[i] - 1;
            if (keyEqual(h.keyKind, h.keys[e], key)) {
                f.slot = i;
                f.occupied = true;
                f.entry = h.alive[e] != 0 ? static_cast<i64>(e) : -1;
                return true;
            }
            i = (i + 1) & mask;
        }
        f.slot = i;
        return true;
    };
    // 색인 다시 만들기 — 살아 있는 항목만 (지운 항목의 칸은 여기서 사라진다)
    auto rebuild = [&](HashData& h, usize want) {
        usize size = 8;
        while (size < want * 2) {
            size *= 2;
        }
        const usize extra = size > h.index.size() ? (size - h.index.size()) * 4 : 0;
        if (m_heapBytes + extra > m_limits.maxHeapBytes) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        if (!spend(h.live)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        m_heapBytes += extra;
        h.index.assign(size, 0);
        h.used = 0;
        const usize mask = size - 1;
        for (usize e = 0; e < h.keys.size(); ++e) {
            if (h.alive[e] == 0) {
                continue;
            }
            usize i = static_cast<usize>(keyHash(h.keyKind, h.keys[e])) & mask;
            while (h.index[i] != 0) {
                i = (i + 1) & mask;
            }
            h.index[i] = static_cast<u32>(e + 1);
            ++h.used;
        }
        return true;
    };
    // 넣기 — 있으면 값만 바꾼다 (자리 그대로), 없으면 끝에 (넣은 차례). Map 이 아니면 value 는 쓰지 않는다
    auto insert = [&](HashData& h, Value key, Value value, bool isMap) {
        if (h.keyKind == bc::TypeKind::F64 && key.asF64() == 0.0) {
            key = Value::f64v(0.0); // -0 은 +0 으로 저장 (JS 의 Map · Set)
        }
        Found f;
        if (!find(h, key, f)) {
            return false;
        }
        if (f.entry >= 0) {
            if (isMap) {
                h.values[static_cast<usize>(f.entry)] = value;
            }
            return true;
        }
        if (h.keys.size() >= m_limits.maxArrayLength) {
            return fail(Trap::OutOfMemory, std::format("항목 상한 {} 을 넘는다", m_limits.maxArrayLength));
        }
        const usize bytes = isMap ? 17 : 9;
        if (m_heapBytes + bytes > m_limits.maxHeapBytes) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        if (!f.occupied && (h.used + 1) * 2 > h.index.size()) {
            if (!rebuild(h, h.live + 1)) {
                return false;
            }
            if (!find(h, key, f)) {
                return false;
            }
        }
        m_heapBytes += bytes;
        h.keys.push_back(key);
        if (isMap) {
            h.values.push_back(value);
        }
        h.alive.push_back(1);
        ++h.live;
        if (!f.occupied) {
            ++h.used;
        }
        h.index[f.slot] = static_cast<u32>(h.keys.size()); // 항목 + 1
        return true;
    };
    auto erase = [&](HashData& h, Value key, bool& removed) {
        Found f;
        if (!find(h, key, f)) {
            return false;
        }
        removed = f.entry >= 0;
        if (removed) {
            h.alive[static_cast<usize>(f.entry)] = 0; // 항목 자리는 남는다 (반복 중의 커서가 그대로)
            --h.live;
        }
        return true;
    };
    // 커서 이후의 첫 살아 있는 항목 (없으면 -1) — 지운 항목을 건너뛰는 만큼 연료
    auto next = [&](HashData& h, i32 cursor) {
        usize i = cursor < 0 ? 0 : static_cast<usize>(cursor);
        while (i < h.keys.size() && h.alive[i] == 0) {
            ++i;
        }
        return i < h.keys.size() ? static_cast<i32>(i) : -1;
    };
    auto toArray = [&](HashData& h, bool values, Value& out) {
        if (!spend(h.keys.size())) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        std::vector<Value> slots;
        slots.reserve(h.live);
        for (usize e = 0; e < h.keys.size(); ++e) {
            if (h.alive[e] != 0) {
                slots.push_back(values ? h.values[e] : h.keys[e]);
            }
        }
        if (!newArray(std::move(slots), out)) {
            return fail(Trap::OutOfMemory, "배열 길이 · 아레나 예산을 넘는다");
        }
        return true;
    };

    switch (id) {
    case IntrinsicId::MapSet:
        if (!insert(coll(args[0]), args[1], args[2], true)) {
            return false;
        }
        result(args[0]);
        return true;
    case IntrinsicId::SetAdd:
        if (!insert(coll(args[0]), args[1], {}, false)) {
            return false;
        }
        result(args[0]);
        return true;
    case IntrinsicId::MapGet: {
        HashData& h = coll(args[0]);
        Found f;
        if (!find(h, args[1], f)) {
            return false;
        }
        if (in.a == bc::kNoReg) {
            return true;
        }
        if (f.entry < 0) {
            R[in.a] = Value::null();
            return true;
        }
        const Value v = h.values[static_cast<usize>(f.entry)];
        if (M.types[fn.regs[in.a]].kind != bc::TypeKind::Box) {
            R[in.a] = v;
            return true;
        }
        Object o;
        o.kind = Object::Kind::Box;
        o.slots.push_back(v);
        Value out;
        if (!alloc(std::move(o), kObjectHeaderBytes + 8, out)) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        R[in.a] = out;
        return true;
    }
    case IntrinsicId::MapHas:
    case IntrinsicId::SetHas: {
        Found f;
        if (!find(coll(args[0]), args[1], f)) {
            return false;
        }
        result(Value::boolean(f.entry >= 0));
        return true;
    }
    case IntrinsicId::MapDelete:
    case IntrinsicId::SetDelete: {
        bool removed = false;
        if (!erase(coll(args[0]), args[1], removed)) {
            return false;
        }
        result(Value::boolean(removed));
        return true;
    }
    case IntrinsicId::MapClear:
    case IntrinsicId::SetClear: {
        // 항목을 비운다 — 반복 중의 커서는 길이를 넘어 끝난다 (JS 의 clear 와 같다)
        HashData& h = coll(args[0]);
        if (!spend(h.index.size() / 64)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        h.keys.clear();
        h.values.clear();
        h.alive.clear();
        std::fill(h.index.begin(), h.index.end(), 0u);
        h.live = 0;
        h.used = 0;
        return true;
    }
    case IntrinsicId::MapSize:
    case IntrinsicId::SetSize:
        result(Value::i32v(static_cast<i32>(coll(args[0]).live)));
        return true;
    case IntrinsicId::MapKeys:
    case IntrinsicId::MapValues:
    case IntrinsicId::SetValues: {
        Value out;
        if (!toArray(coll(args[0]), id == IntrinsicId::MapValues, out)) {
            return false;
        }
        result(out);
        return true;
    }
    case IntrinsicId::MapNext:
    case IntrinsicId::SetNext: {
        HashData& h = coll(args[0]);
        const i32 cursor = args[1].asI32();
        const i32 n = next(h, cursor);
        const usize from = cursor < 0 ? 0 : static_cast<usize>(cursor);
        const usize to = n < 0 ? h.keys.size() : static_cast<usize>(n);
        if (!spend(to > from ? to - from : 0)) {
            return fail(Trap::OutOfFuel, "연료를 다 썼다");
        }
        result(Value::i32v(n));
        return true;
    }
    case IntrinsicId::MapKeyAt:
    case IntrinsicId::MapValueAt:
    case IntrinsicId::SetValueAt: {
        HashData& h = coll(args[0]);
        const i32 e = args[1].asI32();
        if (e < 0 || static_cast<usize>(e) >= h.keys.size()) {
            return fail(Trap::IndexOutOfRange, std::format("항목 {} — {} 개", e, h.keys.size()));
        }
        result(id == IntrinsicId::MapValueAt ? h.values[static_cast<usize>(e)] : h.keys[static_cast<usize>(e)]);
        return true;
    }
    case IntrinsicId::SetFromArray: {
        Value out;
        if (!newCollection(false, M.types[M.types[fn.regs[in.b]].elem].kind, out)) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        const usize n = object(args[0])->slots.size();
        for (usize i = 0; i < n; ++i) {
            if (!spend(1)) {
                return fail(Trap::OutOfFuel, "연료를 다 썼다");
            }
            if (!insert(coll(out), object(args[0])->slots[i], {}, false)) {
                return false;
            }
        }
        result(out);
        return true;
    }
    case IntrinsicId::SetUnion:
    case IntrinsicId::SetIntersection:
    case IntrinsicId::SetDifference: {
        if (object(args[1]) == nullptr) {
            return fail(Trap::NullReference, "null 과의 집합 연산");
        }
        Value out;
        if (!newCollection(false, coll(args[0]).keyKind, out)) {
            return fail(Trap::OutOfMemory, "아레나 예산을 넘는다");
        }
        // 왼쪽 차례, 합집합은 그다음 오른쪽에만 있는 것 (넣은 차례 — 결정적)
        const usize na = coll(args[0]).keys.size();
        for (usize e = 0; e < na; ++e) {
            if (coll(args[0]).alive[e] == 0) {
                continue;
            }
            const Value key = coll(args[0]).keys[e];
            bool keep = true;
            if (id != IntrinsicId::SetUnion) {
                Found f;
                if (!find(coll(args[1]), key, f)) {
                    return false;
                }
                keep = (f.entry >= 0) == (id == IntrinsicId::SetIntersection);
            }
            if (keep && !insert(coll(out), key, {}, false)) {
                return false;
            }
        }
        if (id == IntrinsicId::SetUnion) {
            const usize nb = coll(args[1]).keys.size();
            for (usize e = 0; e < nb; ++e) {
                if (coll(args[1]).alive[e] != 0 && !insert(coll(out), coll(args[1]).keys[e], {}, false)) {
                    return false;
                }
            }
        }
        result(out);
        return true;
    }
    default:
        break;
    }
    return fail(Trap::Unreachable, "모르는 내장");
}

} // namespace sbx::script::vm
