#include "script/lang/Codegen.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <map>
#include <unordered_map>

#include "script/api/MathBuiltins.hpp"
#include "script/vm/Verifier.hpp"

namespace sbx::script {

namespace {

using bc::Op;

constexpr u32 kUnset = 0xFFFF'FFFFu;

static_assert(kMaxSystemIntervalTicks == bc::kMaxIntervalTicks);

class Gen {
public:
    Gen(const Program& p, DiagnosticList& d) : P(p), D(d) {}

    std::optional<bc::Module> run() {
        M.hostApiFingerprint = api::hostApiFingerprint();
        m_void = addType({bc::TypeKind::Void});
        m_i32 = addType({bc::TypeKind::I32});
        m_f64 = addType({bc::TypeKind::F64});
        m_bool = addType({bc::TypeKind::Bool});
        m_string = addType({bc::TypeKind::String});
        M.functions.resize(P.functions.size());
        for (u32 fi = 0; fi < P.functions.size(); ++fi) {
            function(fi);
        }
        for (const ModuleSema& ms : P.modules) {
            for (const auto& [name, sym] : ms.exportValues) {
                const Symbol& s = P.symbols[sym];
                if (s.kind == SymbolKind::Function) {
                    M.exports.push_back({ms.path, name, s.function});
                }
            }
        }
        // 시스템 표 — id 오름차순 (실행 순서의 둘째 열쇠 · 검증기가 본다, ADR-0062)
        for (const SystemDecl& s : P.systems) {
            bc::SystemDesc d;
            d.id = s.id;
            d.module = s.module;
            d.domain = s.domain;
            d.onUpdate = s.onUpdate;
            d.onTick = s.onTick;
            d.onStart = s.onStart;
            d.onStop = s.onStop;
            d.onEvent = s.onEvent;
            d.intervalTicks = s.intervalTicks;
            d.eventsPerTick = s.eventsPerTick;
            for (const SystemEventDecl& e : s.events) {
                d.events.push_back({e.kind, e.minX, e.minY, e.maxX, e.maxY, e.all, e.any, e.none});
            }
            M.systems.push_back(std::move(d));
        }
        std::sort(M.systems.begin(), M.systems.end(),
                  [](const bc::SystemDesc& a, const bc::SystemDesc& b) { return a.id < b.id; });
        if (m_failed) {
            return std::nullopt;
        }
        const bc::VerifyResult v = bc::verify(M);
        if (!v.ok()) {
            for (const std::string& e : v.errors) {
                D.error("S502", "", {}, "코드 생성이 검증을 통과하지 못했다 (컴파일러 버그): " + e);
            }
            return std::nullopt;
        }
        return std::move(M);
    }

private:
    // ---- 타입 ------------------------------------------------------------------------------------------------------
    u32 addType(bc::TypeDesc d) {
        if (M.types.size() >= bc::kMaxTypes) {
            limit("타입이 너무 많다");
            return 0;
        }
        M.types.push_back(std::move(d));
        return static_cast<u32>(M.types.size() - 1);
    }

    TypeKind kind(TypeId t) const { return P.types.kind(t); }
    bool intLike(TypeId t) const { return kind(t) == TypeKind::I32 || kind(t) == TypeKind::Enum; }

    u32 tid(TypeId t) {
        if (const auto it = m_tid.find(t); it != m_tid.end()) {
            return it->second;
        }
        u32 out = m_i32;
        switch (kind(t)) {
        case TypeKind::Error:
        case TypeKind::Null:
            out = m_i32;
            break; // 검사를 통과한 프로그램에는 없다
        case TypeKind::Void:
            out = m_void;
            break;
        case TypeKind::Bool:
            out = m_bool;
            break;
        case TypeKind::I32:
        case TypeKind::Enum:
            out = m_i32;
            break;
        case TypeKind::Number:
            out = m_f64;
            break;
        case TypeKind::String:
            out = m_string;
            break;
        case TypeKind::Array:
            out = arrayOf(tid(P.types.info(t).elem));
            break;
        case TypeKind::Nullable: {
            const u32 inner = tid(P.types.info(t).elem);
            out = bc::regClass(M, inner) == bc::RegClass::Ref ? inner : boxOf(inner);
            break;
        }
        case TypeKind::Component:
        case TypeKind::Opaque: {
            // ComponentRef<T> 는 불투명 Component (값 = stableId — LOADK). T 는 검사기만 안다 (ADR-0060)
            const auto abiT = kind(t) == TypeKind::Component ? api::AbiType::Component : P.types.info(t).abi;
            const auto abi = static_cast<u8>(abiT);
            if (const auto it = m_opaque.find(abi); it != m_opaque.end()) {
                out = it->second;
            } else {
                bc::TypeDesc d;
                d.kind = bc::TypeKind::Opaque;
                d.abi = abiT;
                out = addType(std::move(d));
                m_opaque[abi] = out;
            }
            break;
        }
        case TypeKind::Struct: {
            // 먼저 자리를 잡는다 (재귀 구조체)
            bc::TypeDesc d;
            d.kind = bc::TypeKind::Struct;
            out = addType(std::move(d));
            m_tid[t] = out;
            const StructType& st = P.types.structOf(t);
            std::vector<u32> order(st.fields.size());
            for (u32 i = 0; i < order.size(); ++i) {
                order[i] = i;
            }
            std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return st.fields[a].name < st.fields[b].name; });
            std::vector<u16> slots(st.fields.size());
            std::vector<std::string> names;
            std::vector<u32> types;
            for (u32 k = 0; k < order.size(); ++k) {
                slots[order[k]] = static_cast<u16>(k);
                names.push_back(st.fields[order[k]].name);
            }
            m_slots[t] = slots;
            for (u32 k = 0; k < order.size(); ++k) {
                types.push_back(tid(st.fields[order[k]].type));
            }
            M.types[out].fieldNames = std::move(names);
            M.types[out].fieldTypes = std::move(types);
            return out;
        }
        }
        m_tid[t] = out;
        return out;
    }
    u32 arrayOf(u32 elem) {
        if (const auto it = m_arrayOf.find(elem); it != m_arrayOf.end()) {
            return it->second;
        }
        bc::TypeDesc d;
        d.kind = bc::TypeKind::Array;
        d.elem = elem;
        const u32 out = addType(std::move(d));
        m_arrayOf[elem] = out;
        return out;
    }
    u32 boxOf(u32 inner) {
        if (const auto it = m_boxOf.find(inner); it != m_boxOf.end()) {
            return it->second;
        }
        bc::TypeDesc d;
        d.kind = bc::TypeKind::Box;
        d.elem = inner;
        const u32 out = addType(std::move(d));
        m_boxOf[inner] = out;
        return out;
    }
    // 구조체 필드 (선언 자리) → 배치 자리 (이름 순)
    u16 slot(TypeId structType, u32 declIndex) {
        tid(structType);
        return m_slots[structType][declIndex];
    }

