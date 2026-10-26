#include "script/lang/Checker.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>

#include "script/lang/Parser.hpp"
#include "script/vm/Numbers.hpp"

namespace sbx::script {

namespace {

constexpr TypeId kNoExpect = 0xFFFF'FFFFu;
constexpr i32 kNoTarget = -1;     // import 대상 없음 (보고함)
constexpr i32 kSdkTarget = -2;    // "sandbox"
constexpr i32 kBrokenTarget = -3; // 파싱 오류 · 순환 — 이름은 Error 타입 (따로 보고하지 않는다)

struct ScopeEntry {
    std::string name;
    SymbolId sym = kNoSymbol;
};
struct Scope {
    std::vector<ScopeEntry> names;
    std::vector<std::string> pending; // 이 블록에서 아직 선언되지 않은 let · const (선언 전 사용 — S403)
};
struct ModuleScope {
    std::vector<ScopeEntry> values;
    std::vector<ScopeEntry> types;
};

// 문장의 흐름: exits = 다음 문장으로 떨어지지 않는다 (return · break · continue), returns = 모든 길이 return
struct Flow {
    bool exits = false;
    bool returns = false;
};
// 조건이 참 · 거짓일 때 null 이 아닌 것으로 좁혀지는 이름
struct Facts {
    std::vector<SymbolId> whenTrue;
    std::vector<SymbolId> whenFalse;
};

struct FnCtx {
    u32 module = 0;
    i32 function = -1; // -1 = 최상위 (const 초기값 · enum 값)
    TypeId result = TypeTable::kVoid;
    bool inferring = false;
    std::vector<TypeId> returns;
    u32 loops = 0;
    u32 breakable = 0;
    std::vector<Scope> scopes;
    std::vector<SymbolId> narrowed;
    std::vector<std::string> assignedNames;
};

struct ForbiddenName {
    std::string_view name;
    std::string_view message;
};
constexpr ForbiddenName kForbiddenValues[] = {
    {"undefined", "undefined 는 없다 — null 을 쓴다"},
    {"Date", "벽시계는 없다 (결정론 — 04) — ctx.tick 을 쓴다"},
    {"performance", "벽시계는 없다 (결정론 — 04) — ctx.tick 을 쓴다"},
    {"console", "console 은 없다 — ctx.log 를 쓴다"},
    {"eval", "eval 은 없다"},
    {"Function", "Function 은 없다"},
    {"globalThis", "전역 객체는 없다"},
    {"window", "전역 객체는 없다"},
    {"require", "require 는 없다 — import { … } from \"…\""},
    {"process", "process 는 없다"},
    {"arguments", "arguments 는 없다 — 매개변수를 적는다"},
    {"JSON", "JSON 은 없다 [v0.3 뒤 검토]"},
    {"Object", "Object 는 없다"},
    {"Array", "Array 값은 없다 — 배열 리터럴 [ … ] 을 쓴다"},
    {"Number", "Number 는 없다 — toI32 · Math 를 쓴다"},
    {"String", "String 은 없다 — 템플릿 문자열을 쓴다"},
    {"Boolean", "Boolean 은 없다"},
    {"Symbol", "Symbol 은 없다"},
    {"Map", "Map 은 없다 [v0.4 뒤 검토] — 배열을 쓴다"},
    {"Set", "Set 은 없다 [v0.4 뒤 검토] — 배열을 쓴다"},
    {"Promise", "Promise 는 없다 — 시뮬레이션은 기다리지 않는다"},
    {"setTimeout", "타이머는 없다 — intervalTicks · ctx.tick 을 쓴다"},
    {"setInterval", "타이머는 없다 — intervalTicks · ctx.tick 을 쓴다"},
    {"parseInt", "parseInt 는 없다"},
    {"parseFloat", "parseFloat 는 없다"},
    {"isNaN", "isNaN 은 없다 — x !== x 로 본다"},
    {"isFinite", "isFinite 는 없다"},
};
constexpr ForbiddenName kForbiddenTypes[] = {
    {"any", "any 는 없다 — 타입을 적는다"},
    {"unknown", "unknown 은 없다 — 타입을 적는다"},
    {"undefined", "undefined 는 없다 — T | null 을 쓴다"},
    {"never", "never 는 없다"},
    {"object", "object 는 없다 — interface 를 쓴다"},
    {"Object", "Object 는 없다 — interface 를 쓴다"},
    {"bigint", "bigint 는 없다 — number · i32"},
    {"symbol", "symbol 은 없다"},
    {"Function", "함수 타입은 없다"},
    {"ReadonlyArray", "ReadonlyArray 는 없다 [v0.3 뒤 검토]"},
    {"Readonly", "Readonly 는 없다 — 필드에 readonly 를 적는다"},
    {"Record", "Record 는 없다 — interface 를 쓴다"},
    {"Map", "Map 은 없다 [v0.4 뒤 검토]"},
    {"Set", "Set 은 없다 [v0.4 뒤 검토]"},
    {"Promise", "Promise 는 없다"},
};

const ScopeEntry* findEntry(const std::vector<ScopeEntry>& v, std::string_view name) {
    for (const ScopeEntry& e : v) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

// i32 로 정확한 정수인가
bool fitsI32(f64 v) {
    return std::isfinite(v) && v == std::trunc(v) && v >= -2147483648.0 && v <= 2147483647.0;
}

i32 wrapI32(i64 v) {
    return static_cast<i32>(static_cast<u32>(static_cast<u64>(v)));
}

class Checker {
public:
    Checker(Program& p, DiagnosticList& d) : P(p), D(d) {}

    void run(std::span<const SourceFile> files) {
        parseAll(files);
        setupSdk();
        resolveImports();
        topoOrder();
        for (const u32 mi : P.order) {
            if (P.modules[mi].parsed) {
                declareModule(mi);
            }
        }
        for (const u32 mi : P.order) {
            if (!P.modules[mi].parsed) {
                continue;
            }
            for (u32 fi = 0; fi < P.functions.size(); ++fi) {
                if (P.functions[fi].module == mi) {
                    ensureFunction(fi);
                }
            }
        }
    }

private:
    // ---- 공통 ----------------------------------------------------------------------------------------------------
    ModuleSema& mod() { return P.modules[m_ctx.module]; }
    Ast& ast() { return mod().ast; }
    const Node& node(NodeId n) { return ast().node(n); }
    NodeSema& sema(NodeId n) { return mod().nodes[n]; }
    ConstValue& cval(NodeId n) { return mod().consts[n]; }
    TypeTable& T() { return P.types; }
    TypeId set(NodeId n, TypeId t) {
        sema(n).type = t;
        return t;
    }

    void err(const char* code, NodeId at, std::string message) {
        D.error(code, mod().path, ast().node(at).pos, std::move(message));
    }

    SymbolId newSymbol(Symbol s) {
        P.symbols.push_back(std::move(s));
        m_ready.push_back(false);
        return static_cast<SymbolId>(P.symbols.size() - 1);
    }
    Symbol& sym(SymbolId s) { return P.symbols[s]; }

    // ---- 1 파싱 --------------------------------------------------------------------------------------------------
    void parseAll(std::span<const SourceFile> files) {
        std::vector<const SourceFile*> sorted;
        for (const SourceFile& f : files) {
            sorted.push_back(&f);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const SourceFile* a, const SourceFile* b) { return a->path < b->path; });
        for (usize i = 0; i < sorted.size(); ++i) {
            if (i > 0 && sorted[i]->path == sorted[i - 1]->path) {
                D.error("S416", sorted[i]->path, {}, "같은 경로의 스크립트가 둘이다");
                continue;
            }
            ModuleSema m;
            m.path = sorted[i]->path;
            const usize before = D.errorCount();
            m.ast = parseModule(sorted[i]->text, m.path, D);
            m.parsed = D.errorCount() == before;
            m.nodes.resize(m.ast.size());
            m.consts.resize(m.ast.size());
            P.modules.push_back(std::move(m));
        }
        m_mscopes.resize(P.modules.size());
        m_importTargets.resize(P.modules.size());
    }

    // ---- SDK · 전역 ----------------------------------------------------------------------------------------------
    void setupSdk() {
        auto addType = [&](std::string name, TypeId t) {
            Symbol s;
            s.kind = SymbolKind::Type;
            s.name = name;
            s.type = t;
            const SymbolId id = newSymbol(std::move(s));
            m_ready[id] = true;
            m_sdkTypes.push_back({std::move(name), id});
        };
        auto addBuiltin = [&](std::vector<ScopeEntry>& into, std::string name, Builtin b, TypeId t) {
            Symbol s;
            s.kind = SymbolKind::Builtin;
            s.name = name;
            s.builtin = b;
            s.type = t;
            const SymbolId id = newSymbol(std::move(s));
            m_ready[id] = true;
            into.push_back({std::move(name), id});
            return id;
        };
        addType("i32", TypeTable::kI32);
        for (const api::OpaqueTypeInfo& o : api::kOpaqueTypes) {
            addType(std::string(o.name), T().opaque(o.abi));
        }
        addBuiltin(m_sdkValues, "toI32", Builtin::ToI32, TypeTable::kError);
        addBuiltin(m_sdkValues, "idiv", Builtin::Idiv, TypeTable::kError);
        addBuiltin(m_globals, "Math", Builtin::MathNamespace, TypeTable::kError);
        const SymbolId nan = addBuiltin(m_globals, "NaN", Builtin::NaN, TypeTable::kNumber);
        sym(nan).value = numberConst(std::numeric_limits<f64>::quiet_NaN());
        const SymbolId inf = addBuiltin(m_globals, "Infinity", Builtin::Infinity, TypeTable::kNumber);
        sym(inf).value = numberConst(std::numeric_limits<f64>::infinity());
        // 컴포넌트 정의 (ADR-0059) — Vec2 는 vec2 필드의 값 타입 (ABI Vec2 — ADR-0057 4)
        m_vec2 = T().newStruct("Vec2");
        T().structOf(m_vec2).fields = {StructField{"x", TypeTable::kNumber, false},
                                       StructField{"y", TypeTable::kNumber, false}};
        addType("Vec2", m_vec2);
        addBuiltin(m_sdkValues, "defineComponent", Builtin::DefineComponent, TypeTable::kError);
        addBuiltin(m_sdkValues, "field", Builtin::FieldNamespace, TypeTable::kError);
        addBuiltin(m_sdkValues, "defineSystem", Builtin::DefineSystem, TypeTable::kError);
    }

    static ConstValue numberConst(f64 v) {
        ConstValue c;
        c.kind = ConstValue::Kind::Number;
        c.number = v;
        return c;
    }
    static ConstValue i32Const(i32 v) {
        ConstValue c;
        c.kind = ConstValue::Kind::I32;
        c.int32 = v;
        return c;
    }

    // ---- 2 import 그래프 ----------------------------------------------------------------------------------------
    [[nodiscard]] i32 findModule(std::string_view path) const {
        const auto it = std::lower_bound(P.modules.begin(), P.modules.end(), path,
                                         [](const ModuleSema& m, std::string_view p) { return m.path < p; });
        if (it != P.modules.end() && it->path == path) {
            return static_cast<i32>(it - P.modules.begin());
        }
        return kNoTarget;
    }

    void resolveImports() {
        for (u32 mi = 0; mi < P.modules.size(); ++mi) {
            if (!P.modules[mi].parsed) {
                continue;
            }
            m_ctx.module = mi;
            const Ast& a = ast();
            for (const NodeId item : a.kids(a.root)) {
                if (a.node(item).kind != NodeKind::Import) {
                    continue;
                }
                const std::string_view spec = a.str(item);
                i32 target = kNoTarget;
                if (spec == "sandbox") {
                    target = kSdkTarget;
                } else {
                    const std::string path = resolveImportPath(mod().path, spec);
                    if (path.empty()) {
                        err("S416", item,
                            std::format("import 경로 \"{}\" 를 쓸 수 없다 — \"./이름\" · \"../이름\" (확장자 없이, "
                                        "팩의 같은 폴더 "
                                        "트리 안) 또는 \"sandbox\"",
                                        spec));
                    } else if (const i32 t = findModule(path); t == kNoTarget) {
                        err("S416", item, std::format("\"{}\" 에 해당하는 스크립트 {} 가 없다", spec, path));
                    } else {
                        target = P.modules[static_cast<usize>(t)].parsed ? t : kBrokenTarget;
                    }
                }
                m_importTargets[mi].emplace_back(item, target);
            }
        }
    }

    // 위상 순서 (의존이 먼저). 순환이면 그 import 를 끊고 S416
    void topoOrder() {
        std::vector<u8> color(P.modules.size(), 0); // 0 흰 · 1 회색 · 2 검정
        for (u32 mi = 0; mi < P.modules.size(); ++mi) {
            visit(mi, color);
        }
    }
    void visit(u32 mi, std::vector<u8>& color) {
        if (color[mi] != 0) {
            return;
        }
        color[mi] = 1;
        for (auto& [item, target] : m_importTargets[mi]) {
            if (target < 0) {
                continue;
            }
            const u32 t = static_cast<u32>(target);
            if (color[t] == 1) {
                const u32 saved = m_ctx.module;
                m_ctx.module = mi;
                err("S416", item,
                    std::format(
                        "순환 import — {} 와 {} 가 서로를 (거쳐) 가져온다 (최상위가 선언뿐이라 순환이 필요 없다)",
                        P.modules[mi].path, P.modules[t].path));
                m_ctx.module = saved;
                target = kBrokenTarget;
                continue;
            }
            visit(t, color);
        }
        color[mi] = 2;
        P.order.push_back(mi);
    }

    // ---- 3 모듈 선언 ---------------------------------------------------------------------------------------------
    void bindModule(std::vector<ScopeEntry>& into, NodeId at, const std::string& name, SymbolId s, const char* what) {
        if (findEntry(into, name) != nullptr) {
            err("S402", at, std::format("{} '{}' 가 이 모듈에 이미 있다", what, name));
            return;
        }
        into.push_back({name, s});
    }

    SymbolId errorSymbol(const std::string& name) {
        Symbol s;
        s.kind = SymbolKind::Builtin;
        s.name = name;
        s.type = TypeTable::kError;
        const SymbolId id = newSymbol(std::move(s));
        m_ready[id] = true;
        return id;
    }

    void declareModule(u32 mi) {
        m_ctx = FnCtx{};
        m_ctx.module = mi;
        ModuleScope& ms = m_mscopes[mi];
        const NodeId root = ast().root;

        // import 묶기
        for (const auto& [item, target] : m_importTargets[mi]) {
            for (const NodeId nameNode : ast().kids(item)) {
                const std::string name(ast().str(nameNode));
                const NodeId aliasNode = ast().kid(nameNode, 0);
                const std::string local = aliasNode == kNoNode ? name : std::string(ast().str(aliasNode));
                if (target == kNoTarget || target == kBrokenTarget) {
                    const SymbolId e = errorSymbol(local);
                    bindModule(ms.values, nameNode, local, e, "이름");
                    ms.types.push_back({local, e});
                    continue;
                }
                const ScopeEntry* v = nullptr;
                const ScopeEntry* t = nullptr;
                if (target == kSdkTarget) {
                    v = findEntry(m_sdkValues, name);
                    t = findEntry(m_sdkTypes, name);
                } else {
                    const ModuleSema& from = P.modules[static_cast<usize>(target)];
                    for (const auto& [n, s] : from.exportValues) {
                        if (n == name) {
                            m_tmpEntry[0] = {n, s};
                            v = &m_tmpEntry[0];
                        }
                    }
                    for (const auto& [n, s] : from.exportTypes) {
                        if (n == name) {
                            m_tmpEntry[1] = {n, s};
                            t = &m_tmpEntry[1];
                        }
                    }
                }
                if (v == nullptr && t == nullptr) {
                    err("S416", nameNode, std::format("\"{}\" 는 '{}' 를 내보내지 않는다", ast().str(item), name));
                    const SymbolId e = errorSymbol(local);
                    ms.values.push_back({local, e});
                    ms.types.push_back({local, e});
                    continue;
                }
                if (v != nullptr) {
                    bindModule(ms.values, nameNode, local, v->sym, "이름");
                }
                if (t != nullptr) {
                    bindModule(ms.types, nameNode, local, t->sym, "타입");
                }
            }
        }

        // 최상위 이름
        std::vector<std::pair<NodeId, SymbolId>> aliases;
        std::vector<std::pair<NodeId, SymbolId>> interfaces;
        std::vector<u32> fns;
        std::vector<NodeId> defaults;
        for (const NodeId item : ast().kids(root)) {
            const Node& n = node(item);
            const std::string name(ast().str(item));
            const bool exported = (n.flags & NodeFlag::Exported) != 0;
            switch (n.kind) {
            case NodeKind::Function: {
                FunctionInfo f;
                f.name = name;
                f.module = mi;
                f.decl = item;
                P.functions.push_back(std::move(f));
                Symbol s;
                s.kind = SymbolKind::Function;
                s.name = name;
                s.module = mi;
                s.decl = item;
                s.function = static_cast<u32>(P.functions.size() - 1);
                const SymbolId id = newSymbol(std::move(s));
                m_ready[id] = true;
                bindModule(ms.values, item, name, id, "이름");
                if (exported) {
                    mod().exportValues.emplace_back(name, id);
                }
                fns.push_back(static_cast<u32>(P.functions.size() - 1));
                sema(item) = {TypeTable::kVoid, RefKind::Symbol, id};
                break;
            }
            case NodeKind::VarDecl: {
                Symbol s;
                s.kind = SymbolKind::ModuleConst;
                s.name = name;
                s.module = mi;
                s.decl = item;
                const SymbolId id = newSymbol(std::move(s));
                bindModule(ms.values, item, name, id, "이름");
                if (exported) {
                    mod().exportValues.emplace_back(name, id);
                }
                sema(item) = {TypeTable::kError, RefKind::Symbol, id};
                break;
            }
            case NodeKind::TypeAlias:
            case NodeKind::Interface: {
                Symbol s;
                s.kind = SymbolKind::Type;
                s.name = name;
                s.module = mi;
                s.decl = item;
                if (n.kind == NodeKind::Interface) {
                    s.type = T().newStruct(name);
                }
                const SymbolId id = newSymbol(std::move(s));
                bindModule(ms.types, item, name, id, "타입");
                if (exported) {
                    mod().exportTypes.emplace_back(name, id);
                }
                (n.kind == NodeKind::Interface ? interfaces : aliases).emplace_back(item, id);
                sema(item) = {TypeTable::kError, RefKind::Symbol, id};
                break;
            }
            case NodeKind::Enum: {
                Symbol s;
                s.kind = SymbolKind::Enum;
                s.name = name;
                s.module = mi;
                s.decl = item;
                s.type = T().newEnum(name);
                const SymbolId id = newSymbol(std::move(s));
                bindModule(ms.values, item, name, id, "이름");
                bindModule(ms.types, item, name, id, "타입");
                if (exported) {
                    mod().exportValues.emplace_back(name, id);
                    mod().exportTypes.emplace_back(name, id);
                }
                sema(item) = {sym(id).type, RefKind::Symbol, id};
                break;
            }
            case NodeKind::ExportDefault:
                defaults.push_back(item);
                break;
            default:
                break;
            }
        }

        // interface 필드 (재귀 interface 는 구조체를 먼저 만들었으니 서로를 가리킬 수 있다)
        for (const auto& [item, id] : interfaces) {
            m_ready[id] = true;
            std::vector<StructField> fields;
            for (const NodeId prop : ast().kids(item)) {
                const std::string fname(ast().str(prop));
                bool dup = false;
                for (const StructField& f : fields) {
                    dup = dup || f.name == fname;
                }
                if (dup) {
                    err("S402", prop, std::format("필드 '{}' 가 두 번 있다", fname));
                    continue;
                }
                StructField f;
                f.name = fname;
                f.type = resolveType(ast().kid(prop, 0), false);
                f.readonly = (node(prop).flags & NodeFlag::Readonly) != 0;
                fields.push_back(std::move(f));
            }
            T().structOf(sym(id).type).fields = std::move(fields);
        }
        for (const auto& [item, id] : aliases) {
            (void)item;
            resolveAlias(id);
        }
        for (const u32 fi : fns) {
            declareSignature(fi);
        }
        // enum 과 최상위 const 는 소스 순서대로 (서로의 앞 것을 쓸 수 있다)
        for (const NodeId item : ast().kids(root)) {
            if (node(item).kind == NodeKind::Enum) {
                declareEnum(item, sema(item).refIndex);
            } else if (node(item).kind == NodeKind::VarDecl) {
                declareModuleConst(item);
            }
        }
        // export default defineSystem — 모듈의 함수 서명 · const 가 다 정해진 뒤 (메서드 몸은 4 에서)
        for (usize i = 0; i < defaults.size(); ++i) {
            if (i > 0) {
                err("S426", defaults[i], "export default 는 모듈에 하나 — 시스템마다 파일을 나눈다");
                continue;
            }
            declareSystem(defaults[i]);
        }
    }

    TypeId resolveAlias(SymbolId id) {
        Symbol& s = sym(id);
        if (m_ready[id]) {
            return s.type;
        }
        if (m_resolving.size() <= id) {
            m_resolving.resize(id + 1, false);
        }
        if (m_resolving[id]) {
            const u32 saved = m_ctx.module;
            m_ctx.module = s.module;
            err("S417", s.decl, std::format("타입 별칭 '{}' 가 자기 자신을 가리킨다 (순환)", s.name));
            m_ctx.module = saved;
            s.type = TypeTable::kError;
            m_ready[id] = true;
            return TypeTable::kError;
        }
        m_resolving[id] = true;
        const u32 saved = m_ctx.module;
        m_ctx.module = s.module;
        const TypeId t = resolveType(ast().kid(s.decl, 0), false);
        m_ctx.module = saved;
        sym(id).type = t;
        m_ready[id] = true;
        m_resolving[id] = false;
        return t;
    }

    void declareEnum(NodeId item, SymbolId id) {
        const TypeId et = sym(id).type;
        m_ready[id] = true; // 멤버 값이 앞 멤버를 쓸 수 있다 (E.A) — 아직 없는 멤버는 S406
        i64 next = 0;
        for (const NodeId member : ast().kids(item)) {
            const std::string mname(ast().str(member));
            EnumType& e = T().enumOf(et);
            bool dup = false;
            for (const EnumMemberInfo& m : e.members) {
                dup = dup || m.name == mname;
            }
            if (dup) {
                err("S402", member, std::format("enum 멤버 '{}' 가 두 번 있다", mname));
                continue;
            }
            const NodeId init = ast().kid(member, 0);
            if (init != kNoNode) {
                expr(init, TypeTable::kI32);
                const ConstValue& c = cval(init);
                if ((c.kind == ConstValue::Kind::I32) || (c.kind == ConstValue::Kind::Number && fitsI32(c.number))) {
                    next = c.kind == ConstValue::Kind::I32 ? c.int32 : static_cast<i64>(c.number);
                } else if (!T().isError(sema(init).type)) {
                    err("S418", init, "enum 값은 i32 범위의 정수 상수식이다 (문자열 enum 은 없다)");
                }
            }
            if (next > std::numeric_limits<i32>::max()) {
                err("S418", member, "enum 값이 i32 를 넘는다");
                next = 0;
            }
            T().enumOf(et).members.push_back({mname, static_cast<i32>(next)});
            ++next;
        }
    }

    void declareSignature(u32 fi) {
        FunctionInfo& f = P.functions[fi];
        const NodeId decl = f.decl;
        const auto kids = ast().kids(decl);
        // 자식: Param… , 반환 타입|kNoNode, Block
        const usize paramCount = kids.size() - 2;
        std::vector<TypeId> params;
        std::vector<SymbolId> paramSyms;
        for (usize i = 0; i < paramCount; ++i) {
            const NodeId p = kids[i];
            const std::string pname(ast().str(p));
            bool dup = false;
            for (const SymbolId s : paramSyms) {
                dup = dup || sym(s).name == pname;
            }
            if (dup) {
                err("S402", p, std::format("매개변수 '{}' 가 두 번 있다", pname));
            }
            TypeId pt = TypeTable::kError;
            if (ast().kid(p, 0) == kNoNode) {
                err("S414", p, std::format("매개변수 '{}' 에 타입을 적는다 (x: number)", pname));
            } else {
                pt = resolveType(ast().kid(p, 0), false);
            }
            Symbol s;
            s.kind = SymbolKind::Param;
            s.name = pname;
            s.type = pt;
            s.module = f.module;
            s.decl = p;
            s.mutableVar = true;
            const SymbolId sid = newSymbol(std::move(s));
            m_ready[sid] = true;
            sema(p) = {pt, RefKind::Symbol, sid};
            params.push_back(pt);
            paramSyms.push_back(sid);
        }
        FunctionInfo& g = P.functions[fi];
        g.params = std::move(params);
        g.paramSymbols = std::move(paramSyms);
        const NodeId ret = kids[paramCount];
        if (ret != kNoNode) {
            g.result = resolveType(ret, true);
            g.resultDeclared = true;
        }
    }

    void declareModuleConst(NodeId item) {
        const SymbolId id = sema(item).refIndex;
        const NodeId typeNode = ast().kid(item, 0);
        const NodeId init = ast().kid(item, 1);
        if (init != kNoNode && isDefineComponentCall(init)) {
            declareComponent(item, id, typeNode, init);
            return;
        }
        TypeId declared = kNoExpect;
        if (typeNode != kNoNode) {
            declared = resolveType(typeNode, false);
        }
        TypeId t = TypeTable::kError;
        if (init != kNoNode) {
            const TypeId it = expr(init, declared);
            t = declared == kNoExpect ? it : declared;
            if (declared != kNoExpect) {
                assignTo(init, it, declared);
            }
            const ConstValue& c = cval(init);
            const TypeKind k = T().kind(T().nonNull(t));
            if (T().isError(it)) {
                // 이미 보고했다
            } else if (k == TypeKind::Array || k == TypeKind::Struct) {
                err("S415", init,
                    "최상위 const 는 기본 타입 (number · i32 · boolean · string · enum) 상수만 — 표 · 구조체 상수는 "
                    "[v0.3 뒤 검토], 함수 안에서 만든다");
                t = TypeTable::kError;
            } else if (!c.has()) {
                err("S415", init,
                    "최상위 const 는 상수식이다 (리터럴 · 연산 · 앞의 const · enum 값) — 틱을 넘는 상태는 "
                    "컴포넌트에");
            } else {
                sym(id).value = c;
            }
        }
        sym(id).type = t;
        sema(item).type = t;
        m_ready[id] = true;
    }

    // ---- defineSystem (ADR-0062, 19 17장) --------------------------------------------------------------------------
    // export default defineSystem({ id, domain?, intervalTicks?, onTick(ctx) { … } }) — 값은 상수, onTick 은 메서드
    // 하나가 함수 하나 (Program::functions). 정의에 오류가 있어도 메서드 몸은 검사한다 (이어지는 진단을 위해)
    void declareSystem(NodeId item) {
        const NodeId init = ast().kid(item, 0);
        if (init != kNoNode && node(init).kind == NodeKind::Call && node(ast().kid(init, 0)).kind == NodeKind::Ident &&
            ast().str(ast().kid(init, 0)) == "defineSystem" &&
            !isSdkBuiltin(ast().kid(init, 0), Builtin::DefineSystem)) {
            err("S401", ast().kid(init, 0), "없는 이름 'defineSystem' — import { defineSystem } from \"sandbox\"");
            return;
        }
        if (init == kNoNode || node(init).kind != NodeKind::Call ||
            !isSdkBuiltin(ast().kid(init, 0), Builtin::DefineSystem)) {
            err("S423", item, "export default 는 defineSystem({ … }) 만 — 다른 것은 export function · const 로");
            return;
        }
        const auto kids = ast().kids(init);
        if (kids.size() != 2 || node(kids[1]).kind != NodeKind::ObjectLit) {
            err("S426", init, "defineSystem({ id, onTick(ctx) { … } }) — 객체 리터럴 하나를 넘긴다");
            return;
        }
        SystemDecl sd;
        sd.module = mod().path;
        sd.line = node(init).pos.line;
        sd.column = node(init).pos.column;
        sd.decl = init;
        bool ok = true;
        bool haveId = false;
        bool haveTick = false;
        std::vector<std::string> seen;
        for (const NodeId p : ast().kids(kids[1])) {
            const std::string key(ast().str(p));
            const bool method = node(p).kind == NodeKind::Method;
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
                err("S402", p, std::format("키 '{}' 가 두 번 있다", key));
                ok = false;
                continue;
            }
            seen.push_back(key);
            if (key == "onTick") {
                if (!method) {
                    err("S426", p,
                        "onTick 은 메서드로 적는다 — onTick(ctx) { … } (공용 코드는 최상위 function 을 부른다)");
                    ok = false;
                    continue;
                }
                sd.onTick = declareSystemMethod(p, ok);
                haveTick = true;
                continue;
            }
            if (key == "onStart" || key == "onStop" || key == "onEvent") {
                err("S426", p, std::format("{} 는 [계획 v0.3 M4-T2] — 지금은 onTick 만", key));
                ok = false;
                continue;
            }
            if (key == "onUpdate") {
                err("S426", p, "onUpdate 는 client 도메인 [계획 v0.3 M5] — 서버 시스템은 onTick");
                ok = false;
                continue;
            }
            if (method) {
                err("S426", p, std::format("defineSystem 의 메서드는 onTick 뿐이다 ('{}')", key));
                ok = false;
                continue;
            }
            if (node(p).kind != NodeKind::Property || (node(p).flags & NodeFlag::Shorthand) != 0) {
                err("S426", p, std::format("defineSystem 의 '{}' 는 키: 상수 로 적는다", key));
                ok = false;
                continue;
            }
            const NodeId value = ast().kid(p, 0);
            if (key == "id" || key == "domain") {
                const TypeId t = expr(value, TypeTable::kString);
                if (T().isError(t) || !assignTo(value, t, TypeTable::kString)) {
                    ok = false;
                    continue;
                }
                if (cval(value).kind != ConstValue::Kind::String) {
                    err("S426", value, std::format("{} 는 문자열 상수다", key));
                    ok = false;
                    continue;
                }
                const std::string& text = cval(value).string;
                if (key == "domain") {
                    if (text != "server") {
                        err("S426", value,
                            text == "client" ? std::string("domain \"client\" 는 [계획 v0.3 M5] — 지금은 \"server\" 만")
                                             : std::format("domain '{}' — \"server\" 만 (기본값)", text));
                        ok = false;
                    }
                    continue;
                }
                sd.id = text;
                haveId = true;
                if (!api::isValidComponentId(sd.id)) {
                    err("S426", value,
                        std::format(
                            "id '{}' — 이름공간.이름 (소문자 · 숫자 · _ · 점, 점 하나 이상: \"rpg.mana.regen\")",
                            sd.id));
                    ok = false;
                } else if (api::isEngineComponentNamespace(sd.id)) {
                    err("S426", value,
                        std::format("id '{}' 는 엔진 이름공간이다 — 게임 · 스튜디오 이름공간을 쓴다", sd.id));
                    ok = false;
                } else {
                    for (const SystemDecl& other : P.systems) {
                        if (other.id == sd.id) {
                            err("S426", value,
                                std::format("시스템 id '{}' 가 이미 {}:{} 에 있다", sd.id, other.module, other.line));
                            ok = false;
                        }
                    }
                }
            } else if (key == "intervalTicks") {
                const auto v = constI32Arg(value, "intervalTicks", "S426");
                if (!v) {
                    ok = false;
                } else if (*v < 1 || static_cast<u32>(*v) > kMaxSystemIntervalTicks) {
                    err("S426", value, std::format("intervalTicks 는 1 ~ {} — {} 이다", kMaxSystemIntervalTicks, *v));
                    ok = false;
                } else {
                    sd.intervalTicks = static_cast<u32>(*v);
                }
            } else if (key == "stage") {
                err("S426", p, "단계는 하나로 고정이다 (16b — 순서는 팩 순서 · 시스템 id, ADR-0062)");
                ok = false;
            } else {
                err("S426", p,
                    std::format("defineSystem 에 '{}' 키는 없다 — id · domain · intervalTicks · onTick", key));
                expr(value, kNoExpect);
                ok = false;
            }
        }
        if (!haveId) {
            err("S426", init, "defineSystem 에 id 가 없다 (\"rpg.mana.regen\")");
            ok = false;
        }
        if (!haveTick) {
            err("S426", init, "defineSystem 에 onTick(ctx) { … } 이 없다");
            ok = false;
        }
        if (ok) {
            P.systems.push_back(std::move(sd));
        }
    }

