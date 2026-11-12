#include "script/lang/LanguageService.hpp"

#include <algorithm>
#include <format>

#include "script/lang/MemberCatalog.hpp"
#include "script/lang/ProgramCache.hpp"
#include "script/vm/Intrinsics.hpp"

namespace sbx::script {
namespace {

// 스니펫 (v0.9 ST-06-8 — 27 4장): 문장 처음에서 이름 완성에 섞인다. 본문의 줄은 넣는 줄의 들여쓰기에 맞춘다
// (ScriptAssist)
struct Snippet {
    std::string_view trigger;
    std::string_view detail;
    std::string_view body;
};
constexpr Snippet kSnippets[] = {
    {"system", "틀: export default defineSystem({ id · onTick })",
     "export default defineSystem({\n  id: \"pack.system\",\n  onTick(ctx) {\n  },\n})"},
    {"component", "틀: export const … = defineComponent({ id · fields })",
     "export const Thing = defineComponent({\n  id: \"pack.thing\",\n  fields: { value: field.f32(0) },\n})"},
    {"forquery", "틀: for (const e of ctx.query(C)) { read · write }",
     "for (const e of ctx.query(Thing)) {\n  const c = ctx.read(e, Thing)\n  if (c !== null) {\n    ctx.write(e, "
     "Thing, c)\n  }\n}"},
};

// 자리표 이름 — '.' 뒤에 넣어 문장을 맞춘다 (사람이 쓰지 않을 이름)
constexpr std::string_view kSentinel = "__sbx_complete__";

// 이름 글자 — ASCII 이름 글자와 UTF-8 의 비 ASCII 바이트 (렉서는 비 ASCII 이름을 받지 않는다 — S101. 여기서는 편집 중
// 낱말의 경계만 맞춘다)
[[nodiscard]] bool identChar(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
           u >= 0x80;
}

[[nodiscard]] std::string signatureOf(const api::HostFunction& f) {
    std::string s(f.member);
    if (!f.property) {
        s += '(';
        for (u8 i = 0; i < f.paramCount; ++i) {
            if (i > 0) {
                s += ", ";
            }
            s += f.paramNames[i];
            s += ": ";
            s += api::abiTypeName(f.params[i]);
        }
        s += ')';
    }
    s += ": ";
    s += api::abiTypeName(f.result);
    return s;
}

[[nodiscard]] CompletionItem hostItem(const api::HostFunction& f) {
    return CompletionItem{std::string(f.member),
                          signatureOf(f),
                          std::string(f.doc),
                          f.property ? CompletionKind::Property : CompletionKind::Method,
                          {}};
}

[[nodiscard]] bool startsWithNoCase(std::string_view s, std::string_view prefix) noexcept {
    if (prefix.size() > s.size()) {
        return false;
    }
    for (usize i = 0; i < prefix.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(s[i]) != lower(prefix[i])) {
            return false;
        }
    }
    return true;
}

void finish(std::vector<CompletionItem>& items, std::string_view prefix) {
    std::erase_if(items, [&](const CompletionItem& c) { return !startsWithNoCase(c.label, prefix); });
    // 정확히 접두로 시작하는 것 먼저, 그다음 이름 순 — 같은 이름은 하나
    std::stable_sort(items.begin(), items.end(), [&](const CompletionItem& a, const CompletionItem& b) {
        const bool ea = a.label.starts_with(prefix);
        const bool eb = b.label.starts_with(prefix);
        return ea != eb ? ea : a.label < b.label;
    });
    items.erase(std::unique(items.begin(), items.end(),
                            [](const CompletionItem& a, const CompletionItem& b) { return a.label == b.label; }),
                items.end());
}

[[nodiscard]] std::optional<BuiltinReceiver> receiverOf(const TypeTable& t, TypeId type) {
    switch (t.kind(type)) {
    case TypeKind::Array:
        return BuiltinReceiver::Array;
    case TypeKind::String:
        return BuiltinReceiver::String;
    case TypeKind::Map:
        return BuiltinReceiver::Map;
    case TypeKind::Set:
        return BuiltinReceiver::Set;
    case TypeKind::Coll:
        switch (static_cast<CollKind>(t.info(type).index)) {
        case CollKind::Stack:
            return BuiltinReceiver::Stack;
        case CollKind::Queue:
            return BuiltinReceiver::Queue;
        case CollKind::Deque:
            return BuiltinReceiver::Deque;
        case CollKind::PriorityQueue:
            return BuiltinReceiver::PriorityQueue;
        case CollKind::MinHeap:
            return BuiltinReceiver::MinHeap;
        case CollKind::MaxHeap:
            return BuiltinReceiver::MaxHeap;
        case CollKind::BitSet:
            return BuiltinReceiver::BitSet;
        case CollKind::Graph:
            return BuiltinReceiver::Graph;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

void addBuiltinMembers(std::vector<CompletionItem>& out, BuiltinReceiver r) {
    for (const MemberDoc& m : builtinMembers(r)) {
        out.push_back(CompletionItem{std::string(m.name),
                                     std::string(m.signature),
                                     std::string(m.doc),
                                     m.property ? CompletionKind::Property : CompletionKind::Method,
                                     {}});
    }
}

[[nodiscard]] const ModuleSema* moduleOf(const Program& p, std::string_view path) {
    return p.module(path);
}

// 식 obj 가 이름 n 의 이름공간 (Math · vec2) 인가 — 같은 이름의 지역이 있으면 검사기가 그 값을 쓴다
[[nodiscard]] bool isNamespace(const ModuleSema& m, NodeId obj, std::string_view n) {
    const Node& x = m.ast.node(obj);
    if (x.kind != NodeKind::Ident || m.ast.str(obj) != n) {
        return false;
    }
    const NodeSema& s = m.nodes[obj];
    return s.ref == RefKind::None || s.ref == RefKind::Builtin ||
           (s.ref == RefKind::Symbol && s.type == TypeTable::kError);
}

// 받는 쪽 마디의 멤버 후보
std::vector<CompletionItem> membersFor(const Program& p, const ModuleSema& m, NodeId obj) {
    std::vector<CompletionItem> out;
    if (isNamespace(m, obj, "Math")) {
        addBuiltinMembers(out, BuiltinReceiver::Math);
        return out;
    }
    if (isNamespace(m, obj, "vec2")) {
        addBuiltinMembers(out, BuiltinReceiver::Vec2);
        return out;
    }
    const NodeSema& s = m.nodes[obj];
    if (s.ref == RefKind::Symbol && s.refIndex < p.symbols.size() && p.symbols[s.refIndex].kind == SymbolKind::Enum) {
        const TypeId et = p.symbols[s.refIndex].type;
        for (const EnumMemberInfo& e : p.types.enumOf(et).members) {
            out.push_back(CompletionItem{e.name,
                                         std::format("{}.{} = {}", p.types.enumOf(et).name, e.name, e.value),
                                         {},
                                         CompletionKind::EnumMember,
                                         {}});
        }
        return out;
    }
    TypeId t = s.type;
    if (p.types.isError(t)) {
        return out;
    }
    t = p.types.nonNull(t); // T | null — 좁힌 뒤에 쓸 멤버를 보인다 (좁히지 않으면 검사기가 S407)
    if (p.types.kind(t) == TypeKind::Struct) {
        for (const StructField& f : p.types.structOf(t).fields) {
            out.push_back(
                CompletionItem{f.name,
                               std::format("{}{}: {}", f.readonly ? "readonly " : "", f.name, p.types.name(f.type)),
                               {},
                               CompletionKind::Field,
                               {}});
        }
        return out;
    }
    if (p.types.kind(t) == TypeKind::Opaque) {
        const api::AbiType abi = p.types.info(t).abi;
        for (const api::HostFunction& f : api::kHostFunctions) {
            if (f.owner == abi) {
                out.push_back(hostItem(f));
            }
        }
        return out;
    }
    if (const auto r = receiverOf(p.types, t)) {
        addBuiltinMembers(out, *r);
    }
    return out;
}

// 커서 앞 이름의 시작
[[nodiscard]] usize wordStart(std::string_view text, usize offset) {
    usize p = std::min(offset, text.size());
    while (p > 0 && identChar(text[p - 1])) {
        --p;
    }
    return p;
}

// 이름 [begin, end) — 커서가 이름 안 · 끝에 있으면
[[nodiscard]] std::pair<usize, usize> wordAround(std::string_view text, usize offset) {
    const usize begin = wordStart(text, offset);
    usize end = std::min(offset, text.size());
    while (end < text.size() && identChar(text[end])) {
        ++end;
    }
    return {begin, end};
}

[[nodiscard]] std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// import { … } from "<spec>" 의 { } 안인가 — 그 spec 과 이미 쓴 이름
struct ImportSite {
    std::string spec;
    std::vector<std::string> written;
};
[[nodiscard]] std::optional<ImportSite> importSiteAt(std::string_view text, usize p) {
    const usize brace = text.rfind('{', p == 0 ? 0 : p - 1);
    if (brace == std::string_view::npos || brace >= p) {
        return std::nullopt;
    }
    const usize lineStart = text.rfind('\n', brace) == std::string_view::npos ? 0 : text.rfind('\n', brace) + 1;
    if (trimView(text.substr(lineStart, brace - lineStart)) != "import") {
        return std::nullopt;
    }
    const usize close = text.find('}', p);
    if (close == std::string_view::npos || text.substr(brace + 1, p - brace - 1).find('}') != std::string_view::npos) {
        return std::nullopt;
    }
    const usize q1 = text.find('"', close);
    const usize q2 = q1 == std::string_view::npos ? q1 : text.find('"', q1 + 1);
    if (q2 == std::string_view::npos || text.substr(close, q1 - close).find("from") == std::string_view::npos) {
        return std::nullopt;
    }
    ImportSite s;
    s.spec = std::string(text.substr(q1 + 1, q2 - q1 - 1));
    std::string cur;
    for (usize i = brace + 1; i <= close; ++i) {
        const char c = i < close ? text[i] : ',';
        if (identChar(c)) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            s.written.push_back(cur);
            cur.clear();
        }
    }
    return s;
}

[[nodiscard]] std::string functionSignature(const Program& p, const FunctionInfo& f) {
    std::string s = std::format("function {}(", f.name);
    for (usize i = 0; i < f.params.size(); ++i) {
        const std::string_view pname = i < f.paramSymbols.size() && f.paramSymbols[i] < p.symbols.size()
                                           ? std::string_view(p.symbols[f.paramSymbols[i]].name)
                                           : std::string_view("_");
        s += std::format("{}{}: {}", i > 0 ? ", " : "", pname, p.types.name(f.params[i]));
    }
    s += std::format("): {}", p.types.name(f.result));
    return s;
}

// 선언 줄 위의 주석 (// … 줄들 · /** … */) — Roblox 의 블록 주석 툴팁 (27 2장)
[[nodiscard]] std::string commentAbove(std::string_view text, usize declOffset) {
    usize lineStart = declOffset;
    while (lineStart > 0 && text[lineStart - 1] != '\n') {
        --lineStart;
    }
    std::vector<std::string_view> lines;
    usize end = lineStart;
    bool block = false;
    while (end > 0) {
        usize start = end - 1;
        while (start > 0 && text[start - 1] != '\n') {
            --start;
        }
        const std::string_view line = trimView(text.substr(start, end - 1 - start));
        if (block) {
            lines.push_back(line);
            if (line.starts_with("/*")) {
                break;
            }
        } else if (line.starts_with("//")) {
            lines.push_back(line);
        } else if (line.ends_with("*/") && lines.empty()) {
            block = true;
            lines.push_back(line);
            if (line.starts_with("/*")) {
                break;
            }
        } else {
            break;
        }
        end = start;
    }
    std::string out;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        std::string_view l = *it;
        for (const std::string_view mark : {"/**", "/*", "*/", "///", "//", "*"}) {
            if (l.starts_with(mark)) {
                l.remove_prefix(mark.size());
            }
            if (l.ends_with("*/")) {
                l.remove_suffix(2);
            }
        }
        l = trimView(l);
        if (l.empty()) {
            continue;
        }
        out += out.empty() ? "" : "\n";
        out += l;
    }
    return out;
}

[[nodiscard]] std::string describeSymbol(const Program& p, const Symbol& s, std::string& doc,
                                         std::string_view docText) {
    switch (s.kind) {
    case SymbolKind::Function:
        if (s.function < p.functions.size()) {
            const FunctionInfo& f = p.functions[s.function];
            if (f.decl != kNoNode && f.module < p.modules.size()) {
                doc = commentAbove(docText, p.modules[f.module].ast.node(f.decl).pos.offset);
            }
            return functionSignature(p, f);
        }
        return std::format("function {}", s.name);
    case SymbolKind::Local:
        return std::format("{} {}: {}", s.mutableVar ? "let" : "const", s.name, p.types.name(s.type));
    case SymbolKind::Param:
        return std::format("매개변수 {}: {}", s.name, p.types.name(s.type));
    case SymbolKind::ModuleConst:
        return std::format("const {}: {}", s.name, p.types.name(s.type));
    case SymbolKind::Type:
        return std::format("type {}", p.types.name(s.type));
    case SymbolKind::Enum:
        return std::format("enum {}", p.types.name(s.type));
    case SymbolKind::Component:
        if (s.function < p.components.size()) {
            return std::format("컴포넌트 {} — \"{}\" (데이터 {})", s.name, p.components[s.function].schema.id,
                               p.types.name(p.components[s.function].dataType));
        }
        return std::format("컴포넌트 {}", s.name);
    case SymbolKind::Builtin:
        return std::format("내장 {}", s.name);
    }
    return s.name;
}

// 같은 입력이면 기억한 결과 (v0.9 ST-06-8 — ProgramCache)
[[nodiscard]] std::shared_ptr<const AnalyzedProgram> analyze(const LanguageQuery& q,
                                                             std::span<const SourceFile> files) {
    return analyzeCached(files, q.imports);
}

} // namespace

std::vector<CompletionItem> contextMembers(api::HostDomain domain) {
    const api::AbiType owner =
        domain == api::HostDomain::Server ? api::AbiType::ServerContext : api::AbiType::ClientContext;
    std::vector<CompletionItem> out;
    for (const api::HostFunction& f : api::kHostFunctions) {
        if (f.owner != owner || f.domain != domain) {
            continue;
        }
        out.push_back(hostItem(f));
    }
    std::sort(out.begin(), out.end(),
              [](const CompletionItem& a, const CompletionItem& b) { return a.label < b.label; });
    return out;
}

std::vector<CompletionItem> completeAt(std::string_view source, usize offset, api::HostDomain domain) {
    if (offset > source.size()) {
        return {};
    }
    // 커서 앞의 접두 (식별자 글자) → 그 앞이 '.' → 그 앞의 수신자 이름
    const usize p = wordStart(source, offset);
    const std::string_view prefix = source.substr(p, offset - p);
    if (p == 0 || source[p - 1] != '.') {
        return {};
    }
    usize r = p - 1;
    const usize end = r;
    while (r > 0 && identChar(source[r - 1])) {
        --r;
    }
    if (source.substr(r, end - r) != "ctx") {
        return {};
    }
    std::vector<CompletionItem> all = contextMembers(domain);
    std::erase_if(all, [&](const CompletionItem& c) { return !c.label.starts_with(prefix); });
    return all;
}

std::vector<CompletionItem> complete(const LanguageQuery& q) {
    if (q.file >= q.files.size()) {
        return {};
    }
    const SourceFile& doc = q.files[q.file];
    const std::string_view text = doc.text;
    const usize offset = std::min(q.offset, text.size());
    const usize p = wordStart(text, offset);
    const std::string prefix(text.substr(p, offset - p));
    std::vector<CompletionItem> out;

    // 1) 식 뒤 '.' — 자리표를 넣어 검사기에서 받는 쪽의 타입을 얻는다
    usize dot = p;
    while (dot > 0 && (text[dot - 1] == ' ' || text[dot - 1] == '\t')) {
        --dot;
    }
    if (dot > 0 && text[dot - 1] == '.') {
        std::vector<SourceFile> files(q.files.begin(), q.files.end());
        files[q.file].text = std::string(text.substr(0, p)) + std::string(kSentinel) + std::string(text.substr(offset));
        const auto analyzed = analyze(q, files);
        const Program& prog = analyzed->program;
        const ModuleSema* m = moduleOf(prog, doc.path);
        if (m != nullptr && m->parsed) {
            for (NodeId n = 0; n < m->ast.size(); ++n) {
                if (m->ast.node(n).kind == NodeKind::Member && m->ast.str(n).starts_with(kSentinel)) {
                    out = membersFor(prog, *m, m->ast.kid(n, 0));
                    break;
                }
            }
        } else {
            // 파싱 오류 (다른 문장) — 글만 보고 ctx. (도메인은 글에 "client" 시스템이 있으면 클라이언트)
            const bool client = text.find("domain: \"client\"") != std::string_view::npos;
            out = completeAt(text, offset, client ? api::HostDomain::Client : api::HostDomain::Server);
        }
        finish(out, prefix);
        return out;
    }

    // 2) import { | } from "…"
    if (const auto site = importSiteAt(text, p)) {
        if (site->spec == "sandbox") {
            for (std::string& n : sandboxExportNames()) {
                out.push_back(CompletionItem{std::move(n), {}, {}, CompletionKind::Function, {}});
            }
        } else {
            const auto analyzed = analyze(q, q.files);
            const Program& prog = analyzed->program;
            const std::string path = resolveImportPath(doc.path, site->spec);
            if (const ModuleSema* target = moduleOf(prog, path)) {
                for (const auto& [n, s] : target->exportValues) {
                    out.push_back(CompletionItem{n,
                                                 s < prog.symbols.size() ? prog.types.name(prog.symbols[s].type) : "",
                                                 {},
                                                 CompletionKind::Variable,
                                                 {}});
                }
                for (const auto& [n, s] : target->exportTypes) {
                    out.push_back(CompletionItem{n, "type", {}, CompletionKind::Type, {}});
                }
            }
        }
        std::erase_if(out, [&](const CompletionItem& c) {
            return std::find(site->written.begin(), site->written.end(), c.label) != site->written.end() &&
                   c.label != prefix;
        });
        finish(out, prefix);
        return out;
    }

    // 3) 이름 — 접두가 있을 때만 (빈 자리에서 모든 이름을 쏟지 않는다)
    if (prefix.empty()) {
        return out;
    }
    const auto analyzed = analyze(q, q.files);
    const Program& prog = analyzed->program;
    const ModuleSema* m = moduleOf(prog, doc.path);
    u32 mi = 0;
    for (; mi < prog.modules.size(); ++mi) {
        if (&prog.modules[mi] == m) {
            break;
        }
    }
    if (m != nullptr) {
        // 커서를 품은 함수 — 그 함수의 선언보다 뒤 · 커서보다 앞에서 선언한 지역 · 매개변수
        usize fnStart = 0;
        for (const FunctionInfo& f : prog.functions) {
            if (f.module == mi && f.decl != kNoNode) {
                const usize at = m->ast.node(f.decl).pos.offset;
                if (at <= p && at >= fnStart) {
                    fnStart = at;
                }
            }
        }
        for (const Symbol& s : prog.symbols) {
            if (s.module != mi || s.decl == kNoNode) {
                continue;
            }
            const usize at = m->ast.node(s.decl).pos.offset;
            switch (s.kind) {
            case SymbolKind::Local:
            case SymbolKind::Param:
                if (at >= fnStart && at < p) {
                    out.push_back(CompletionItem{s.name, prog.types.name(s.type), {}, CompletionKind::Variable, {}});
                }
                break;
            case SymbolKind::ModuleConst:
                out.push_back(CompletionItem{s.name, prog.types.name(s.type), {}, CompletionKind::Constant, {}});
                break;
            case SymbolKind::Function:
                out.push_back(CompletionItem{
                    s.name,
                    s.function < prog.functions.size() ? functionSignature(prog, prog.functions[s.function]) : "",
                    {},
                    CompletionKind::Function,
                    {}});
                break;
            case SymbolKind::Type:
                out.push_back(CompletionItem{s.name, "type", {}, CompletionKind::Type, {}});
                break;
            case SymbolKind::Enum:
                out.push_back(CompletionItem{s.name, "enum", {}, CompletionKind::Enum, {}});
                break;
            case SymbolKind::Component:
                out.push_back(CompletionItem{s.name, "컴포넌트", {}, CompletionKind::Component, {}});
                break;
            case SymbolKind::Builtin:
                break;
            }
        }
        // 가져온 이름 (import { A, B as C } …)
        for (const NodeId item : m->ast.kids(m->ast.root)) {
            if (m->ast.node(item).kind != NodeKind::Import) {
                continue;
            }
            for (const NodeId nm : m->ast.kids(item)) {
                const std::string_view shown =
                    m->ast.node(nm).kidCount > 0 ? m->ast.str(m->ast.kid(nm, 0)) : m->ast.str(nm);
                out.push_back(CompletionItem{std::string(shown),
                                             std::format("from \"{}\"", m->ast.str(item)),
                                             {},
                                             CompletionKind::Variable,
                                             {}});
            }
        }
    }
    for (const std::string_view g : {"Math", "NaN", "Infinity"}) {
        out.push_back(CompletionItem{std::string(g), "전역", {}, CompletionKind::Namespace, {}});
    }
    std::erase_if(out, [&](const CompletionItem& c) { return c.label == prefix; }); // 다 쓴 이름은 보이지 않는다
    // 스니펫 (v0.9 ST-06-8) — 문장의 처음 (그 줄에서 이름 앞이 빈칸뿐) 에서만
    usize lineStart = p;
    while (lineStart > 0 && text[lineStart - 1] != '\n') {
        --lineStart;
    }
    if (text.substr(lineStart, p - lineStart).find_first_not_of(" \t") == std::string_view::npos) {
        for (const Snippet& sn : kSnippets) {
            out.push_back(CompletionItem{std::string(sn.trigger), std::string(sn.detail), std::string(sn.body),
                                         CompletionKind::Snippet, std::string(sn.body)});
        }
    }
    finish(out, prefix);
    return out;
}

std::optional<HoverInfo> hover(const LanguageQuery& q) {
    if (q.file >= q.files.size()) {
        return std::nullopt;
    }
    const SourceFile& doc = q.files[q.file];
    const std::string_view text = doc.text;
    const auto [begin, end] = wordAround(text, q.offset);
    if (begin == end) {
        return std::nullopt;
    }
    const std::string_view word = text.substr(begin, end - begin);
    const auto analyzed = analyze(q, q.files);
    const Program& prog = analyzed->program;
    const ModuleSema* m = moduleOf(prog, doc.path);
    if (m == nullptr || !m->parsed) {
        return std::nullopt;
    }
    HoverInfo h;
    h.begin = begin;
    h.end = end;
    for (NodeId n = 0; n < m->ast.size(); ++n) {
        const Node& x = m->ast.node(n);
        if (x.kind == NodeKind::Ident && x.pos.offset == begin && m->ast.str(n) == word) {
            const NodeSema& s = m->nodes[n];
            if (s.ref == RefKind::Symbol && s.refIndex < prog.symbols.size()) {
                const Symbol& sym = prog.symbols[s.refIndex];
                // 다른 모듈의 함수면 그 파일의 주석
                std::string_view docText = text;
                if (sym.kind == SymbolKind::Function && sym.function < prog.functions.size()) {
                    const std::string& fpath = prog.modules[prog.functions[sym.function].module].path;
                    for (const SourceFile& f : q.files) {
                        if (f.path == fpath) {
                            docText = f.text;
                        }
                    }
                }
                h.title = describeSymbol(prog, sym, h.doc, docText);
                return h;
            }
            if (word == "Math" || word == "vec2") {
                h.title = std::format("내장 이름공간 {}", word);
                return h;
            }
            h.title = std::format("{}: {}", word, prog.types.name(s.type));
            return h;
        }
        if (x.kind != NodeKind::Member || m->ast.str(n) != word) {
            continue;
        }
        // 멤버 이름은 '.' (마디 자리) 뒤 — 빈칸을 건너뛴 자리
        usize at = x.pos.offset + 1;
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
        if (at != begin) {
            continue;
        }
        const NodeSema& s = m->nodes[n];
        const NodeId obj = m->ast.kid(n, 0);
        TypeId ot = prog.types.nonNull(m->nodes[obj].type);
        if (s.ref == RefKind::HostFunction && s.refIndex < api::kHostFunctions.size()) {
            const api::HostFunction& f = api::kHostFunctions[s.refIndex];
            h.title = signatureOf(f);
            h.doc = std::string(f.doc);
            return h;
        }
        if (s.ref == RefKind::Field && !prog.types.isError(ot) && prog.types.kind(ot) == TypeKind::Struct) {
            const StructType& st = prog.types.structOf(ot);
            if (s.refIndex < st.fields.size()) {
                h.title = std::format("필드 {}.{}: {}", st.name.empty() ? std::string("{…}") : st.name, word,
                                      prog.types.name(st.fields[s.refIndex].type));
                return h;
            }
        }
        if (s.ref == RefKind::EnumMember) {
            h.title = std::format("enum 멤버 {} = {}", word, prog.types.name(s.type));
            return h;
        }
        // 내장 멤버 — 받는 쪽 타입 · 이름공간으로 표에서
        std::optional<BuiltinReceiver> r;
        if (isNamespace(*m, obj, "Math")) {
            r = BuiltinReceiver::Math;
        } else if (isNamespace(*m, obj, "vec2")) {
            r = BuiltinReceiver::Vec2;
        } else if (!prog.types.isError(ot)) {
            r = receiverOf(prog.types, ot);
            if (!r && prog.types.kind(ot) == TypeKind::Opaque) {
                if (const api::HostFunction* f = api::findHostMember(prog.types.info(ot).abi, word)) {
                    h.title = signatureOf(*f);
                    h.doc = std::string(f->doc);
                    return h;
                }
            }
        }
        if (r) {
            for (const MemberDoc& md : builtinMembers(*r)) {
                if (md.name == word) {
                    h.title = std::format("{}.{}", builtinReceiverName(*r), md.signature);
                    h.doc = std::string(md.doc);
                    return h;
                }
            }
        }
        return std::nullopt;
    }
    // 선언 자리의 이름 (function f · const x · interface P …) — 이 모듈의 같은 이름 기호 중 커서 앞의 가장 가까운 선언
    u32 mi = 0;
    for (; mi < prog.modules.size(); ++mi) {
        if (&prog.modules[mi] == m) {
            break;
        }
    }
    const Symbol* best = nullptr;
    usize bestAt = 0;
    for (const Symbol& s : prog.symbols) {
        if (s.module != mi || s.name != word || s.decl == kNoNode) {
            continue;
        }
        const usize at = m->ast.node(s.decl).pos.offset;
        if (at <= begin && (best == nullptr || at >= bestAt)) {
            best = &s;
            bestAt = at;
        }
    }
    if (best != nullptr) {
        h.title = describeSymbol(prog, *best, h.doc, text);
        return h;
    }
    return std::nullopt;
}

usize completionWordStart(std::string_view text, usize offset) noexcept {
    return wordStart(text, offset);
}

void filterCompletions(std::vector<CompletionItem>& items, std::string_view prefix) {
    finish(items, prefix);
}

} // namespace sbx::script