    // ---- 상수 표 ----------------------------------------------------------------------------------------------------
    u16 str(const std::string& s) {
        if (const auto it = m_strIdx.find(s); it != m_strIdx.end()) {
            return it->second;
        }
        if (M.strings.size() >= bc::kMaxPoolEntries) {
            limit("문자열 상수가 너무 많다");
            return 0;
        }
        M.strings.push_back(s);
        const auto i = static_cast<u16>(M.strings.size() - 1);
        m_strIdx[s] = i;
        return i;
    }
    u16 num(f64 v) {
        const u64 bits = std::bit_cast<u64>(v);
        if (const auto it = m_numIdx.find(bits); it != m_numIdx.end()) {
            return it->second;
        }
        if (M.numbers.size() >= bc::kMaxPoolEntries) {
            limit("수 상수가 너무 많다");
            return 0;
        }
        M.numbers.push_back(v);
        const auto i = static_cast<u16>(M.numbers.size() - 1);
        m_numIdx[bits] = i;
        return i;
    }
    // 컴포넌트 표의 자리 (LOADK) — 처음 쓰는 차례
    u16 component(const std::string& id) {
        for (usize i = 0; i < M.components.size(); ++i) {
            if (M.components[i] == id) {
                return static_cast<u16>(i);
            }
        }
        if (M.components.size() >= bc::kMaxComponents) {
            limit("컴포넌트가 너무 많다");
            return 0;
        }
        M.components.push_back(id);
        return static_cast<u16>(M.components.size() - 1);
    }
    u16 import(u32 hostIndex) {
        if (const auto it = m_importIdx.find(hostIndex); it != m_importIdx.end()) {
            return it->second;
        }
        const api::HostFunction& h = api::kHostFunctions[hostIndex];
        bc::HostImport im;
        im.id = std::string(h.id);
        im.params.push_back(h.owner);
        for (u8 i = 0; i < h.paramCount; ++i) {
            im.params.push_back(h.params[i]);
        }
        im.result = h.result;
        M.imports.push_back(std::move(im));
        const auto i = static_cast<u16>(M.imports.size() - 1);
        m_importIdx[hostIndex] = i;
        return i;
    }

    void limit(std::string what) {
        if (!m_failed) {
            const std::string file = m_module < P.modules.size() ? P.modules[m_module].path : std::string();
            SourcePos pos;
            if (F != nullptr && m_fn < P.functions.size()) {
                pos = P.modules[P.functions[m_fn].module].ast.node(P.functions[m_fn].decl).pos;
            }
            D.error("S501", file, pos, what + " — 함수 · 스크립트를 나눈다");
        }
        m_failed = true;
    }

    // ---- 레지스터 · 명령 ------------------------------------------------------------------------------------------
    u16 newReg(u32 type) {
        if (F->regs.size() >= bc::kMaxRegisters) {
            limit(std::format("레지스터가 {} 개를 넘는다", bc::kMaxRegisters));
            return 0;
        }
        F->regs.push_back(type);
        return static_cast<u16>(F->regs.size() - 1);
    }
    u16 temp(u32 type) {
        auto& free = m_free[type];
        u16 r = 0;
        if (!free.empty()) {
            r = free.back();
            free.pop_back();
        } else {
            r = newReg(type);
        }
        m_temps.push_back(r);
        return r;
    }
    // 연속 레지스터 (호출 인자 · MATHF 의 둘). 임시로 돌려준다
    u16 block(std::span<const u32> types) {
        const auto base = static_cast<u16>(F->regs.size());
        for (const u32 t : types) {
            m_temps.push_back(newReg(t));
        }
        return base;
    }
    void releaseTemps(usize mark) {
        for (usize i = mark; i < m_temps.size(); ++i) {
            const u16 r = m_temps[i];
            if (r < F->regs.size()) {
                m_free[F->regs[r]].push_back(r);
            }
        }
        m_temps.resize(mark);
    }

    usize emit(Op op, u16 a = 0, u16 b = 0, u16 c = 0) {
        if (F->code.size() >= bc::kMaxInstructions) {
            limit("명령이 너무 많다");
            return 0;
        }
        F->code.push_back({op, 0, a, b, c});
        return F->code.size() - 1;
    }
    void emitWide(Op op, u16 a, u32 w) { emit(op, a, static_cast<u16>(w & 0xFFFFu), static_cast<u16>(w >> 16)); }
    [[nodiscard]] usize here() const { return F->code.size(); }
    void patch(usize at, usize target) {
        if (at < F->code.size()) {
            F->code[at].b = static_cast<u16>(target & 0xFFFFu);
            F->code[at].c = static_cast<u16>(target >> 16);
        }
    }
    usize jump(Op op, u16 cond = 0) { return emit(op, cond, 0, 0); }

    // ---- 마디
    // ---------------------------------------------------------------------------------------------------------
    const Ast& ast() const { return MS->ast; }
    const NodeSema& sema(NodeId n) const { return MS->nodes[n]; }
    const ConstValue& cval(NodeId n) const { return MS->consts[n]; }
    TypeId typeOf(NodeId n) const { return sema(n).type; }

    u16 loadConst(const ConstValue& c, TypeId t) {
        if (kind(t) == TypeKind::Nullable) {
            const TypeId inner = P.types.nonNull(t);
            return convert(loadConst(c, inner), inner, t);
        }
        switch (kind(t)) {
        case TypeKind::I32:
        case TypeKind::Enum: {
            const u16 r = temp(m_i32);
            const i32 v = c.kind == ConstValue::Kind::I32 ? c.int32 : static_cast<i32>(c.number);
            emitWide(Op::LOADI, r, static_cast<u32>(v));
            return r;
        }
        case TypeKind::Number: {
            const u16 r = temp(m_f64);
            emit(Op::LOADF, r, num(c.asNumber()));
            return r;
        }
        case TypeKind::Bool: {
            const u16 r = temp(m_bool);
            emit(Op::LOADB, r, c.boolean ? 1 : 0);
            return r;
        }
        case TypeKind::String: {
            const u16 r = temp(m_string);
            emit(Op::LOADS, r, str(c.string));
            return r;
        }
        default:
            return temp(m_i32);
        }
    }