    // 시스템 메서드 → 함수 하나 (서명: 매개변수 0 · 1 개 — 타입을 적지 않으면 ServerContext, 반환 void). 서명이 틀리면
    // ok = false (몸은 그래도 검사한다 — 적은 반환 타입으로)
    u32 declareSystemMethod(NodeId m, bool& ok) {
        FunctionInfo f;
        f.name = std::string(ast().str(m));
        f.module = m_ctx.module;
        f.decl = m;
        f.result = TypeTable::kVoid;
        f.resultDeclared = true;
        const auto kids = ast().kids(m);
        const usize paramCount = kids.size() - 2;
        const TypeId ctxType = T().opaque(api::AbiType::ServerContext);
        if (paramCount > 1) {
            err("S426", kids[1], std::format("{} 의 매개변수는 ctx 하나다", f.name));
            ok = false;
        }
        for (usize i = 0; i < paramCount; ++i) {
            const NodeId p = kids[i];
            TypeId pt = ctxType;
            if (ast().kid(p, 0) != kNoNode) {
                pt = resolveType(ast().kid(p, 0), false);
                if (!T().isError(pt) && pt != ctxType) {
                    err("S426", p, std::format("{} 의 ctx 는 ServerContext 다 — {} 이다", f.name, T().name(pt)));
                    pt = TypeTable::kError;
                    ok = false;
                }
            }
            if (i > 0) {
                pt = TypeTable::kError;
            }
            Symbol s;
            s.kind = SymbolKind::Param;
            s.name = std::string(ast().str(p));
            s.type = pt;
            s.module = f.module;
            s.decl = p;
            s.mutableVar = true;
            const SymbolId sid = newSymbol(std::move(s));
            m_ready[sid] = true;
            sema(p) = {pt, RefKind::Symbol, sid};
            f.params.push_back(pt);
            f.paramSymbols.push_back(sid);
        }
        if (const NodeId ret = kids[paramCount]; ret != kNoNode) {
            const TypeId rt = resolveType(ret, true);
            if (!T().isError(rt) && rt != TypeTable::kVoid) {
                err("S426", ret, std::format("{} 는 값을 돌려주지 않는다 (void) — {} 이다", f.name, T().name(rt)));
                f.result = rt; // 몸의 return 이 이어서 S412 를 내지 않게
                ok = false;
            }
        }
        P.functions.push_back(std::move(f));
        return static_cast<u32>(P.functions.size() - 1);
    }

