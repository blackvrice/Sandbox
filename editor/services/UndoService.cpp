#include "editor/services/UndoService.hpp"

#include <algorithm>

namespace sbx::editor::services {

void UndoService::attach(UndoKind kind, IUndoParticipant* participant) noexcept {
    m_participants[static_cast<usize>(kind)] = participant;
}

void UndoService::dropAllRedo() {
    m_redo.clear();
    ++m_redoEpoch;
    for (IUndoParticipant* p : m_participants) {
        if (p != nullptr) {
            p->dropRedo();
        }
    }
}

void UndoService::record(UndoKind kind, std::string label, u64 tag) {
    m_undo.push_back({kind, std::move(label), tag});
    if (m_undo.size() > kMaxSteps) {
        m_undo.erase(m_undo.begin());
        ++m_dropped;
    }
    // 진행 중인 되돌리기도 다시 하기 쪽으로 갈 칸이다 — 그것도 무르게 (epoch)
    if (!m_redo.empty() || (m_pending && m_pending->undoing)) {
        dropAllRedo();
    }
}

bool UndoService::step(bool undoing) {
    std::vector<UndoStep>& from = undoing ? m_undo : m_redo;
    if (from.empty() || m_pending) {
        return false;
    }
    IUndoParticipant* p = m_participants[static_cast<usize>(from.back().kind)];
    if (p == nullptr) {
        return false;
    }
    const UndoResult r = undoing ? p->undoLast() : p->redoLast();
    if (r == UndoResult::Refused) {
        return false;
    }
    UndoStep s = std::move(from.back());
    from.pop_back();
    if (r == UndoResult::Done) {
        (undoing ? m_redo : m_undo).push_back(std::move(s));
    } else {
        m_pending = Pending{std::move(s), undoing, m_undo.size(), m_redoEpoch};
    }
    return true;
}

bool UndoService::undo() {
    return step(true);
}

bool UndoService::redo() {
    return step(false);
}

void UndoService::finish(UndoKind kind, bool ok) {
    if (!m_pending || m_pending->step.kind != kind) {
        return;
    }
    Pending p = std::move(*m_pending);
    m_pending.reset();
    if (!ok) {
        return;
    }
    if (p.undoing) {
        if (p.redoEpoch == m_redoEpoch) {
            m_redo.push_back(std::move(p.step));
        }
    } else {
        m_undo.insert(m_undo.begin() + static_cast<std::ptrdiff_t>(std::min(p.at, m_undo.size())), std::move(p.step));
    }
}

void UndoService::clear(UndoKind kind) {
    const auto of = [kind](const UndoStep& s) { return s.kind == kind; };
    std::erase_if(m_undo, of);
    std::erase_if(m_redo, of);
    if (m_pending && m_pending->step.kind == kind) {
        m_pending.reset();
    }
}

void UndoService::clear(UndoKind kind, u64 tag) {
    const auto of = [kind, tag](const UndoStep& s) { return s.kind == kind && s.tag == tag; };
    std::erase_if(m_undo, of);
    std::erase_if(m_redo, of);
    if (m_pending && of(m_pending->step)) {
        m_pending.reset();
    }
}

void UndoService::dropOldest(UndoKind kind) {
    const auto it = std::ranges::find(m_undo, kind, &UndoStep::kind);
    if (it != m_undo.end()) {
        m_undo.erase(it);
    }
}

} // namespace sbx::editor::services