    // 상자 · 참조에서 null 이 아닌 값 (좁힌 이름 · ?? · 널 가능 비교)
    u16 unwrap(u16 r, TypeId nullable) {
        const u32 t = tid(nullable);
        if (M.types[t].kind == bc::TypeKind::Box) {
            const u16 u = temp(M.types[t].elem);
            emit(Op::UNBOX, u, r);
            return u;
        }
        return r;
    }
    u16 isNull(u16 r, TypeId t) {
        const u16 n = temp(tid(t));
        emit(Op::LOADNULL, n);
        const u16 c = temp(m_bool);
        emit(Op::EQR, c, r, n);
        return c;
    }

    // 값 r (타입 from) 을 to 자리로 (검사기가 넣을 수 있다고 한 경우만)
    u16 convert(u16 r, TypeId from, TypeId to) {
        if (from == to || kind(to) == TypeKind::Error || kind(from) == TypeKind::Error) {
            return r;
        }
        if (kind(to) == TypeKind::Nullable) {
            const TypeId inner = P.types.nonNull(to);
            if (from == TypeTable::kNull) {
                const u16 n = temp(tid(to));
                emit(Op::LOADNULL, n);
                return n;
            }
            if (kind(from) == TypeKind::Nullable) {
                if (bc::sameType(M, tid(from), tid(to))) {
                    return r;
                }
                // Nullable(A) → Nullable(B): null 이면 null, 아니면 풀고 바꾸고 다시 싼다
                const u16 dst = temp(tid(to));
                emit(Op::LOADNULL, dst);
                const usize j = jump(Op::JMPT, isNull(r, from));
                const u16 v = convert(unwrap(r, from), P.types.nonNull(from), inner);
                emit(Op::MOV, dst, wrap(v, inner, to));
                patch(j, here());
                return dst;
            }
            return wrap(convert(r, from, inner), inner, to);
        }
        if (bc::sameType(M, tid(from), tid(to))) {
            return r;
        }
        if (kind(to) == TypeKind::Number && intLike(from)) {
            const u16 f = temp(m_f64);
            emit(Op::I2F, f, r);
            return f;
        }
        return r; // 같은 배치 (구조체 · enum) — 검증기가 다시 본다
    }
    u16 wrap(u16 v, TypeId inner, TypeId nullable) {
        const u32 t = tid(nullable);
        if (M.types[t].kind == bc::TypeKind::Box) {
            const u16 b = temp(t);
            emit(Op::BOX, b, v);
            return b;
        }
        (void)inner;
        return v;
    }

    // 마디 n 의 값을 target 자리로 (null 리터럴은 그 자리의 타입으로)
    u16 genTo(NodeId n, TypeId target) {
        if (typeOf(n) == TypeTable::kNull) {
            const u16 r = temp(tid(target));
            emit(Op::LOADNULL, r);
            return r;
        }
        return convert(gen(n), typeOf(n), target);
    }

    u16 toStr(u16 r, TypeId t) {
        switch (kind(t)) {
        case TypeKind::String:
            return r;
        case TypeKind::I32:
        case TypeKind::Enum: {
            const u16 s = temp(m_string);
            emit(Op::TOSTRI, s, r);
            return s;
        }
        case TypeKind::Number: {
            const u16 s = temp(m_string);
            emit(Op::TOSTRF, s, r);
            return s;
        }
        case TypeKind::Bool: {
            const u16 s = temp(m_string);
            emit(Op::TOSTRB, s, r);
            return s;
        }
        default:
            return r;
        }
    }

    // i32 연산의 피연산자 (number 면 JS ToInt32 — 비트 연산)
    u16 asI32(NodeId n) {
        if (kind(typeOf(n)) == TypeKind::Number) {
            const u16 i = temp(m_i32);
            emit(Op::F2I, i, gen(n));
            return i;
        }
        return gen(n);
    }
    u16 asI32Reg(u16 r, TypeId t) {
        if (kind(t) == TypeKind::Number) {
            const u16 i = temp(m_i32);
            emit(Op::F2I, i, r);
            return i;
        }
        return r;
    }

    u16 gen(NodeId n) {
        if (cval(n).has()) {
            return loadConst(cval(n), typeOf(n));
        }
        const Node& x = ast().node(n);
        switch (x.kind) {
        case NodeKind::Null: {
            const u16 r = temp(m_string);
            emit(Op::LOADNULL, r);
            return r;
        }
        case NodeKind::Ident: {
            const SymbolId sid = sema(n).refIndex;
            const Symbol& s = P.symbols[sid];
            if (s.kind == SymbolKind::Component) {
                const u16 r = temp(tid(typeOf(n)));
                emit(Op::LOADK, r, component(P.components[s.function].schema.id));
                return r;
            }
            const auto it = m_symReg.find(sid);
            const u16 r = it == m_symReg.end() ? temp(tid(typeOf(n))) : it->second;
            if (s.type != typeOf(n) && kind(s.type) == TypeKind::Nullable) {
                return unwrap(r, s.type); // 좁힌 이름
            }
            return r;
        }
        case NodeKind::Paren:
        case NodeKind::AsConst:
            return convert(gen(ast().kid(n, 0)), typeOf(ast().kid(n, 0)), typeOf(n));
        case NodeKind::Unary:
            return unary(n);
        case NodeKind::Binary:
            return binary(n);
        case NodeKind::Assign:
            assign(n);
            return bc::kNoReg;
        case NodeKind::Update:
            update(n);
            return bc::kNoReg;
        case NodeKind::Conditional: {
            const TypeId t = typeOf(n);
            const u16 dst = temp(tid(t));
            const usize jElse = jump(Op::JMPF, gen(ast().kid(n, 0)));
            emit(Op::MOV, dst, genTo(ast().kid(n, 1), t));
            const usize jEnd = jump(Op::JMP);
            patch(jElse, here());
            emit(Op::MOV, dst, genTo(ast().kid(n, 2), t));
            patch(jEnd, here());
            return dst;
        }
        case NodeKind::Call:
            return call(n);
        case NodeKind::Member:
            return member(n);
        case NodeKind::Index: {
            const u16 arr = gen(ast().kid(n, 0));
            const u16 idx = index(ast().kid(n, 1));
            const u16 r = temp(tid(typeOf(n)));
            emit(Op::ARRGET, r, arr, idx);
            return r;
        }
        case NodeKind::ArrayLit: {
            const TypeId t = typeOf(n);
            const TypeId elem = P.types.info(t).elem;
            const u16 dst = temp(tid(t));
            emit(Op::NEWARR, dst);
            for (const NodeId e : ast().kids(n)) {
                emit(Op::ARRPUSH, bc::kNoReg, dst, genTo(e, elem));
            }
            return dst;
        }
        case NodeKind::ObjectLit: {
            const TypeId t = typeOf(n);
            const u16 dst = temp(tid(t));
            emit(Op::NEWOBJ, dst);
            const StructType& st = P.types.structOf(t);
            for (const NodeId p : ast().kids(n)) {
                if (ast().node(p).kind != NodeKind::Property) {
                    continue;
                }
                const i32 fi = st.indexOf(ast().str(p));
                if (fi < 0) {
                    continue;
                }
                const TypeId ft = st.fields[static_cast<usize>(fi)].type;
                const u16 v = genTo(ast().kid(p, 0), ft);
                emit(Op::SETF, dst, slot(t, static_cast<u32>(fi)), v);
            }
            return dst;
        }
        case NodeKind::Template: {
            u16 acc = bc::kNoReg;
            for (const NodeId k : ast().kids(n)) {
                u16 part = 0;
                if (ast().node(k).kind == NodeKind::String) {
                    if (ast().str(k).empty() && acc != bc::kNoReg) {
                        continue;
                    }
                    part = temp(m_string);
                    emit(Op::LOADS, part, str(std::string(ast().str(k))));
                } else {
                    part = toStr(gen(k), typeOf(k));
                }
                if (acc == bc::kNoReg) {
                    acc = part;
                } else {
                    const u16 next = temp(m_string);
                    emit(Op::CONCAT, next, acc, part);
                    acc = next;
                }
            }
            return acc == bc::kNoReg ? loadEmptyString() : acc;
        }
        default:
            return temp(m_i32);
        }
    }