    // ---- defineComponent (ADR-0059, 19 15장) -----------------------------------------------------------------------
    // 조용한 찾기 (최상위 문맥 — 모듈 이름공간 · 전역). 없으면 보통 길이 S401 을 낸다
    bool isSdkBuiltin(NodeId n, Builtin b) {
        if (node(n).kind != NodeKind::Ident) {
            return false;
        }
        const std::string_view name = ast().str(n);
        const ScopeEntry* e = findEntry(m_mscopes[m_ctx.module].values, name);
        if (e == nullptr) {
            e = findEntry(m_globals, name);
        }
        return e != nullptr && sym(e->sym).kind == SymbolKind::Builtin && sym(e->sym).builtin == b;
    }
    bool isDefineComponentCall(NodeId init) {
        return node(init).kind == NodeKind::Call && isSdkBuiltin(ast().kid(init, 0), Builtin::DefineComponent);
    }

    std::optional<f64> constNumberArg(NodeId v, std::string_view what, const char* code = "S424") {
        const TypeId t = expr(v, TypeTable::kNumber);
        if (T().isError(t) || !assignTo(v, t, TypeTable::kNumber)) {
            return std::nullopt;
        }
        const ConstValue& c = cval(v);
        if (!c.has() || !std::isfinite(c.asNumber())) {
            err(code, v, std::format("{} 는 유한한 수 상수다", what));
            return std::nullopt;
        }
        return c.asNumber();
    }
    std::optional<i32> constI32Arg(NodeId v, std::string_view what, const char* code = "S424") {
        const TypeId t = expr(v, TypeTable::kI32);
        if (T().isError(t) || !assignTo(v, t, TypeTable::kI32)) {
            return std::nullopt;
        }
        const ConstValue& c = cval(v);
        if (c.kind == ConstValue::Kind::I32) {
            return c.int32;
        }
        if (c.kind == ConstValue::Kind::Number && fitsI32(c.number)) {
            return static_cast<i32>(c.number);
        }
        err(code, v, std::format("{} 는 i32 정수 상수다", what));
        return std::nullopt;
    }
    std::optional<bool> constBoolArg(NodeId v, std::string_view what) {
        const TypeId t = expr(v, TypeTable::kBool);
        if (T().isError(t) || !assignTo(v, t, TypeTable::kBool)) {
            return std::nullopt;
        }
        if (cval(v).kind != ConstValue::Kind::Bool) {
            err("S424", v, std::format("{} 는 boolean 상수다 (true · false)", what));
            return std::nullopt;
        }
        return cval(v).boolean;
    }

    // { min, max } — 둘 다 적는다
    bool rangeArg(NodeId v, bool integer, api::ComponentFieldSchema& f) {
        if (node(v).kind != NodeKind::ObjectLit) {
            err("S424", v, "범위는 { min: …, max: … } 객체 리터럴이다");
            expr(v, kNoExpect);
            return false;
        }
        std::optional<f64> lo;
        std::optional<f64> hi;
        bool ok = true;
        for (const NodeId p : ast().kids(v)) {
            const std::string_view key = ast().str(p);
            if (node(p).kind != NodeKind::Property || (node(p).flags & NodeFlag::Shorthand) != 0 ||
                (key != "min" && key != "max")) {
                err("S424", p, std::format("범위의 키는 min · max 뿐이다 ('{}')", key));
                ok = false;
                continue;
            }
            std::optional<f64>& slot = key == "min" ? lo : hi;
            if (slot) {
                err("S402", p, std::format("키 '{}' 가 두 번 있다", key));
                ok = false;
                continue;
            }
            const NodeId value = ast().kid(p, 0);
            if (integer) {
                if (const auto x = constI32Arg(value, std::format("범위 {}", key))) {
                    slot = static_cast<f64>(*x);
                }
            } else {
                slot = constNumberArg(value, std::format("범위 {}", key));
            }
            ok = ok && slot.has_value();
        }
        if (!ok) {
            return false;
        }
        if (!lo || !hi) {
            err("S424", v, "범위는 min 과 max 를 둘 다 적는다");
            return false;
        }
        if (*lo > *hi) {
            err("S424", v, std::format("범위 min {} 이 max {} 보다 크다", *lo, *hi));
            return false;
        }
        f.hasRange = true;
        f.min = *lo;
        f.max = *hi;
        return true;
    }

    // field.종류(…) 하나 → 필드 스키마 (이름은 부르는 쪽이 채운다)
    std::optional<api::ComponentFieldSchema> fieldDef(NodeId v, TypeId& valueType) {
        using K = api::ComponentFieldKind;
        if (node(v).kind != NodeKind::Call || node(ast().kid(v, 0)).kind != NodeKind::Member ||
            !isSdkBuiltin(ast().kid(ast().kid(v, 0), 0), Builtin::FieldNamespace)) {
            err("S424", v, "필드는 field.i32 · f32 · f64 · bool · vec2 · entity · enumOf (…) 로 적는다");
            if (node(v).kind != NodeKind::Call) {
                expr(v, kNoExpect);
            }
            return std::nullopt;
        }
        const NodeId callee = ast().kid(v, 0);
        const std::string_view kind = ast().str(callee);
        const auto kids = ast().kids(v);
        const std::vector<NodeId> args(kids.begin() + 1, kids.end());
        api::ComponentFieldSchema f;
        auto argCount = [&](usize minArgs, usize maxArgs) {
            if (args.size() < minArgs || args.size() > maxArgs) {
                const std::string want =
                    minArgs == maxArgs ? std::format("{}", maxArgs) : std::format("{} ~ {}", minArgs, maxArgs);
                err("S405", v, std::format("field.{} 는 인자 {} 개 — {} 개를 넘겼다", kind, want, args.size()));
                return false;
            }
            return true;
        };
        bool ok = true;
        if (kind == "i32" || kind == "f32" || kind == "f64") {
            const bool integer = kind == "i32";
            f.kind = integer ? K::I32 : (kind == "f32" ? K::F32 : K::F64);
            valueType = integer ? TypeTable::kI32 : TypeTable::kNumber;
            if (!argCount(0, 2)) {
                return std::nullopt;
            }
            if (!args.empty()) {
                if (integer) {
                    const auto x = constI32Arg(args[0], "기본값");
                    ok = x.has_value();
                    f.defaultValue = x ? static_cast<f64>(*x) : 0.0;
                } else {
                    const auto x = constNumberArg(args[0], "기본값");
                    ok = x.has_value();
                    f.defaultValue = x.value_or(0.0);
                }
            }
            if (args.size() == 2) {
                ok = rangeArg(args[1], integer, f) && ok;
            }
            if (!ok) {
                return std::nullopt;
            }
            if (f.kind == K::F32) {
                if (std::fabs(f.defaultValue) > static_cast<f64>(std::numeric_limits<f32>::max())) {
                    err("S424", args[0], std::format("기본값 {} 이 f32 범위 밖이다", f.defaultValue));
                    return std::nullopt;
                }
                f.defaultValue = api::roundToF32(f.defaultValue);
            }
            if (f.hasRange && !(f.defaultValue >= f.min && f.defaultValue <= f.max)) {
                err("S424", args.empty() ? v : args[0],
                    std::format("기본값 {} 이 범위 [{}, {}] 밖이다{}", f.defaultValue, f.min, f.max,
                                args.empty() ? " — 기본값을 적는다" : ""));
                return std::nullopt;
            }
        } else if (kind == "bool") {
            f.kind = K::Bool;
            valueType = TypeTable::kBool;
            if (!argCount(0, 1)) {
                return std::nullopt;
            }
            if (!args.empty()) {
                const auto x = constBoolArg(args[0], "기본값");
                ok = x.has_value();
                f.defaultValue = x.value_or(false) ? 1.0 : 0.0;
            }
        } else if (kind == "vec2") {
            f.kind = K::Vec2;
            valueType = m_vec2;
            if (!argCount(0, 2)) {
                return std::nullopt;
            }
            f64* const slots[] = {&f.defaultValue, &f.defaultY};
            for (usize i = 0; i < args.size(); ++i) {
                const auto x = constNumberArg(args[i], i == 0 ? "x" : "y");
                if (!x) {
                    ok = false;
                    continue;
                }
                if (std::fabs(*x) > static_cast<f64>(std::numeric_limits<f32>::max())) {
                    err("S424", args[i], std::format("{} 이 f32 범위 밖이다 (vec2 = f32 둘)", *x));
                    ok = false;
                    continue;
                }
                *slots[i] = api::roundToF32(*x);
            }
        } else if (kind == "entity") {
            f.kind = K::Entity;
            valueType = T().nullableOf(T().opaque(api::AbiType::Entity));
            if (!argCount(0, 0)) {
                return std::nullopt;
            }
        } else if (kind == "enumOf") {
            f.kind = K::Enum;
            if (!argCount(1, 1)) {
                return std::nullopt;
            }
            const TypeId t = expr(args[0], kNoExpect);
            if (T().isError(t)) {
                return std::nullopt;
            }
            if (T().kind(t) != TypeKind::Enum || cval(args[0]).kind != ConstValue::Kind::I32) {
                err("S424", args[0], "field.enumOf 의 인자는 enum 멤버다 (E.A — 기본값이자 필드의 타입)");
                return std::nullopt;
            }
            valueType = t;
            const EnumType& e = T().enumOf(t);
            f.enumName = e.name;
            for (const EnumMemberInfo& m : e.members) {
                f.enumMembers.emplace_back(m.name, m.value);
            }
            f.defaultValue = static_cast<f64>(cval(args[0]).int32);
        } else {
            err("S406", callee,
                std::format("field 에는 '{}' 가 없다 — i32 · f32 · f64 · bool · vec2 · entity · enumOf", kind));
            for (const NodeId a : args) {
                expr(a, kNoExpect);
            }
            return std::nullopt;
        }
        if (!ok) {
            return std::nullopt;
        }
        return f;
    }

