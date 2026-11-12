#include "editor/services/ScriptAssist.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <utility>

#include "core/content/ContentDatabase.hpp"
#include "core/content/ContentOverlay.hpp"
#include "editor/services/DocumentService.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/text/TextPosition.hpp"
#include "foundation/text/Utf8.hpp"
#include "script/lang/Checker.hpp"
#include "script/lang/ProgramCache.hpp"

namespace sbx::editor {

namespace {

[[nodiscard]] bool identByte(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
           u >= 0x80;
}

[[nodiscard]] bool lineBreak(char c) noexcept {
    return c == '\n' || c == '\r';
}

// 한 팩의 scripts/*.ts — 오버레이 소스가 있으면 그것 (콘텐츠 로더의 applyScriptOverlay 와 같다: 같은 경로는 바꾸고,
// 없으면 더한다). 경로 정렬
[[nodiscard]] std::map<std::string, std::string>
packScripts(const content::ContentDatabase& db, const content::ContentSourcePack& sp, const ScriptDrafts* drafts) {
    std::map<std::string, std::string> out;
    for (const content::ContentSourceFile& f : sp.files) {
        if (f.rel.starts_with("scripts/") && f.rel.ends_with(".ts")) {
            out[f.rel] = f.text;
        }
    }
    for (const auto& [id, value] : db.overlay().items(content::ContentKind::Script)) {
        const auto path = content::scriptOverlayPath(id);
        if (path && path->pack == sp.info.id && value.is_object() && value.contains("source") &&
            value["source"].is_string()) {
            out[path->rel] = value["source"].get<std::string>();
        }
    }
    if (drafts != nullptr) {
        for (const auto& [id, text] : *drafts) {
            if (const auto path = content::scriptOverlayPath(id); path && path->pack == sp.info.id) {
                out[path->rel] = text;
            }
        }
    }
    return out;
}

} // namespace

script::LanguageQuery ScriptWorkspace::query(usize offset) const noexcept {
    return script::LanguageQuery{files, file, offset, imports};
}

std::optional<ScriptWorkspace> scriptWorkspace(const content::ContentDatabase& db, std::string_view overlayId,
                                               std::string_view text, const ScriptDrafts* drafts) {
    const auto path = content::scriptOverlayPath(overlayId);
    if (!path || db.sources() == nullptr) {
        return std::nullopt;
    }
    const auto& packs = db.sources()->packs;
    const auto packOf = [&](std::string_view id) -> const content::ContentSourcePack* {
        const auto it = std::find_if(packs.begin(), packs.end(),
                                     [&](const content::ContentSourcePack& p) { return p.info.id == id; });
        return it != packs.end() ? &*it : nullptr;
    };
    const content::ContentSourcePack* own = packOf(path->pack);
    if (own == nullptr) {
        return std::nullopt;
    }
    ScriptWorkspace ws;
    std::map<std::string, std::string> scripts = packScripts(db, *own, drafts);
    scripts[path->rel] = std::string(text); // 편집 중인 글 (저장 · 적용 전)
    for (auto& [rel, source] : scripts) {
        if (rel == path->rel) {
            ws.file = ws.files.size();
        }
        ws.files.push_back(script::SourceFile{rel, std::move(source)});
    }
    // 의존 팩 (이어지는 의존까지) — 콘텐츠 로더의 compileScripts 와 같은 입력 (ADR-0075)
    ws.imports.push_back({std::string(), own->info.dependencies});
    std::vector<std::string> pending = own->info.dependencies;
    std::set<std::string> seen;
    while (!pending.empty()) {
        const std::string id = std::move(pending.back());
        pending.pop_back();
        const content::ContentSourcePack* dep = packOf(id);
        if (!seen.insert(id).second || dep == nullptr) {
            continue;
        }
        for (auto& [rel, source] : packScripts(db, *dep, drafts)) {
            ws.files.push_back(script::SourceFile{std::format("@{}/{}", id, rel), std::move(source)});
        }
        ws.imports.push_back({id, dep->info.dependencies});
        pending.insert(pending.end(), dep->info.dependencies.begin(), dep->info.dependencies.end());
    }
    return ws;
}

std::pair<usize, usize> markRange(std::string_view text, usize offset) noexcept {
    usize begin = std::min(offset, text.size());
    if (begin == text.size() || lineBreak(text[begin])) {
        // 줄 끝 · 글 끝 — 앞 글자 (빈 줄 · 빈 글이면 그 자리 하나)
        if (begin == 0 || lineBreak(text[begin - 1])) {
            return {begin, begin};
        }
        usize b = begin - 1;
        while (b > 0 && (static_cast<unsigned char>(text[b]) & 0xC0) == 0x80) {
            --b;
        }
        return {b, begin};
    }
    usize end = begin;
    if (identByte(text[begin])) {
        while (end < text.size() && identByte(text[end])) {
            ++end;
        }
        return {begin, end};
    }
    (void)utf8::decodeNext(text, end); // 글자 하나
    return {begin, end};
}

std::vector<ScriptMark> analyzeScript(const ScriptWorkspace& ws) {
    std::vector<ScriptMark> out;
    if (ws.file >= ws.files.size()) {
        return out;
    }
    const auto analyzed =
        script::analyzeCached(ws.files, ws.imports); // hover · 완성과 같은 글이면 다시 검사하지 않는다
    const script::DiagnosticList& diags = analyzed->diagnostics;
    const script::SourceFile& doc = ws.files[ws.file];
    for (const script::Diagnostic& d : diags.items()) {
        if (d.file != doc.path) {
            continue; // 다른 파일의 진단은 그 파일을 열었을 때
        }
        const auto [begin, end] = markRange(doc.text, d.pos.offset);
        std::string text =
            std::format("{}:{}: {} {} — {}", d.pos.line, d.pos.column,
                        d.severity == script::Diagnostic::Severity::Error ? "오류" : "경고", d.code, d.message);
        if (!d.help.empty()) {
            text += "\n고치는 법: " + d.help;
        }
        out.push_back(
            ScriptMark{begin, end, d.severity == script::Diagnostic::Severity::Error, d.code, std::move(text)});
    }
    return out;
}

ScriptSaveResult saveScriptFile(services::DocumentService& docs, const std::filesystem::path& path,
                                std::string_view text, std::optional<std::string_view> base, bool force) {
    namespace fs = std::filesystem;
    ScriptSaveResult out;
    const std::string where = io::displayPath(path);
    std::error_code ec;
    const bool exists = fs::is_regular_file(path, ec);
    std::string disk;
    if (exists) {
        Expected<std::string> bytes = io::readFile(path);
        if (!bytes) {
            out.message = bytes.error().describe();
            return out;
        }
        disk = std::move(*bytes);
    }
    if (!force) {
        if (base && exists && disk != *base) {
            out.status = ScriptSaveResult::Status::DiskChanged;
            out.message = std::format("{} — 바깥에서 파일이 바뀌었습니다 (덮어쓰지 않았다)", where);
            return out;
        }
        if (base && !exists) {
            out.status = ScriptSaveResult::Status::DiskChanged;
            out.message = std::format("{} — 바깥에서 파일이 없어졌습니다 (덮어쓰면 다시 만든다)", where);
            return out;
        }
    }
    // 줄 끝 — 파일이 쓰던 것 (편집기가 넣은 LF 를 되돌린다)
    out.written = text::applyLineEnding(text, text::detectLineEnding(base ? *base : std::string_view(disk)));
    if (!exists) {
        fs::create_directories(path.parent_path(), ec);
        if (Expected<void> w = io::writeFileAtomic(path, {}); !w) {
            out.message = w.error().describe();
            return out;
        }
    }
    const Expected<services::DocId> id = docs.open(path);
    if (!id) {
        out.message = id.error().describe();
        return out;
    }
    // 열려 있던 문서는 그때의 디스크를 안다 — 디스크는 위에서 base 와 대조했으니 지금 것으로 맞춘다
    if (docs.diskState(*id) != services::DiskState::Same) {
        if (Expected<void> r = docs.reload(*id); !r) {
            out.message = r.error().describe();
            return out;
        }
    }
    if (Expected<void> s = docs.setText(*id, out.written, "스크립트 저장"); !s) {
        out.message = s.error().describe();
        return out;
    }
    if (Expected<void> s = docs.save(*id); !s) {
        out.message = s.error().describe();
        return out;
    }
    out.status = ScriptSaveResult::Status::Saved;
    out.message = std::format("{} 에 저장했습니다", where);
    return out;
}

// --- 분석 ---

void ScriptAssist::edited(f64 now) noexcept {
    ++m_version;
    m_lastEdit = now;
    m_hoverOffset.reset();
}

void ScriptAssist::reset() noexcept {
    ++m_version;
    m_analyzedVersion = m_version;
    m_lastEdit.reset();
    m_analyzeNow = true;
    m_marks.clear();
    closeCompletion();
    m_hoverOffset.reset();
}

bool ScriptAssist::analysisDue(f64 now) const noexcept {
    if (m_analyzeNow) {
        return true;
    }
    return m_lastEdit.has_value() && now - *m_lastEdit >= kAnalysisDelay;
}

bool ScriptAssist::analyzed(u64 version, std::vector<ScriptMark> marks) {
    if (version != m_version) {
        return false; // 그사이 고쳤다 — 늦은 결과 (L3)
    }
    m_marks = std::move(marks);
    m_analyzedVersion = version;
    m_lastEdit.reset();
    m_analyzeNow = false;
    return true;
}

const ScriptMark* ScriptAssist::markAt(usize offset) const noexcept {
    for (const ScriptMark& m : m_marks) {
        if (offset >= m.begin && (offset < m.end || (m.begin == m.end && offset == m.begin))) {
            return &m;
        }
    }
    return nullptr;
}

// --- 완성 ---

void ScriptAssist::openCompletion(const ScriptWorkspace& ws, usize cursor) {
    closeCompletion();
    if (ws.file >= ws.files.size()) {
        return;
    }
    const std::string_view text = ws.files[ws.file].text;
    cursor = std::min(cursor, text.size());
    const usize anchor = script::completionWordStart(text, cursor);
    // 접두 없이 먼저 묻는다 ('.' 뒤 · import { } 안은 접두 없이도 모든 후보) — 없으면 접두로 (이름 완성은 접두가 있어야
    // 한다)
    m_all = script::complete(ws.query(anchor));
    m_basePrefix.clear();
    m_dropExact = false;
    if (m_all.empty() && anchor < cursor) {
        m_all = script::complete(ws.query(cursor));
        m_basePrefix = std::string(text.substr(anchor, cursor - anchor));
        m_dropExact = true;
    }
    m_anchor = anchor;
    m_open = true;
    refilter(text.substr(anchor, cursor - anchor));
}

bool ScriptAssist::refreshCompletion(std::string_view text, usize cursor) {
    if (!m_open) {
        return false;
    }
    if (cursor > text.size() || cursor < m_anchor || script::completionWordStart(text, cursor) != m_anchor) {
        closeCompletion(); // 이름을 벗어났다 (이름 밖 글자를 쳤다 · 커서를 옮겼다)
        return false;
    }
    const std::string_view prefix = text.substr(m_anchor, cursor - m_anchor);
    if (prefix.size() < m_basePrefix.size()) {
        closeCompletion();
        return true; // 처음 물은 접두보다 짧다 — 다시 묻는다
    }
    refilter(prefix);
    return false;
}

void ScriptAssist::refilter(std::string_view prefix) {
    m_shown = m_all;
    script::filterCompletions(m_shown, prefix);
    if (m_dropExact) {
        std::erase_if(m_shown, [&](const script::CompletionItem& c) { return c.label == prefix; });
    }
    if (m_shown.empty()) {
        closeCompletion();
        return;
    }
    m_selected = std::min(m_selected, m_shown.size() - 1);
}

void ScriptAssist::closeCompletion() noexcept {
    m_open = false;
    m_all.clear();
    m_shown.clear();
    m_selected = 0;
    m_basePrefix.clear();
}

void ScriptAssist::moveSelection(i32 delta) noexcept {
    if (!m_open || m_shown.empty()) {
        return;
    }
    const auto n = static_cast<i64>(m_shown.size());
    const i64 next = ((static_cast<i64>(m_selected) + delta) % n + n) % n;
    m_selected = static_cast<usize>(next);
}

std::optional<TextReplace> ScriptAssist::acceptCompletion(std::string_view text, usize cursor) {
    if (!m_open || m_selected >= m_shown.size() || cursor < m_anchor) {
        closeCompletion();
        return std::nullopt;
    }
    const script::CompletionItem& c = m_shown[m_selected];
    std::string insert = c.insertText.empty() ? c.label : c.insertText;
    if (!c.insertText.empty()) {
        // 넣는 줄의 들여쓰기를 둘째 줄부터
        usize lineStart = std::min(m_anchor, text.size());
        while (lineStart > 0 && text[lineStart - 1] != '\n') {
            --lineStart;
        }
        usize indentEnd = lineStart;
        while (indentEnd < text.size() && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) {
            ++indentEnd;
        }
        const std::string indent(text.substr(lineStart, indentEnd - lineStart));
        for (usize at = insert.find('\n'); at != std::string::npos; at = insert.find('\n', at + 1 + indent.size())) {
            insert.insert(at + 1, indent);
        }
    }
    TextReplace r{m_anchor, cursor, std::move(insert)};
    closeCompletion();
    return r;
}

// --- hover ---

const std::optional<script::HoverInfo>* ScriptAssist::cachedHover(usize offset) const noexcept {
    return m_hoverOffset == offset && m_hoverVersion == m_version ? &m_hover : nullptr;
}

void ScriptAssist::storeHover(usize offset, std::optional<script::HoverInfo> info) {
    m_hoverOffset = offset;
    m_hoverVersion = m_version;
    m_hover = std::move(info);
}

} // namespace sbx::editor
