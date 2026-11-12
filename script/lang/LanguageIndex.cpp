#include "script/lang/LanguageIndex.hpp"

#include <algorithm>
#include <format>

#include "script/lang/Lexer.hpp"
#include "script/lang/ProgramCache.hpp"

namespace sbx::script {

namespace {

[[nodiscard]] bool identChar(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
           u >= 0x80;
}

[[nodiscard]] std::pair<usize, usize> wordAround(std::string_view text, usize offset) {
    usize b = std::min(offset, text.size());
    while (b > 0 && identChar(text[b - 1])) {
        --b;
    }
    usize e = std::min(offset, text.size());
    while (e < text.size() && identChar(text[e])) {
        ++e;
    }
    return {b, e};
}

[[nodiscard]] bool wholeWordAt(std::string_view text, usize at, std::string_view name) {
    return text.substr(at, name.size()) == name && (at == 0 || !identChar(text[at - 1])) &&
           (at + name.size() >= text.size() || !identChar(text[at + name.size()]));
}

// 선언 이름의 자리 — 선언 마디 자리 (키워드 · export 앞일 수 있다) 부터 그 이름 낱말
[[nodiscard]] std::optional<usize> declNameAt(std::string_view text, usize from, std::string_view name) {
    constexpr usize kWindow = 512;
    for (usize i = from; i < text.size() && i < from + kWindow;) {
        const usize f = text.find(name, i);
        if (f == std::string_view::npos || f >= from + kWindow) {
            break;
        }
        if (wholeWordAt(text, f, name)) {
            return f;
        }
        i = f + 1;
    }
    return std::nullopt;
}

[[nodiscard]] bool indexable(const Symbol& s) noexcept {
    switch (s.kind) {
    case SymbolKind::Local:
    case SymbolKind::Param:
    case SymbolKind::ModuleConst:
    case SymbolKind::Function:
    case SymbolKind::Type:
    case SymbolKind::Enum:
    case SymbolKind::Component:
        return s.decl != kNoNode;
    case SymbolKind::Builtin:
        return false;
    }
    return false;
}

struct Index {
    std::shared_ptr<const AnalyzedProgram> analyzed;
    const Program* prog = nullptr;
    std::vector<i32> fileOfModule; // 모듈 → q.files 의 자리 (없으면 -1)
};

[[nodiscard]] Index makeIndex(const LanguageQuery& q) {
    Index ix;
    ix.analyzed = analyzeCached(q.files, q.imports);
    ix.prog = &ix.analyzed->program;
    ix.fileOfModule.assign(ix.prog->modules.size(), -1);
    for (usize mi = 0; mi < ix.prog->modules.size(); ++mi) {
        for (usize fi = 0; fi < q.files.size(); ++fi) {
            if (q.files[fi].path == ix.prog->modules[mi].path) {
                ix.fileOfModule[mi] = static_cast<i32>(fi);
            }
        }
    }
    return ix;
}

// import 마디의 대상 모듈 — 검사기의 resolveImports 와 같은 규칙 (팩 안 상대 · 의존 팩 "@팩/…"). "sandbox" · 없음은 -1
[[nodiscard]] i32 importTarget(const Program& p, const ModuleSema& m, NodeId item) {
    const std::string_view spec = m.ast.str(item);
    std::string path;
    if (spec == "sandbox") {
        return -1;
    }
    if (spec.starts_with("@")) {
        const usize slash = spec.find('/');
        if (slash == std::string_view::npos) {
            return -1;
        }
        const std::string local = resolveImportPath("scripts/index.ts", std::format("./{}", spec.substr(slash + 1)));
        path = local.empty() ? std::string() : std::format("{}/{}", spec.substr(0, slash), local);
    } else {
        const std::string_view pack = foreignPackOf(m.path);
        const std::string prefix = pack.empty() ? std::string() : std::format("@{}/", pack);
        const std::string local = resolveImportPath(std::string_view(m.path).substr(prefix.size()), spec);
        path = local.empty() ? std::string() : prefix + local;
    }
    for (usize i = 0; i < p.modules.size(); ++i) {
        if (p.modules[i].path == path) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

// import { X } 의 X 가 가리키는 기호 (값 먼저, 없으면 타입)
[[nodiscard]] SymbolId importedSymbol(const Program& p, const ModuleSema& m, NodeId item, NodeId nameNode) {
    const i32 t = importTarget(p, m, item);
    if (t < 0) {
        return kNoSymbol;
    }
    const ModuleSema& from = p.modules[static_cast<usize>(t)];
    const std::string_view name = m.ast.str(nameNode);
    for (const auto& [n, s] : from.exportValues) {
        if (n == name) {
            return s;
        }
    }
    for (const auto& [n, s] : from.exportTypes) {
        if (n == name) {
            return s;
        }
    }
    return kNoSymbol;
}

// 타입 자리 이름의 기호 — 그 타입을 가진 Type · Enum 기호 (같은 이름)
[[nodiscard]] SymbolId typeSymbol(const Program& p, std::string_view name, TypeId type) {
    for (SymbolId s = 0; s < p.symbols.size(); ++s) {
        const Symbol& sym = p.symbols[s];
        if ((sym.kind == SymbolKind::Type || sym.kind == SymbolKind::Enum) && sym.name == name && sym.type == type) {
            return s;
        }
    }
    return kNoSymbol;
}

[[nodiscard]] std::optional<SourceRange> declarationOf(const LanguageQuery& q, const Index& ix, SymbolId s) {
    const Symbol& sym = ix.prog->symbols[s];
    if (sym.module >= ix.fileOfModule.size() || ix.fileOfModule[sym.module] < 0 || sym.decl == kNoNode) {
        return std::nullopt;
    }
    const auto fi = static_cast<usize>(ix.fileOfModule[sym.module]);
    const ModuleSema& m = ix.prog->modules[sym.module];
    const auto at = declNameAt(q.files[fi].text, m.ast.node(sym.decl).pos.offset, sym.name);
    if (!at) {
        return std::nullopt;
    }
    return SourceRange{fi, *at, *at + sym.name.size()};
}

// 커서의 기호
[[nodiscard]] SymbolId symbolAt(const LanguageQuery& q, const Index& ix) {
    if (q.file >= q.files.size()) {
        return kNoSymbol;
    }
    const SourceFile& doc = q.files[q.file];
    const auto [begin, end] = wordAround(doc.text, q.offset);
    if (begin == end) {
        return kNoSymbol;
    }
    const std::string_view word = std::string_view(doc.text).substr(begin, end - begin);
    const Program& p = *ix.prog;
    u32 mi = 0;
    for (; mi < p.modules.size(); ++mi) {
        if (p.modules[mi].path == doc.path) {
            break;
        }
    }
    if (mi == p.modules.size() || !p.modules[mi].parsed) {
        return kNoSymbol;
    }
    const ModuleSema& m = p.modules[mi];
    for (NodeId n = 0; n < m.ast.size(); ++n) {
        const Node& x = m.ast.node(n);
        if (x.pos.offset != begin || m.ast.str(n) != word) {
            continue;
        }
        if (x.kind == NodeKind::Ident && m.nodes[n].ref == RefKind::Symbol && m.nodes[n].refIndex < p.symbols.size()) {
            return indexable(p.symbols[m.nodes[n].refIndex]) ? m.nodes[n].refIndex : kNoSymbol;
        }
        if (x.kind == NodeKind::TypeName) {
            return typeSymbol(p, word, m.nodes[n].type);
        }
    }
    // import { X } 의 X
    for (const NodeId item : m.ast.kids(m.ast.root)) {
        if (m.ast.node(item).kind != NodeKind::Import) {
            continue;
        }
        for (const NodeId nameNode : m.ast.kids(item)) {
            if (m.ast.node(nameNode).pos.offset == begin && m.ast.str(nameNode) == word) {
                return importedSymbol(p, m, item, nameNode);
            }
        }
    }
    // 선언 자리
    for (SymbolId s = 0; s < p.symbols.size(); ++s) {
        const Symbol& sym = p.symbols[s];
        if (sym.module == mi && sym.name == word && indexable(sym)) {
            if (const auto d = declarationOf(q, ix, s); d && d->file == q.file && d->begin == begin) {
                return s;
            }
        }
    }
    return kNoSymbol;
}

[[nodiscard]] std::vector<SourceRange> referencesOf(const LanguageQuery& q, const Index& ix, SymbolId s) {
    const Program& p = *ix.prog;
    const Symbol& sym = p.symbols[s];
    std::vector<SourceRange> uses;
    for (usize mi = 0; mi < p.modules.size(); ++mi) {
        const ModuleSema& m = p.modules[mi];
        if (ix.fileOfModule[mi] < 0 || !m.parsed) {
            continue;
        }
        const auto fi = static_cast<usize>(ix.fileOfModule[mi]);
        for (NodeId n = 0; n < m.ast.size(); ++n) {
            const Node& x = m.ast.node(n);
            const std::string_view name = m.ast.str(n);
            if (x.kind == NodeKind::Ident && m.nodes[n].ref == RefKind::Symbol && m.nodes[n].refIndex == s) {
                uses.push_back({fi, x.pos.offset, x.pos.offset + name.size()});
            } else if (x.kind == NodeKind::TypeName && name == sym.name &&
                       (sym.kind == SymbolKind::Type || sym.kind == SymbolKind::Enum) && m.nodes[n].type == sym.type) {
                uses.push_back({fi, x.pos.offset, x.pos.offset + name.size()});
            }
        }
        for (const NodeId item : m.ast.kids(m.ast.root)) {
            if (m.ast.node(item).kind != NodeKind::Import) {
                continue;
            }
            for (const NodeId nameNode : m.ast.kids(item)) {
                if (importedSymbol(p, m, item, nameNode) == s) {
                    const usize at = m.ast.node(nameNode).pos.offset;
                    uses.push_back({fi, at, at + m.ast.str(nameNode).size()});
                }
            }
        }
    }
    std::sort(uses.begin(), uses.end(), [](const SourceRange& a, const SourceRange& b) {
        return a.file != b.file ? a.file < b.file : a.begin < b.begin;
    });
    uses.erase(std::unique(uses.begin(), uses.end()), uses.end());
    std::vector<SourceRange> out;
    const std::optional<SourceRange> decl = declarationOf(q, ix, s);
    if (decl) {
        out.push_back(*decl);
    }
    for (const SourceRange& r : uses) {
        if (!decl || !(r == *decl)) {
            out.push_back(r);
        }
    }
    return out;
}

[[nodiscard]] bool validName(std::string_view name) {
    DiagnosticList d;
    const std::vector<Token> toks = tokenize(name, "", d);
    return d.ok() && toks.size() == 2 && toks[0].kind == Tok::Identifier && toks[0].value == name &&
           toks[1].kind == Tok::EndOfFile;
}

// 글에 편집을 (뒤에서부터 — 자리가 밀리지 않게)
[[nodiscard]] std::string applyEdits(std::string text, std::vector<FileEdit> edits) {
    std::sort(edits.begin(), edits.end(), [](const FileEdit& a, const FileEdit& b) { return a.begin > b.begin; });
    for (const FileEdit& e : edits) {
        text.replace(e.begin, e.end - e.begin, e.text);
    }
    return text;
}

// spec 에서 name 을 가져오는 편집 — 같은 spec 의 import 가 있으면 그 { } 안 끝에, 없으면 파일 맨 위 (다른 import 뒤) 에
// 새 줄
[[nodiscard]] FileEdit importEdit(const LanguageQuery& q, const ModuleSema& m, std::string_view spec,
                                  std::string_view name) {
    const std::string_view text = q.files[q.file].text;
    usize lastImportEnd = 0;
    for (const NodeId item : m.ast.kids(m.ast.root)) {
        if (m.ast.node(item).kind != NodeKind::Import) {
            continue;
        }
        const usize at = m.ast.node(item).pos.offset;
        const usize lineEnd = text.find('\n', at);
        lastImportEnd = lineEnd == std::string_view::npos ? text.size() : lineEnd + 1;
        if (m.ast.str(item) == spec) {
            const usize close = text.find('}', at);
            if (close != std::string_view::npos) {
                usize b = close;
                while (b > at && (text[b - 1] == ' ' || text[b - 1] == '\t')) {
                    --b;
                }
                return FileEdit{q.file, b, b, std::format(", {}", name)};
            }
        }
    }
    return FileEdit{q.file, lastImportEnd, lastImportEnd, std::format("import {{ {} }} from \"{}\"\n", name, spec)};
}

} // namespace

usize editDistance(std::string_view a, std::string_view b) {
    std::vector<usize> prev(b.size() + 1);
    std::vector<usize> cur(b.size() + 1);
    for (usize j = 0; j <= b.size(); ++j) {
        prev[j] = j;
    }
    for (usize i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (usize j = 1; j <= b.size(); ++j) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u)});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::string relativeImportSpec(std::string_view from, std::string_view to) {
    const auto dirs = [](std::string_view p) {
        std::vector<std::string_view> out;
        usize s = 0;
        for (usize i = 0; i < p.size(); ++i) {
            if (p[i] == '/') {
                out.push_back(p.substr(s, i - s));
                s = i + 1;
            }
        }
        return out;
    };
    const std::vector<std::string_view> a = dirs(from);
    const std::vector<std::string_view> b = dirs(to);
    usize common = 0;
    while (common < a.size() && common < b.size() && a[common] == b[common]) {
        ++common;
    }
    std::string out;
    for (usize i = common; i < a.size(); ++i) {
        out += "../";
    }
    if (out.empty()) {
        out = "./";
    }
    for (usize i = common; i < b.size(); ++i) {
        out += std::string(b[i]) + "/";
    }
    std::string_view file = to.substr(to.rfind('/') == std::string_view::npos ? 0 : to.rfind('/') + 1);
    if (file.ends_with(".ts")) {
        file.remove_suffix(3);
    }
    return out + std::string(file);
}

std::optional<SourceRange> findDefinition(const LanguageQuery& q) {
    const Index ix = makeIndex(q);
    const SymbolId s = symbolAt(q, ix);
    if (s == kNoSymbol) {
        return std::nullopt;
    }
    return declarationOf(q, ix, s);
}

std::vector<SourceRange> findReferences(const LanguageQuery& q) {
    const Index ix = makeIndex(q);
    const SymbolId s = symbolAt(q, ix);
    if (s == kNoSymbol) {
        return {};
    }
    return referencesOf(q, ix, s);
}

RenameResult renameSymbol(const LanguageQuery& q, std::string_view newName) {
    RenameResult r;
    const Index ix = makeIndex(q);
    const SymbolId s = symbolAt(q, ix);
    if (s == kNoSymbol) {
        r.error = "이름을 바꿀 기호가 아니다 (지역 · 매개변수 · 함수 · 상수 · 컴포넌트 · enum · interface · type 이름 "
                  "위에서)";
        return r;
    }
    const Symbol& sym = ix.prog->symbols[s];
    if (!validName(newName)) {
        r.error = std::format("'{}' 는 이름으로 쓸 수 없다 (글자 · 숫자 · _ · $, 숫자로 시작하지 않는다, 예약어 아님)",
                              newName);
        return r;
    }
    if (newName == sym.name) {
        return r; // 바꿀 것이 없다
    }
    if (foreignPackOf(ix.prog->modules[sym.module].path).size() > 0) {
        r.error = std::format("'{}' 는 의존 팩 ({}) 의 이름이다 — 그 팩에서 바꾼다", sym.name,
                              ix.prog->modules[sym.module].path);
        return r;
    }
    for (const SourceRange& at : referencesOf(q, ix, s)) {
        // as 로 다른 이름을 붙여 쓰는 자리 (그 지역 이름) 는 그대로
        if (std::string_view(q.files[at.file].text).substr(at.begin, at.end - at.begin) == sym.name) {
            r.edits.push_back({at.file, at.begin, at.end, std::string(newName)});
        }
    }
    std::sort(r.edits.begin(), r.edits.end(), [](const FileEdit& a, const FileEdit& b) {
        return a.file != b.file ? a.file < b.file : a.begin < b.begin;
    });
    // 바꾼 글을 검사 — 오류가 늘면 (같은 범위의 다른 이름과 부딪힌다 · 가린다) 거절
    std::vector<SourceFile> files(q.files.begin(), q.files.end());
    for (usize fi = 0; fi < files.size(); ++fi) {
        std::vector<FileEdit> mine;
        for (const FileEdit& e : r.edits) {
            if (e.file == fi) {
                mine.push_back(e);
            }
        }
        if (!mine.empty()) {
            files[fi].text = applyEdits(std::move(files[fi].text), std::move(mine));
        }
    }
    const auto after = analyzeCached(files, q.imports);
    if (after->diagnostics.errorCount() > ix.analyzed->diagnostics.errorCount()) {
        for (const Diagnostic& d : after->diagnostics.items()) {
            const bool old =
                std::any_of(ix.analyzed->diagnostics.items().begin(), ix.analyzed->diagnostics.items().end(),
                            [&](const Diagnostic& o) { return o.code == d.code && o.message == d.message; });
            if (!old && d.severity == Diagnostic::Severity::Error) {
                r.error = std::format("'{}' 로 바꾸면 이름이 부딪힌다 — {}", newName, d.describe());
                break;
            }
        }
        if (r.error.empty()) {
            r.error = std::format("'{}' 로 바꾸면 검사 오류가 는다", newName);
        }
        r.edits.clear();
    }
    return r;
}

std::vector<QuickFix> quickFixes(const LanguageQuery& q) {
    std::vector<QuickFix> out;
    if (q.file >= q.files.size()) {
        return out;
    }
    const Index ix = makeIndex(q);
    const Program& p = *ix.prog;
    const SourceFile& doc = q.files[q.file];
    const auto [begin, end] = wordAround(doc.text, q.offset);
    if (begin == end) {
        return out;
    }
    const std::string name(std::string_view(doc.text).substr(begin, end - begin));
    const bool unknown = std::any_of(
        ix.analyzed->diagnostics.items().begin(), ix.analyzed->diagnostics.items().end(),
        [&](const Diagnostic& d) { return d.code == "S401" && d.file == doc.path && d.pos.offset == begin; });
    u32 mi = 0;
    for (; mi < p.modules.size(); ++mi) {
        if (p.modules[mi].path == doc.path) {
            break;
        }
    }
    if (!unknown || mi == p.modules.size() || !p.modules[mi].parsed) {
        return out;
    }
    const ModuleSema& m = p.modules[mi];
    // import 더하기 — "sandbox" 의 이름, 팩의 다른 파일이 내보내는 이름 (의존 팩은 "@팩/…")
    const std::vector<std::string> sdk = sandboxExportNames();
    if (std::find(sdk.begin(), sdk.end(), name) != sdk.end()) {
        out.push_back({std::format("import {{ {} }} from \"sandbox\"", name), {importEdit(q, m, "sandbox", name)}});
    }
    for (usize oi = 0; oi < p.modules.size(); ++oi) {
        const ModuleSema& o = p.modules[oi];
        if (oi == mi || !o.parsed) {
            continue;
        }
        const bool exports =
            std::any_of(o.exportValues.begin(), o.exportValues.end(), [&](const auto& e) { return e.first == name; }) ||
            std::any_of(o.exportTypes.begin(), o.exportTypes.end(), [&](const auto& e) { return e.first == name; });
        if (!exports) {
            continue;
        }
        std::string spec;
        if (const std::string_view pack = foreignPackOf(o.path); !pack.empty()) {
            std::string_view rest = std::string_view(o.path).substr(pack.size() + 2); // "@inv/" 뒤 — "scripts/a.ts"
            if (!rest.starts_with("scripts/")) {
                continue;
            }
            rest.remove_prefix(8);
            if (rest.ends_with(".ts")) {
                rest.remove_suffix(3);
            }
            spec = std::format("@{}/{}", pack, rest);
        } else if (foreignPackOf(m.path).empty()) {
            spec = relativeImportSpec(m.path, o.path);
        } else {
            continue;
        }
        out.push_back({std::format("import {{ {} }} from \"{}\"", name, spec), {importEdit(q, m, spec, name)}});
    }
    // 비슷한 이름 — 이 모듈의 기호 · 가져온 이름 · 전역
    std::vector<std::string> names;
    for (const Symbol& s : p.symbols) {
        if (s.module == mi && indexable(s)) {
            names.push_back(s.name);
        }
    }
    for (const NodeId item : m.ast.kids(m.ast.root)) {
        if (m.ast.node(item).kind == NodeKind::Import) {
            for (const NodeId nameNode : m.ast.kids(item)) {
                const NodeId alias = m.ast.kid(nameNode, 0);
                names.emplace_back(alias == kNoNode ? m.ast.str(nameNode) : m.ast.str(alias));
            }
        }
    }
    for (const char* g : {"Math", "NaN", "Infinity"}) {
        names.emplace_back(g);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::vector<std::pair<usize, std::string>> near;
    for (std::string& n : names) {
        const usize d = editDistance(name, n);
        if (d > 0 && d <= 2 && n.size() + 2 > name.size() / 2) {
            near.emplace_back(d, std::move(n));
        }
    }
    std::sort(near.begin(), near.end());
    for (usize i = 0; i < near.size() && i < 3; ++i) {
        out.push_back({std::format("'{}' 로 바꾸기", near[i].second), {{q.file, begin, end, near[i].second}}});
    }
    return out;
}

} // namespace sbx::script
