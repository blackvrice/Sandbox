#include "editor/Editor.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <utility>

#include "core/content/ContentDatabase.hpp"
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

void Editor::setTool(Tool t) {
    if (t == m_tool) {
        return;
    }
    // 끌던 것은 버린다 (보낸 명령은 그대로)
    m_drag = Drag::None;
    m_boxing = false;
    m_moving.clear();
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
        if (const auto it = std::ranges::find(m_creates, o.sequence); it != m_creates.end()) {
            m_creates.erase(it);
            if (o.accepted) {
                m_lastCreated = std::move(o.created);
            }
        }
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
    if (const u32 seq = send(host, std::move(c)); seq != 0) {
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
        (void)send(host, std::move(paint)); // 프레임당 하나
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
    if (drag == Drag::Box) {
        std::vector<NetEntityId> ids;
        if (m_boxing) {
            ids = host.pickBox({m_dragStart, in.cursor});
        } else if (const auto id = host.pickAt(in.cursor)) {
            ids = {*id};
        }
        if (m_dragTool == Tool::Erase) {
            if (!ids.empty()) {
                cmd::DeleteEntity d;
                d.targets = std::move(ids);
                (void)send(host, std::move(d));
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
            if (const u32 seq = send(host, std::move(mv)); seq != 0) {
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
    if (in.tool) {
        setTool(*in.tool);
    }
    if (in.cancel) {
        if (m_drag != Drag::None) {
            m_drag = Drag::None;
            m_boxing = false;
            m_moving.clear();
        } else {
            host.setSelection({});
        }
    }
    if (in.deleteSelection && !host.selection().empty()) {
        cmd::DeleteEntity d;
        d.targets.assign(host.selection().begin(), host.selection().end());
        (void)send(host, std::move(d));
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
