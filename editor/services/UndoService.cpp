#include "editor/services/UndoService.hpp"

#include <algorithm>

namespace sbx::editor::services {

void UndoService::attach(UndoKind kind, IUndoParticipant* participant) noexcept {
    m_participants[static_cast<usize>(kind)] = participant;
}

void UndoService::record(UndoKind kind, std::string label, u64 tag) {
    m_undo.push_back({kind, std::move(label), tag});
    if (m_undo.size() > kMaxSteps) {
        m_undo.erase(m_undo.begin());
        ++m_dropped;
    }
    if (!m_redo.empty()) {
        m_redo.clear();
        for (IUndoParticipant* p : m_participants) {
            if (p != nullptr) {
                p->dropRedo();
            }
        }
    }
}

bool UndoService::undo() {
    if (m_undo.empty()) {
        return false;
    }
    IUndoParticipant* p = m_participants[static_cast<usize>(m_undo.back().kind)];
    if (p == nullptr || !p->undoLast()) {
        return false;
    }
    m_redo.push_back(std::move(m_undo.back()));
    m_undo.pop_back();
    return true;
}

bool UndoService::redo() {
    if (m_redo.empty()) {
        return false;
    }
    IUndoParticipant* p = m_participants[static_cast<usize>(m_redo.back().kind)];
    if (p == nullptr || !p->redoLast()) {
        return false;
    }
    m_undo.push_back(std::move(m_redo.back()));
    m_redo.pop_back();
    return true;
}

void UndoService::clear(UndoKind kind) {
    const auto of = [kind](const UndoStep& s) { return s.kind == kind; };
    std::erase_if(m_undo, of);
    std::erase_if(m_redo, of);
}

void UndoService::clear(UndoKind kind, u64 tag) {
    const auto of = [kind, tag](const UndoStep& s) { return s.kind == kind && s.tag == tag; };
    std::erase_if(m_undo, of);
    std::erase_if(m_redo, of);
}

} // namespace sbx::editor::services
