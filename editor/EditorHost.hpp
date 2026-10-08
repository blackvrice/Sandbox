#pragma once
// 에디터 ↔ 클라이언트 경계 (Phase 12A, docs/10-EDITOR.md 1 · 2장, ADR-0028).
//
//   ICommandSink  편집 결과를 명령(SimCommand payload)으로 내보내는 곳 (E1 · E2 — 에디터는 Network 를 모른다).
//                 SandboxClient 의 NetworkSession 이 ClientSession 으로 서버에 보낸다 (싱글플레이도 같은 길).
//   IEditorHost   에디터가 보는 월드: 복제 월드의 콘텐츠 · 지형, 그린 개체 고르기, 선택(클라이언트 로컬 — E3),
//                 EditPreview(서버가 확정할 때까지 옮겨 그리기), 보낸 명령의 결과.
//
// 에디터는 월드를 직접 바꾸지 않는다. 한 스레드(Main)에서 쓴다.

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "core/components/core/Identity.hpp"
#include "render/renderer/Camera2D.hpp"

namespace sbx::content {
class ContentDatabase;
}
namespace sbx::world {
class WorldGrid;
}

namespace sbx::editor {

class ICommandSink {
public:
    virtual ~ICommandSink() = default;
    // 보낸 명령의 sequence. 0 = 보내지 못했다 (접속 전 · 다시 접속 중)
    virtual u32 submit(cmd::CommandPayload payload) = 0;
};

// submit 으로 보낸 명령의 결과 (서버 CommandResult)
struct CommandOutcome {
    u32 sequence = 0;
    bool accepted = false;
    u64 appliedTick = 0;
    std::vector<NetEntityId> created; // CreateEntity 가 만든 개체
    std::string message;              // 거절 사유 (accepted 면 빈 문자열)
};

// EditPreview (10-EDITOR 6.1): 이 개체를 offset 만큼 옮겨 그린다 (클라이언트 전용 — 복제 · 저장되지 않는다)
struct PreviewOffset {
    NetEntityId id = kInvalidNetEntityId;
    Vec2 offset{};
};

class IEditorHost : public ICommandSink {
public:
    // submit 으로 보낸 명령의 결과 (도착 순서). 가져가면 비운다
    [[nodiscard]] virtual std::vector<CommandOutcome> takeOutcomes() = 0;
    // 접속 전이면 null
    [[nodiscard]] virtual const content::ContentDatabase* content() const = 0;
    [[nodiscard]] virtual const world::WorldGrid* grid() const = 0;
    // 그린 개체 고르기 (그린 위치 · 크기 — preview 포함). pickBox 는 netId 오름차순
    [[nodiscard]] virtual std::optional<NetEntityId> pickAt(Vec2 world) const = 0;
    [[nodiscard]] virtual std::vector<NetEntityId> pickBox(render::WorldRect area) const = 0;
    // 선택 (netId 오름차순)
    [[nodiscard]] virtual std::span<const NetEntityId> selection() const = 0;
    virtual void setSelection(std::vector<NetEntityId> ids) = 0;
    // 이번 프레임부터 그릴 때 더할 위치 (빈 목록 = 없음). 에디터가 프레임마다 부른다
    virtual void setPreview(std::span<const PreviewOffset> offsets) = 0;
    // 적용한 스냅숏 수 · 마지막 스냅숏의 서버 틱 · 그린 틱 — preview 를 언제 걷을지 (ADR-0028 결정 4)
    [[nodiscard]] virtual u64 snapshotsApplied() const = 0;
    [[nodiscard]] virtual u64 serverTick() const = 0;
    [[nodiscard]] virtual f64 renderTick() const = 0;
};

} // namespace sbx::editor
