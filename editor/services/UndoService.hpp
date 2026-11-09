#pragma once
// 하나의 되돌리기 줄 (v0.8 ST-06-1 (2) — docs/25-STUDIO-ARCHITECTURE.md 3장, ADR-0089 2 · ADR-0109). 사용자에게 Ctrl+Z
// 는 하나다 — 월드 편집 (SimCommand — editor/Undo) 과 파일 문서 편집 (DocumentService) 을 일어난 차례로 한 기록 줄에
// 둔다.
//
// 기록의 내용은 참여자 (IUndoParticipant — 종류마다 하나) 가 자기 더미에 갖는다. 서비스는 "다음에 누구의 것을 되돌리나"
// 의 차례만 갖는다: 참여자가 편집을 기록하면 record(종류, 이름, tag) 를 부르고, undo() 는 줄의 마지막 칸의 참여자에게
// undoLast() 를 시킨다. 참여자의 더미와 줄의 그 종류 칸은 같은 차례다 (record 와 undoLast · redoLast 가 짝).
//
// 새 기록은 다시 하기 줄을 비운다 — 모든 참여자에게 dropRedo(). 종류 · tag 로 지우기 (clear): 월드가 바뀌면 world 칸만
// (10 12D), 문서를 닫거나 다시 읽으면 그 문서 (tag = DocId) 의 칸만 — 참여자도 자기 더미에서 같은 칸을 지운다. 참여자가
// 지금 되돌릴 수 없으면 (월드 편집의 결과를 기다리는 중) undoLast 가 false — 줄은 그대로.

#include <optional>
#include <string>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::editor::services {

enum class UndoKind : u8 { World = 0, Document, Count };

// 참여자의 대답 (v0.8 ST-06-1 (3)): 했다 · 시작했다 (결과를 기다린다 — 끝나면 UndoService::finish) · 지금은 못 한다
enum class UndoResult : u8 { Done, Started, Refused };

class IUndoParticipant {
public:
    virtual ~IUndoParticipant() = default;
    // 자기 더미의 마지막 것 (줄의 그 종류 칸과 같다)
    virtual UndoResult undoLast() = 0;
    virtual UndoResult redoLast() = 0;
    virtual void dropRedo() = 0; // 다시 하기 더미를 비운다 (어느 참여자든 새 기록을 했다)
};

struct UndoStep {
    UndoKind kind = UndoKind::World;
    std::string label; // "배치" · "eco/units.json 고침" … (메뉴의 "되돌리기: …")
    u64 tag = 0;       // 종류 안의 묶음 (문서 = DocId, 월드 = 0)
};

class UndoService {
public:
    // 넘으면 가장 오래된 칸을 버린다 (참여자에게 알리지 않는다 — 그 칸은 영영 되돌리지 않는다)
    static constexpr usize kMaxSteps = 1000;

    void attach(UndoKind kind, IUndoParticipant* participant) noexcept;
    void record(UndoKind kind, std::string label, u64 tag = 0);
    // 했거나 시작했으면 true (없거나 · 참여자가 지금은 못 함 · 앞의 것이 진행 중 = false)
    bool undo();
    bool redo();
    // 시작한 되돌리기 · 다시 하기가 끝났다 (참여자가 부른다). ok 면 칸을 반대쪽 줄로 — 되돌리기는 그 사이 새 기록이
    // 다시 하기를 비웠으면 버린다, 다시 하기는 시작할 때의 자리에 끼운다 (그 사이 기록한 칸 앞). 실패면 칸을 버린다
    // (참여자도 버렸다)
    void finish(UndoKind kind, bool ok);
    [[nodiscard]] bool pending() const noexcept { return m_pending.has_value(); }
    // 종류의 칸 (tag 가 있으면 그 tag 만) 을 두 줄에서 지운다 — 참여자는 자기 더미에서 같은 칸을 이미 지웠거나 지운다.
    // 진행 중인 것이 그 종류면 그것도 잊는다
    void clear(UndoKind kind);
    void clear(UndoKind kind, u64 tag);
    // 되돌리기 줄에서 그 종류의 가장 오래된 칸 하나 — 참여자가 자기 상한으로 가장 오래된 것을 버렸을 때
    void dropOldest(UndoKind kind);
    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty() && !m_pending; }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty() && !m_pending; }
    [[nodiscard]] const UndoStep* nextUndo() const noexcept { return m_undo.empty() ? nullptr : &m_undo.back(); }
    [[nodiscard]] const UndoStep* nextRedo() const noexcept { return m_redo.empty() ? nullptr : &m_redo.back(); }
    [[nodiscard]] const std::vector<UndoStep>& undoSteps() const noexcept { return m_undo; }
    [[nodiscard]] u64 dropped() const noexcept { return m_dropped; } // 상한으로 버린 칸 (처음부터)

private:
    struct Pending {
        UndoStep step;
        bool undoing = true;
        usize at = 0;      // 다시 하기 — 끝나면 m_undo 의 이 자리에
        u64 redoEpoch = 0; // 되돌리기 — 시작 때의 m_redoEpoch (달라졌으면 다시 하기 줄이 비워졌다)
    };
    bool step(bool undoing);
    void dropAllRedo();

    IUndoParticipant* m_participants[static_cast<usize>(UndoKind::Count)] = {};
    std::vector<UndoStep> m_undo;
    std::vector<UndoStep> m_redo;
    std::optional<Pending> m_pending;
    u64 m_redoEpoch = 0; // 다시 하기 줄을 비운 횟수
    u64 m_dropped = 0;
};

} // namespace sbx::editor::services