    // 정의 모양: { id, version?, fields, persistent?, replicated?, editorVisible?, serverOnly? } — 모두 상수
    void declareComponent(NodeId item, SymbolId id, NodeId typeNode, NodeId init) {
        auto fail = [&] {
            sym(id).type = TypeTable::kError;
            sema(item).type = TypeTable::kError;
            m_ready[id] = true;
        };
        if (typeNode != kNoNode) {
            err("S424", typeNode, "defineComponent 의 const 에는 타입을 적지 않는다 (정의가 타입을 정한다)");
        }
        const auto kids = ast().kids(init);
        if (kids.size() != 2 || node(kids[1]).kind != NodeKind::ObjectLit) {
            err("S424", init, "defineComponent({ id, version, fields: { … } }) — 객체 리터럴 하나를 넘긴다");
            for (usize i = 1; i < kids.size(); ++i) {
                expr(kids[i], kNoExpect);
            }
            fail();
            return;
        }
        const NodeId def = kids[1];
        api::ComponentSchema schema;
        schema.module = mod().path;
        schema.line = node(init).pos.line;
        schema.column = node(init).pos.column;
        bool ok = typeNode == kNoNode;
        bool haveId = false;
        bool haveFields = false;
        std::optional<bool> replicated;
        std::vector<std::string> seen;
        std::vector<std::pair<api::ComponentFieldSchema, TypeId>> fields;
        for (const NodeId p : ast().kids(def)) {
            const std::string key(ast().str(p));
            if (node(p).kind != NodeKind::Property || (node(p).flags & NodeFlag::Shorthand) != 0) {
                err("S424", p, std::format("defineComponent 의 '{}' 는 키: 상수 로 적는다", key));
                ok = false;
                continue;
            }
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
                err("S402", p, std::format("키 '{}' 가 두 번 있다", key));
                ok = false;
                continue;
            }
            seen.push_back(key);
            const NodeId value = ast().kid(p, 0);
            if (key == "id") {
                const TypeId t = expr(value, TypeTable::kString);
                if (T().isError(t) || !assignTo(value, t, TypeTable::kString)) {
                    ok = false;
                    continue;
                }
                if (cval(value).kind != ConstValue::Kind::String) {
                    err("S424", value, "id 는 문자열 상수다");
                    ok = false;
                    continue;
                }
                schema.id = cval(value).string;
                haveId = true;
                if (!api::isValidComponentId(schema.id)) {
                    err("S424", value,
                        std::format("id '{}' — 이름공간.이름 (소문자 · 숫자 · _ · 점, 점 하나 이상: \"rpg.mana\")",
                                    schema.id));
                    ok = false;
                } else if (api::isEngineComponentNamespace(schema.id)) {
                    err("S424", value,
                        std::format("id '{}' 는 엔진 이름공간이다 — 게임 · 스튜디오 이름공간을 쓴다 (\"rpg.mana\")",
                                    schema.id));
                    ok = false;
                } else {
                    for (const ComponentDecl& other : P.components) {
                        if (other.schema.id == schema.id) {
                            err("S424", value,
                                std::format("컴포넌트 id '{}' 가 이미 {}:{} 에 있다", schema.id, other.schema.module,
                                            other.schema.line));
                            ok = false;
                        }
                    }
                }
            } else if (key == "version") {
                const auto v = constI32Arg(value, "version");
                if (!v) {
                    ok = false;
                } else if (*v < 1 || static_cast<u32>(*v) > api::kMaxComponentVersion) {
                    err("S424", value, std::format("version 은 1 ~ {} — {} 이다", api::kMaxComponentVersion, *v));
                    ok = false;
                } else {
                    schema.version = static_cast<u16>(*v);
                }
            } else if (key == "persistent" || key == "replicated" || key == "editorVisible" || key == "serverOnly") {
                const auto b = constBoolArg(value, key);
                if (!b) {
                    ok = false;
                    continue;
                }
                if (key == "persistent") {
                    schema.persistent = *b;
                } else if (key == "replicated") {
                    replicated = *b;
                } else if (key == "editorVisible") {
                    schema.editorVisible = *b;
                } else {
                    schema.serverOnly = *b;
                }
            } else if (key == "fields") {
                haveFields = true;
                if (node(value).kind != NodeKind::ObjectLit) {
                    err("S424", value, "fields 는 { 이름: field.종류(…), … } 객체 리터럴이다");
                    expr(value, kNoExpect);
                    ok = false;
                    continue;
                }
                for (const NodeId fp : ast().kids(value)) {
                    const std::string fname(ast().str(fp));
                    if (node(fp).kind != NodeKind::Property || (node(fp).flags & NodeFlag::Shorthand) != 0) {
                        err("S424", fp, std::format("필드 '{}' 는 이름: field.종류(…) 로 적는다", fname));
                        ok = false;
                        continue;
                    }
                    bool dup = false;
                    for (const auto& entry : fields) {
                        dup = dup || entry.first.name == fname;
                    }
                    if (dup) {
                        err("S402", fp, std::format("필드 '{}' 가 두 번 있다", fname));
                        ok = false;
                        continue;
                    }
                    if (!api::isValidFieldName(fname)) {
                        err("S424", fp, std::format("필드 이름 '{}' 는 식별자다 ([A-Za-z_][A-Za-z0-9_]*)", fname));
                        ok = false;
                        continue;
                    }
                    TypeId vt = TypeTable::kError;
                    auto f = fieldDef(ast().kid(fp, 0), vt);
                    if (!f) {
                        ok = false;
                        continue;
                    }
                    f->name = fname;
                    set(fp, vt);
                    fields.emplace_back(std::move(*f), vt);
                }
                if (fields.size() > api::kMaxComponentFields) {
                    err("S424", value,
                        std::format("필드는 {} 개까지 — {} 개다", api::kMaxComponentFields, fields.size()));
                    ok = false;
                }
            } else {
                err("S424", p,
                    std::format("defineComponent 에 '{}' 는 없다 — id · version · fields · persistent · replicated · "
                                "editorVisible · serverOnly",
                                key));
                expr(value, kNoExpect);
                ok = false;
            }
        }
        if (!haveId && std::find(seen.begin(), seen.end(), "id") == seen.end()) {
            err("S424", def, "defineComponent 에 id 를 적는다 (\"rpg.mana\")");
            ok = false;
        }
        if (!haveFields) {
            err("S424", def, "defineComponent 에 fields 를 적는다 ({ current: field.f32(100) })");
            ok = false;
        }
        if (schema.serverOnly && replicated.value_or(false)) {
            err("S424", def, "serverOnly 컴포넌트는 복제하지 않는다 — replicated 를 빼거나 false 로");
            ok = false;
        }
        schema.replicated = replicated.value_or(!schema.serverOnly);
        if (!ok) {
            fail();
            return;
        }
        // 이름 순 (ADR-0059 2 — 선언 순서를 바꿔도 같은 컴포넌트)
        std::sort(fields.begin(), fields.end(),
                  [](const auto& a, const auto& b) { return a.first.name < b.first.name; });
        const TypeId data = T().newStruct(sym(id).name);
        std::vector<StructField> sf;
        for (auto& entry : fields) {
            sf.push_back(StructField{entry.first.name, entry.second, false});
            schema.fields.push_back(std::move(entry.first));
        }
        T().structOf(data).fields = std::move(sf);
        ComponentDecl d;
        d.schema = std::move(schema);
        d.symbol = id;
        d.dataType = data;
        d.module = m_ctx.module;
        d.decl = item;
        P.components.push_back(std::move(d));
        const u32 index = static_cast<u32>(P.components.size() - 1);
        const TypeId ref = T().newComponentRef(data, index); // ComponentRef<T> — ctx.read · has · query · write 의 인자
        Symbol& s = sym(id);
        s.kind = SymbolKind::Component;
        s.function = index;
        s.type = ref;
        sema(item).type = ref;
        m_ready[id] = true;
    }

    // ---- 타입 풀기 ------------------------------------------------------------------------------------------------
    TypeId resolveType(NodeId n, bool allowVoid) {
        if (n == kNoNode) {
            return TypeTable::kError;
        }
        const Node& tn = node(n);
        switch (tn.kind) {
        case NodeKind::NullType:
            return set(n, TypeTable::kNull);
        case NodeKind::ArrayType: {
            const TypeId e = resolveType(ast().kid(n, 0), false);
            return set(n, T().arrayOf(e));
        }
        case NodeKind::UnionType: {
            TypeId inner = kNoExpect;
            bool hasNull = false;
            bool bad = false;
            for (const NodeId k : ast().kids(n)) {
                const TypeId t = resolveType(k, false);
                if (t == TypeTable::kNull) {
                    hasNull = true;
                } else if (T().kind(t) == TypeKind::Nullable) {
                    hasNull = true;
                    bad = bad || (inner != kNoExpect && inner != T().nonNull(t));
                    inner = T().nonNull(t);
                } else if (inner == kNoExpect || inner == t) {
                    inner = t;
                } else if (!T().isError(t) && !T().isError(inner)) {
                    bad = true;
                }
            }
            if (bad) {
                err("S417", n, "유니언은 T | null 만 — 서로 다른 타입의 유니언은 없다 (enum · 구조체로)");
                return set(n, TypeTable::kError);
            }
            if (inner == kNoExpect) {
                return set(n, TypeTable::kNull);
            }
            return set(n, hasNull ? T().nullableOf(inner) : inner);
        }
        case NodeKind::ObjectType: {
            const TypeId st = T().newStruct("");
            std::vector<StructField> fields;
            for (const NodeId prop : ast().kids(n)) {
                StructField f;
                f.name = std::string(ast().str(prop));
                bool dup = false;
                for (const StructField& g : fields) {
                    dup = dup || g.name == f.name;
                }
                if (dup) {
                    err("S402", prop, std::format("필드 '{}' 가 두 번 있다", f.name));
                    continue;
                }
                f.type = resolveType(ast().kid(prop, 0), false);
                f.readonly = (node(prop).flags & NodeFlag::Readonly) != 0;
                fields.push_back(std::move(f));
            }
            T().structOf(st).fields = std::move(fields);
            return set(n, st);
        }
        case NodeKind::TypeName:
            break;
        default:
            return set(n, TypeTable::kError); // 파서가 이미 보고했다 (오류 마디)
        }
        const std::string_view name = ast().str(n);
        const auto args = ast().kids(n);
        auto noArgs = [&](TypeId t) {
            if (!args.empty()) {
                err("S417", n, std::format("'{}' 는 타입 인자를 받지 않는다", name));
            }
            return set(n, t);
        };
        if (name == "number") {
            return noArgs(TypeTable::kNumber);
        }
        if (name == "boolean") {
            return noArgs(TypeTable::kBool);
        }
        if (name == "string") {
            return noArgs(TypeTable::kString);
        }
        if (name == "void") {
            if (!allowVoid) {
                err("S417", n, "void 는 반환 타입에만");
                return set(n, TypeTable::kError);
            }
            return set(n, TypeTable::kVoid);
        }
        if (name == "Array") {
            if (args.size() != 1) {
                err("S417", n, "Array 는 타입 인자 하나 (Array<T>)");
                return set(n, TypeTable::kError);
            }
            return set(n, T().arrayOf(resolveType(args[0], false)));
        }
        if (name == "bool") {
            err("S401", n, "타입 'bool' 은 없다 — TS 이름은 boolean");
            return set(n, TypeTable::kError);
        }
        if (const ScopeEntry* e = findEntry(m_mscopes[m_ctx.module].types, name)) {
            const Symbol& s = sym(e->sym);
            TypeId t = s.type;
            if (s.kind == SymbolKind::Type && s.decl != kNoNode && node0Kind(s) == NodeKind::TypeAlias) {
                t = resolveAlias(e->sym);
            }
            return noArgs(t);
        }
        for (const ForbiddenName& f : kForbiddenTypes) {
            if (f.name == name) {
                err("S413", n, std::string(f.message));
                return set(n, TypeTable::kError);
            }
        }
        if (findEntry(m_sdkTypes, name) != nullptr) {
            err("S401", n, std::format("없는 타입 '{}' — import {{ {} }} from \"sandbox\"", name, name));
        } else {
            err("S401", n, std::format("없는 타입 '{}'", name));
        }
        return set(n, TypeTable::kError);
    }

    // 심볼의 선언 마디 종류 (그 심볼의 모듈에서)
    NodeKind node0Kind(const Symbol& s) { return P.modules[s.module].ast.node(s.decl).kind; }

    // ---- 4 함수 몸 ----------------------------------------------------------------------------------------------
    void ensureFunction(u32 fi) {
        FunctionInfo& f = P.functions[fi];
        if (f.state != FunctionInfo::State::Pending) {
            return;
        }
        f.state = FunctionInfo::State::Checking;
        FnCtx saved = std::move(m_ctx);
        m_ctx = FnCtx{};
        m_ctx.module = f.module;
        m_ctx.function = static_cast<i32>(fi);
        m_ctx.inferring = !f.resultDeclared;
        m_ctx.result = f.resultDeclared ? f.result : TypeTable::kVoid;
        const NodeId decl = f.decl;
        const auto kids = ast().kids(decl);
        const NodeId body = kids.back();
        collectAssigned(body);
        Scope fnScope;
        for (const SymbolId p : f.paramSymbols) {
            Symbol& s = sym(p);
            s.maybeAssigned = isAssignedName(s.name);
            fnScope.names.push_back({s.name, p});
        }
        m_ctx.scopes.push_back(std::move(fnScope));
        // 몸 블록은 매개변수와 같은 범위 (function f(a) { let a } 는 JS 문법 오류)
        const Flow flow = blockStatements(body, /*newScope=*/false);
        FunctionInfo& g = P.functions[fi];
        if (m_ctx.inferring) {
            TypeId r = TypeTable::kVoid;
            bool bad = false;
            for (usize i = 0; i < m_ctx.returns.size(); ++i) {
                r = i == 0 ? m_ctx.returns[0] : unify(r, m_ctx.returns[i], bad);
            }
            if (bad) {
                err("S412", decl, std::format("함수 '{}' 의 return 타입이 서로 다르다 — 반환 타입을 적는다", g.name));
                r = TypeTable::kError;
            }
            g.result = r;
        }
        if (g.result != TypeTable::kVoid && !T().isError(g.result) && !flow.returns) {
            err("S412", decl,
                std::format("함수 '{}' 가 모든 길에서 값을 돌려주지 않는다 ({})", g.name, T().name(g.result)));
        }
        g.state = FunctionInfo::State::Done;
        m_ctx = std::move(saved);
    }

    void collectAssigned(NodeId n) {
        if (n == kNoNode) {
            return;
        }
        const Node& x = node(n);
        if ((x.kind == NodeKind::Assign || x.kind == NodeKind::Update)) {
            const NodeId target = ast().kid(n, 0);
            if (target != kNoNode && node(target).kind == NodeKind::Ident) {
                m_ctx.assignedNames.emplace_back(ast().str(target));
            }
        }
        for (const NodeId k : ast().kids(n)) {
            collectAssigned(k);
        }
    }
    bool isAssignedName(std::string_view name) const {
        return std::find(m_ctx.assignedNames.begin(), m_ctx.assignedNames.end(), name) != m_ctx.assignedNames.end();
    }

    // ---- 범위 · 이름 --------------------------------------------------------------------------------------------
    void pushScope(NodeId blockLike) {
        Scope s;
        if (blockLike != kNoNode) {
            prescan(blockLike, s);
        }
        m_ctx.scopes.push_back(std::move(s));
    }
    // 블록 바로 아래의 let · const 이름 (선언 전 사용을 알아보려고)
    void prescan(NodeId block, Scope& s) {
        for (const NodeId k : ast().kids(block)) {
            if (k == kNoNode) {
                continue;
            }
            if (node(k).kind == NodeKind::VarDecl) {
                s.pending.emplace_back(ast().str(k));
            } else if (node(k).kind == NodeKind::Case) {
                prescan(k, s);
            }
        }
    }
    void popScope() { m_ctx.scopes.pop_back(); }

    void declareLocal(NodeId at, const std::string& name, SymbolId id) {
        Scope& s = m_ctx.scopes.back();
        if (findEntry(s.names, name) != nullptr) {
            err("S402", at, std::format("'{}' 가 이 범위에 이미 있다", name));
        }
        s.names.push_back({name, id});
        const auto it = std::find(s.pending.begin(), s.pending.end(), name);
        if (it != s.pending.end()) {
            s.pending.erase(it);
        }
    }

    // 값 이름 찾기. 없으면 kNoSymbol (부른 쪽이 보고)
    SymbolId lookupValue(NodeId at, std::string_view name, bool& reported) {
        reported = false;
        for (auto it = m_ctx.scopes.rbegin(); it != m_ctx.scopes.rend(); ++it) {
            if (const ScopeEntry* e = findEntry(it->names, name)) {
                return e->sym;
            }
            if (std::find(it->pending.begin(), it->pending.end(), name) != it->pending.end()) {
                err("S403", at, std::format("'{}' 를 선언하기 전에 쓴다", name));
                reported = true;
                return kNoSymbol;
            }
        }
        if (const ScopeEntry* e = findEntry(m_mscopes[m_ctx.module].values, name)) {
            if (m_ctx.function < 0 && !m_ready[e->sym]) {
                err("S403", at, std::format("'{}' 를 선언하기 전에 쓴다 (최상위 const 는 위에서부터)", name));
                reported = true;
                return kNoSymbol;
            }
            return e->sym;
        }
        if (const ScopeEntry* e = findEntry(m_globals, name)) {
            return e->sym;
        }
        return kNoSymbol;
    }

    void unknownValue(NodeId at, std::string_view name) {
        for (const ForbiddenName& f : kForbiddenValues) {
            if (f.name == name) {
                err("S413", at, std::string(f.message));
                return;
            }
        }
        if (findEntry(m_sdkValues, name) != nullptr) {
            err("S401", at, std::format("없는 이름 '{}' — import {{ {} }} from \"sandbox\"", name, name));
        } else if (findEntry(m_mscopes[m_ctx.module].types, name) != nullptr ||
                   findEntry(m_sdkTypes, name) != nullptr) {
            err("S411", at, std::format("'{}' 는 타입이다 — 값으로 쓸 수 없다", name));
        } else {
            err("S401", at, std::format("없는 이름 '{}'", name));
        }
    }