    u16 loadEmptyString() {
        const u16 r = temp(m_string);
        emit(Op::LOADS, r, str(""));
        return r;
    }

    u16 index(NodeId idx) {
        if (intLike(typeOf(idx))) {
            return gen(idx);
        }
        const u16 i = temp(m_i32);
        emit(Op::F2IDX, i, genTo(idx, TypeTable::kNumber));
        return i;
    }

    u16 unary(NodeId n) {
        const Node& x = ast().node(n);
        const NodeId a = ast().kid(n, 0);
        const TypeId t = typeOf(n);
        switch (x.op) {
        case Tok::Minus: {
            const u16 v = genTo(a, t);
            const u16 r = temp(tid(t));
            emit(kind(t) == TypeKind::Number ? Op::NEGF : Op::NEGI, r, v);
            return r;
        }
        case Tok::Plus:
            return genTo(a, t);
        case Tok::Tilde: {
            const u16 v = asI32(a);
            const u16 r = temp(m_i32);
            emit(Op::NOTI, r, v);
            return r;
        }
        case Tok::Bang: {
            const u16 v = gen(a);
            const u16 r = temp(m_bool);
            emit(Op::NOTB, r, v);
            return r;
        }
        default:
            return temp(tid(t));
        }
    }

    // 같음 (===) — 두 레지스터 (타입 ta · tb, null 리터럴 아님)
    u16 equalRegs(u16 ra, TypeId ta, u16 rb, TypeId tb) {
        const bool na = kind(ta) == TypeKind::Nullable;
        const bool nb = kind(tb) == TypeKind::Nullable;
        if (na || nb) {
            // 같은 참조 배치의 null 가능 (string | null 은 내용 비교라 아래로)
            const u16 dst = temp(m_bool);
            if (na && nb) {
                const u16 c1 = isNull(ra, ta);
                const u16 c2 = isNull(rb, tb);
                const usize j1 = jump(Op::JMPT, c1);
                const usize j2 = jump(Op::JMPT, c2);
                // 인자의 계산 순서는 C++ 가 정하지 않는다 — 명령 순서가 툴체인마다 같게 하나씩
                const u16 ua = unwrap(ra, ta);
                const u16 ub = unwrap(rb, tb);
                emit(Op::MOV, dst, equalRegs(ua, P.types.nonNull(ta), ub, P.types.nonNull(tb)));
                const usize jEnd1 = jump(Op::JMP);
                patch(j1, here());
                emit(Op::MOV, dst, c2); // 둘 다 null 이면 참
                const usize jEnd2 = jump(Op::JMP);
                patch(j2, here());
                emit(Op::LOADB, dst, 0);
                patch(jEnd1, here());
                patch(jEnd2, here());
                return dst;
            }
            const u16 r = na ? ra : rb;
            const TypeId t = na ? ta : tb;
            const usize jNull = jump(Op::JMPT, isNull(r, t));
            const u16 v = unwrap(r, t);
            emit(Op::MOV, dst,
                 na ? equalRegs(v, P.types.nonNull(ta), rb, tb) : equalRegs(ra, ta, v, P.types.nonNull(tb)));
            const usize jEnd = jump(Op::JMP);
            patch(jNull, here());
            emit(Op::LOADB, dst, 0);
            patch(jEnd, here());
            return dst;
        }
        const u16 dst = temp(m_bool);
        if (P.types.isNumeric(ta) && P.types.isNumeric(tb)) {
            if (intLike(ta) && intLike(tb)) {
                emit(Op::EQI, dst, ra, rb);
            } else {
                const u16 fa = convert(ra, ta, TypeTable::kNumber);
                const u16 fb = convert(rb, tb, TypeTable::kNumber);
                emit(Op::EQF, dst, fa, fb);
            }
            return dst;
        }
        switch (kind(ta)) {
        case TypeKind::Bool:
            emit(Op::EQB, dst, ra, rb);
            break;
        case TypeKind::String:
            emit(Op::EQS, dst, ra, rb);
            break;
        case TypeKind::Opaque:
        case TypeKind::Component:
            emit(Op::EQO, dst, ra, rb);
            break;
        default:
            emit(Op::EQR, dst, ra, rb);
            break;
        }
        return dst;
    }

    u16 equal(NodeId a, NodeId b) {
        if (typeOf(a) == TypeTable::kNull) {
            std::swap(a, b);
        }
        if (typeOf(b) == TypeTable::kNull) {
            if (typeOf(a) == TypeTable::kNull) {
                const u16 r = temp(m_bool);
                emit(Op::LOADB, r, 1);
                return r;
            }
            return isNull(gen(a), typeOf(a));
        }
        const u16 ra = gen(a);
        const u16 rb = gen(b);
        return equalRegs(ra, typeOf(a), rb, typeOf(b));
    }

