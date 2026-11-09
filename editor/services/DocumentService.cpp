#include "editor/services/DocumentService.hpp"

#include <algorithm>
#include <format>
#include <system_error>

#include <nlohmann/json.hpp>

#include "foundation/io/FileIo.hpp"

namespace sbx::editor::services {
namespace {

namespace fs = std::filesystem;

// 같은 파일을 같은 키로 (있으면 정규 경로, 없으면 절대 경로 정리)
fs::path keyOf(const fs::path& path) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(path, ec);
    if (ec) {
        p = fs::absolute(path, ec).lexically_normal();
    }
    return p;
}

std::string lowerExtension(const fs::path& path) {
    std::string e = path.extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return e;
}

bool continuationByte(std::string_view s, usize at) {
    return at < s.size() && (static_cast<u8>(s[at]) & 0xC0u) == 0x80u;
}

const std::string kEmpty;
const fs::path kNoPath;

} // namespace

DocumentService::DocumentService(UndoService* undo) : m_undo(undo) {
    if (m_undo != nullptr) {
        m_undo->attach(UndoKind::Document, this);
    }
    // 팩 JSON · UI 문서 · 메타 — JSON 으로 읽혀야 저장한다 (내용 규칙은 sbx check · 콘텐츠 로더의 몫)
    m_validators.emplace(".json", [](const fs::path&, std::string_view text) -> std::optional<std::string> {
        const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded()) {
            return std::string("JSON 으로 읽히지 않는다");
        }
        return std::nullopt;
    });
}

DocumentService::~DocumentService() {
    if (m_undo != nullptr) {
        m_undo->clear(UndoKind::Document);
        m_undo->attach(UndoKind::Document, nullptr);
    }
}

void DocumentService::setValidator(std::string extension, DocumentValidator validator) {
    m_validators.insert_or_assign(std::move(extension), std::move(validator));
}

DocumentService::Document* DocumentService::doc(DocId id) {
    const auto it = m_docs.find(id);
    return it == m_docs.end() ? nullptr : &it->second;
}

const DocumentService::Document* DocumentService::doc(DocId id) const {
    const auto it = m_docs.find(id);
    return it == m_docs.end() ? nullptr : &it->second;
}

std::optional<DocId> DocumentService::find(const fs::path& path) const {
    const fs::path key = keyOf(path);
    for (const auto& [id, d] : m_docs) {
        if (d.path == key) {
            return id;
        }
    }
    return std::nullopt;
}

Expected<DocId> DocumentService::open(const fs::path& path) {
    if (const std::optional<DocId> existing = find(path)) {
        return *existing;
    }
    Expected<std::string> bytes = io::readFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const DocId id = m_next++;
    Document d;
    d.path = keyOf(path);
    d.disk = sha256(*bytes);
    d.saved = *bytes;
    d.text = std::move(*bytes);
    m_docs.emplace(id, std::move(d));
    return id;
}

const std::string& DocumentService::text(DocId id) const {
    const Document* d = doc(id);
    return d != nullptr ? d->text : kEmpty;
}

const fs::path& DocumentService::path(DocId id) const {
    const Document* d = doc(id);
    return d != nullptr ? d->path : kNoPath;
}

bool DocumentService::dirty(DocId id) const {
    const Document* d = doc(id);
    return d != nullptr && d->text != d->saved;
}

std::vector<DocId> DocumentService::openDocuments() const {
    std::vector<DocId> out;
    for (const auto& [id, d] : m_docs) {
        out.push_back(id);
    }
    return out;
}

void DocumentService::replace(Document& d, usize offset, usize remove, std::string_view insert) {
    d.text.replace(offset, remove, insert);
}

Expected<void> DocumentService::apply(DocId id, const TextEdit& edit, std::string label) {
    Document* d = doc(id);
    if (d == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("열린 문서 {} 가 없다", id));
    }
    if (edit.offset > d->text.size() || edit.remove > d->text.size() - edit.offset) {
        return makeError(ErrorCode::OutOfRange, std::format("편집 범위 {} + {} 가 글 ({} 바이트) 밖이다", edit.offset,
                                                            edit.remove, d->text.size()));
    }
    if (continuationByte(d->text, edit.offset) || continuationByte(d->text, edit.offset + edit.remove)) {
        return makeError(ErrorCode::InvalidArgument, "편집 범위가 UTF-8 글자 가운데다");
    }
    if (edit.remove == 0 && edit.insert.empty()) {
        return {}; // 바뀐 것 없음 — 칸을 만들지 않는다
    }
    m_edits.push_back({id, edit.offset, d->text.substr(edit.offset, edit.remove), edit.insert});
    if (m_edits.size() > kMaxEdits) {
        m_edits.erase(m_edits.begin());
    }
    m_redo.clear();
    replace(*d, edit.offset, edit.remove, edit.insert);
    if (m_undo != nullptr) {
        m_undo->record(UndoKind::Document,
                       label.empty() ? std::format("{} 고침", d->path.filename().generic_string()) : std::move(label),
                       id);
    }
    return {};
}