    [[nodiscard]] bool isNarrowed(SymbolId s) const {
        return std::find(m_ctx.narrowed.begin(), m_ctx.narrowed.end(), s) != m_ctx.narrowed.end();
    }
    [[nodiscard]] bool narrowable(SymbolId s) {
        const Symbol& x = sym(s);
        return (x.kind == SymbolKind::ModuleConst || x.kind == SymbolKind::Local || x.kind == SymbolKind::Param) &&
               !x.maybeAssigned && T().kind(x.type) == TypeKind::Nullable;
    }

    // ---- 문장 ----------------------------------------------------------------------------------------------------
    Flow blockStatements(NodeId block, bool newScope) {
        const usize narrowMark = m_ctx.narrowed.size();
        if (newScope) {
            pushScope(block);
        } else {
            prescan(block, m_ctx.scopes.back());
        }
        Flow flow;
        for (const NodeId s : ast().kids(block)) {
            if (s == kNoNode) {
                continue;
            }
            const Flow f = statement(s);
            if (!flow.exits) {
                flow = f;
            }
        }
        if (newScope) {
            popScope();
        }
        m_ctx.narrowed.resize(narrowMark);
        return flow;
    }

    Flow statement(NodeId s) {
        const Node& n = node(s);
        switch (n.kind) {
        case NodeKind::Block:
            return blockStatements(s, true);
        case NodeKind::Empty:
            return {};
        case NodeKind::ExprStmt: {
            const NodeId e = ast().kid(s, 0);
            if (e != kNoNode) {
                expr(e, kNoExpect);
            }
            return {};
        }
        case NodeKind::VarDecl:
            localVar(s, /*forOfType=*/kNoExpect);
            return {};
        case NodeKind::If:
            return ifStatement(s);
        case NodeKind::While: {
            const usize mark = m_ctx.narrowed.size();
            const Facts f = condition(ast().kid(s, 0));
            applyFacts(f.whenTrue);
            loopBody(ast().kid(s, 1));
            m_ctx.narrowed.resize(mark);
            return {};
        }
        case NodeKind::DoWhile: {
            loopBody(ast().kid(s, 0));
            condition(ast().kid(s, 1));
            return {};
        }
        case NodeKind::For:
            return forStatement(s);
        case NodeKind::ForOf:
            return forOfStatement(s);
        case NodeKind::Break:
            if (m_ctx.breakable == 0) {
                err("S419", s, "break 는 반복문 · switch 안에서만");
            }
            return {true, false};
        case NodeKind::Continue:
            if (m_ctx.loops == 0) {
                err("S419", s, "continue 는 반복문 안에서만");
            }
            return {true, false};
        case NodeKind::Return:
            return returnStatement(s);
        case NodeKind::Switch:
            return switchStatement(s);
        default:
            return {};
        }
    }

    void loopBody(NodeId body) {
        ++m_ctx.loops;
        ++m_ctx.breakable;
        if (body != kNoNode) {
            statement(body);
        }
        --m_ctx.loops;
        --m_ctx.breakable;
    }

    void applyFacts(const std::vector<SymbolId>& syms) {
        for (const SymbolId s : syms) {
            m_ctx.narrowed.push_back(s);
        }
    }

    Flow ifStatement(NodeId s) {
        const usize mark = m_ctx.narrowed.size();
        const Facts f = condition(ast().kid(s, 0));
        applyFacts(f.whenTrue);
        const Flow thenFlow = ast().kid(s, 1) == kNoNode ? Flow{} : statement(ast().kid(s, 1));
        m_ctx.narrowed.resize(mark);
        Flow elseFlow;
        const NodeId elseNode = ast().kid(s, 2);
        if (elseNode != kNoNode) {
            applyFacts(f.whenFalse);
            elseFlow = statement(elseNode);
            m_ctx.narrowed.resize(mark);
        }
        // 한쪽이 빠져나가면 뒤의 문장은 다른 쪽의 사실을 안다 (if (x === null) return)
        if (thenFlow.exits && !elseFlow.exits) {
            applyFacts(f.whenFalse);
        } else if (elseFlow.exits && !thenFlow.exits && elseNode != kNoNode) {
            applyFacts(f.whenTrue);
        }
        if (elseNode == kNoNode) {
            return {};
        }
        return {thenFlow.exits && elseFlow.exits, thenFlow.returns && elseFlow.returns};
    }

    Flow forStatement(NodeId s) {
        pushScope(kNoNode);
        const usize mark = m_ctx.narrowed.size();
        const NodeId init = ast().kid(s, 0);
        if (init != kNoNode) {
            statement(init);
        }
        const NodeId cond = ast().kid(s, 1);
        Facts f;
        if (cond != kNoNode) {
            f = condition(cond);
        }
        applyFacts(f.whenTrue);
        ++m_ctx.loops;
        ++m_ctx.breakable;
        if (const NodeId update = ast().kid(s, 2); update != kNoNode) {
            statement(update);
        }
        if (const NodeId body = ast().kid(s, 3); body != kNoNode) {
            statement(body);
        }
        --m_ctx.loops;
        --m_ctx.breakable;
        m_ctx.narrowed.resize(mark);
        popScope();
        return {};
    }

    Flow forOfStatement(NodeId s) {
        const NodeId decl = ast().kid(s, 0);
        const NodeId iter = ast().kid(s, 1);
        const TypeId it = expr(iter, kNoExpect);
        TypeId elem = TypeTable::kError;
        if (T().kind(it) == TypeKind::Array) {
            elem = T().info(it).elem;
        } else if (T().kind(it) == TypeKind::Nullable) {
            err("S407", iter, std::format("null 일 수 있다 ({}) — if (x !== null) 로 좁힌다", T().name(it)));
        } else if (!T().isError(it)) {
            err("S422", iter,
                std::format("for-of 는 배열만 돈다 — {} 는 아니다 (질의는 [계획 v0.3 M3])", T().name(it)));
        }
        pushScope(kNoNode);
        if (decl != kNoNode) {
            localVar(decl, elem);
        }
        loopBody(ast().kid(s, 2));
        popScope();
        return {};
    }

    Flow returnStatement(NodeId s) {
        const NodeId value = ast().kid(s, 0);
        if (m_ctx.function < 0) {
            return {true, true};
        }
        if (m_ctx.inferring) {
            if (value == kNoNode) {
                m_ctx.returns.push_back(TypeTable::kVoid);
            } else {
                const TypeId t = expr(value, kNoExpect);
                if (t == TypeTable::kVoid) {
                    err("S412", value, "값이 없는 식을 돌려준다");
                }
                m_ctx.returns.push_back(t == TypeTable::kNull ? TypeTable::kError : t);
                if (t == TypeTable::kNull) {
                    err("S414", value, "null 을 돌려주는 함수는 반환 타입을 적는다 (T | null)");
                }
            }
            return {true, true};
        }
        if (m_ctx.result == TypeTable::kVoid) {
            if (value != kNoNode) {
                err("S412", value, "void 함수는 값을 돌려주지 않는다");
                expr(value, kNoExpect);
            }
            return {true, true};
        }
        if (value == kNoNode) {
            if (!T().isError(m_ctx.result)) {
                err("S412", s, std::format("값을 돌려줘야 한다 ({})", T().name(m_ctx.result)));
            }
            return {true, true};
        }
        const TypeId t = expr(value, m_ctx.result);
        assignTo(value, t, m_ctx.result);
        return {true, true};
    }

    Flow switchStatement(NodeId s) {
        const auto kids = ast().kids(s);
        const NodeId disc = kids[0];
        const TypeId dt = expr(disc, kNoExpect);
        const TypeKind dk = T().kind(dt);
        if (!T().isError(dt) && !T().isNumeric(dt) && dk != TypeKind::String) {
            err("S422", disc, std::format("switch 는 정수 · 문자열 · enum 으로 — {} 는 아니다", T().name(dt)));
        }
        pushScope(s);
        ++m_ctx.breakable;
        bool hasDefault = false;
        bool allReturn = true;
        for (usize i = 1; i < kids.size(); ++i) {
            const NodeId c = kids[i];
            const auto ck = ast().kids(c);
            const NodeId test = ck[0];
            if (test == kNoNode) {
                hasDefault = true;
            } else {
                const TypeId tt = expr(test, dt);
                if (!comparable(tt, dt)) {
                    err("S404", test,
                        std::format("case 의 타입 {} 이 switch 의 {} 와 맞지 않다", T().name(tt), T().name(dt)));
                }
            }
            Flow flow;
            for (usize k = 1; k < ck.size(); ++k) {
                if (ck[k] == kNoNode) {
                    continue;
                }
                const Flow f = statement(ck[k]);
                if (!flow.exits) {
                    flow = f;
                }
            }
            const bool empty = ck.size() == 1;
            const bool last = i + 1 == kids.size();
            if (!empty && !flow.exits && !last) {
                err("S420", c, "case 가 다음 case 로 떨어진다 — break · return 을 적는다 (빈 case 만 묶을 수 있다)");
            }
            if (!empty || last) {
                allReturn = allReturn && flow.returns;
            }
        }
        --m_ctx.breakable;
        popScope();
        return {false, hasDefault && allReturn};
    }

    void localVar(NodeId s, TypeId forOfType) {
        const Node& n = node(s);
        const std::string name(ast().str(s));
        const bool isConst = (n.flags & NodeFlag::Const) != 0;
        const NodeId typeNode = ast().kid(s, 0);
        const NodeId init = ast().kid(s, 1);
        TypeId t = TypeTable::kError;
        ConstValue value;
        if (forOfType != kNoExpect) {
            t = forOfType;
        } else {
            TypeId declared = kNoExpect;
            if (typeNode != kNoNode) {
                declared = resolveType(typeNode, false);
            }
            if (init == kNoNode) {
                if (!isConst) {
                    err("S414", s, std::format("'{}' 에 초기값을 준다 (let x: T = …)", name));
                }
                t = declared == kNoExpect ? TypeTable::kError : declared;
            } else {
                const TypeId it = expr(init, declared);
                if (declared != kNoExpect) {
                    assignTo(init, it, declared);
                    t = declared;
                } else if (it == TypeTable::kNull) {
                    err("S414", init,
                        std::format("null 로 시작하는 '{}' 는 타입을 적는다 (let x: T | null = null)", name));
                } else if (it == TypeTable::kVoid) {
                    err("S422", init, "값이 없는 식 (void) 으로 변수를 만들 수 없다");
                } else {
                    t = it;
                }
                if (isConst) {
                    value = cval(init);
                    // i32 로 적은 상수는 i32 값으로 (coerce 가 바꿨다)
                }
            }
        }
        Symbol sy;
        sy.kind = SymbolKind::Local;
        sy.name = name;
        sy.type = t;
        sy.module = m_ctx.module;
        sy.decl = s;
        sy.mutableVar = !isConst && forOfType == kNoExpect;
        sy.maybeAssigned = sy.mutableVar && isAssignedName(name);
        if (forOfType != kNoExpect && !isConst) {
            sy.mutableVar = true;
            sy.maybeAssigned = isAssignedName(name);
        }
        sy.value = value;
        const SymbolId id = newSymbol(std::move(sy));
        m_ready[id] = true;
        declareLocal(s, name, id);
        sema(s) = {t, RefKind::Symbol, id};
    }

    // ---- 조건 · 좁히기 ---------------------------------------------------------------------------------------------
    Facts condition(NodeId n) {
        Facts f = logical(n);
        const TypeId t = sema(n).type;
        if (t != TypeTable::kBool && !T().isError(t)) {
            err("S408", n,
                std::format("조건은 boolean 이다 — {} 는 아니다 (x !== null · x !== 0 처럼 쓴다)", T().name(t)));
        }
        return f;
    }

    // 논리식을 검사하며 좁히기 사실을 낸다 (&& · || · ! · === null · !== null · 괄호). 그 밖은 expr
    Facts logical(NodeId n) {
        const Node& x = node(n);
        Facts f;
        if (x.kind == NodeKind::Paren) {
            f = logical(ast().kid(n, 0));
            set(n, sema(ast().kid(n, 0)).type);
            cval(n) = cval(ast().kid(n, 0));
            return f;
        }
        if (x.kind == NodeKind::Unary && x.op == Tok::Bang) {
            const NodeId a = ast().kid(n, 0);
            Facts inner = logical(a);
            requireBool(a);
            set(n, TypeTable::kBool);
            if (cval(a).kind == ConstValue::Kind::Bool) {
                ConstValue c;
                c.kind = ConstValue::Kind::Bool;
                c.boolean = !cval(a).boolean;
                cval(n) = c;
            }
            f.whenTrue = std::move(inner.whenFalse);
            f.whenFalse = std::move(inner.whenTrue);
            return f;
        }
        if (x.kind == NodeKind::Binary && (x.op == Tok::AmpAmp || x.op == Tok::PipePipe)) {
            const NodeId a = ast().kid(n, 0);
            const NodeId b = ast().kid(n, 1);
            const Facts fa = logical(a);
            requireBool(a);
            const usize mark = m_ctx.narrowed.size();
            applyFacts(x.op == Tok::AmpAmp ? fa.whenTrue : fa.whenFalse);
            const Facts fb = logical(b);
            requireBool(b);
            m_ctx.narrowed.resize(mark);
            set(n, TypeTable::kBool);
            if (cval(a).kind == ConstValue::Kind::Bool && cval(b).kind == ConstValue::Kind::Bool) {
                ConstValue c;
                c.kind = ConstValue::Kind::Bool;
                c.boolean =
                    x.op == Tok::AmpAmp ? (cval(a).boolean && cval(b).boolean) : (cval(a).boolean || cval(b).boolean);
                cval(n) = c;
            }
            if (x.op == Tok::AmpAmp) {
                f.whenTrue = fa.whenTrue;
                f.whenTrue.insert(f.whenTrue.end(), fb.whenTrue.begin(), fb.whenTrue.end());
            } else {
                f.whenFalse = fa.whenFalse;
                f.whenFalse.insert(f.whenFalse.end(), fb.whenFalse.begin(), fb.whenFalse.end());
            }
            return f;
        }
        expr(n, kNoExpect);
        // x !== null · null !== x · x === null
        if (x.kind == NodeKind::Binary && (x.op == Tok::NotEqEq || x.op == Tok::EqEqEq)) {
            NodeId a = ast().kid(n, 0);
            NodeId b = ast().kid(n, 1);
            if (node(a).kind == NodeKind::Null) {
                std::swap(a, b);
            }
            if (node(b).kind == NodeKind::Null && node(a).kind == NodeKind::Ident && sema(a).ref == RefKind::Symbol &&
                narrowable(sema(a).refIndex)) {
                (x.op == Tok::NotEqEq ? f.whenTrue : f.whenFalse).push_back(sema(a).refIndex);
            }
        }
        return f;
    }

    void requireBool(NodeId n) {
        const TypeId t = sema(n).type;
        if (t != TypeTable::kBool && !T().isError(t)) {
            err("S422", n,
                std::format("&& · || · ! 는 boolean 에만 — {} 는 아니다 (x !== null · x !== 0 처럼 쓴다)",
                            T().name(t)));
        }
    }

    // ---- 넣기 · 맞추기 --------------------------------------------------------------------------------------------
    // number 상수 (정수 · i32 범위) 를 i32 로 — 리터럴 · 상수식이 i32 자리에 들어갈 때
    bool coerceToI32(NodeId n) {
        ConstValue& c = cval(n);
        if (c.kind == ConstValue::Kind::Number && fitsI32(c.number) && sema(n).type == TypeTable::kNumber) {
            c = i32Const(static_cast<i32>(c.number));
            set(n, TypeTable::kI32);
            return true;
        }
        return false;
    }

