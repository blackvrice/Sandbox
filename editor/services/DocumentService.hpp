#pragma once
// 파일 문서 (v0.8 ST-06-1 (2) — docs/25-STUDIO-ARCHITECTURE.md 3장, ADR-0089 2 · ADR-0109). 팩 JSON · 스크립트 · UI
// 문서 · 에셋 메타를 열고 · 고치고 · 저장한다. UI 코드는 파일을 직접 쓰지 않는다 — 이 서비스를 부른다.
//
// 편집: 바이트 범위 바꾸기 {offset, 지울 길이, 넣을 글} — 범위가 글 밖이거나 UTF-8 글자 가운데면 거절. 편집마다
// 되돌리기 한 칸 (UndoService 의 Document 칸 — tag = DocId). 문서마다 저장한 글을 기억한다 (dirty = 지금 글 ≠ 저장한
// 글). 저장 (차례): 검사 (확장자마다 — .json 은 JSON 으로 읽혀야 한다, 그 밖은 setValidator — 실패면 쓰지 않는다) →
// 바깥 변경 (열거나 마지막으로 저장 · 다시 읽은 때의 디스크 바이트 SHA-256 과 지금 디스크가 다르거나 파일이 없어졌으면
// 거절 — 덮어쓰지 않는다, force 면 쓴다) → 원자적 쓰기 (임시 파일 → 이름 바꾸기 — io::writeFileAtomic). 다시 읽기
// (reload) · 닫기 (close) 는 그 문서의 되돌리기 칸을 지운다. 고친 채로 닫기는 force 가 아니면 거절. 같은 경로 (정규화한
// 절대 경로) 를 두 번 열면 같은 문서.

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor/services/UndoService.hpp"
#include "foundation/hash/Sha256.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::editor::services {

using DocId = u32;

struct TextEdit {
    usize offset = 0;     // 바이트
    usize remove = 0;     // 지울 바이트 수
    std::string insert{}; // 넣을 글 (UTF-8)
};

enum class DiskState : u8 { Same, Changed, Missing };

// 저장 전 검사 — 문제면 이유 (nullopt = 통과)
using DocumentValidator =
    std::function<std::optional<std::string>(const std::filesystem::path& path, std::string_view text)>;

class DocumentService final : public IUndoParticipant {
public:
    static constexpr usize kMaxEdits = UndoService::kMaxSteps;

    explicit DocumentService(UndoService* undo = nullptr);
    ~DocumentService() override;
    DocumentService(const DocumentService&) = delete;
    DocumentService& operator=(const DocumentService&) = delete;

    Expected<DocId> open(const std::filesystem::path& path);
    Expected<void> apply(DocId id, const TextEdit& edit, std::string label = {});
    Expected<void> setText(DocId id, std::string text, std::string label = {}); // 전체 바꾸기 (편집 한 칸)
    Expected<void> save(DocId id, bool force = false);
    Expected<void> reload(DocId id); // 디스크의 것으로 (고친 것 · 되돌리기 칸을 버린다)
    Expected<void> close(DocId id, bool force = false);
    [[nodiscard]] DiskState diskState(DocId id) const;
    // 확장자 (".json" · ".ts" …, 소문자) 의 저장 전 검사 — 같은 확장자는 바꾼다 (.json 의 기본 검사도)
    void setValidator(std::string extension, DocumentValidator validator);

    [[nodiscard]] bool isOpen(DocId id) const { return m_docs.contains(id); }
    [[nodiscard]] const std::string& text(DocId id) const;
    [[nodiscard]] const std::filesystem::path& path(DocId id) const;
    [[nodiscard]] bool dirty(DocId id) const;
    [[nodiscard]] std::vector<DocId> openDocuments() const;
    [[nodiscard]] std::optional<DocId> find(const std::filesystem::path& path) const;

    // IUndoParticipant (UndoService 가 부른다)
    UndoResult undoLast() override;
    UndoResult redoLast() override;
    void dropRedo() override { m_redo.clear(); }

private:
    struct Document {
        std::filesystem::path path;
        std::string text;
        std::string saved;   // 마지막으로 저장 · 읽은 글 (dirty 비교)
        Sha256Digest disk{}; // 그때의 디스크 바이트
    };
    struct Edit {
        DocId doc = 0;
        usize offset = 0;
        std::string removed;
        std::string inserted;
    };
    Document* doc(DocId id);
    [[nodiscard]] const Document* doc(DocId id) const;
    void replace(Document& d, usize offset, usize remove, std::string_view insert);
    void forget(DocId id); // 그 문서의 되돌리기 칸 (내 더미 · UndoService)

    UndoService* m_undo;
    std::map<DocId, Document> m_docs;
    DocId m_next = 1;
    std::vector<Edit> m_edits; // 되돌리기 더미 (UndoService 의 Document 칸과 같은 차례)
    std::vector<Edit> m_redo;
    std::map<std::string, DocumentValidator, std::less<>> m_validators;
};

} // namespace sbx::editor::services