Expected<void> DocumentService::setText(DocId id, std::string text, std::string label) {
    const Document* d = doc(id);
    if (d == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("열린 문서 {} 가 없다", id));
    }
    if (d->text == text) {
        return {};
    }
    return apply(id, {0, d->text.size(), std::move(text)}, std::move(label));
}

UndoResult DocumentService::undoLast() {
    if (m_edits.empty()) {
        return UndoResult::Refused;
    }
    Edit e = std::move(m_edits.back());
    m_edits.pop_back();
    if (Document* d = doc(e.doc)) {
        replace(*d, e.offset, e.inserted.size(), e.removed);
    }
    m_redo.push_back(std::move(e));
    return UndoResult::Done;
}

UndoResult DocumentService::redoLast() {
    if (m_redo.empty()) {
        return UndoResult::Refused;
    }
    Edit e = std::move(m_redo.back());
    m_redo.pop_back();
    if (Document* d = doc(e.doc)) {
        replace(*d, e.offset, e.removed.size(), e.inserted);
    }
    m_edits.push_back(std::move(e));
    return UndoResult::Done;
}

void DocumentService::forget(DocId id) {
    const auto of = [id](const Edit& e) { return e.doc == id; };
    std::erase_if(m_edits, of);
    std::erase_if(m_redo, of);
    if (m_undo != nullptr) {
        m_undo->clear(UndoKind::Document, id);
    }
}

DiskState DocumentService::diskState(DocId id) const {
    const Document* d = doc(id);
    if (d == nullptr) {
        return DiskState::Missing;
    }
    std::error_code ec;
    if (!fs::exists(d->path, ec)) {
        return DiskState::Missing;
    }
    const Expected<std::string> bytes = io::readFile(d->path);
    if (!bytes) {
        return DiskState::Missing;
    }
    return sha256(*bytes) == d->disk ? DiskState::Same : DiskState::Changed;
}

Expected<void> DocumentService::save(DocId id, bool force) {
    Document* d = doc(id);
    if (d == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("열린 문서 {} 가 없다", id));
    }
    const std::string where = io::displayPath(d->path);
    if (const auto it = m_validators.find(lowerExtension(d->path)); it != m_validators.end() && it->second) {
        if (const std::optional<std::string> why = it->second(d->path, d->text)) {
            return makeError(ErrorCode::ValidationFailed, std::format("저장하지 않았다 — {}", *why), where);
        }
    }
    if (!force) {
        switch (diskState(id)) {
        case DiskState::Same:
            break;
        case DiskState::Changed:
            return makeError(ErrorCode::AlreadyExists,
                             "바깥에서 파일이 바뀌었다 — 덮어쓰지 않았다 (다시 읽거나 강제 저장)", where);
        case DiskState::Missing:
            return makeError(ErrorCode::NotFound,
                             "바깥에서 파일이 없어졌다 — 덮어쓰지 않았다 (강제 저장하면 다시 만든다)", where);
        }
    }
    if (Expected<void> w = io::writeFileAtomic(d->path, d->text); !w) {
        return w;
    }
    d->saved = d->text;
    d->disk = sha256(d->text);
    return {};
}

Expected<void> DocumentService::reload(DocId id) {
    Document* d = doc(id);
    if (d == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("열린 문서 {} 가 없다", id));
    }
    Expected<std::string> bytes = io::readFile(d->path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    d->disk = sha256(*bytes);
    d->saved = *bytes;
    d->text = std::move(*bytes);
    forget(id);
    return {};
}

Expected<void> DocumentService::close(DocId id, bool force) {
    const Document* d = doc(id);
    if (d == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("열린 문서 {} 가 없다", id));
    }
    if (!force && d->text != d->saved) {
        return makeError(ErrorCode::InvalidArgument, "저장하지 않은 고침이 있다 — 저장하거나 강제로 닫는다",
                         io::displayPath(d->path));
    }
    forget(id);
    m_docs.erase(id);
    return {};
}

} // namespace sbx::editor::services
