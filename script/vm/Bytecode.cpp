#include "script/vm/Bytecode.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <format>

namespace sbx::script::bc {

namespace {

constexpr std::array<std::string_view, static_cast<usize>(Op::Count)> kOpNames{
    "NOP",    "MOV",    "LOADI",  "LOADB",  "LOADF",   "LOADS",  "LOADNULL", "ADDI",   "SUBI",   "MULI",
    "REMI",   "IDIV",   "ANDI",   "ORI",    "XORI",    "SHLI",   "SHRI",     "MINI",   "MAXI",   "NEGI",
    "NOTI",   "ADDF",   "SUBF",   "MULF",   "DIVF",    "REMF",   "POWF",     "NEGF",   "MATHF",  "I2F",
    "F2I",    "F2IDX",  "EQI",    "LTI",    "LEI",     "EQF",    "LTF",      "LEF",    "EQB",    "EQS",
    "LTS",    "LES",    "EQR",    "EQO",    "NOTB",    "CONCAT", "TOSTRI",   "TOSTRF", "TOSTRB", "STRLEN",
    "NEWARR", "ARRLEN", "ARRGET", "ARRSET", "ARRPUSH", "NEWOBJ", "GETF",     "SETF",   "BOX",    "UNBOX",
    "JMP",    "JMPT",   "JMPF",   "CALL",   "CALLH",   "RET",    "RETV",     "TRAP",
};

constexpr std::array<u8, 4> kMagic{'S', 'B', 'X', 'C'};

class Writer {
public:
    void u8v(u8 v) { m_out.push_back(v); }
    void u16v(u16 v) {
        u8v(static_cast<u8>(v));
        u8v(static_cast<u8>(v >> 8));
    }
    void u32v(u32 v) {
        for (int i = 0; i < 4; ++i) {
            u8v(static_cast<u8>(v >> (8 * i)));
        }
    }
    void u64v(u64 v) {
        for (int i = 0; i < 8; ++i) {
            u8v(static_cast<u8>(v >> (8 * i)));
        }
    }
    void str(std::string_view s) {
        u32v(static_cast<u32>(s.size()));
        m_out.insert(m_out.end(), s.begin(), s.end());
    }
    std::vector<u8> take() { return std::move(m_out); }

private:
    std::vector<u8> m_out;
};

class Reader {
public:
    explicit Reader(std::span<const u8> b) : m_b(b) {}