    bool assignTo(NodeId at, TypeId src, TypeId dst) {
        if (T().assignable(src, dst)) {
            return true;
        }
        const TypeId dstInner = T().nonNull(dst);
        if (dstInner == TypeTable::kI32 && src == TypeTable::kNumber) {
            if (coerceToI32(at)) {
                return true;
            }
            const ConstValue& c = cval(at);
            if (c.kind == ConstValue::Kind::Number) {
                err("S409", at, std::format("{} 는 i32 가 아니다 (정수 · -2^31 ~ 2^31-1 만)", c.number));
            } else {
                err("S409", at, "number 를 i32 자리에 넣을 수 없다 — toI32(x) 로 바꾼다 (JS ToInt32)");
            }
            return false;
        }
        err("S404", at, std::format("타입이 맞지 않다 — {} 를 {} 자리에 넣을 수 없다", T().name(src), T().name(dst)));
        return false;
    }

    TypeId unify(TypeId a, TypeId b, bool& bad) {
        if (a == b) {
            return a;
        }
        if (T().isError(a) || T().isError(b)) {
            return TypeTable::kError;
        }
        if (a == TypeTable::kNull) {
            return T().nullableOf(b);
        }
        if (b == TypeTable::kNull) {
            return T().nullableOf(a);
        }
        if (T().isNumeric(a) && T().isNumeric(b)) {
            const bool ai = T().kind(a) != TypeKind::Number;
            const bool bi = T().kind(b) != TypeKind::Number;
            return ai && bi ? TypeTable::kI32 : TypeTable::kNumber;
        }
        if (T().kind(a) == TypeKind::Nullable || T().kind(b) == TypeKind::Nullable) {
            const TypeId inner = unify(T().nonNull(a), T().nonNull(b), bad);
            return T().nullableOf(inner);
        }
        if (T().assignable(a, b)) {
            return b;
        }
        if (T().assignable(b, a)) {
            return a;
        }
        bad = true;
        return TypeTable::kError;
    }

    bool comparable(TypeId a, TypeId b) {
        if (T().isError(a) || T().isError(b)) {
            return true;
        }
        if (T().isNumeric(a) && T().isNumeric(b)) {
            // 서로 다른 enum 은 비교하지 않는다 (TS 오류)
            return !(T().kind(a) == TypeKind::Enum && T().kind(b) == TypeKind::Enum && a != b);
        }
        return T().assignable(a, b) || T().assignable(b, a) ||
               (T().kind(a) == TypeKind::Nullable && T().assignable(b, T().nonNull(a))) ||
               (T().kind(b) == TypeKind::Nullable && T().assignable(a, T().nonNull(b)));
    }

    // ---- 식 --------------------------------------------------------------------------------------------------------
    TypeId expr(NodeId n, TypeId expected) {
        if (n == kNoNode) {
            return TypeTable::kError;
        }
        const Node& x = node(n);
        switch (x.kind) {
        case NodeKind::Number: {
            if (expected != kNoExpect && T().nonNull(expected) == TypeTable::kI32 && fitsI32(x.number)) {
                cval(n) = i32Const(static_cast<i32>(x.number));
                return set(n, TypeTable::kI32);
            }
            cval(n) = numberConst(x.number);
            return set(n, TypeTable::kNumber);
        }
        case NodeKind::String: {
            ConstValue c;
            c.kind = ConstValue::Kind::String;
            c.string = std::string(ast().str(n));
            cval(n) = std::move(c);
            return set(n, TypeTable::kString);
        }
        case NodeKind::Bool: {
            ConstValue c;
            c.kind = ConstValue::Kind::Bool;
            c.boolean = (x.flags & NodeFlag::True) != 0;
            cval(n) = c;
            return set(n, TypeTable::kBool);
        }
        case NodeKind::Null:
            return set(n, TypeTable::kNull);
        case NodeKind::Template: {
            for (const NodeId k : ast().kids(n)) {
                if (node(k).kind == NodeKind::String) {
                    continue;
                }
                const TypeId t = expr(k, kNoExpect);
                if (!isPrimitive(t)) {
                    err("S422", k,
                        std::format("템플릿에는 number · i32 · boolean · string · enum 만 — {} 는 아니다",
                                    T().name(t)));
                }
            }
            return set(n, TypeTable::kString);
        }
        case NodeKind::Ident:
            return identValue(n);
        case NodeKind::Paren: {
            const NodeId inner = ast().kid(n, 0);
            const NodeKind ik = inner == kNoNode ? NodeKind::Null : node(inner).kind;
            if (ik == NodeKind::Binary || ik == NodeKind::Unary || ik == NodeKind::Paren) {
                const Node& in = node(inner);
                if (in.op == Tok::AmpAmp || in.op == Tok::PipePipe || in.op == Tok::Bang) {
                    logical(n);
                    return sema(n).type;
                }
            }
            const TypeId t = expr(inner, expected);
            cval(n) = cval(inner);
            return set(n, t);
        }
        case NodeKind::AsConst: {
            const TypeId t = expr(ast().kid(n, 0), expected);
            cval(n) = cval(ast().kid(n, 0));
            return set(n, t);
        }
        case NodeKind::Unary:
            return unary(n, expected);
        case NodeKind::Binary:
            return binary(n, expected);
        case NodeKind::Assign:
            return assign(n);
        case NodeKind::Update: {
            const NodeId target = ast().kid(n, 0);
            const TypeId t = place(target);
            if (!T().isError(t) && T().kind(t) != TypeKind::I32 && T().kind(t) != TypeKind::Number) {
                err("S422", n, std::format("++ · -- 는 i32 · number 에만 — {} 는 아니다", T().name(t)));
            }
            return set(n, t);
        }
        case NodeKind::Conditional: {
            const usize mark = m_ctx.narrowed.size();
            const Facts f = condition(ast().kid(n, 0));
            applyFacts(f.whenTrue);
            const TypeId a = expr(ast().kid(n, 1), expected);
            m_ctx.narrowed.resize(mark);
            applyFacts(f.whenFalse);
            const TypeId b = expr(ast().kid(n, 2), expected);
            m_ctx.narrowed.resize(mark);
            bool bad = false;
            TypeId t = unify(a, b, bad);
            if (bad) {
                err("S404", n, std::format("? : 의 두 쪽 타입이 다르다 ({} · {})", T().name(a), T().name(b)));
                t = TypeTable::kError;
            }
            return set(n, t);
        }
        case NodeKind::Call:
            return call(n);
        case NodeKind::Member:
            return member(n);
        case NodeKind::Index: {
            const TypeId ot = expr(ast().kid(n, 0), kNoExpect);
            const NodeId idx = ast().kid(n, 1);
            const TypeId it = expr(idx, kNoExpect);
            if (!T().isError(it) && !T().isNumeric(it)) {
                err("S422", idx, std::format("첨자는 i32 · number — {} 는 아니다", T().name(it)));
            }
            if (T().kind(ot) == TypeKind::Array) {
                return set(n, T().info(ot).elem);
            }
            if (T().kind(ot) == TypeKind::Nullable) {
                err("S407", n, std::format("null 일 수 있다 ({}) — if (x !== null) 로 좁힌다", T().name(ot)));
            } else if (!T().isError(ot)) {
                err("S406", n,
                    std::format("{} 에는 [ ] 가 없다 (배열만 — 문자열 첨자는 [v0.3 뒤 검토])", T().name(ot)));
            }
            return set(n, TypeTable::kError);
        }
        case NodeKind::ArrayLit:
            return arrayLiteral(n, expected);
        case NodeKind::ObjectLit:
            return objectLiteral(n, expected);
        default:
            return set(n, TypeTable::kError); // 파서가 보고한 오류 마디 등
        }
    }

    bool isPrimitive(TypeId t) {
        const TypeKind k = T().kind(t);
        return k == TypeKind::Error || k == TypeKind::I32 || k == TypeKind::Number || k == TypeKind::Bool ||
               k == TypeKind::String || k == TypeKind::Enum;
    }

    TypeId identValue(NodeId n) {
        const std::string_view name = ast().str(n);
        bool reported = false;
        const SymbolId id = lookupValue(n, name, reported);
        if (id == kNoSymbol) {
            if (!reported) {
                unknownValue(n, name);
            }
            return set(n, TypeTable::kError);
        }
        sema(n).ref = RefKind::Symbol;
        sema(n).refIndex = id;
        const Symbol& s = sym(id);
        switch (s.kind) {
        case SymbolKind::Local:
        case SymbolKind::Param:
        case SymbolKind::ModuleConst: {
            cval(n) = s.value;
            if (isNarrowed(id)) {
                return set(n, T().nonNull(s.type));
            }
            return set(n, s.type);
        }
        case SymbolKind::Function:
            err("S411", n, std::format("함수 '{}' 는 값이 아니다 — 부르기만 한다 (함수 값 · 클로저 없음)", name));
            return set(n, TypeTable::kError);
        case SymbolKind::Enum:
            err("S411", n, std::format("enum '{}' 는 값이 아니다 — {}.멤버 로 쓴다", name, name));
            return set(n, TypeTable::kError);
        case SymbolKind::Type:
            err("S411", n, std::format("'{}' 는 타입이다 — 값으로 쓸 수 없다", name));
            return set(n, TypeTable::kError);
        case SymbolKind::Component:
            return set(n, s.type); // ComponentRef<T> (코드 생성 = LOADK)
        case SymbolKind::Builtin:
            if (s.builtin == Builtin::NaN || s.builtin == Builtin::Infinity) {
                cval(n) = s.value;
                return set(n, TypeTable::kNumber);
            }
            if (s.builtin == Builtin::None) {
                return set(n, TypeTable::kError); // 가져오지 못한 이름 — 이미 보고했다
            }
            if (s.builtin == Builtin::DefineComponent || s.builtin == Builtin::FieldNamespace) {
                err("S425", n, std::format("'{}' 는 최상위 const 의 defineComponent({{ … }}) 안에서만 쓴다", name));
                return set(n, TypeTable::kError);
            }
            if (s.builtin == Builtin::DefineSystem) {
                err("S427", n, "defineSystem 은 export default defineSystem({ … }) 으로만 쓴다");
                return set(n, TypeTable::kError);
            }
            err("S411", n, std::format("'{}' 는 값이 아니다 — 부르거나 멤버를 쓴다", name));
            return set(n, TypeTable::kError);
        }
        return set(n, TypeTable::kError);
    }

    TypeId unary(NodeId n, TypeId expected) {
        const Node& x = node(n);
        const NodeId a = ast().kid(n, 0);
        if (x.op == Tok::Bang) {
            logical(n);
            return sema(n).type;
        }
        const TypeId t = expr(a, x.op == Tok::Tilde ? kNoExpect : expected);
        if (T().isError(t)) {
            return set(n, TypeTable::kError);
        }
        if (!T().isNumeric(t)) {
            err("S422", n, std::format("단항 {} 는 i32 · number 에만 — {} 는 아니다", tokName(x.op), T().name(t)));
            return set(n, TypeTable::kError);
        }
        const bool isInt = T().kind(t) != TypeKind::Number;
        const ConstValue& c = cval(a);
        if (x.op == Tok::Tilde) {
            if (c.has()) {
                cval(n) = i32Const(~(c.kind == ConstValue::Kind::I32 ? c.int32 : toInt32(c.number)));
            }
            return set(n, TypeTable::kI32);
        }
        // - · + : i32 는 감싼다 (-(-2^31) = -2^31 — ADR-0056 3)
        if (c.has()) {
            if (isInt) {
                cval(n) = i32Const(x.op == Tok::Minus ? wrapI32(-static_cast<i64>(c.int32)) : c.int32);
            } else {
                cval(n) = numberConst(x.op == Tok::Minus ? -c.number : c.number);
            }
        }
        return set(n, isInt ? TypeTable::kI32 : TypeTable::kNumber);
    }

    static i32 toInt32(f64 v) { return vm::toInt32(v); } // 실행과 같은 함수 (script/vm/Numbers)

    TypeId binary(NodeId n, TypeId expected) {
        const Node& x = node(n);
        const Tok op = x.op;
        if (op == Tok::AmpAmp || op == Tok::PipePipe) {
            logical(n);
            return sema(n).type;
        }
        const NodeId a = ast().kid(n, 0);
        const NodeId b = ast().kid(n, 1);
        if (op == Tok::QuestionQuestion) {
            const TypeId lt = expr(a, expected);
            if (T().isError(lt)) {
                expr(b, kNoExpect);
                return set(n, TypeTable::kError);
            }
            if (T().kind(lt) != TypeKind::Nullable) {
                err("S422", n, std::format("?? 의 왼쪽이 null 일 수 없다 ({})", T().name(lt)));
            }
            const TypeId inner = T().nonNull(lt);
            const TypeId rt = expr(b, inner);
            if (T().assignable(rt, inner) || (inner == TypeTable::kI32 && coerceToI32(b))) {
                return set(n, inner);
            }
            if (T().assignable(rt, lt)) {
                return set(n, lt);
            }
            err("S404", b, std::format("?? 의 오른쪽 {} 이 왼쪽 {} 와 맞지 않다", T().name(rt), T().name(lt)));
            return set(n, TypeTable::kError);
        }
        const bool arithmetic = op == Tok::Plus || op == Tok::Minus || op == Tok::Star || op == Tok::Percent;
        const TypeId pass = arithmetic && expected != kNoExpect && T().nonNull(expected) == TypeTable::kI32
                                ? TypeTable::kI32
                                : kNoExpect;
        TypeId lt = expr(a, pass);
        TypeId rt = expr(b, pass);
        if (T().isError(lt) || T().isError(rt)) {
            return set(n, op == Tok::EqEqEq || op == Tok::NotEqEq || op == Tok::Less || op == Tok::Greater ||
                                  op == Tok::LessEq || op == Tok::GreaterEq
                              ? TypeTable::kBool
                              : TypeTable::kError);
        }
        // i32 와 정수 상수를 섞으면 상수를 i32 로 (a + 1 은 i32)
        auto isIntLike = [&](TypeId t) { return T().kind(t) == TypeKind::I32 || T().kind(t) == TypeKind::Enum; };
        if (isIntLike(lt) && rt == TypeTable::kNumber && coerceToI32(b)) {
            rt = TypeTable::kI32;
        } else if (isIntLike(rt) && lt == TypeTable::kNumber && coerceToI32(a)) {
            lt = TypeTable::kI32;
        }
        const ConstValue& ca = cval(a);
        const ConstValue& cb = cval(b);
        const bool constant = ca.has() && cb.has();

        if (op == Tok::EqEqEq || op == Tok::NotEqEq || op == Tok::EqEq || op == Tok::NotEq) {
            if (!comparable(lt, rt)) {
                err("S422", n, std::format("{} 와 {} 는 비교할 수 없다", T().name(lt), T().name(rt)));
            }
            if (constant) {
                ConstValue c;
                c.kind = ConstValue::Kind::Bool;
                bool eq = false;
                if (ca.kind == ConstValue::Kind::String || cb.kind == ConstValue::Kind::String) {
                    eq = ca.kind == cb.kind && ca.string == cb.string;
                } else if (ca.kind == ConstValue::Kind::Bool || cb.kind == ConstValue::Kind::Bool) {
                    eq = ca.kind == cb.kind && ca.boolean == cb.boolean;
                } else {
                    eq = ca.asNumber() == cb.asNumber();
                }
                c.boolean = (op == Tok::EqEqEq || op == Tok::EqEq) ? eq : !eq;
                cval(n) = c;
            }
            return set(n, TypeTable::kBool);
        }
        if (op == Tok::Less || op == Tok::Greater || op == Tok::LessEq || op == Tok::GreaterEq) {
            const bool ok =
                (T().isNumeric(lt) && T().isNumeric(rt)) || (lt == TypeTable::kString && rt == TypeTable::kString);
            if (!ok) {
                err("S422", n,
                    std::format("{} 는 두 수 · 두 문자열에만 — {} · {}", tokName(op), T().name(lt), T().name(rt)));
            } else if (constant && ca.kind != ConstValue::Kind::String) {
                const f64 l = ca.asNumber();
                const f64 r = cb.asNumber();
                ConstValue c;
                c.kind = ConstValue::Kind::Bool;
                c.boolean = op == Tok::Less ? l < r : op == Tok::Greater ? l > r : op == Tok::LessEq ? l <= r : l >= r;
                cval(n) = c;
            }
            return set(n, TypeTable::kBool);
        }
        if (op == Tok::Plus && (lt == TypeTable::kString || rt == TypeTable::kString)) {
            if (!isPrimitive(lt) || !isPrimitive(rt)) {
                err("S422", n, std::format("문자열 + 는 기본 타입과만 — {} · {}", T().name(lt), T().name(rt)));
            } else if (constant && ca.kind == ConstValue::Kind::String && cb.kind == ConstValue::Kind::String) {
                ConstValue c;
                c.kind = ConstValue::Kind::String;
                c.string = ca.string + cb.string;
                cval(n) = std::move(c);
            }
            // 숫자 → 문자열은 상수로 접지 않는다 (JS 의 Number#toString 은 VM 이 — [계획 M2])
            return set(n, TypeTable::kString);
        }
        if (!T().isNumeric(lt) || !T().isNumeric(rt)) {
            err("S422", n, std::format("{} 는 i32 · number 에만 — {} · {}", tokName(op), T().name(lt), T().name(rt)));
            return set(n, TypeTable::kError);
        }
        const bool bothInt = isIntLike(lt) && isIntLike(rt);
        const bool bitwise = op == Tok::Amp || op == Tok::Pipe || op == Tok::Caret || op == Tok::Shl || op == Tok::Shr;
        TypeId result = TypeTable::kNumber;
        if (bitwise || (arithmetic && bothInt)) {
            result = TypeTable::kI32;
        }
        if (constant && !foldArithmetic(n, op, ca, cb, result == TypeTable::kI32)) {
            return set(n, TypeTable::kError); // 보고했다 (i32 % 0)
        }
        return set(n, result);
    }

