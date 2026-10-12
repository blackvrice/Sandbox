#include "editor/Editor.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <utility>

#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/world/WorldGrid.hpp"
#include "render/renderer/RenderWorld.hpp"

namespace sbx::editor {

namespace {

constexpr u32 kBoxColor = render::packRgba8(255, 214, 0, 200);
constexpr u32 kEraseColor = render::packRgba8(255, 80, 64, 220);
constexpr u32 kBrushColor = render::packRgba8(255, 255, 255, 180);
constexpr u32 kPlaceColor = render::packRgba8(120, 255, 140, 220);

[[nodiscard]] Vec2i tileOf(Vec2 p) noexcept {
    return {static_cast<i32>(std::floor(p.x)), static_cast<i32>(std::floor(p.y))};
}

[[nodiscard]] bool contains(std::span<const NetEntityId> sorted, NetEntityId id) {
    return std::ranges::binary_search(sorted, id);
}

[[nodiscard]] std::vector<NetEntityId> merged(std::span<const NetEntityId> a, std::span<const NetEntityId> b) {
    std::vector<NetEntityId> out;
    std::ranges::set_union(a, b, std::back_inserter(out));
    return out;
}

[[nodiscard]] std::optional<usize> materialIndex(const content::ContentDatabase& content, std::string_view id) {
    const auto mats = content.terrainMaterials();
    for (usize i = 0; i < mats.size(); ++i) {
        if (mats[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

std::string_view toolName(Tool t) noexcept {
    switch (t) {
    case Tool::Select:
        return "선택";
    case Tool::Move:
        return "이동";
    case Tool::Place:
        return "배치";
    case Tool::TerrainBrush:
        return "지형";
    case Tool::Erase:
        return "지우기";
    }
    return "?";
}

bool toolAllowed(Tool t, const EditPermissions& p) noexcept {
    switch (t) {
    case Tool::Select:
        return true;
    case Tool::Move:
    case Tool::Place:
    case Tool::Erase:
        return p.entities;
    case Tool::TerrainBrush:
        return p.terrain;
    }
    return false;
}

void Editor::setTool(Tool t) {
    if (t == m_tool) {
        return;
    }
    // 끌던 것은 버린다 (보낸 명령은 그대로 — 열린 기록은 닫는다)
    m_drag = Drag::None;
    m_boxing = false;
    m_moving.clear();
    endTxn();
    m_tool = t;
}

u32 Editor::send(IEditorHost& host, cmd::CommandPayload payload) {
    const u32 seq = host.submit(std::move(payload));
    if (seq == 0) {
        ++m_stats.notSent;
        m_message = "명령을 보내지 못했습니다 (접속 전 · 다시 접속 중)";
    } else {
        ++m_stats.sent;
    }
    return seq;
}

void Editor::handleOutcomes(IEditorHost& host, f64 now) {
    for (CommandOutcome& o : host.takeOutcomes()) {
        if (o.accepted) {
            ++m_stats.accepted;
        } else {
            ++m_stats.rejected;
            m_message = o.message;
        }
        if (const auto it = std::ranges::find(m_pending, o.sequence, &Pending::sequence); it != m_pending.end()) {
            if (!o.accepted) {
                m_pending.erase(it); // 거절 — 옮겨 그리던 것을 바로 거둔다
            } else {
                it->accepted = true;
                it->snapshotsAtAccept = host.snapshotsApplied();
            }
        }
        // 되돌리기 기록 (12B): 편집 · 되돌리기 단계
        for (Txn& txn : m_txns) {
            for (TxnStep& s : txn.steps) {
                if (s.sequence != o.sequence || s.done) {
                    continue;
                }
                s.done = true;
                s.accepted = o.accepted;
                if (o.accepted && std::holds_alternative<cmd::CreateEntity>(s.forward.payload)) {
                    s.forward.recreates = o.created; // 다시 하기에서 만들어질 새 netId 를 이 id 에 잇는다
                    cmd::DeleteEntity del;
                    del.targets = o.created;
                    s.inverse = {UndoCmd{std::move(del), {}}};
                }
            }
        }
        if (m_replay) {
            for (ReplayStep& s : m_replay->steps) {
                if (s.sequence == o.sequence && !s.done) {
                    s.done = true;
                    s.accepted = o.accepted;
                    for (usize i = 0; o.accepted && i < std::min(s.recreates.size(), o.created.size()); ++i) {
                        m_remap.set(s.recreates[i], o.created[i]);
                    }
                }
            }
        }
        if (const auto it = std::ranges::find(m_creates, o.sequence); it != m_creates.end()) {
            m_creates.erase(it);
            if (o.accepted) {
                m_lastCreated = std::move(o.created);
            }
        }
    }
    settleTxns();
    if (m_replay && std::ranges::all_of(m_replay->steps, &ReplayStep::done)) {
        const bool all = std::ranges::all_of(m_replay->steps, &ReplayStep::accepted);
        if (all) {
            (m_replay->undoing ? m_redo : m_undo).push_back(std::move(m_replay->entry));
        } else {
            m_message = std::format("{} 일부가 거절되어 기록에서 뺐습니다 ({})",
                                    m_replay->undoing ? "되돌리기" : "다시 하기", m_replay->entry.label);
        }
        m_replay.reset();
    }
    // 받아들여진 이동: 그 뒤 처음 온 스냅숏의 틱까지 그린 틱이 따라오면 그려진 위치가 이미 옮긴 위치다
    std::erase_if(m_pending, [&](Pending& p) {
        if (now - p.sentAt > kPreviewTimeoutSeconds) {
            return true; // 결과가 오지 않았다 (끊김) — 옛 위치로
        }
        if (!p.accepted) {
            return false;
        }
        if (!p.untilTick && host.snapshotsApplied() > p.snapshotsAtAccept) {
            p.untilTick = host.serverTick();
        }
        return p.untilTick && host.renderTick() + 1e-6 >= static_cast<f64>(*p.untilTick);
    });
}

Vec2 Editor::snapped(Vec2 p) const {
    if (!m_settings.snap) {
        return p;
    }
    return {std::floor(p.x) + 0.5f, std::floor(p.y) + 0.5f}; // 칸 가운데
}

bool Editor::pastThreshold(const EditorInput& in) const {
    const Vec2 d = in.cursor - m_dragStart;
    return d.length() * in.pixelsPerUnit > kDragThresholdPixels;
}

void Editor::placeAt(Vec2 at, IEditorHost& host) {
    if (m_settings.prefab.empty()) {
        m_message = "배치할 Prefab 을 고르십시오 (편집 패널)";
        return;
    }
    const Vec2 p = snapped(at);
    if (const world::WorldGrid* grid = host.grid(); grid != nullptr && !grid->containsPoint(p)) {
        m_message = "월드 밖에는 놓을 수 없습니다";
        return;
    }
    cmd::CreateEntity c;
    c.position = p;
    c.prefab = m_settings.prefab;
    if (const u32 seq = sendEdit(host, std::move(c), {}); seq != 0) { // 역명령(DeleteEntity)은 결과의 새 netId 로
        m_creates.push_back(seq);
    }
}

void Editor::paintAlong(Vec2 from, Vec2 to, IEditorHost& host) {
    const content::ContentDatabase* content = host.content();
    const world::WorldGrid* grid = host.grid();
    if (content == nullptr || grid == nullptr) {
        return;
    }
    const auto index = materialIndex(*content, m_strokeMaterial);
    if (!index) {
        m_message = std::format("모르는 지형 머티리얼: {}", m_strokeMaterial);
        return;
    }
    const i32 r = static_cast<i32>(std::min(m_settings.brushRadius, kMaxBrushRadius));
    const bool circle = m_settings.brushShape == cmd::BrushShape::Circle;
    // 지난 커서 → 지금 커서를 반 칸 간격으로 (빠르게 끌어도 빈틈이 없게). 한 프레임 256 걸음까지
    const f32 dist = (to - from).length();
    const i32 steps = std::clamp(static_cast<i32>(std::ceil(dist / 0.5f)), 1, 256);
    cmd::PaintTerrain paint;
    paint.materialId = m_strokeMaterial;
    for (i32 s = 0; s <= steps && paint.cells.size() < kMaxCellsPerCommand; ++s) {
        const Vec2 p = from + (to - from) * (static_cast<f32>(s) / static_cast<f32>(steps));
        const Vec2i c = tileOf(p);
        for (i32 dy = -r; dy <= r; ++dy) {
            for (i32 dx = -r; dx <= r; ++dx) {
                if (circle && dx * dx + dy * dy > r * r) {
                    continue;
                }
                const Vec2i t{c.x + dx, c.y + dy};
                if (!grid->containsTile(t) || !m_stroke.insert({t.x, t.y}).second) {
                    continue; // 경계 밖 · 이번 붓질에서 이미 보낸 칸
                }
                if (grid->materialAt(t) == *index) {
                    continue; // 이미 그 머티리얼
                }
                if (paint.cells.size() < kMaxCellsPerCommand) {
                    paint.cells.push_back(t);
                }
            }
        }
    }
    if (!paint.cells.empty()) {
        std::vector<UndoCmd> inverse = paintInverse(paint, *grid, *content); // 보내기 전 머티리얼
        (void)sendEdit(host, std::move(paint), std::move(inverse));          // 프레임당 하나
    }
}

void Editor::beginPress(const EditorInput& in, IEditorHost& host, bool secondary) {
    m_dragStart = in.cursor;
    m_dragAdditive = in.additive;
    m_dragSecondary = secondary;
    m_dragTool = m_tool;
    m_boxing = false;
    switch (m_tool) {
    case Tool::Select:
    case Tool::Erase:
        m_drag = Drag::Box;
        break;
    case Tool::Move: {
        const auto id = host.pickAt(in.cursor);
        if (!id) {
            m_drag = Drag::Box; // 빈 곳 = 선택처럼
            break;
        }
        const auto sel = host.selection();
        if (!contains(sel, *id)) {
            const NetEntityId one[] = {*id};
            host.setSelection(in.additive ? merged(sel, one) : std::vector<NetEntityId>{*id});
        }
        m_moving.assign(host.selection().begin(), host.selection().end());
        m_drag = Drag::Move;
        break;
    }
    case Tool::Place:
        beginTxn("배치"); // 끌어서 놓은 것 전부 = 기록 한 칸
        m_drag = Drag::Place;
        m_lastPlace = in.cursor;
        placeAt(in.cursor, host);
        break;
    case Tool::TerrainBrush: {
        m_stroke.clear();
        m_strokeMaterial = m_settings.material;
        if (secondary) {
            const content::ContentDatabase* content = host.content();
            const world::WorldGrid* grid = host.grid();
            if (content == nullptr || grid == nullptr || grid->fillMaterial() >= content->terrainMaterials().size()) {
                return;
            }
            m_strokeMaterial = content->terrainMaterials()[grid->fillMaterial()].id; // 지우기 = 바탕 머티리얼
        }
        if (m_strokeMaterial.empty()) {
            m_message = "칠할 머티리얼을 고르십시오 (편집 패널)";
            return;
        }
        beginTxn(std::format("지형 {}", m_strokeMaterial)); // 붓질 한 번 = 기록 한 칸
        m_drag = Drag::Paint;
        m_lastPaint = in.cursor;
        paintAlong(in.cursor, in.cursor, host);
        break;
    }
    }
}

void Editor::continueDrag(const EditorInput& in, IEditorHost& host) {
    switch (m_drag) {
    case Drag::Box:
    case Drag::Move:
        if (pastThreshold(in)) {
            m_boxing = true;
        }
        break;
    case Drag::Place:
        if ((in.cursor - m_lastPlace).length() >= std::max(m_settings.placeSpacing, 0.25f)) {
            m_lastPlace = in.cursor;
            placeAt(in.cursor, host);
        }
        break;
    case Drag::Paint:
        paintAlong(m_lastPaint, in.cursor, host);
        m_lastPaint = in.cursor;
        break;
    case Drag::None:
        break;
    }
}

void Editor::endDrag(const EditorInput& in, IEditorHost& host) {
    const Drag drag = std::exchange(m_drag, Drag::None);
    if (drag == Drag::Place || drag == Drag::Paint) {
        endTxn();
    }
    if (drag == Drag::Box) {
        std::vector<NetEntityId> ids;
        if (m_boxing) {
            ids = host.pickBox({m_dragStart, in.cursor});
        } else if (const auto id = host.pickAt(in.cursor)) {
            ids = {*id};
        }
        if (m_dragTool == Tool::Erase) {
            if (!ids.empty()) {
                std::vector<UndoCmd> inverse = recreates(host, ids);
                cmd::DeleteEntity d;
                d.targets = std::move(ids);
                (void)sendEdit(host, std::move(d), std::move(inverse));
            }
        } else if (!m_dragAdditive) {
            host.setSelection(std::move(ids));
        } else if (!m_boxing && ids.size() == 1) {
            // 클릭 + Shift: 있으면 빼고 없으면 더한다
            std::vector<NetEntityId> sel(host.selection().begin(), host.selection().end());
            if (const auto it = std::ranges::lower_bound(sel, ids[0]); it != sel.end() && *it == ids[0]) {
                sel.erase(it);
            } else {
                sel.insert(it, ids[0]);
            }
            host.setSelection(std::move(sel));
        } else {
            host.setSelection(merged(host.selection(), ids));
        }
    } else if (drag == Drag::Move) {
        Vec2 delta = in.cursor - m_dragStart;
        if (m_settings.snap) {
            delta = {std::round(delta.x), std::round(delta.y)};
        }
        if (m_boxing && (delta.x != 0.f || delta.y != 0.f) && !m_moving.empty()) {
            cmd::MoveEntity mv;
            mv.targets = m_moving;
            mv.value = delta;
            cmd::MoveEntity back;
            back.targets = m_moving;
            back.value = -delta;
            if (const u32 seq = sendEdit(host, std::move(mv), {UndoCmd{std::move(back), {}}}); seq != 0) {
                Pending p;
                p.sequence = seq;
                p.ids = m_moving;
                p.offset = delta;
                p.sentAt = in.now;
                m_pending.push_back(std::move(p));
            }
        }
        m_moving.clear();
    }
    m_boxing = false;
}

void Editor::rebuildPreview(IEditorHost& host) {
    std::map<NetEntityId, Vec2> sum;
    for (const Pending& p : m_pending) {
        for (const NetEntityId id : p.ids) {
            sum[id] += p.offset;
        }
    }
    if (m_drag == Drag::Move && m_boxing) {
        Vec2 delta = m_cursor - m_dragStart;
        if (m_settings.snap) {
            delta = {std::round(delta.x), std::round(delta.y)};
        }
        for (const NetEntityId id : m_moving) {
            sum[id] += delta;
        }
    }
    m_preview.clear();
    for (const auto& [id, offset] : sum) {
        m_preview.push_back({id, offset});
    }
    host.setPreview(m_preview);
}

void Editor::update(const EditorInput& in, IEditorHost& host) {
    m_cursor = in.cursor;
    // 처음 접속했을 때 고를 것이 비어 있으면 콘텐츠의 첫 항목으로 (패널에서 바꾼다)
    if (const content::ContentDatabase* content = host.content()) {
        if (m_settings.prefab.empty() && !content->prefabs().empty()) {
            m_settings.prefab = content->prefabs().front().id;
        }
        const auto mats = content->terrainMaterials();
        if (m_settings.material.empty() && !mats.empty()) {
            const usize fill = host.grid() != nullptr ? host.grid()->fillMaterial() : 0;
            m_settings.material = mats[mats.size() > 1 && fill == 0 ? 1 : 0].id; // 바탕이 아닌 첫 머티리얼
        }
    }
    handleOutcomes(host, in.now);
    // 역할 (12C): 막힌 툴이면 선택 툴로 (끌던 것은 버린다)
    const EditPermissions perms = host.permissions();
    if (!toolAllowed(m_tool, perms)) {
        setTool(Tool::Select);
        m_message = "지금 역할로는 그 툴을 쓸 수 없어 선택 툴로 바꿨습니다";
    }
    if (in.tool) {
        if (toolAllowed(*in.tool, perms)) {
            setTool(*in.tool);
        } else {
            m_message = std::format("지금 역할로는 {} 툴을 쓸 수 없습니다", toolName(*in.tool));
        }
    }
    if ((in.undo || in.redo) && !perms.anyEdit()) {
        m_message = "지금 역할로는 편집할 수 없습니다";
    } else if (in.undo) {
        undo(host);
    } else if (in.redo) {
        redo(host);
    }
    if (in.focus) {
        requestFocus();
    }
    if (in.cancel) {
        if (m_drag != Drag::None) {
            m_drag = Drag::None;
            m_boxing = false;
            m_moving.clear();
            endTxn();
        } else {
            host.setSelection({});
        }
    }
    if (in.deleteSelection && !host.selection().empty() && perms.entities) {
        cmd::DeleteEntity d;
        d.targets.assign(host.selection().begin(), host.selection().end());
        std::vector<UndoCmd> inverse = recreates(host, d.targets);
        (void)sendEdit(host, std::move(d), std::move(inverse));
        host.setSelection({});
    }
    if (m_drag == Drag::None) {
        if (in.primaryPressed) {
            beginPress(in, host, false);
        } else if (in.secondaryPressed && m_tool == Tool::TerrainBrush) {
            beginPress(in, host, true);
        }
    }
    if (m_drag != Drag::None) {
        continueDrag(in, host);
        const bool held = m_dragSecondary ? in.secondaryDown : in.primaryDown;
        if (!held) {
            endDrag(in, host);
        }
    }
    rebuildPreview(host);
}

// ---- 12B: 되돌리기 기록 ----

void Editor::beginTxn(std::string label) {
    endTxn();
    m_txns.push_back({std::move(label), {}, true});
}

void Editor::endTxn() {
    if (!m_txns.empty() && m_txns.back().open) {
        m_txns.back().open = false;
        settleTxns();
    }
}

u32 Editor::sendEdit(IEditorHost& host, cmd::CommandPayload payload, std::vector<UndoCmd> inverse) {
    const bool oneShot = m_txns.empty() || !m_txns.back().open;
    if (oneShot) {
        beginTxn(std::string(cmd::commandName(payload)));
    }
    cmd::CommandPayload forward = payload;
    const u32 seq = send(host, std::move(payload));
    if (seq != 0) {
        m_txns.back().steps.push_back({seq, UndoCmd{std::move(forward), {}}, std::move(inverse)});
    }
    if (oneShot) {
        endTxn();
    }
    return seq;
}

void Editor::settleTxns() {
    // 앞에서부터: 닫혔고 결과가 다 온 것 → 받아들여진 단계만 기록 한 칸 (순서를 지키려고 앞이 끝나야 뒤를 본다)
    while (!m_txns.empty() && !m_txns.front().open && std::ranges::all_of(m_txns.front().steps, &TxnStep::done)) {
        Txn txn = std::move(m_txns.front());
        m_txns.erase(m_txns.begin());
        UndoEntry entry;
        entry.label = std::move(txn.label);
        for (TxnStep& s : txn.steps) {
            if (s.accepted) {
                entry.forward.push_back(std::move(s.forward));
            }
        }
        for (auto it = txn.steps.rbegin(); it != txn.steps.rend(); ++it) {
            if (it->accepted) {
                for (UndoCmd& c : it->inverse) {
                    entry.inverse.push_back(std::move(c));
                }
            }
        }
        if (entry.forward.empty()) {
            continue; // 다 거절 — 기록하지 않는다
        }
        m_undo.push_back(std::move(entry));
        if (m_undo.size() > kMaxUndo) {
            m_undo.erase(m_undo.begin());
        }
        m_redo.clear();
    }
}

void Editor::replay(IEditorHost& host, bool undoing) {
    std::vector<UndoEntry>& from = undoing ? m_undo : m_redo;
    if (from.empty()) {
        return;
    }
    if (!idle()) {
        m_message = "앞 편집의 결과를 기다리는 중입니다";
        return;
    }
    Replay r;
    r.undoing = undoing;
    r.entry = std::move(from.back());
    from.pop_back();
    bool anySent = false;
    for (const UndoCmd& c : undoing ? r.entry.inverse : r.entry.forward) {
        cmd::CommandPayload p = c.payload;
        m_remap.apply(p); // 다시 만든 개체는 새 netId 로
        const u32 seq = send(host, std::move(p));
        r.steps.push_back({seq, c.recreates, seq == 0, false});
        anySent = anySent || seq != 0;
    }
    if (!anySent) {
        from.push_back(std::move(r.entry)); // 하나도 못 보냈다 — 그대로 둔다
        return;
    }
    m_replay = std::move(r);
}

void Editor::undo(IEditorHost& host) {
    replay(host, true);
}

void Editor::redo(IEditorHost& host) {
    replay(host, false);
}

std::string_view Editor::undoLabel() const noexcept {
    return m_undo.empty() ? std::string_view() : std::string_view(m_undo.back().label);
}

std::string_view Editor::redoLabel() const noexcept {
    return m_redo.empty() ? std::string_view() : std::string_view(m_redo.back().label);
}

std::vector<UndoCmd> Editor::recreates(IEditorHost& host, std::span<const NetEntityId> ids) const {
    std::vector<UndoCmd> out;
    const ecs::ComponentCatalog* catalog = host.catalog();
    if (catalog == nullptr) {
        return out;
    }
    for (const NetEntityId id : ids) {
        const std::vector<ComponentState> comps = host.components(id);
        if (!comps.empty()) {
            out.push_back(recreateCommand(id, comps, *catalog)); // 모르는 개체(관심 밖)는 되돌릴 수 없다
        }
    }
    return out;
}

// ---- 12B: Inspector ----

void Editor::setField(IEditorHost& host, ecs::StableId component, std::string_view field, const ecs::Json& value) {
    if (!host.permissions().entities) {
        m_message = "지금 역할로는 값을 바꿀 수 없습니다";
        return;
    }
    const ecs::ComponentCatalog* catalog = host.catalog();
    const ecs::ComponentInfo* info = catalog != nullptr ? catalog->find(component) : nullptr;
    if (info == nullptr) {
        return;
    }
    beginTxn(std::format("{}.{}", info->name, field));
    const std::string key(field);
    for (const NetEntityId id : std::vector<NetEntityId>(host.selection().begin(), host.selection().end())) {
        for (const ComponentState& s : host.components(id)) {
            if (s.stableId != component || !s.value.is_object() || !s.value.contains(key) || s.value[key] == value) {
                continue;
            }
            cmd::ChangeComponent change;
            change.target = id;
            change.stableId = component;
            change.patch = ecs::Json::object({{key, value}});
            cmd::ChangeComponent back = change;
            back.patch = ecs::Json::object({{key, s.value[key]}});
            (void)sendEdit(host, std::move(change), {UndoCmd{std::move(back), {}}});
        }
    }
    endTxn();
}

void Editor::addComponent(IEditorHost& host, ecs::StableId component) {
    if (!host.permissions().entities) {
        m_message = "지금 역할로는 컴포넌트를 바꿀 수 없습니다";
        return;
    }
    beginTxn("컴포넌트 더하기");
    for (const NetEntityId id : std::vector<NetEntityId>(host.selection().begin(), host.selection().end())) {
        const auto comps = host.components(id);
        if (comps.empty() ||
            std::ranges::any_of(comps, [&](const ComponentState& s) { return s.stableId == component; })) {
            continue;
        }
        cmd::AddComponent add;
        add.target = id;
        add.component = {component, ecs::Json::object()};
        cmd::RemoveComponent back;
        back.target = id;
        back.stableId = component;
        (void)sendEdit(host, std::move(add), {UndoCmd{std::move(back), {}}});
    }
    endTxn();
}

void Editor::removeComponent(IEditorHost& host, ecs::StableId component) {
    if (!host.permissions().entities) {
        m_message = "지금 역할로는 컴포넌트를 바꿀 수 없습니다";
        return;
    }
    const ecs::ComponentCatalog* catalog = host.catalog();
    const ecs::ComponentInfo* info = catalog != nullptr ? catalog->find(component) : nullptr;
    if (info == nullptr) {
        return;
    }
    beginTxn(std::format("{} 빼기", info->name));
    for (const NetEntityId id : std::vector<NetEntityId>(host.selection().begin(), host.selection().end())) {
        for (const ComponentState& s : host.components(id)) {
            if (s.stableId != component) {
                continue;
            }
            cmd::RemoveComponent rm;
            rm.target = id;
            rm.stableId = component;
            cmd::AddComponent back;
            back.target = id;
            back.component = {component, withoutEntityRefs(s.value, *info)};
            (void)sendEdit(host, std::move(rm), {UndoCmd{std::move(back), {}}});
        }
    }
    endTxn();
}

// ---- 12C: 콘텐츠 ----

bool Editor::editContent(IEditorHost& host, content::ContentKind kind, const std::string& id, ecs::Json value) {
    if (!host.permissions().content) {
        m_message = "지금 역할로는 콘텐츠를 바꿀 수 없습니다";
        return false;
    }
    const content::ContentDatabase* content = host.content();
    if (content == nullptr) {
        return false;
    }
    const ecs::Json* now = content->overlay().find(kind, id);
    ecs::Json before = now != nullptr ? *now : ecs::Json(nullptr);
    if (before == value) {
        return false; // 바뀌는 것이 없다
    }
    cmd::EditContent back{kind, id, std::move(before)};
    beginTxn(std::format("{} {}", content::contentKindName(kind), id));
    const u32 seq = sendEdit(host, cmd::EditContent{kind, id, std::move(value)}, {UndoCmd{std::move(back), {}}});
    endTxn();
    return seq != 0;
}

void Editor::drawOverlay(render::DebugDrawList& out) const {
    if (m_drag == Drag::Box && m_boxing) {
        out.rect(m_dragStart, m_cursor, m_dragTool == Tool::Erase ? kEraseColor : kBoxColor, 1.f);
    }
    if (m_tool == Tool::TerrainBrush) {
        const Vec2i t = tileOf(m_cursor);
        const Vec2 c{static_cast<f32>(t.x) + 0.5f, static_cast<f32>(t.y) + 0.5f};
        const f32 r = static_cast<f32>(std::min(m_settings.brushRadius, kMaxBrushRadius)) + 0.5f;
        if (m_settings.brushShape == cmd::BrushShape::Circle) {
            out.circle(c, r, kBrushColor, 1.f);
        } else {
            out.rect(c - Vec2{r, r}, c + Vec2{r, r}, kBrushColor, 1.f);
        }
    } else if (m_tool == Tool::Place) {
        const Vec2 p = snapped(m_cursor);
        out.rect(p - Vec2{0.5f, 0.5f}, p + Vec2{0.5f, 0.5f}, kPlaceColor, 1.f);
    }
}

std::string Editor::status() const {
    std::string s = std::format("툴 {}", toolName(m_tool));
    if (m_tool == Tool::Place) {
        s += " " + (m_settings.prefab.empty() ? std::string("(Prefab 없음)") : m_settings.prefab);
    } else if (m_tool == Tool::TerrainBrush) {
        s += std::format(" {} r{}", m_settings.material, m_settings.brushRadius);
    }
    return s;
}

} // namespace sbx::editor