    bool u8v(u8& v) {
        if (m_pos + 1 > m_b.size()) {
            return fail();
        }
        v = m_b[m_pos++];
        return true;
    }
    bool u16v(u16& v) {
        u8 lo = 0;
        u8 hi = 0;
        if (!u8v(lo) || !u8v(hi)) {
            return false;
        }
        v = static_cast<u16>(lo | (hi << 8));
        return true;
    }
    bool u32v(u32& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            u8 x = 0;
            if (!u8v(x)) {
                return false;
            }
            v |= static_cast<u32>(x) << (8 * i);
        }
        return true;
    }
    bool u64v(u64& v) {
        v = 0;
        for (int i = 0; i < 8; ++i) {
            u8 x = 0;
            if (!u8v(x)) {
                return false;
            }
            v |= static_cast<u64>(x) << (8 * i);
        }
        return true;
    }
    // 개수: 상한을 넘거나 남은 바이트로 담을 수 없으면 (항목당 최소 minBytes) 실패 — 거대한 할당을 막는다
    bool count(u32& n, u32 limit, usize minBytes, std::string_view what) {
        if (!u32v(n)) {
            return false;
        }
        if (n > limit || static_cast<u64>(n) * minBytes > m_b.size() - m_pos) {
            m_error = std::format("{} 개수 {} 가 상한 · 남은 크기를 넘는다", what, n);
            return false;
        }
        return true;
    }
    bool str(std::string& s) {
        u32 n = 0;
        if (!count(n, kMaxStringBytes, 1, "문자열 바이트")) {
            return false;
        }
        s.assign(reinterpret_cast<const char*>(m_b.data() + m_pos), n);
        m_pos += n;
        return true;
    }
    [[nodiscard]] bool atEnd() const noexcept { return m_pos == m_b.size(); }
    [[nodiscard]] usize pos() const noexcept { return m_pos; }
    [[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
    bool fail() {
        if (m_error.empty()) {
            m_error = std::format("바이트열이 {} 에서 잘렸다", m_pos);
        }
        return false;
    }
    std::span<const u8> m_b;
    usize m_pos = 0;
    std::string m_error;
};

bool readAbi(Reader& r, api::AbiType& out) {
    u8 v = 0;
    if (!r.u8v(v)) {
        return false;
    }
    if (v > static_cast<u8>(api::AbiType::ServerContext)) {
        return false;
    }
    out = static_cast<api::AbiType>(v);
    return true;
}

} // namespace

std::string_view opName(Op op) noexcept {
    const auto i = static_cast<usize>(op);
    return i < kOpNames.size() ? kOpNames[i] : std::string_view("?");
}

const Export* Module::findExport(std::string_view module, std::string_view name) const noexcept {
    for (const Export& e : exports) {
        if (e.module == module && e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

RegClass regClass(const Module& m, u32 type) noexcept {
    if (type >= m.types.size()) {
        return RegClass::Void;
    }
    switch (m.types[type].kind) {
    case TypeKind::Void:
        return RegClass::Void;
    case TypeKind::I32:
        return RegClass::I32;
    case TypeKind::F64:
        return RegClass::F64;
    case TypeKind::Bool:
        return RegClass::Bool;
    case TypeKind::Opaque:
        return RegClass::Opaque;
    case TypeKind::String:
    case TypeKind::Array:
    case TypeKind::Box:
    case TypeKind::Struct:
        return RegClass::Ref;
    }
    return RegClass::Void;
}

namespace {
std::string typeNameRec(const Module& m, u32 type, u32 depth) {
    if (type >= m.types.size()) {
        return std::format("#{}?", type);
    }
    const TypeDesc& t = m.types[type];
    if (depth > 8) {
        return "…";
    }
    switch (t.kind) {
    case TypeKind::Void:
        return "void";
    case TypeKind::I32:
        return "i32";
    case TypeKind::F64:
        return "f64";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::String:
        return "string";
    case TypeKind::Opaque:
        return std::string(api::abiTypeName(t.abi));
    case TypeKind::Array:
        return typeNameRec(m, t.elem, depth + 1) + "[]";
    case TypeKind::Box:
        return "box<" + typeNameRec(m, t.elem, depth + 1) + ">";
    case TypeKind::Struct:
        return std::format("struct#{}", type);
    }
    return "?";
}
} // namespace

std::string typeName(const Module& m, u32 type) {
    return typeNameRec(m, type, 0);
}

std::vector<u8> serialize(const Module& m) {
    Writer w;
    for (const u8 b : kMagic) {
        w.u8v(b);
    }
    w.u32v(m.format);
    w.u32v(m.hostApiVersion);
    w.u64v(m.hostApiFingerprint);
    w.u32v(static_cast<u32>(m.types.size()));
    for (const TypeDesc& t : m.types) {
        w.u8v(static_cast<u8>(t.kind));
        if (t.kind == TypeKind::Array || t.kind == TypeKind::Box) {
            w.u32v(t.elem);
        } else if (t.kind == TypeKind::Opaque) {
            w.u8v(static_cast<u8>(t.abi));
        } else if (t.kind == TypeKind::Struct) {
            w.u32v(static_cast<u32>(t.fieldNames.size()));
            for (usize i = 0; i < t.fieldNames.size(); ++i) {
                w.str(t.fieldNames[i]);
                w.u32v(i < t.fieldTypes.size() ? t.fieldTypes[i] : 0);
            }
        }
    }
    w.u32v(static_cast<u32>(m.strings.size()));
    for (const std::string& s : m.strings) {
        w.str(s);
    }
    w.u32v(static_cast<u32>(m.numbers.size()));
    for (const f64 v : m.numbers) {
        w.u64v(std::bit_cast<u64>(v));
    }
    w.u32v(static_cast<u32>(m.imports.size()));
    for (const HostImport& im : m.imports) {
        w.str(im.id);
        w.u8v(static_cast<u8>(im.result));
        w.u32v(static_cast<u32>(im.params.size()));
        for (const api::AbiType p : im.params) {
            w.u8v(static_cast<u8>(p));
        }
    }
    w.u32v(static_cast<u32>(m.functions.size()));
    for (const Function& f : m.functions) {
        w.str(f.name);
        w.u32v(static_cast<u32>(f.params.size()));
        for (const u32 p : f.params) {
            w.u32v(p);
        }
        w.u32v(f.result);
        w.u32v(static_cast<u32>(f.regs.size()));
        for (const u32 r : f.regs) {
            w.u32v(r);
        }
        w.u32v(static_cast<u32>(f.code.size()));
        for (const Insn& in : f.code) {
            w.u8v(static_cast<u8>(in.op));
            w.u8v(in.pad);
            w.u16v(in.a);
            w.u16v(in.b);
            w.u16v(in.c);
        }
    }
    w.u32v(static_cast<u32>(m.exports.size()));
    for (const Export& e : m.exports) {
        w.str(e.module);
        w.str(e.name);
        w.u32v(e.function);
    }
    return w.take();
}

Expected<Module> deserialize(std::span<const u8> bytes) {
    Reader r(bytes);
    Module m;
    auto bad = [&](std::string_view what) {
        return makeError(ErrorCode::ParseError, r.error().empty() ? std::string(what) : r.error(),
                         std::format("sbc:{}", r.pos()));
    };
    for (const u8 b : kMagic) {
        u8 v = 0;
        if (!r.u8v(v) || v != b) {
            return makeError(ErrorCode::ParseError, "SandboxScript 바이트코드가 아니다 (머리 SBXC 가 없다)", "sbc:0");
        }
    }
    if (!r.u32v(m.format) || !r.u32v(m.hostApiVersion) || !r.u64v(m.hostApiFingerprint)) {
        return bad("머리가 잘렸다");
    }
    if (m.format != kFormatVersion) {
        return makeError(
            ErrorCode::VersionMismatch,
            std::format("바이트코드 형식 {} — 이 엔진은 {} (소스에서 다시 컴파일한다)", m.format, kFormatVersion));
    }
    u32 n = 0;
    if (!r.count(n, kMaxTypes, 1, "타입")) {
        return bad("타입 표");
    }
    m.types.resize(n);
    for (TypeDesc& t : m.types) {
        u8 k = 0;
        if (!r.u8v(k) || k > static_cast<u8>(TypeKind::Struct)) {
            return bad("타입 종류");
        }
        t.kind = static_cast<TypeKind>(k);
        if (t.kind == TypeKind::Array || t.kind == TypeKind::Box) {
            if (!r.u32v(t.elem)) {
                return bad("타입 원소");
            }
        } else if (t.kind == TypeKind::Opaque) {
            if (!readAbi(r, t.abi)) {
                return bad("불투명 타입");
            }
        } else if (t.kind == TypeKind::Struct) {
            u32 nf = 0;
            if (!r.count(nf, kMaxFields, 8, "필드")) {
                return bad("필드");
            }
            t.fieldNames.resize(nf);
            t.fieldTypes.resize(nf);
            for (u32 i = 0; i < nf; ++i) {
                if (!r.str(t.fieldNames[i]) || !r.u32v(t.fieldTypes[i])) {
                    return bad("필드");
                }
            }
        }
    }
    if (!r.count(n, kMaxPoolEntries, 4, "문자열")) {
        return bad("문자열 표");
    }
    m.strings.resize(n);
    for (std::string& s : m.strings) {
        if (!r.str(s)) {
            return bad("문자열");
        }
    }
    if (!r.count(n, kMaxPoolEntries, 8, "수")) {
        return bad("수 표");
    }
    m.numbers.resize(n);
    for (f64& v : m.numbers) {
        u64 bits = 0;
        if (!r.u64v(bits)) {
            return bad("수");
        }
        v = std::bit_cast<f64>(bits);
    }
    if (!r.count(n, kMaxImports, 9, "import")) {
        return bad("import 표");
    }
    m.imports.resize(n);
    for (HostImport& im : m.imports) {
        u32 np = 0;
        if (!r.str(im.id) || !readAbi(r, im.result) || !r.count(np, api::kMaxHostParams + 1, 1, "import 인자")) {
            return bad("import");
        }
        im.params.resize(np);
        for (api::AbiType& p : im.params) {
            if (!readAbi(r, p)) {
                return bad("import 인자");
            }
        }
    }
    if (!r.count(n, kMaxFunctions, 20, "함수")) {
        return bad("함수 표");
    }
    m.functions.resize(n);
    for (Function& f : m.functions) {
        u32 np = 0;
        if (!r.str(f.name) || !r.count(np, kMaxParams, 4, "매개변수")) {
            return bad("함수");
        }
        f.params.resize(np);
        for (u32& p : f.params) {
            if (!r.u32v(p)) {
                return bad("매개변수");
            }
        }
        u32 nr = 0;
        if (!r.u32v(f.result) || !r.count(nr, kMaxRegisters, 4, "레지스터")) {
            return bad("레지스터");
        }
        f.regs.resize(nr);
        for (u32& t : f.regs) {
            if (!r.u32v(t)) {
                return bad("레지스터");
            }
        }
        u32 ni = 0;
        if (!r.count(ni, kMaxInstructions, 8, "명령")) {
            return bad("명령");
        }
        f.code.resize(ni);
        for (Insn& in : f.code) {
            u8 op = 0;
            if (!r.u8v(op) || !r.u8v(in.pad) || !r.u16v(in.a) || !r.u16v(in.b) || !r.u16v(in.c)) {
                return bad("명령");
            }
            in.op = static_cast<Op>(op); // 범위는 검증기가 본다 (Count 이상 = 모르는 명령)
        }
    }
    if (!r.count(n, kMaxFunctions, 12, "export")) {
        return bad("export 표");
    }
    m.exports.resize(n);
    for (Export& e : m.exports) {
        if (!r.str(e.module) || !r.str(e.name) || !r.u32v(e.function)) {
            return bad("export");
        }
    }
    if (!r.atEnd()) {
        return makeError(ErrorCode::ParseError, std::format("끝에 남는 바이트 {} 개", bytes.size() - r.pos()),
                         std::format("sbc:{}", r.pos()));
    }
    return m;
}

std::string disassemble(const Module& m) {
    std::string out = std::format("sbc format {} · host api {} · fingerprint {:016x}\n", m.format, m.hostApiVersion,
                                  m.hostApiFingerprint);
    for (usize i = 0; i < m.imports.size(); ++i) {
        out += std::format("import {} {}\n", i, m.imports[i].id);
    }
    for (usize fi = 0; fi < m.functions.size(); ++fi) {
        const Function& f = m.functions[fi];
        std::string params;
        for (usize i = 0; i < f.params.size(); ++i) {
            params += (i > 0 ? ", " : "") + typeName(m, f.params[i]);
        }
        out += std::format("fn {} {}({}): {}  regs {}\n", fi, f.name, params, typeName(m, f.result), f.regs.size());
        for (usize pc = 0; pc < f.code.size(); ++pc) {
            const Insn& in = f.code[pc];
            std::string args;
            switch (in.op) {
            case Op::NOP:
            case Op::RET:
                break;
            case Op::LOADI:
                args = std::format("r{}, {}", in.a, static_cast<i32>(in.wide()));
                break;
            case Op::LOADB:
                args = std::format("r{}, {}", in.a, in.b != 0 ? "true" : "false");
                break;
            case Op::LOADF:
                args = std::format("r{}, {}", in.a, in.b < m.numbers.size() ? std::format("{}", m.numbers[in.b]) : "?");
                break;
            case Op::LOADS:
                args = std::format("r{}, \"{}\"", in.a, in.b < m.strings.size() ? m.strings[in.b] : std::string("?"));
                break;
            case Op::LOADNULL:
            case Op::NEWARR:
            case Op::NEWOBJ:
            case Op::RETV:
                args = std::format("r{}", in.a);
                break;
            case Op::TRAP:
                args = std::format("{}", in.a);
                break;
            case Op::JMP:
                args = std::format("@{}", in.wide());
                break;
            case Op::JMPT:
            case Op::JMPF:
                args = std::format("r{}, @{}", in.a, in.wide());
                break;
            case Op::MOV:
            case Op::NEGI:
            case Op::NOTI:
            case Op::NEGF:
            case Op::I2F:
            case Op::F2I:
            case Op::F2IDX:
            case Op::NOTB:
            case Op::TOSTRI:
            case Op::TOSTRF:
            case Op::TOSTRB:
            case Op::STRLEN:
            case Op::ARRLEN:
            case Op::BOX:
            case Op::UNBOX:
                args = std::format("r{}, r{}", in.a, in.b);
                break;
            case Op::MATHF:
                args = std::format("r{}, r{}, {}", in.a, in.b, in.c);
                break;
            case Op::GETF:
                args = std::format("r{}, r{}.{}", in.a, in.b, in.c);
                break;
            case Op::SETF:
                args = std::format("r{}.{}, r{}", in.a, in.b, in.c);
                break;
            case Op::CALL:
            case Op::CALLH: {
                const std::string dst = in.a == kNoReg ? std::string("_") : std::format("r{}", in.a);
                args = std::format("{}, {}{}, r{}", dst, in.op == Op::CALL ? "fn " : "import ", in.b, in.c);
                break;
            }
            case Op::ARRPUSH:
                args = std::format("{}, r{}, r{}", in.a == kNoReg ? std::string("_") : std::format("r{}", in.a), in.b,
                                   in.c);
                break;
            default:
                args = std::format("r{}, r{}, r{}", in.a, in.b, in.c);
                break;
            }
            out += std::format("  {:4}  {:8} {}\n", pc, opName(in.op), args);
        }
    }
    for (const Export& e : m.exports) {
        out += std::format("export {}:{} = fn {}\n", e.module, e.name, e.function);
    }
    return out;
}

} // namespace sbx::script::bc