    // 상수 접기. 상수로 오류면 보고하고 false
    bool foldArithmetic(NodeId n, Tok op, const ConstValue& ca, const ConstValue& cb, bool intResult) {
        if (intResult) {
            const i32 l = ca.kind == ConstValue::Kind::I32 ? ca.int32 : toInt32(ca.number);
            const i32 r = cb.kind == ConstValue::Kind::I32 ? cb.int32 : toInt32(cb.number);
            i32 v = 0;
            switch (op) {
            case Tok::Plus:
                v = wrapI32(static_cast<i64>(l) + r);
                break;
            case Tok::Minus:
                v = wrapI32(static_cast<i64>(l) - r);
                break;
            case Tok::Star:
                v = wrapI32(static_cast<i64>(l) * r);
                break;
            case Tok::Percent:
                if (r == 0) {
                    err("S409", n, "i32 를 0 으로 나눈 나머지 (실행하면 트랩)");
                    return false;
                }
                v = r == -1 ? 0 : l % r;
                break;
            case Tok::Amp:
                v = l & r;
                break;
            case Tok::Pipe:
                v = l | r;
                break;
            case Tok::Caret:
                v = l ^ r;
                break;
            case Tok::Shl:
                v = static_cast<i32>(static_cast<u32>(l) << (static_cast<u32>(r) & 31u));
                break;
            case Tok::Shr:
                v = l >> (static_cast<u32>(r) & 31u);
                break;
            default:
                return true;
            }
            cval(n) = i32Const(v);
            return true;
        }
        const f64 l = ca.asNumber();
        const f64 r = cb.asNumber();
        f64 v = 0;
        switch (op) {
        case Tok::Plus:
            v = l + r;
            break;
        case Tok::Minus:
            v = l - r;
            break;
        case Tok::Star:
            v = l * r;
            break;
        case Tok::Slash:
            v = l / r;
            break;
        case Tok::Percent:
            v = std::fmod(l, r);
            break;
        case Tok::StarStar:
            v = vm::jsPow(l, r); // 실행과 같은 규칙 (pow(1, NaN) = NaN — C 의 pow 와 다르다)
            break;
        default:
            return true;
        }
        cval(n) = numberConst(v);
        return true;
    }

    // 대입할 자리의 타입 (이름 · 필드 · 원소). 대입할 수 없으면 보고하고 Error
    TypeId place(NodeId target) {
        const Node& t = node(target);
        if (t.kind == NodeKind::Ident) {
            const TypeId ty = expr(target, kNoExpect);
            if (sema(target).ref != RefKind::Symbol) {
                return ty;
            }
            const Symbol& s = sym(sema(target).refIndex);
            if (s.kind == SymbolKind::ModuleConst) {
                err("S410", target, std::format("최상위 const '{}' 에 대입할 수 없다", s.name));
                return TypeTable::kError;
            }
            if (!s.mutableVar) {
                err("S410", target, std::format("const '{}' 에 대입할 수 없다 — let 으로 선언한다", s.name));
                return TypeTable::kError;
            }
            return s.type; // 좁힌 타입이 아니라 선언 타입에 넣는다
        }
        if (t.kind == NodeKind::Member) {
            const TypeId ty = expr(target, kNoExpect);
            const NodeSema& ms = sema(target);
            if (ms.ref == RefKind::Field) {
                const TypeId ot = sema(ast().kid(target, 0)).type;
                if (T().kind(ot) == TypeKind::Struct && T().structOf(ot).fields[ms.refIndex].readonly) {
                    err("S410", target, std::format("readonly 필드 '{}' 에 대입할 수 없다", ast().str(target)));
                    return TypeTable::kError;
                }
                return ty;
            }
            if (!T().isError(ty)) {
                err("S410", target, std::format("'{}' 에는 대입할 수 없다 (읽기 전용)", ast().str(target)));
            }
            return TypeTable::kError;
        }
        if (t.kind == NodeKind::Index) {
            return expr(target, kNoExpect);
        }
        return TypeTable::kError;
    }

    TypeId assign(NodeId n) {
        const Node& x = node(n);
        const NodeId target = ast().kid(n, 0);
        const NodeId value = ast().kid(n, 1);
        const TypeId tt = place(target);
        if (x.op == Tok::Assign) {
            const TypeId vt = expr(value, T().isError(tt) ? kNoExpect : tt);
            if (!T().isError(tt)) {
                assignTo(value, vt, tt);
            }
            return set(n, TypeTable::kVoid);
        }
        // 복합 대입: a op= b 는 a = a op b 와 같은 타입 규칙
        const TypeId vt =
            expr(value, T().isError(tt) || T().nonNull(tt) != TypeTable::kI32 ? kNoExpect : TypeTable::kI32);
        if (T().isError(tt) || T().isError(vt)) {
            return set(n, TypeTable::kVoid);
        }
        const Tok op = x.op;
        if (op == Tok::AmpAmpAssign || op == Tok::PipePipeAssign) {
            if (tt != TypeTable::kBool || vt != TypeTable::kBool) {
                err("S422", n, "&&= · ||= 는 boolean 에만");
            }
            return set(n, TypeTable::kVoid);
        }
        if (op == Tok::QuestionQuestionAssign) {
            if (T().kind(tt) != TypeKind::Nullable) {
                err("S422", n, std::format("??= 의 왼쪽이 null 일 수 없다 ({})", T().name(tt)));
            } else if (!T().assignable(vt, tt) && !(T().nonNull(tt) == TypeTable::kI32 && coerceToI32(value))) {
                assignTo(value, vt, tt);
            }
            return set(n, TypeTable::kVoid);
        }
        if (op == Tok::PlusAssign && tt == TypeTable::kString) {
            if (!isPrimitive(vt)) {
                err("S422", n, std::format("문자열 += 는 기본 타입과만 — {}", T().name(vt)));
            }
            return set(n, TypeTable::kVoid);
        }
        if (!T().isNumeric(tt) || !T().isNumeric(vt) || T().kind(tt) == TypeKind::Enum) {
            err("S422", n, std::format("{} 는 i32 · number 에만 — {} · {}", tokName(op), T().name(tt), T().name(vt)));
            return set(n, TypeTable::kVoid);
        }
        const bool targetInt = tt == TypeTable::kI32;
        const bool valueInt = T().kind(vt) != TypeKind::Number || coerceToI32(value);
        const bool producesInt =
            op == Tok::AmpAssign || op == Tok::PipeAssign || op == Tok::CaretAssign || op == Tok::ShlAssign ||
            op == Tok::ShrAssign ||
            ((op == Tok::PlusAssign || op == Tok::MinusAssign || op == Tok::StarAssign || op == Tok::PercentAssign) &&
             valueInt);
        if (targetInt && !producesInt) {
            err("S409", n, std::format("i32 에 {} 의 결과 (number) 를 넣을 수 없다 — toI32(…) 로", tokName(op)));
        }
        return set(n, TypeTable::kVoid);
    }

    // 컴포넌트 Host 항목 (ctx.read · has · query · write — ADR-0060): 컴포넌트 인자가 T 를 정하고, 나머지 매개변수 ·
    // 결과의 T · T | null 이 그 값 구조체다
    TypeId componentHostCall(NodeId n, const api::HostFunction& h) {
        const auto kids = ast().kids(n);
        const usize argc = kids.size() - 1;
        if (argc != h.paramCount) {
            err("S405", n, std::format("{} 는 인자 {} 개 — {} 개를 넘겼다", h.id, h.paramCount, argc));
            argsLoose(n);
            return TypeTable::kError;
        }
        u8 ci = 0;
        while (h.params[ci] != api::AbiType::Component) {
            ++ci;
        }
        const NodeId refArg = kids[ci + 1];
        const TypeId rt = expr(refArg, kNoExpect);
        TypeId data = TypeTable::kError;
        if (!T().isError(rt)) {
            if (T().kind(rt) == TypeKind::Component) {
                data = T().info(rt).elem;
            } else {
                err("S404", refArg,
                    std::format("{} 의 {} 는 컴포넌트 참조 (defineComponent 의 const) 다 — {} 를 넘겼다", h.id,
                                h.paramNames[ci], T().name(rt)));
            }
        }
        for (u8 i = 0; i < h.paramCount; ++i) {
            if (i == ci) {
                continue;
            }
            const TypeId pt = h.params[i] == api::AbiType::ComponentData ? data : abiType(h.params[i]);
            const NodeId arg = kids[i + 1];
            const TypeId at = expr(arg, T().isError(pt) ? kNoExpect : pt);
            if (!T().isError(pt) && !T().isError(at)) {
                assignTo(arg, at, pt);
            }
        }
        switch (h.result) {
        case api::AbiType::ComponentDataOrNull:
            return T().isError(data) ? TypeTable::kError : T().nullableOf(data);
        case api::AbiType::ComponentData:
            return data;
        case api::AbiType::EntityList:
            return T().arrayOf(T().opaque(api::AbiType::Entity));
        default:
            return abiType(h.result);
        }
    }

    TypeId abiType(api::AbiType a) {
        switch (a) {
        case api::AbiType::Void:
            return TypeTable::kVoid;
        case api::AbiType::Bool:
            return TypeTable::kBool;
        case api::AbiType::I32:
            return TypeTable::kI32;
        case api::AbiType::Number:
            return TypeTable::kNumber;
        case api::AbiType::String:
            return TypeTable::kString;
        case api::AbiType::Entity:
        case api::AbiType::ServerContext:
            return T().opaque(a);
        case api::AbiType::Component: // 제네릭 — componentHostCall 이 푼다
        case api::AbiType::ComponentData:
        case api::AbiType::ComponentDataOrNull:
            return TypeTable::kError;
        case api::AbiType::EntityList:
            return T().arrayOf(T().opaque(api::AbiType::Entity));
        }
        return TypeTable::kError;
    }

    // 인자 목록 검사 (호출 마디의 kids[1..])
    void checkArgs(NodeId callNode, std::span<const TypeId> params, std::string_view what) {
        const auto kids = ast().kids(callNode);
        const usize argc = kids.size() - 1;
        if (argc != params.size()) {
            err("S405", callNode, std::format("{} 는 인자 {} 개 — {} 개를 넘겼다", what, params.size(), argc));
        }
        for (usize i = 0; i < argc; ++i) {
            const NodeId arg = kids[i + 1];
            if (i < params.size()) {
                const TypeId at = expr(arg, params[i]);
                assignTo(arg, at, params[i]);
            } else {
                expr(arg, kNoExpect);
            }
        }
    }

    TypeId call(NodeId n) {
        const auto kids = ast().kids(n);
        const NodeId callee = kids[0];
        const Node& c = node(callee);
        if (c.kind == NodeKind::Ident) {
            const std::string_view name = ast().str(callee);
            bool reported = false;
            const SymbolId id = lookupValue(callee, name, reported);
            if (id == kNoSymbol) {
                if (!reported) {
                    unknownValue(callee, name);
                }
                argsLoose(n);
                return set(n, TypeTable::kError);
            }
            const Symbol& s = sym(id);
            sema(callee).ref = RefKind::Symbol;
            sema(callee).refIndex = id;
            if (s.kind == SymbolKind::Function) {
                const u32 fi = s.function;
                FunctionInfo& f = P.functions[fi];
                // 최상위 (const 초기값) 에서는 몸을 미리 검사하지 않는다 — 어차피 상수식이 아니다 (S415)
                if (!f.resultDeclared && f.state == FunctionInfo::State::Pending && m_ctx.function >= 0) {
                    ensureFunction(fi);
                }
                if (!P.functions[fi].resultDeclared && P.functions[fi].state == FunctionInfo::State::Checking) {
                    err("S421", callee, std::format("'{}' 는 재귀 · 서로 부르는 함수다 — 반환 타입을 적는다", name));
                    checkArgs(n, P.functions[fi].params, name);
                    return set(n, TypeTable::kError);
                }
                const std::vector<TypeId> params = P.functions[fi].params;
                checkArgs(n, params, name);
                return set(n, P.functions[fi].result);
            }
            if (s.kind == SymbolKind::Builtin && s.builtin == Builtin::ToI32) {
                const TypeId p[] = {TypeTable::kNumber};
                checkArgs(n, p, "toI32");
                return set(n, TypeTable::kI32);
            }
            if (s.kind == SymbolKind::Builtin && s.builtin == Builtin::Idiv) {
                const TypeId p[] = {TypeTable::kI32, TypeTable::kI32};
                checkArgs(n, p, "idiv");
                return set(n, TypeTable::kI32);
            }
            if (s.kind == SymbolKind::Builtin && s.builtin == Builtin::None) {
                argsLoose(n);
                return set(n, TypeTable::kError);
            }
            if (s.kind == SymbolKind::Builtin && s.builtin == Builtin::DefineComponent) {
                err("S425", callee, "defineComponent 는 최상위 const 의 초기값으로만 부른다 (export const Mana = …)");
                return set(n, TypeTable::kError);
            }
            if (s.kind == SymbolKind::Builtin && s.builtin == Builtin::DefineSystem) {
                err("S427", callee,
                    "defineSystem 은 export default defineSystem({ … }) 으로만 부른다 (파일 하나에 하나)");
                return set(n, TypeTable::kError); // 인자는 보지 않는다 (메서드가 S423 으로 이어지지 않게)
            }
            err("S411", callee, std::format("'{}' 는 부를 수 없다", name));
            argsLoose(n);
            return set(n, TypeTable::kError);
        }
        if (c.kind == NodeKind::Member) {
            const NodeId objNode = ast().kid(callee, 0);
            const std::string_view member = ast().str(callee);
            // Math.f(…)
            if (isMathNamespace(objNode)) {
                return mathCall(n, callee, member);
            }
            const TypeId ot = expr(objNode, kNoExpect);
            if (T().isError(ot)) {
                argsLoose(n);
                return set(n, TypeTable::kError);
            }
            if (T().kind(ot) == TypeKind::Nullable) {
                err("S407", callee, std::format("null 일 수 있다 ({}) — if (x !== null) 로 좁힌다", T().name(ot)));
                argsLoose(n);
                return set(n, TypeTable::kError);
            }
            if (T().kind(ot) == TypeKind::Opaque) {
                const api::HostFunction* h = api::findHostMember(T().info(ot).abi, member);
                if (h != nullptr && !h->property && api::isComponentGeneric(*h)) {
                    sema(callee).ref = RefKind::HostFunction;
                    sema(callee).refIndex = static_cast<u32>(api::hostFunctionIndex(*h));
                    set(callee, TypeTable::kVoid);
                    return set(n, componentHostCall(n, *h));
                }
                if (h != nullptr && !h->property) {
                    sema(callee).ref = RefKind::HostFunction;
                    sema(callee).refIndex = static_cast<u32>(api::hostFunctionIndex(*h));
                    std::vector<TypeId> params;
                    for (u8 i = 0; i < h->paramCount; ++i) {
                        params.push_back(abiType(h->params[i]));
                    }
                    checkArgs(n, params, h->id);
                    set(callee, TypeTable::kVoid);
                    return set(n, abiType(h->result));
                }
            }
            if (T().kind(ot) == TypeKind::Array && member == "push") {
                sema(callee).ref = RefKind::ArrayPush;
                const TypeId p[] = {T().info(ot).elem};
                checkArgs(n, p, "push");
                return set(n, TypeTable::kI32);
            }
            err("S406", callee, std::format("{} 에는 메서드 '{}' 가 없다", T().name(ot), member));
            argsLoose(n);
            return set(n, TypeTable::kError);
        }
        expr(callee, kNoExpect);
        if (!T().isError(sema(callee).type)) {
            err("S411", callee, "부를 수 없는 식이다 (함수 값 없음)");
        }
        argsLoose(n);
        return set(n, TypeTable::kError);
    }