    u16 binary(NodeId n) {
        const Node& x = ast().node(n);
        const Tok op = x.op;
        const NodeId a = ast().kid(n, 0);
        const NodeId b = ast().kid(n, 1);
        const TypeId t = typeOf(n);
        switch (op) {
        case Tok::AmpAmp:
        case Tok::PipePipe: {
            const u16 dst = temp(m_bool);
            emit(Op::MOV, dst, gen(a));
            const usize j = jump(op == Tok::AmpAmp ? Op::JMPF : Op::JMPT, dst);
            emit(Op::MOV, dst, gen(b));
            patch(j, here());
            return dst;
        }
        case Tok::QuestionQuestion: {
            const u16 dst = temp(tid(t));
            const u16 l = gen(a);
            const TypeId lt = typeOf(a);
            const usize jNull = jump(Op::JMPT, isNull(l, lt));
            emit(Op::MOV, dst, convert(unwrap(l, lt), P.types.nonNull(lt), t));
            const usize jEnd = jump(Op::JMP);
            patch(jNull, here());
            emit(Op::MOV, dst, genTo(b, t));
            patch(jEnd, here());
            return dst;
        }
        case Tok::EqEqEq:
        case Tok::EqEq:
            return equal(a, b);
        case Tok::NotEqEq:
        case Tok::NotEq: {
            const u16 e = equal(a, b);
            const u16 r = temp(m_bool);
            emit(Op::NOTB, r, e);
            return r;
        }
        case Tok::Less:
        case Tok::LessEq:
        case Tok::Greater:
        case Tok::GreaterEq: {
            const bool swap = op == Tok::Greater || op == Tok::GreaterEq;
            const bool le = op == Tok::LessEq || op == Tok::GreaterEq;
            const u16 dst = temp(m_bool);
            // 값은 소스 순서 (a 다음 b) 로 계산하고, > · >= 는 피연산자 자리만 바꾼다 (JS 의 계산 순서)
            if (kind(typeOf(a)) == TypeKind::String) {
                const u16 va = gen(a);
                const u16 vb = gen(b);
                emit(le ? Op::LES : Op::LTS, dst, swap ? vb : va, swap ? va : vb);
            } else if (intLike(typeOf(a)) && intLike(typeOf(b))) {
                const u16 va = gen(a);
                const u16 vb = gen(b);
                emit(le ? Op::LEI : Op::LTI, dst, swap ? vb : va, swap ? va : vb);
            } else {
                const u16 va = genTo(a, TypeTable::kNumber);
                const u16 vb = genTo(b, TypeTable::kNumber);
                emit(le ? Op::LEF : Op::LTF, dst, swap ? vb : va, swap ? va : vb);
            }
            return dst;
        }
        default:
            break;
        }
        if (kind(t) == TypeKind::String) {
            const u16 l = toStr(gen(a), typeOf(a));
            const u16 r = toStr(gen(b), typeOf(b));
            const u16 dst = temp(m_string);
            emit(Op::CONCAT, dst, l, r);
            return dst;
        }
        if (kind(t) == TypeKind::I32) {
            const u16 l = asI32(a);
            const u16 r = asI32(b);
            const u16 dst = temp(m_i32);
            emit(intOp(op), dst, l, r);
            return dst;
        }
        const u16 l = genTo(a, TypeTable::kNumber);
        const u16 r = genTo(b, TypeTable::kNumber);
        const u16 dst = temp(m_f64);
        emit(floatOp(op), dst, l, r);
        return dst;
    }

    static Op intOp(Tok op) {
        switch (op) {
        case Tok::Plus:
        case Tok::PlusAssign:
        case Tok::PlusPlus:
            return Op::ADDI;
        case Tok::Minus:
        case Tok::MinusAssign:
        case Tok::MinusMinus:
            return Op::SUBI;
        case Tok::Star:
        case Tok::StarAssign:
            return Op::MULI;
        case Tok::Percent:
        case Tok::PercentAssign:
            return Op::REMI;
        case Tok::Amp:
        case Tok::AmpAssign:
            return Op::ANDI;
        case Tok::Pipe:
        case Tok::PipeAssign:
            return Op::ORI;
        case Tok::Caret:
        case Tok::CaretAssign:
            return Op::XORI;
        case Tok::Shl:
        case Tok::ShlAssign:
            return Op::SHLI;
        case Tok::Shr:
        case Tok::ShrAssign:
            return Op::SHRI;
        default:
            return Op::NOP;
        }
    }
    static Op floatOp(Tok op) {
        switch (op) {
        case Tok::Plus:
        case Tok::PlusAssign:
        case Tok::PlusPlus:
            return Op::ADDF;
        case Tok::Minus:
        case Tok::MinusAssign:
        case Tok::MinusMinus:
            return Op::SUBF;
        case Tok::Star:
        case Tok::StarAssign:
            return Op::MULF;
        case Tok::Slash:
        case Tok::SlashAssign:
            return Op::DIVF;
        case Tok::Percent:
        case Tok::PercentAssign:
            return Op::REMF;
        case Tok::StarStar:
        case Tok::StarStarAssign:
            return Op::POWF;
        default:
            return Op::NOP;
        }
    }
    static bool isBitwise(Tok op) {
        return op == Tok::Amp || op == Tok::Pipe || op == Tok::Caret || op == Tok::Shl || op == Tok::Shr ||
               op == Tok::AmpAssign || op == Tok::PipeAssign || op == Tok::CaretAssign || op == Tok::ShlAssign ||
               op == Tok::ShrAssign;
    }

    // ---- 대입 -----------------------------------------------------------------------------------------------------
    struct Place {
        enum class Kind : u8 { Reg, Field, Element } kind = Kind::Reg;
        u16 reg = 0;   // Reg: 변수 레지스터 · Field/Element: 객체 · 배열
        u16 slot = 0;  // Field
        u16 index = 0; // Element
        TypeId type = TypeTable::kError;
    };

    Place place(NodeId target) {
        Place p;
        const Node& t = ast().node(target);
        if (t.kind == NodeKind::Ident) {
            const SymbolId sid = sema(target).refIndex;
            p.kind = Place::Kind::Reg;
            p.reg = m_symReg[sid];
            p.type = P.symbols[sid].type;
        } else if (t.kind == NodeKind::Member) {
            const NodeId obj = ast().kid(target, 0);
            p.kind = Place::Kind::Field;
            p.reg = gen(obj);
            p.slot = slot(typeOf(obj), sema(target).refIndex);
            p.type = typeOf(target);
        } else {
            p.kind = Place::Kind::Element;
            p.reg = gen(ast().kid(target, 0));
            p.index = index(ast().kid(target, 1));
            p.type = typeOf(target);
        }
        return p;
    }
    u16 load(const Place& p) {
        if (p.kind == Place::Kind::Reg) {
            return p.reg;
        }
        const u16 r = temp(tid(p.type));
        if (p.kind == Place::Kind::Field) {
            emit(Op::GETF, r, p.reg, p.slot);
        } else {
            emit(Op::ARRGET, r, p.reg, p.index);
        }
        return r;
    }
    void store(const Place& p, u16 v) {
        switch (p.kind) {
        case Place::Kind::Reg:
            if (v != p.reg) {
                emit(Op::MOV, p.reg, v);
            }
            break;
        case Place::Kind::Field:
            emit(Op::SETF, p.reg, p.slot, v);
            break;
        case Place::Kind::Element:
            emit(Op::ARRSET, p.reg, p.index, v);
            break;
        }
    }

