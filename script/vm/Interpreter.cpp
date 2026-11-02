#include "script/vm/Interpreter.hpp"

#include <algorithm>
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
        case Op::INTR: {
            Trap t = Trap::None;
            std::string msg;
            if (!intrinsic(in, *F, R, fuel, t, msg)) {
                return trap(t, std::move(msg));
            }
            break;
        }
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

} // namespace sbx::script::vm