    void argsLoose(NodeId callNode) {
        const auto kids = ast().kids(callNode);
        for (usize i = 1; i < kids.size(); ++i) {
            expr(kids[i], kNoExpect);
        }
    }

    bool isMathNamespace(NodeId objNode) {
        if (node(objNode).kind != NodeKind::Ident || ast().str(objNode) != "Math") {
            return false;
        }
        bool reported = false;
        const SymbolId id = lookupValue(objNode, "Math", reported);
        if (id == kNoSymbol || sym(id).kind != SymbolKind::Builtin || sym(id).builtin != Builtin::MathNamespace) {
            return false;
        }
        sema(objNode).ref = RefKind::Symbol;
        sema(objNode).refIndex = id;
        return true;
    }

    TypeId mathCall(NodeId n, NodeId callee, std::string_view member) {
        const auto kids = ast().kids(n);
        const usize argc = kids.size() - 1;
        if (member == "random") {
            err("S413", callee, "Math.random 은 없다 (결정론) — ctx.random [계획 v0.3 M2]");
            argsLoose(n);
            return set(n, TypeTable::kError);
        }
        for (usize i = 0; i < std::size(kMathFunctions); ++i) {
            const MathFunction& f = kMathFunctions[i];
            if (f.name != member) {
                continue;
            }
            sema(callee).ref = RefKind::Builtin;
            sema(callee).refIndex = static_cast<u32>(Builtin::MathFunctionBase) + static_cast<u32>(i);
            if ((f.arity == 0 && argc == 0) || (f.arity != 0 && argc != f.arity)) {
                err("S405", n,
                    f.arity == 0 ? std::format("Math.{} 는 인자 1 개 이상", f.name)
                                 : std::format("Math.{} 는 인자 {} 개 — {} 개를 넘겼다", f.name, f.arity, argc));
            }
            bool allInt = true;
            for (usize k = 1; k < kids.size(); ++k) {
                const TypeId t = expr(kids[k], kNoExpect);
                if (!T().isError(t) && !T().isNumeric(t)) {
                    err("S404", kids[k],
                        std::format("Math.{} 의 인자는 i32 · number — {} 는 아니다", f.name, T().name(t)));
                }
                allInt = allInt && T().kind(t) != TypeKind::Number;
            }
            // min · max 에 i32 와 정수 상수를 섞으면 상수를 i32 로 (Math.max(hp, 0) 은 i32) — 이항 연산과 같은 규칙
            if (f.keepsI32 && !allInt) {
                bool anyInt = false;
                bool coercible = true;
                for (usize k = 1; k < kids.size(); ++k) {
                    const TypeId t = sema(kids[k]).type;
                    anyInt = anyInt || T().kind(t) == TypeKind::I32 || T().kind(t) == TypeKind::Enum;
                    coercible = coercible &&
                                (T().kind(t) != TypeKind::Number ||
                                 (cval(kids[k]).kind == ConstValue::Kind::Number && fitsI32(cval(kids[k]).number)));
                }
                if (anyInt && coercible) {
                    for (usize k = 1; k < kids.size(); ++k) {
                        coerceToI32(kids[k]);
                    }
                    allInt = true;
                }
            }
            return set(n, f.keepsI32 && allInt ? TypeTable::kI32 : TypeTable::kNumber);
        }
        err("S406", callee, std::format("Math.{} 는 없다", member));
        argsLoose(n);
        return set(n, TypeTable::kError);
    }

    TypeId member(NodeId n) {
        const NodeId objNode = ast().kid(n, 0);
        const std::string_view name = ast().str(n);
        // E.A — enum 멤버
        if (node(objNode).kind == NodeKind::Ident) {
            bool reported = false;
            const SymbolId id = lookupValue(objNode, ast().str(objNode), reported);
            if (reported) {
                return set(n, TypeTable::kError);
            }
            if (id != kNoSymbol && sym(id).kind == SymbolKind::Enum) {
                sema(objNode).ref = RefKind::Symbol;
                sema(objNode).refIndex = id;
                const TypeId et = sym(id).type;
                const EnumType& e = T().enumOf(et);
                for (usize i = 0; i < e.members.size(); ++i) {
                    if (e.members[i].name == name) {
                        sema(n).ref = RefKind::EnumMember;
                        sema(n).refIndex = static_cast<u32>(i);
                        cval(n) = i32Const(e.members[i].value);
                        return set(n, et);
                    }
                }
                err("S406", n, std::format("enum {} 에는 멤버 '{}' 가 없다", e.name, name));
                return set(n, TypeTable::kError);
            }
            if (isMathNamespace(objNode)) {
                for (usize i = 0; i < std::size(kMathConstants); ++i) {
                    if (kMathConstants[i].name == name) {
                        sema(n).ref = RefKind::Builtin;
                        sema(n).refIndex = static_cast<u32>(Builtin::MathConstantBase) + static_cast<u32>(i);
                        cval(n) = numberConst(kMathConstants[i].value);
                        return set(n, TypeTable::kNumber);
                    }
                }
                if (name == "random") {
                    err("S413", n, "Math.random 은 없다 (결정론) — ctx.random [계획 v0.3 M2]");
                } else {
                    err("S411", n, std::format("Math.{} 는 값이 아니다 — 부른다", name));
                }
                return set(n, TypeTable::kError);
            }
        }
        const TypeId ot = expr(objNode, kNoExpect);
        if (T().isError(ot)) {
            return set(n, TypeTable::kError);
        }
        switch (T().kind(ot)) {
        case TypeKind::Struct: {
            const i32 idx = T().structOf(ot).indexOf(name);
            if (idx < 0) {
                err("S406", n, std::format("{} 에는 필드 '{}' 가 없다", T().name(ot), name));
                return set(n, TypeTable::kError);
            }
            sema(n).ref = RefKind::Field;
            sema(n).refIndex = static_cast<u32>(idx);
            return set(n, T().structOf(ot).fields[static_cast<usize>(idx)].type);
        }
        case TypeKind::Array:
            if (name == "length") {
                sema(n).ref = RefKind::ArrayLength;
                return set(n, TypeTable::kI32);
            }
            break;
        case TypeKind::String:
            if (name == "length") {
                sema(n).ref = RefKind::StringLength;
                return set(n, TypeTable::kI32);
            }
            break;
        case TypeKind::Opaque: {
            const api::HostFunction* h = api::findHostMember(T().info(ot).abi, name);
            if (h != nullptr && h->property) {
                sema(n).ref = RefKind::HostFunction;
                sema(n).refIndex = static_cast<u32>(api::hostFunctionIndex(*h));
                return set(n, abiType(h->result));
            }
            if (h != nullptr) {
                err("S411", n, std::format("{} 는 메서드다 — 부른다 ({}(…))", h->id, name));
                return set(n, TypeTable::kError);
            }
            break;
        }
        case TypeKind::Nullable:
            err("S407", n, std::format("null 일 수 있다 ({}) — if (x !== null) 로 좁힌다", T().name(ot)));
            return set(n, TypeTable::kError);
        default:
            break;
        }
        err("S406", n, std::format("{} 에는 '{}' 가 없다", T().name(ot), name));
        return set(n, TypeTable::kError);
    }

    TypeId arrayLiteral(NodeId n, TypeId expected) {
        const auto elems = ast().kids(n);
        const TypeId exp = expected == kNoExpect ? kNoExpect : T().nonNull(expected);
        if (exp != kNoExpect && T().kind(exp) == TypeKind::Array) {
            const TypeId et = T().info(exp).elem;
            for (const NodeId e : elems) {
                const TypeId t = expr(e, et);
                assignTo(e, t, et);
            }
            return set(n, exp);
        }
        if (elems.empty()) {
            if (exp == kNoExpect) {
                err("S414", n, "빈 배열은 타입을 적는다 (const xs: i32[] = [])");
            }
            return set(n, TypeTable::kError);
        }
        TypeId t = kNoExpect;
        bool bad = false;
        for (const NodeId e : elems) {
            const TypeId et = expr(e, kNoExpect);
            t = t == kNoExpect ? et : unify(t, et, bad);
        }
        if (bad) {
            err("S404", n, "배열 원소의 타입이 서로 다르다 — 타입을 적는다");
            return set(n, TypeTable::kError);
        }
        if (t == TypeTable::kNull) {
            err("S414", n, "null 만 든 배열은 타입을 적는다");
            return set(n, TypeTable::kError);
        }
        return set(n, T().arrayOf(t));
    }

    TypeId objectLiteral(NodeId n, TypeId expected) {
        const auto props = ast().kids(n);
        const TypeId exp = expected == kNoExpect ? kNoExpect : T().nonNull(expected);
        std::vector<std::string> seen;
        for (const NodeId p : props) {
            if (node(p).kind == NodeKind::Method) {
                err("S423", p, "객체 리터럴의 메서드는 export default defineSystem({ … }) 안에서만 (onTick)");
                continue;
            }
            const std::string key(ast().str(p));
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
                err("S402", p, std::format("키 '{}' 가 두 번 있다", key));
            }
            seen.push_back(key);
        }
        if (exp != kNoExpect && T().kind(exp) == TypeKind::Struct) {
            const StructType st = T().structOf(exp); // 복사 — 검사하며 표가 자랄 수 있다
            bool ok = true;
            for (const NodeId p : props) {
                if (node(p).kind != NodeKind::Property) {
                    continue;
                }
                const std::string_view key = ast().str(p);
                const StructField* f = st.find(key);
                const NodeId value = ast().kid(p, 0);
                if (f == nullptr) {
                    err("S404", p, std::format("{} 에는 필드 '{}' 가 없다", T().name(exp), key));
                    expr(value, kNoExpect);
                    ok = false;
                    continue;
                }
                const TypeId vt = expr(value, f->type);
                assignTo(value, vt, f->type);
                set(p, f->type);
            }
            for (const StructField& f : st.fields) {
                if (std::find(seen.begin(), seen.end(), f.name) == seen.end()) {
                    err("S404", n, std::format("{} 의 필드 '{}' 가 빠졌다", T().name(exp), f.name));
                    ok = false;
                }
            }
            return set(n, ok ? exp : TypeTable::kError);
        }
        // 기대 타입이 없으면 익명 구조체
        std::vector<StructField> fields;
        for (const NodeId p : props) {
            if (node(p).kind != NodeKind::Property) {
                continue;
            }
            StructField f;
            f.name = std::string(ast().str(p));
            const TypeId vt = expr(ast().kid(p, 0), kNoExpect);
            if (vt == TypeTable::kNull) {
                err("S414", p, std::format("null 인 필드 '{}' 는 타입을 적는다 (interface 로)", f.name));
            }
            f.type = vt == TypeTable::kNull ? TypeTable::kError : vt;
            set(p, f.type);
            fields.push_back(std::move(f));
        }
        const TypeId st = T().newStruct("");
        T().structOf(st).fields = std::move(fields);
        return set(n, st);
    }

    Program& P;
    DiagnosticList& D;
    std::vector<ModuleScope> m_mscopes;
    std::vector<std::vector<std::pair<NodeId, i32>>> m_importTargets; // 모듈마다 (Import 마디, 대상)
    std::vector<ScopeEntry> m_sdkValues;
    std::vector<ScopeEntry> m_sdkTypes;
    std::vector<ScopeEntry> m_globals;
    TypeId m_vec2 = TypeTable::kError; // SDK 의 Vec2 { x, y: number }
    std::vector<bool> m_ready;         // 심볼마다 — 타입 · 값이 정해졌다
    std::vector<bool> m_resolving;     // 타입 별칭 순환 검사
    ScopeEntry m_tmpEntry[2];
    FnCtx m_ctx;
};

} // namespace

const ModuleSema* Program::module(std::string_view path) const noexcept {
    for (const ModuleSema& m : modules) {
        if (m.path == path) {
            return &m;
        }
    }
    return nullptr;
}

SymbolId Program::exportedValue(std::string_view path, std::string_view name) const noexcept {
    const ModuleSema* m = module(path);
    if (m == nullptr) {
        return kNoSymbol;
    }
    for (const auto& [n, s] : m->exportValues) {
        if (n == name) {
            return s;
        }
    }
    return kNoSymbol;
}

Program checkProgram(std::span<const SourceFile> files, DiagnosticList& diags) {
    Program p;
    Checker(p, diags).run(files);
    return p;
}

std::string resolveImportPath(std::string_view from, std::string_view spec) {
    if (!(spec.starts_with("./") || spec.starts_with("../"))) {
        return {};
    }
    if (spec.ends_with(".ts") || spec.ends_with(".js") || spec.find('\\') != std::string_view::npos) {
        return {};
    }
    std::vector<std::string> parts;
    // from 의 폴더
    usize start = 0;
    while (true) {
        const usize slash = from.find('/', start);
        if (slash == std::string_view::npos) {
            break;
        }
        parts.emplace_back(from.substr(start, slash - start));
        start = slash + 1;
    }
    const std::string top = parts.empty() ? std::string() : parts.front();
    start = 0;
    while (start <= spec.size()) {
        usize slash = spec.find('/', start);
        if (slash == std::string_view::npos) {
            slash = spec.size();
        }
        const std::string_view seg = spec.substr(start, slash - start);
        if (seg.empty()) {
            return {};
        }
        if (seg == "..") {
            if (parts.empty()) {
                return {};
            }
            parts.pop_back();
        } else if (seg != ".") {
            parts.emplace_back(seg);
        }
        start = slash + 1;
    }
    if (parts.empty() || parts.back() == "." || parts.back() == "..") {
        return {};
    }
    // 처음 폴더 (scripts/) 밖으로는 못 나간다
    if (!top.empty() && (parts.size() < 2 || parts.front() != top)) {
        return {};
    }
    std::string out;
    for (usize i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += '/';
        }
        out += parts[i];
    }
    return out + ".ts";
}

} // namespace sbx::script