    void assign(NodeId n) {
        const Tok op = ast().node(n).op;
        const NodeId value = ast().kid(n, 1);
        const Place p = place(ast().kid(n, 0));
        if (op == Tok::Assign) {
            store(p, genTo(value, p.type));
            return;
        }
        const u16 cur = load(p);
        const TypeId vt = typeOf(value);
        if (op == Tok::AmpAmpAssign || op == Tok::PipePipeAssign) {
            const u16 t = temp(m_bool);
            emit(Op::MOV, t, cur);
            const usize j = jump(op == Tok::AmpAmpAssign ? Op::JMPF : Op::JMPT, t);
            emit(Op::MOV, t, gen(value));
            patch(j, here());
            store(p, t);
            return;
        }
        if (op == Tok::QuestionQuestionAssign) {
            const u16 t = temp(tid(p.type));
            emit(Op::MOV, t, cur);
            const usize j = jump(Op::JMPF, isNull(cur, p.type));
            emit(Op::MOV, t, genTo(value, p.type));
            patch(j, here());
            store(p, t);
            return;
        }
        if (kind(p.type) == TypeKind::String) {
            const u16 s = toStr(gen(value), vt);
            const u16 t = temp(m_string);
            emit(Op::CONCAT, t, cur, s);
            store(p, t);
            return;
        }
        if (isBitwise(op)) {
            const u16 l = asI32Reg(cur, p.type);
            const u16 r = asI32(value);
            u16 t = temp(m_i32);
            emit(intOp(op), t, l, r);
            if (kind(p.type) == TypeKind::Number) {
                const u16 f = temp(m_f64);
                emit(Op::I2F, f, t);
                t = f;
            }
            store(p, t);
            return;
        }
        if (intLike(p.type)) {
            const u16 r = gen(value);
            const u16 t = temp(m_i32);
            emit(intOp(op), t, cur, r);
            store(p, t);
            return;
        }
        const u16 r = genTo(value, TypeTable::kNumber);
        const u16 t = temp(m_f64);
        emit(floatOp(op), t, cur, r);
        store(p, t);
    }

    void update(NodeId n) {
        const Tok op = ast().node(n).op;
        const Place p = place(ast().kid(n, 0));
        const u16 cur = load(p);
        if (intLike(p.type)) {
            const u16 one = temp(m_i32);
            emitWide(Op::LOADI, one, 1);
            const u16 t = temp(m_i32);
            emit(op == Tok::PlusPlus ? Op::ADDI : Op::SUBI, t, cur, one);
            store(p, t);
        } else {
            const u16 one = temp(m_f64);
            emit(Op::LOADF, one, num(1.0));
            const u16 t = temp(m_f64);
            emit(op == Tok::PlusPlus ? Op::ADDF : Op::SUBF, t, cur, one);
            store(p, t);
        }
    }

    // ---- 호출 · 멤버 ----------------------------------------------------------------------------------------------
    u16 call(NodeId n) {
        const auto kids = ast().kids(n);
        const NodeId callee = kids[0];
        const NodeSema& cs = sema(callee);
        const TypeId rt = typeOf(n);
        if (cs.ref == RefKind::Symbol) {
            const Symbol& s = P.symbols[cs.refIndex];
            if (s.kind == SymbolKind::Function) {
                const FunctionInfo& f = P.functions[s.function];
                std::vector<u32> types;
                for (const TypeId pt : f.params) {
                    types.push_back(tid(pt));
                }
                const u16 base = block(types);
                for (usize i = 0; i < f.params.size() && i + 1 < kids.size(); ++i) {
                    emit(Op::MOV, static_cast<u16>(base + i), genTo(kids[i + 1], f.params[i]));
                }
                const u16 dst = kind(f.result) == TypeKind::Void ? bc::kNoReg : temp(tid(f.result));
                emit(Op::CALL, dst, static_cast<u16>(s.function), base);
                return dst;
            }
            if (s.builtin == Builtin::ToI32) {
                const u16 r = temp(m_i32);
                emit(Op::F2I, r, genTo(kids[1], TypeTable::kNumber));
                return r;
            }
            if (s.builtin == Builtin::Idiv) {
                const u16 l = genTo(kids[1], TypeTable::kI32);
                const u16 r = genTo(kids[2], TypeTable::kI32);
                const u16 dst = temp(m_i32);
                emit(Op::IDIV, dst, l, r);
                return dst;
            }
            return temp(tid(rt));
        }
        if (cs.ref == RefKind::Builtin) {
            return mathCall(n, cs.refIndex - static_cast<u32>(Builtin::MathFunctionBase));
        }
        if (cs.ref == RefKind::HostFunction) {
            const api::HostFunction& h = api::kHostFunctions[cs.refIndex];
            const NodeId obj = ast().kid(callee, 0);
            const u16 recv = gen(obj);
            std::vector<u32> types{tid(typeOf(obj))};
            std::vector<u16> vals{recv};
            // 컴포넌트 항목의 T = 컴포넌트 인자의 값 구조체 (검사기가 맞췄다)
            TypeId data = TypeTable::kError;
            for (u8 i = 0; i < h.paramCount && i + 1u < kids.size(); ++i) {
                if (h.params[i] == api::AbiType::Component) {
                    data = P.types.info(typeOf(kids[i + 1])).elem;
                }
            }
            for (u8 i = 0; i < h.paramCount && i + 1u < kids.size(); ++i) {
                // 불투명 인자는 그 식의 타입 그대로 (검사기가 같은 ABI 인지 봤다)
                const bool opaque = h.params[i] == api::AbiType::Entity || h.params[i] == api::AbiType::ServerContext ||
                                    h.params[i] == api::AbiType::ClientContext ||
                                    h.params[i] == api::AbiType::Component;
                const TypeId pt = opaque                                       ? typeOf(kids[i + 1])
                                  : h.params[i] == api::AbiType::ComponentData ? data
                                                                               : hostType(h.params[i]);
                types.push_back(tid(pt));
                vals.push_back(genTo(kids[i + 1], pt));
            }
            const u16 base = block(types);
            for (usize i = 0; i < vals.size(); ++i) {
                emit(Op::MOV, static_cast<u16>(base + i), vals[i]);
            }
            const u16 dst = h.result == api::AbiType::Void ? bc::kNoReg : temp(tid(rt));
            emit(Op::CALLH, dst, import(cs.refIndex), base);
            return dst;
        }
        if (cs.ref == RefKind::ArrayPush) {
            const NodeId obj = ast().kid(callee, 0);
            const u16 arr = gen(obj);
            const TypeId elem = P.types.info(typeOf(obj)).elem;
            const u16 v = genTo(kids[1], elem);
            const u16 dst = temp(m_i32);
            emit(Op::ARRPUSH, dst, arr, v);
            return dst;
        }
        return temp(tid(rt));
    }

