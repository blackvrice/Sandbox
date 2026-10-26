#include "script/vm/Verifier.hpp"

#include <algorithm>
#include <format>

#include "foundation/text/Utf8.hpp"
#include "script/api/ComponentSchema.hpp"
#include "script/api/MathBuiltins.hpp"

namespace sbx::script::bc {

namespace {

bool validUtf8(std::string_view s) {
    usize i = 0;
    while (i < s.size()) {
        const usize start = i;
        const char32_t cp = utf8::decodeNext(s, i);
        if (cp == utf8::kReplacement && s.substr(start, 3) != "\xEF\xBF\xBD") {
            return false;
        }
    }
    return true;
}

bool sameRec(const Module& m, u32 a, u32 b, std::vector<std::pair<u32, u32>>& assumed) {
    if (a == b) {
        return true;
    }
    if (a >= m.types.size() || b >= m.types.size()) {
        return false;
    }
    const TypeDesc& x = m.types[a];
    const TypeDesc& y = m.types[b];
    if (x.kind != y.kind) {
        return false;
    }
    switch (x.kind) {
    case TypeKind::Void:
    case TypeKind::I32:
    case TypeKind::F64:
    case TypeKind::Bool:
    case TypeKind::String:
        return true;
    case TypeKind::Opaque:
        return x.abi == y.abi;
    case TypeKind::Array:
    case TypeKind::Box:
        return sameRec(m, x.elem, y.elem, assumed);
    case TypeKind::Struct: {
        if (std::find(assumed.begin(), assumed.end(), std::pair{a, b}) != assumed.end()) {
            return true;
        }
        if (x.fieldNames != y.fieldNames || x.fieldTypes.size() != y.fieldTypes.size()) {
            return false;
        }
        assumed.emplace_back(a, b);
        for (usize i = 0; i < x.fieldTypes.size(); ++i) {
            if (!sameRec(m, x.fieldTypes[i], y.fieldTypes[i], assumed)) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

TypeKind abiKind(api::AbiType a) {
    switch (a) {
    case api::AbiType::Void:
        return TypeKind::Void;
    case api::AbiType::Bool:
        return TypeKind::Bool;
    case api::AbiType::I32:
        return TypeKind::I32;
    case api::AbiType::Number:
        return TypeKind::F64;
    case api::AbiType::String:
        return TypeKind::String;
    case api::AbiType::Entity:
    case api::AbiType::ServerContext:
    case api::AbiType::Component:
        return TypeKind::Opaque;
    case api::AbiType::ComponentData:
    case api::AbiType::ComponentDataOrNull:
        return TypeKind::Struct; // 구조체는 참조 — null 이 될 수 있는 것도 같은 레지스터 타입
    case api::AbiType::EntityList:
        return TypeKind::Array;
    }
    return TypeKind::Void;
}

class Verifier {
public:
    explicit Verifier(const Module& m) : M(m) {}

    VerifyResult run() {
        header();
        if (!R.errors.empty()) {
            return std::move(R); // 버전이 다르면 나머지 뜻이 다를 수 있다
        }
        types();
        pools();
        imports();
        if (R.errors.empty()) {
            for (usize i = 0; i < M.functions.size() && R.errors.size() < kMaxVerifyErrors; ++i) {
                function(static_cast<u32>(i));
            }
            exports();
            systems();
        }
        return std::move(R);
    }

private:
    void fail(std::string msg) {
        if (R.errors.size() < kMaxVerifyErrors) {
            R.errors.push_back(std::move(msg));
        }
    }

    void header() {
        if (M.format != kFormatVersion || M.hostApiVersion != api::kHostApiVersion ||
            M.hostApiFingerprint != api::hostApiFingerprint()) {
            R.versionMismatch = true;
            fail(std::format("형식 {} · Host 표 {} · 지문 {:016x} 이 이 엔진 ({} · {} · {:016x}) 과 다르다 — 소스에서 "
                             "다시 컴파일한다",
                             M.format, M.hostApiVersion, M.hostApiFingerprint, kFormatVersion, api::kHostApiVersion,
                             api::hostApiFingerprint()));
        }
    }

    [[nodiscard]] bool validType(u32 t) const { return t < M.types.size(); }
    [[nodiscard]] TypeKind kindOf(u32 t) const { return validType(t) ? M.types[t].kind : TypeKind::Void; }

    void types() {
        for (usize i = 0; i < M.types.size(); ++i) {
            const TypeDesc& t = M.types[i];
            switch (t.kind) {
            case TypeKind::Array:
                if (!validType(t.elem) || kindOf(t.elem) == TypeKind::Void) {
                    fail(std::format("타입 {}: 배열 원소 타입 {} 가 없다 · void", i, t.elem));
                }
                break;
            case TypeKind::Box: {
                const TypeKind k = kindOf(t.elem);
                if (!validType(t.elem) ||
                    !(k == TypeKind::I32 || k == TypeKind::F64 || k == TypeKind::Bool || k == TypeKind::Opaque)) {
                    fail(std::format("타입 {}: 상자의 안쪽은 i32 · f64 · bool · 불투명만", i));
                }
                break;
            }
            case TypeKind::Opaque:
                if (t.abi != api::AbiType::Entity && t.abi != api::AbiType::ServerContext &&
                    t.abi != api::AbiType::Component) {
                    fail(std::format("타입 {}: 불투명 타입이 아닌 ABI", i));
                }
                break;
            case TypeKind::Struct:
                if (t.fieldNames.size() != t.fieldTypes.size()) {
                    fail(std::format("타입 {}: 필드 이름 · 타입 수가 다르다", i));
                    break;
                }
                for (usize k = 0; k < t.fieldNames.size(); ++k) {
                    if (!validType(t.fieldTypes[k]) || kindOf(t.fieldTypes[k]) == TypeKind::Void) {
                        fail(std::format("타입 {}: 필드 '{}' 의 타입이 없다 · void", i, t.fieldNames[k]));
                    }
                    if (k > 0 && !(t.fieldNames[k - 1] < t.fieldNames[k])) {
                        fail(std::format("타입 {}: 필드가 이름 순이 아니다 · 겹친다 ('{}')", i, t.fieldNames[k]));
                    }
                    if (!validUtf8(t.fieldNames[k])) {
                        fail(std::format("타입 {}: 필드 이름이 UTF-8 이 아니다", i));
                    }
                }
                break;
            default:
                break;
            }
        }
    }

    void pools() {
        for (usize i = 0; i < M.strings.size(); ++i) {
            if (!validUtf8(M.strings[i])) {
                fail(std::format("문자열 {} 가 UTF-8 이 아니다", i));
            }
        }
        for (usize i = 0; i < M.components.size(); ++i) {
            if (!api::isValidComponentId(M.components[i])) {
                fail(std::format("컴포넌트 {}: '{}' 는 컴포넌트 이름이 아니다", i, M.components[i]));
            }
            for (usize k = 0; k < i; ++k) {
                if (M.components[k] == M.components[i]) {
                    fail(std::format("컴포넌트 {}: '{}' 가 두 번 있다", i, M.components[i]));
                }
            }
        }
    }

    void imports() {
        for (usize i = 0; i < M.imports.size(); ++i) {
            const HostImport& im = M.imports[i];
            const api::HostFunction* h = api::findHostFunction(im.id);
            if (h == nullptr) {
                fail(std::format("import {}: Host 함수 '{}' 가 이 엔진에 없다", i, im.id));
                continue;
            }
            bool same = im.result == h->result && im.params.size() == static_cast<usize>(h->paramCount) + 1 &&
                        im.params[0] == h->owner;
            for (u8 k = 0; same && k < h->paramCount; ++k) {
                same = im.params[k + 1] == h->params[k];
            }
            if (!same) {
                fail(std::format("import {}: '{}' 의 서명이 Host 표와 다르다 ({})", i, im.id,
                                 api::canonicalSignature(*h)));
            }
        }
    }

    void exports() {
        for (usize i = 0; i < M.exports.size(); ++i) {
            const Export& e = M.exports[i];
            if (e.function >= M.functions.size()) {
                fail(std::format("export {}:{}: 함수 {} 가 없다", e.module, e.name, e.function));
            }
            for (usize k = 0; k < i; ++k) {
                if (M.exports[k].module == e.module && M.exports[k].name == e.name) {
                    fail(std::format("export {}:{} 가 두 번 있다", e.module, e.name));
                }
            }
        }
    }

    // 시스템 표 (ADR-0062): id 규칙 · 오름차순 (겹침 없음), onTick = (ServerContext?) → void, 주기 1 ~ 65535
    void systems() {
        for (usize i = 0; i < M.systems.size(); ++i) {
            const SystemDesc& s = M.systems[i];
            if (!api::isValidComponentId(s.id) || api::isEngineComponentNamespace(s.id)) {
                fail(
                    std::format("시스템 {}: '{}' 는 시스템 id 가 아니다 (이름공간.이름, 엔진 이름공간 금지)", i, s.id));
            }
            if (i > 0 && !(M.systems[i - 1].id < s.id)) {
                fail(std::format("시스템 {}: '{}' — 표가 id 오름차순이 아니다 · 겹친다", i, s.id));
            }
            if (!validUtf8(s.module)) {
                fail(std::format("시스템 {}: 모듈 경로가 UTF-8 이 아니다", i));
            }
            if (s.intervalTicks < 1 || s.intervalTicks > kMaxIntervalTicks) {
                fail(std::format("시스템 {}: intervalTicks {} 는 1 ~ {} 밖이다", s.id, s.intervalTicks,
                                 kMaxIntervalTicks));
            }
            if (s.onTick >= M.functions.size()) {
                fail(std::format("시스템 {}: onTick 함수 {} 가 없다", s.id, s.onTick));
                continue;
            }
            const Function& f = M.functions[s.onTick];
            const bool ctxOk = f.params.empty() || (f.params.size() == 1 && kindOf(f.params[0]) == TypeKind::Opaque &&
                                                    M.types[f.params[0]].abi == api::AbiType::ServerContext);
            if (!ctxOk || kindOf(f.result) != TypeKind::Void) {
                fail(std::format("시스템 {}: onTick 의 서명은 (ServerContext) → void 다", s.id));
            }
        }
    }

    // ---- 함수 --------------------------------------------------------------------------------------------------
    const Function* F = nullptr;
    u32 m_fi = 0;
    usize m_pc = 0;

    void bad(std::string msg) {
        const Insn& in = F->code[m_pc];
        fail(std::format("fn {} {} @{} {}: {}", m_fi, F->name, m_pc, opName(in.op), msg));
    }

    // 레지스터 r 의 타입 (없으면 보고하고 false)
    bool reg(u16 r, u32& type) {
        if (r >= F->regs.size()) {
            bad(std::format("레지스터 r{} 가 없다 ({} 개)", r, F->regs.size()));
            return false;
        }
        type = F->regs[r];
        return true;
    }
    bool regKind(u16 r, TypeKind k) {
        u32 t = 0;
        if (!reg(r, t)) {
            return false;
        }
        if (kindOf(t) != k) {
            bad(std::format("r{} 의 타입이 맞지 않다 ({})", r, typeName(M, t)));
            return false;
        }
        return true;
    }
    bool regSame(u16 r, u32 want) {
        u32 t = 0;
        if (!reg(r, t)) {
            return false;
        }
        if (!sameType(M, t, want)) {
            bad(std::format("r{} 의 타입 {} 이 {} 가 아니다", r, typeName(M, t), typeName(M, want)));
            return false;
        }
        return true;
    }
    void three(TypeKind a, TypeKind b, TypeKind c, const Insn& in) {
        regKind(in.a, a);
        regKind(in.b, b);
        regKind(in.c, c);
    }
    void target(u32 t) {
        if (t >= F->code.size()) {
            bad(std::format("점프 대상 @{} 가 함수 밖이다 ({} 명령)", t, F->code.size()));
        }
    }

    void function(u32 fi) {
        m_fi = fi;
        F = &M.functions[fi];
        const Function& f = *F;
        auto fnFail = [&](std::string msg) { fail(std::format("fn {} {}: {}", fi, f.name, msg)); };
        if (f.params.size() > kMaxParams || f.regs.size() > kMaxRegisters || f.code.size() > kMaxInstructions) {
            fnFail("상한을 넘는다");
            return;
        }
        if (!validType(f.result)) {
            fnFail("반환 타입이 없다");
            return;
        }
        if (f.params.size() > f.regs.size()) {
            fnFail("매개변수가 레지스터보다 많다");
            return;
        }
        for (usize i = 0; i < f.regs.size(); ++i) {
            if (!validType(f.regs[i]) || kindOf(f.regs[i]) == TypeKind::Void) {
                fnFail(std::format("레지스터 r{} 의 타입이 없다 · void", i));
                return;
            }
            if (i < f.params.size() && f.params[i] != f.regs[i]) {
                fnFail(std::format("매개변수 {} 와 레지스터 r{} 의 타입이 다르다", i, i));
                return;
            }
        }
        if (f.code.empty()) {
            fnFail("명령이 없다");
            return;
        }
        const Op last = f.code.back().op;
        if (last != Op::RET && last != Op::RETV && last != Op::JMP && last != Op::TRAP) {
            fnFail("마지막 명령이 끝 (RET · RETV · JMP · TRAP) 이 아니다");
        }
        for (m_pc = 0; m_pc < f.code.size() && R.errors.size() < kMaxVerifyErrors; ++m_pc) {
            insn(f.code[m_pc]);
        }
    }

    void insn(const Insn& in) {
        if (in.pad != 0) {
            bad("pad 가 0 이 아니다");
            return;
        }
        if (in.op >= Op::Count) {
            fail(std::format("fn {} {} @{}: 모르는 명령 {}", m_fi, F->name, m_pc, static_cast<u32>(in.op)));
            return;
        }
        using K = TypeKind;
        u32 ta = 0;
        u32 tb = 0;
        switch (in.op) {
        case Op::NOP:
            break;
        case Op::MOV:
            if (reg(in.a, ta)) {
                regSame(in.b, ta);
            }
            break;
        case Op::LOADI:
            regKind(in.a, K::I32);
            break;
        case Op::LOADB:
            regKind(in.a, K::Bool);
            if (in.b > 1) {
                bad("bool 즉시값은 0 · 1");
            }
            break;
        case Op::LOADF:
            regKind(in.a, K::F64);
            if (in.b >= M.numbers.size()) {
                bad(std::format("수 {} 가 없다", in.b));
            }
            break;
        case Op::LOADS:
            regKind(in.a, K::String);
            if (in.b >= M.strings.size()) {
                bad(std::format("문자열 {} 가 없다", in.b));
            }
            break;
        case Op::LOADNULL:
            if (reg(in.a, ta) && regClass(M, ta) != RegClass::Ref) {
                bad(std::format("null 은 참조 타입에만 ({})", typeName(M, ta)));
            }
            break;
        case Op::ADDI:
        case Op::SUBI:
        case Op::MULI:
        case Op::REMI:
        case Op::IDIV:
        case Op::ANDI:
        case Op::ORI:
        case Op::XORI:
        case Op::SHLI:
        case Op::SHRI:
        case Op::MINI:
        case Op::MAXI:
            three(K::I32, K::I32, K::I32, in);
            break;
        case Op::NEGI:
        case Op::NOTI:
            regKind(in.a, K::I32);
            regKind(in.b, K::I32);
            break;
        case Op::ADDF:
        case Op::SUBF:
        case Op::MULF:
        case Op::DIVF:
        case Op::REMF:
        case Op::POWF:
            three(K::F64, K::F64, K::F64, in);
            break;
        case Op::NEGF:
            regKind(in.a, K::F64);
            regKind(in.b, K::F64);
            break;
        case Op::MATHF: {
            regKind(in.a, K::F64);
            regKind(in.b, K::F64);
            if (in.c >= std::size(kMathFunctions)) {
                bad(std::format("Math 함수 {} 가 없다", in.c));
                break;
            }
            if (kMathFunctions[in.c].arity != 1) {
                regKind(static_cast<u16>(in.b + 1), K::F64); // 둘째 인자 = b+1
            }
            break;
        }
        case Op::I2F:
            regKind(in.a, K::F64);
            regKind(in.b, K::I32);
            break;
        case Op::F2I:
        case Op::F2IDX:
            regKind(in.a, K::I32);
            regKind(in.b, K::F64);
            break;
        case Op::EQI:
        case Op::LTI:
        case Op::LEI:
            three(K::Bool, K::I32, K::I32, in);
            break;
        case Op::EQF:
        case Op::LTF:
        case Op::LEF:
            three(K::Bool, K::F64, K::F64, in);
            break;
        case Op::EQB:
            three(K::Bool, K::Bool, K::Bool, in);
            break;
        case Op::EQS:
        case Op::LTS:
        case Op::LES:
            three(K::Bool, K::String, K::String, in);
            break;
        case Op::EQR:
            regKind(in.a, K::Bool);
            if (reg(in.b, tb) && regClass(M, tb) != RegClass::Ref) {
                bad(std::format("EQR 는 참조끼리 ({})", typeName(M, tb)));
            } else if (in.b < F->regs.size()) {
                regSame(in.c, tb);
            }
            break;
        case Op::EQO:
            regKind(in.a, K::Bool);
            if (regKind(in.b, K::Opaque)) {
                regSame(in.c, F->regs[in.b]);
            }
            break;
        case Op::NOTB:
            regKind(in.a, K::Bool);
            regKind(in.b, K::Bool);
            break;
        case Op::CONCAT:
            three(K::String, K::String, K::String, in);
            break;
        case Op::TOSTRI:
            regKind(in.a, K::String);
            regKind(in.b, K::I32);
            break;
        case Op::TOSTRF:
            regKind(in.a, K::String);
            regKind(in.b, K::F64);
            break;
        case Op::TOSTRB:
            regKind(in.a, K::String);
            regKind(in.b, K::Bool);
            break;
        case Op::STRLEN:
            regKind(in.a, K::I32);
            regKind(in.b, K::String);
            break;
        case Op::NEWARR:
            regKind(in.a, K::Array);
            break;
        case Op::ARRLEN:
            regKind(in.a, K::I32);
            regKind(in.b, K::Array);
            break;
        case Op::ARRGET:
            if (regKind(in.b, K::Array)) {
                regSame(in.a, M.types[F->regs[in.b]].elem);
            }
            regKind(in.c, K::I32);
            break;
        case Op::ARRSET:
            if (regKind(in.a, K::Array)) {
                regSame(in.c, M.types[F->regs[in.a]].elem);
            }
            regKind(in.b, K::I32);
            break;
        case Op::ARRPUSH:
            if (in.a != kNoReg) {
                regKind(in.a, K::I32);
            }
            if (regKind(in.b, K::Array)) {
                regSame(in.c, M.types[F->regs[in.b]].elem);
            }
            break;
        case Op::NEWOBJ:
            regKind(in.a, K::Struct);
            break;
        case Op::GETF:
            if (regKind(in.b, K::Struct)) {
                const TypeDesc& s = M.types[F->regs[in.b]];
                if (in.c >= s.fieldTypes.size()) {
                    bad(std::format("필드 {} 가 없다 ({} 개)", in.c, s.fieldTypes.size()));
                } else {
                    regSame(in.a, s.fieldTypes[in.c]);
                }
            }
            break;
        case Op::SETF:
            if (regKind(in.a, K::Struct)) {
                const TypeDesc& s = M.types[F->regs[in.a]];
                if (in.b >= s.fieldTypes.size()) {
                    bad(std::format("필드 {} 가 없다 ({} 개)", in.b, s.fieldTypes.size()));
                } else {
                    regSame(in.c, s.fieldTypes[in.b]);
                }
            }
            break;
        case Op::BOX:
            if (regKind(in.a, K::Box)) {
                regSame(in.b, M.types[F->regs[in.a]].elem);
            }
            break;
        case Op::UNBOX:
            if (regKind(in.b, K::Box)) {
                regSame(in.a, M.types[F->regs[in.b]].elem);
            }
            break;
        case Op::JMP:
            target(in.wide());
            break;
        case Op::JMPT:
        case Op::JMPF:
            regKind(in.a, K::Bool);
            target(in.wide());
            break;
        case Op::CALL: {
            if (in.b >= M.functions.size()) {
                bad(std::format("함수 {} 가 없다", in.b));
                break;
            }
            const Function& callee = M.functions[in.b];
            args(in.c, callee.params.size());
            for (usize i = 0; i < callee.params.size(); ++i) {
                regSame(static_cast<u16>(in.c + i), callee.params[i]);
            }
            result(in.a, kindOf(callee.result) == K::Void, [&] { regSame(in.a, callee.result); });
            break;
        }
        case Op::CALLH: {
            if (in.b >= M.imports.size()) {
                bad(std::format("import {} 가 없다", in.b));
                break;
            }
            const HostImport& im = M.imports[in.b];
            args(in.c, im.params.size());
            for (usize i = 0; i < im.params.size(); ++i) {
                const u16 r = static_cast<u16>(in.c + i);
                if (regKind(r, abiKind(im.params[i])) && abiKind(im.params[i]) == K::Opaque &&
                    M.types[F->regs[r]].abi != im.params[i]) {
                    bad(std::format("r{} 는 {} 가 아니다", r, api::abiTypeName(im.params[i])));
                }
            }
            result(in.a, im.result == api::AbiType::Void, [&] {
                if (regKind(in.a, abiKind(im.result)) && abiKind(im.result) == K::Opaque &&
                    M.types[F->regs[in.a]].abi != im.result) {
                    bad(std::format("r{} 는 {} 가 아니다", in.a, api::abiTypeName(im.result)));
                }
                if (im.result == api::AbiType::EntityList && kindOf(F->regs[in.a]) == K::Array) {
                    const u32 e = M.types[F->regs[in.a]].elem;
                    if (kindOf(e) != K::Opaque || M.types[e].abi != api::AbiType::Entity) {
                        bad(std::format("r{} 는 Entity[] 가 아니다", in.a));
                    }
                }
            });
            break;
        }
        case Op::RET:
            if (kindOf(F->result) != K::Void) {
                bad("값을 돌려줘야 하는 함수의 RET");
            }
            break;
        case Op::RETV:
            if (kindOf(F->result) == K::Void) {
                bad("void 함수의 RETV");
            } else {
                regSame(in.a, F->result);
            }
            break;
        case Op::TRAP:
            break;
        case Op::LOADK:
            if (regKind(in.a, K::Opaque) && M.types[F->regs[in.a]].abi != api::AbiType::Component) {
                bad(std::format("r{} 는 ComponentRef 가 아니다", in.a));
            }
            if (in.b >= M.components.size()) {
                bad(std::format("컴포넌트 {} 가 없다", in.b));
            }
            break;
        case Op::Count:
            break;
        }
        (void)ta;
    }

    void args(u16 base, usize n) {
        if (n > 0 && static_cast<usize>(base) + n > F->regs.size()) {
            bad(std::format("인자 레지스터 r{} … r{} 가 없다", base, static_cast<usize>(base) + n - 1));
        }
    }
    template <class Check>
    void result(u16 dst, bool isVoid, Check check) {
        if (isVoid) {
            if (dst != kNoReg) {
                bad("void 호출의 결과 레지스터");
            }
        } else if (dst != kNoReg) {
            check();
        }
    }

    const Module& M;
    VerifyResult R;
};

} // namespace

bool sameType(const Module& m, u32 a, u32 b) {
    std::vector<std::pair<u32, u32>> assumed;
    return sameRec(m, a, b, assumed);
}

VerifyResult verify(const Module& m) {
    return Verifier(m).run();
}

Expected<Module> loadModule(std::span<const u8> bytes) {
    Expected<Module> m = deserialize(bytes);
    if (!m) {
        return m;
    }
    const VerifyResult r = verify(*m);
    if (!r.ok()) {
        std::string msg;
        for (usize i = 0; i < r.errors.size() && i < 4; ++i) {
            msg += (i > 0 ? "; " : "") + r.errors[i];
        }
        if (r.errors.size() > 4) {
            msg += std::format("; … {} 개 더", r.errors.size() - 4);
        }
        return makeError(r.versionMismatch ? ErrorCode::VersionMismatch : ErrorCode::ValidationFailed, std::move(msg));
    }
    return m;
}

} // namespace sbx::script::bc
