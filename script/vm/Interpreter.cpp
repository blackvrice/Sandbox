#include "script/vm/Interpreter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

#include "foundation/assert/Assert.hpp"
#include "foundation/hash/Fnv1a.hpp"
#include "script/api/MathBuiltins.hpp"
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
    const u64 budget = m_limits.fuel;
    u64 fuel = budget;
    if (entry >= M.functions.size() || args.size() != M.functions[entry].params.size()) {
        res.trap = Trap::BadCall;
        res.message = entry >= M.functions.size() ? std::format("함수 {} 가 없다", entry)
                                                  : std::format("인자 {} 개 — 함수는 {} 개를 받는다", args.size(),
                                                                M.functions[entry].params.size());
        return res;
    }

    auto enter = [&](u32 f, u16 resultReg) {
        const bc::Function& fn = M.functions[f];
        if (m_frames.size() >= m_limits.maxDepth || m_regs.size() + fn.regs.size() > m_limits.maxStackSlots) {
            return false;
        }
        m_frames.push_back({f, 0, static_cast<u32>(m_regs.size()), resultReg});
        m_regs.resize(m_regs.size() + fn.regs.size(), Value{});
        return true;
    };
    if (!enter(entry, bc::kNoReg)) {
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

    const bc::Function* F = &M.functions[entry];
    Value* R = m_regs.data();
    u32 pc = 0;
    u32 hostCalls = 0;

    auto trap = [&](Trap t, std::string msg) {
        res.trap = t;
        res.function = m_frames.back().function;
        res.pc = pc;
        res.message = std::move(msg);
        res.fuelUsed = budget - fuel;
        return res;
    };
    auto str = [&](Value v) { return stringOf(v); };
    auto newString = [&](std::string s, Value& out) {
        if (s.size() > m_limits.maxStringBytes) {
            return false;
        }
        const usize bytes = kObjectHeaderBytes + s.size();
        Object o;
        o.kind = Object::Kind::String;
        o.text = std::move(s);
        return alloc(std::move(o), bytes, out);
    };
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
            if (!enter(callee, in.a)) {
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
            if (m_frames.empty()) {
                res.value = result;
                res.fuelUsed = budget - fuel;
                return res;
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
        case Op::Count:
            return trap(Trap::Unreachable, "모르는 명령");
        }
        ++pc;
    }
}

} // namespace sbx::script::vm