    static TypeId hostType(api::AbiType a) {
        switch (a) {
        case api::AbiType::Bool:
            return TypeTable::kBool;
        case api::AbiType::I32:
            return TypeTable::kI32;
        case api::AbiType::Number:
            return TypeTable::kNumber;
        case api::AbiType::String:
            return TypeTable::kString;
        default:
            return TypeTable::kError;
        }
    }

    u16 mathCall(NodeId n, u32 fnIndex) {
        const auto kids = ast().kids(n);
        const MathFunction& f = kMathFunctions[fnIndex];
        const usize argc = kids.size() - 1;
        if (kind(typeOf(n)) == TypeKind::I32) { // min · max 의 i32
            u16 acc = gen(kids[1]);
            for (usize i = 2; i <= argc; ++i) {
                const u16 r = gen(kids[i]);
                const u16 t = temp(m_i32);
                emit(f.name == "min" ? Op::MINI : Op::MAXI, t, acc, r);
                acc = t;
            }
            return acc;
        }
        std::vector<u16> args;
        for (usize i = 1; i <= argc; ++i) {
            args.push_back(genTo(kids[i], TypeTable::kNumber));
        }
        if (f.arity == 1) {
            const u16 dst = temp(m_f64);
            emit(Op::MATHF, dst, args[0], static_cast<u16>(fnIndex));
            return dst;
        }
        if (args.size() == 1) { // Math.min(x) = x, Math.hypot(x) = |x|
            if (f.name == "hypot") {
                const u16 dst = temp(m_f64);
                emit(Op::MATHF, dst, args[0], static_cast<u16>(mathIndex("abs")));
                return dst;
            }
            return args[0];
        }
        // 둘씩 잇는다 (가변 인자 — ADR-0058 · 19 12장)
        u16 acc = args[0];
        for (usize i = 1; i < args.size(); ++i) {
            const u32 two[] = {m_f64, m_f64};
            const u16 base = block(two);
            emit(Op::MOV, base, acc);
            emit(Op::MOV, static_cast<u16>(base + 1), args[i]);
            const u16 t = temp(m_f64);
            emit(Op::MATHF, t, base, static_cast<u16>(fnIndex));
            acc = t;
        }
        return acc;
    }
    static u32 mathIndex(std::string_view name) {
        for (u32 i = 0; i < std::size(kMathFunctions); ++i) {
            if (kMathFunctions[i].name == name) {
                return i;
            }
        }
        return 0;
    }

    u16 member(NodeId n) {
        const NodeSema& s = sema(n);
        const NodeId obj = ast().kid(n, 0);
        switch (s.ref) {
        case RefKind::Field: {
            const u16 o = gen(obj);
            const u16 r = temp(tid(typeOf(n)));
            emit(Op::GETF, r, o, slot(typeOf(obj), s.refIndex));
            return r;
        }
        case RefKind::HostFunction: {
            const u16 recv = gen(obj);
            const u32 types[] = {tid(typeOf(obj))};
            const u16 base = block(types);
            emit(Op::MOV, base, recv);
            const u16 dst = temp(tid(typeOf(n)));
            emit(Op::CALLH, dst, import(s.refIndex), base);
            return dst;
        }
        case RefKind::ArrayLength:
        case RefKind::StringLength: {
            const u16 o = gen(obj);
            const u16 r = temp(m_i32);
            emit(s.ref == RefKind::ArrayLength ? Op::ARRLEN : Op::STRLEN, r, o);
            return r;
        }
        default:
            return temp(tid(typeOf(n)));
        }
    }

    // ---- 문장 ------------------------------------------------------------------------------------------------------
    struct Loop {
        bool isLoop = true; // false = switch (break 만)
        std::vector<usize> breaks;
        std::vector<usize> continues;
    };

    void statement(NodeId s) {
        if (s == kNoNode) {
            return;
        }
        const usize mark = m_temps.size();
        const Node& x = ast().node(s);
        switch (x.kind) {
        case NodeKind::Block:
            for (const NodeId k : ast().kids(s)) {
                statement(k);
            }
            break;
        case NodeKind::VarDecl: {
            const SymbolId sid = sema(s).refIndex;
            const TypeId t = P.symbols[sid].type;
            const u16 reg = newReg(tid(t));
            const NodeId init = ast().kid(s, 1);
            if (init != kNoNode) {
                const u16 v = genTo(init, t);
                emit(Op::MOV, reg, v);
            }
            m_symReg[sid] = reg;
            break;
        }
        case NodeKind::ExprStmt:
            gen(ast().kid(s, 0));
            break;
        case NodeKind::If: {
            const usize jElse = jump(Op::JMPF, gen(ast().kid(s, 0)));
            statement(ast().kid(s, 1));
            if (ast().kid(s, 2) != kNoNode) {
                const usize jEnd = jump(Op::JMP);
                patch(jElse, here());
                statement(ast().kid(s, 2));
                patch(jEnd, here());
            } else {
                patch(jElse, here());
            }
            break;
        }
        case NodeKind::While: {
            const usize top = here();
            const usize jEnd = jump(Op::JMPF, gen(ast().kid(s, 0)));
            m_loops.push_back({});
            statement(ast().kid(s, 1));
            jumpTo(Op::JMP, 0, top);
            closeLoop(top, here());
            patch(jEnd, here());
            break;
        }
        case NodeKind::DoWhile: {
            const usize top = here();
            m_loops.push_back({});
            statement(ast().kid(s, 0));
            const usize cont = here();
            jumpTo(Op::JMPT, gen(ast().kid(s, 1)), top);
            closeLoop(cont, here());
            break;
        }
        case NodeKind::For: {
            statement(ast().kid(s, 0));
            const usize top = here();
            usize jEnd = kUnset;
            if (const NodeId cond = ast().kid(s, 1); cond != kNoNode) {
                jEnd = jump(Op::JMPF, gen(cond));
            }
            m_loops.push_back({});
            statement(ast().kid(s, 3));
            const usize cont = here();
            statement(ast().kid(s, 2));
            jumpTo(Op::JMP, 0, top);
            closeLoop(cont, here());
            if (jEnd != kUnset) {
                patch(jEnd, here());
            }
            break;
        }
        case NodeKind::ForOf: {
            const NodeId decl = ast().kid(s, 0);
            const NodeId iter = ast().kid(s, 1);
            const TypeId at = typeOf(iter);
            const u16 arr = newReg(tid(at));
            emit(Op::MOV, arr, gen(iter));
            const u16 idx = newReg(m_i32);
            emitWide(Op::LOADI, idx, 0);
            const SymbolId vs = sema(decl).refIndex;
            const u16 var = newReg(tid(P.symbols[vs].type));
            m_symReg[vs] = var;
            const u16 len = temp(m_i32);
            const u16 c = temp(m_bool);
            const u16 one = temp(m_i32);
            const usize top = here();
            emit(Op::ARRLEN, len, arr); // 길이는 돌 때마다 다시 읽는다 (JS 의 for-of 와 같다 — 몸에서 push 하면 보인다)
            emit(Op::LTI, c, idx, len);
            const usize jEnd = jump(Op::JMPF, c);
            emit(Op::ARRGET, var, arr, idx);
            m_loops.push_back({});
            statement(ast().kid(s, 2));
            const usize cont = here();
            emitWide(Op::LOADI, one, 1);
            emit(Op::ADDI, idx, idx, one);
            jumpTo(Op::JMP, 0, top);
            closeLoop(cont, here());
            patch(jEnd, here());
            break;
        }
        case NodeKind::Break:
            if (!m_loops.empty()) { // 가장 안쪽 반복문 · switch
                m_loops.back().breaks.push_back(jump(Op::JMP));
            }
            break;
        case NodeKind::Continue:
            for (auto it = m_loops.rbegin(); it != m_loops.rend(); ++it) {
                if (it->isLoop) {
                    it->continues.push_back(jump(Op::JMP));
                    break;
                }
            }
            break;
        case NodeKind::Return: {
            const NodeId v = ast().kid(s, 0);
            if (v == kNoNode) {
                emit(Op::RET);
            } else {
                emit(Op::RETV, genTo(v, P.functions[m_fn].result));
            }
            break;
        }
        case NodeKind::Switch:
            switchStatement(s);
            break;
        default:
            break;
        }
        releaseTemps(mark);
    }

