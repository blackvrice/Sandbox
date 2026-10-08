#pragma once
// 에디터 툴 (Phase 12A, docs/10-EDITOR.md 3 · 6.1장, ADR-0028).
//
//   Select        클릭 = 맨 위 개체 하나 · 끌기(4 px 넘게) = 박스 · Shift = 더하기/빼기. 명령 없음 (로컬)
//   Move          선택한 개체(또는 누른 개체)를 끈다 — 끄는 동안 EditPreview, 놓으면 MoveEntity{선택, delta}.
//                 빈 곳을 누르면 Select 처럼. 격자 맞춤이면 delta 를 정수 칸으로
//   Place         Palette 에서 고른 Prefab 을 클릭 자리에 CreateEntity. 누른 채 끌면 spacing 칸마다 하나 더 (프레임당
//   1) TerrainBrush  왼쪽 = 고른 머티리얼, 오른쪽 = 월드 바탕(fill) 머티리얼. 프레임마다 지난 커서 → 지금 커서 사이를
//   브러시로
//                 훑어 새 칸만 PaintTerrain{cells} 하나 (경계 안 · 이미 그 머티리얼인 칸은 뺀다 · 상한 4096)
//   Erase         클릭 = 그 개체, 끌기 = 박스 안 전부를 DeleteEntity
//   Delete 키      어느 툴에서든 선택 전부를 DeleteEntity
//
// EditPreview 걷기: 거절되면 바로. 받아들여지면 그 뒤 처음 적용한 스냅숏의 서버 틱까지 그린 틱이 따라오면 (그때
// 그려지는 위치가 옮긴 위치다). 결과가 오지 않으면(끊김) 3 초 뒤.
//
// 에디터는 월드를 바꾸지 않는다 — 명령만 낸다 (E1). 툴 상태 · 선택은 클라이언트 로컬 (E3).

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "editor/EditorHost.hpp"
#include "render/renderer/DebugDraw.hpp"

namespace sbx::editor {

enum class Tool : u8 { Select = 0, Move, Place, TerrainBrush, Erase };
inline constexpr usize kToolCount = 5;
[[nodiscard]] std::string_view toolName(Tool t) noexcept; // "선택" · "이동" · …

// 한 프레임의 입력 (Application 이 InputState · 카메라에서 만든다 — ImGui 가 가져간 입력은 이미 빠져 있다)
struct EditorInput {
    f64 now = 0;                 // 초 (단조 — preview 시간 초과)
    Vec2 cursor{};               // 월드 좌표
    f32 pixelsPerUnit = 32.f;    // 끌기 문턱(4 px)을 월드로
    bool primaryPressed = false; // 왼쪽 누른 순간
    bool primaryDown = false;
    bool secondaryPressed = false; // 오른쪽
    bool secondaryDown = false;
    bool additive = false;        // Shift (Select · Move 의 빈 곳 클릭)
    bool cancel = false;          // Esc — 끌기 취소 · 선택 해제
    bool deleteSelection = false; // Delete
    std::optional<Tool> tool;     // 단축키로 고른 툴
};

struct EditorSettings {
    std::string prefab;     // Place
    f32 placeSpacing = 2.f; // Place 끌기 간격 (칸)
    bool snap = false;      // Move · Place: 격자 맞춤
    std::string material;   // TerrainBrush 왼쪽
    u32 brushRadius = 1;    // 0 = 한 칸, 최대 8
    cmd::BrushShape brushShape = cmd::BrushShape::Circle;
};

struct EditorStats {
    u64 sent = 0;
    u64 accepted = 0;
    u64 rejected = 0;
    u64 notSent = 0; // submit 이 0 (접속 전)
};

class Editor {
public:
    static constexpr f32 kDragThresholdPixels = 4.f;
    static constexpr f64 kPreviewTimeoutSeconds = 3.0;
    static constexpr u32 kMaxBrushRadius = 8;
    static constexpr usize kMaxCellsPerCommand = 4096; // PaintTerrain 상한 (03 4.1)

    void update(const EditorInput& in, IEditorHost& host);
    // 끌고 있는 박스 · 브러시 원 · 배치 자리 (월드 선 — SelectionPass)
    void drawOverlay(render::DebugDrawList& out) const;

    [[nodiscard]] Tool tool() const noexcept { return m_tool; }
    void setTool(Tool t);
    [[nodiscard]] EditorSettings& settings() noexcept { return m_settings; }
    [[nodiscard]] const EditorSettings& settings() const noexcept { return m_settings; }
    [[nodiscard]] const EditorStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return m_message; }
    // 지금 그리는 preview (시험 · 패널)
    [[nodiscard]] const std::vector<PreviewOffset>& preview() const noexcept { return m_preview; }
    // 가장 최근 받아들여진 CreateEntity 의 새 개체 (시험)
    [[nodiscard]] const std::vector<NetEntityId>& lastCreated() const noexcept { return m_lastCreated; }
    // 제목 줄 한 토막 ("툴 배치 eco.rabbit")
    [[nodiscard]] std::string status() const;

private:
    enum class Drag : u8 { None = 0, Box, Move, Place, Paint };
    struct Pending {
        u32 sequence = 0;
        std::vector<NetEntityId> ids;
        Vec2 offset{};
        f64 sentAt = 0;
        bool accepted = false;
        u64 snapshotsAtAccept = 0;
        std::optional<u64> untilTick; // 받아들여진 뒤 처음 온 스냅숏의 서버 틱
    };

    u32 send(IEditorHost& host, cmd::CommandPayload payload);
    void handleOutcomes(IEditorHost& host, f64 now);
    void beginPress(const EditorInput& in, IEditorHost& host, bool secondary);
    void continueDrag(const EditorInput& in, IEditorHost& host);
    void endDrag(const EditorInput& in, IEditorHost& host);
    void paintAlong(Vec2 from, Vec2 to, IEditorHost& host);
    void placeAt(Vec2 at, IEditorHost& host);
    [[nodiscard]] Vec2 snapped(Vec2 p) const;
    [[nodiscard]] bool pastThreshold(const EditorInput& in) const;
    void rebuildPreview(IEditorHost& host);

    Tool m_tool = Tool::Select;
    EditorSettings m_settings;
    EditorStats m_stats;
    std::string m_message;

    Drag m_drag = Drag::None;
    bool m_dragAdditive = false;
    bool m_dragSecondary = false; // Paint: 오른쪽 (바탕 머티리얼)
    Tool m_dragTool = Tool::Select;
    Vec2 m_dragStart{};
    Vec2 m_cursor{};
    Vec2 m_lastPaint{};
    Vec2 m_lastPlace{};
    bool m_boxing = false;
    std::vector<NetEntityId> m_moving;      // Move 끄는 대상
    std::set<std::pair<i32, i32>> m_stroke; // 이번 붓질에서 보낸 칸 (같은 칸을 다시 보내지 않는다)
    std::string m_strokeMaterial;

    std::vector<Pending> m_pending;       // 결과 · 확정 스냅숏을 기다리는 Move
    std::vector<PreviewOffset> m_preview; // 지금 그리는 것 (끄는 중 + m_pending)
    std::vector<u32> m_creates;           // 결과를 기다리는 CreateEntity
    std::vector<NetEntityId> m_lastCreated;
};

} // namespace sbx::editor