    void jumpTo(Op op, u16 cond, usize target) { patch(jump(op, cond), target); }

    void closeLoop(usize continueTarget, usize breakTarget) {
        Loop l = std::move(m_loops.back());
        m_loops.pop_back();
        for (const usize j : l.breaks) {
            patch(j, breakTarget);
        }
        for (const usize j : l.continues) {
            patch(j, continueTarget);
        }
    }

    void switchStatement(NodeId s) {
        const auto kids = ast().kids(s);
        const NodeId disc = kids[0];
        const TypeId dt = typeOf(disc);
        const u16 d = newReg(tid(dt));
        emit(Op::MOV, d, gen(disc));
        std::vector<usize> caseJumps(kids.size(), kUnset);
        usize defaultCase = kUnset;
        for (usize i = 1; i < kids.size(); ++i) {
            const NodeId test = ast().kid(kids[i], 0);
            if (test == kNoNode) {
                defaultCase = i;
                continue;
            }
            const usize mark = m_temps.size();
            const u16 v = gen(test);
            caseJumps[i] = jump(Op::JMPT, equalRegs(d, dt, v, typeOf(test)));
            releaseTemps(mark);
        }
        const usize jDefault = jump(Op::JMP);
        Loop sw;
        sw.isLoop = false;
        m_loops.push_back(std::move(sw));
        for (usize i = 1; i < kids.size(); ++i) {
            if (i == defaultCase) {
                patch(jDefault, here());
            } else {
                patch(caseJumps[i], here());
            }
            const auto body = ast().kids(kids[i]);
            for (usize k = 1; k < body.size(); ++k) {
                statement(body[k]);
            }
        }
        if (defaultCase == kUnset) {
            patch(jDefault, here());
        }
        Loop l = std::move(m_loops.back());
        m_loops.pop_back();
        for (const usize j : l.breaks) {
            patch(j, here());
        }
    }

    void function(u32 fi) {
        const FunctionInfo& info = P.functions[fi];
        m_fn = fi;
        m_module = info.module;
        MS = &P.modules[info.module];
        F = &M.functions[fi];
        m_symReg.clear();
        m_free.clear();
        m_temps.clear();
        m_loops.clear();
        F->name = std::format("{}:{}", MS->path, info.name);
        for (usize i = 0; i < info.params.size(); ++i) {
            const u32 t = tid(info.params[i]);
            F->params.push_back(t);
            m_symReg[info.paramSymbols[i]] = newReg(t);
        }
        F->result = tid(info.result);
        const NodeId body = MS->ast.kids(info.decl).back();
        for (const NodeId s : MS->ast.kids(body)) {
            statement(s);
        }
        if (kind(info.result) == TypeKind::Void) {
            emit(Op::RET);
        } else {
            emit(Op::TRAP, static_cast<u16>(bc::TrapCode::Unreachable));
        }
    }

    const Program& P;
    DiagnosticList& D;
    bc::Module M;
    bool m_failed = false;
    u32 m_void = 0;
    u32 m_i32 = 0;
    u32 m_f64 = 0;
    u32 m_bool = 0;
    u32 m_string = 0;
    std::unordered_map<TypeId, u32> m_tid;
    std::unordered_map<TypeId, std::vector<u16>> m_slots;
    std::map<u32, u32> m_arrayOf;
    std::map<u32, u32> m_boxOf;
    std::map<u8, u32> m_opaque;
    std::map<std::string, u16> m_strIdx;
    std::map<u64, u16> m_numIdx;
    std::map<u32, u16> m_importIdx;

    bc::Function* F = nullptr;
    const ModuleSema* MS = nullptr;
    u32 m_fn = 0;
    u32 m_module = 0;
    std::unordered_map<SymbolId, u16> m_symReg;
    std::map<u32, std::vector<u16>> m_free;
    std::vector<u16> m_temps;
    std::vector<Loop> m_loops;
};

} // namespace

std::optional<bc::Module> generateBytecode(const Program& program, DiagnosticList& diags) {
    if (!diags.ok()) {
        diags.error("S500", "", {}, "검사 오류가 있어 코드를 만들지 않는다");
        return std::nullopt;
    }
    return Gen(program, diags).run();
}

std::optional<bc::Module> compileScripts(std::span<const SourceFile> files, DiagnosticList& diags) {
    const Program p = checkProgram(files, diags);
    if (!diags.ok()) {
        return std::nullopt;
    }
    return generateBytecode(p, diags);
}

} // namespace sbx::script
