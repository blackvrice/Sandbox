#include "network/protocol/CommandCodec.hpp"

#include <cmath>
#include <limits>
#include <type_traits>

#include "core/simulation/SimulationWorld.hpp"

namespace sbx::net {

namespace {

void writeJson(BitWriter& w, const ecs::Json& j) {
    // 잘못된 UTF-8 이 섞인 문자열이 있어도 예외 없이 (U+FFFD 로) — 받는 쪽이 어차피 검사한다
    w.writeString(j.dump(-1, ' ', false, ecs::Json::error_handler_t::replace));
}

bool readJson(BitReader& r, ecs::Json& out) {
    const std::string text = r.readString(kMaxJsonTextBytes);
    if (r.error()) {
        return false;
    }
    out = ecs::Json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        r.fail();
        return false;
    }
    return true;
}

void writeVec2(BitWriter& w, Vec2 v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
}

Vec2 readVec2(BitReader& r) {
    Vec2 v;
    v.x = r.readF32();
    v.y = r.readF32();
    if (!std::isfinite(v.x) || !std::isfinite(v.y)) {
        r.fail();
    }
    return v;
}

void writeTargets(BitWriter& w, const std::vector<NetEntityId>& targets) {
    w.writeVarU(targets.size());
    for (const NetEntityId id : targets) {
        w.writeVarU(id);
    }
}

std::vector<NetEntityId> readTargets(BitReader& r) {
    const u64 n = r.readVarU(sim::kMaxCommandTargets);
    std::vector<NetEntityId> out;
    if (r.error() || r.remainingBits() < n * 8) { // 원소마다 최소 1바이트 — 큰 개수로 메모리를 먼저 잡지 않게
        r.fail();
        return out;
    }
    out.reserve(static_cast<usize>(n));
    for (u64 i = 0; i < n; ++i) {
        out.push_back(static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull)));
    }
    return out;
}

i32 readI32(BitReader& r) {
    const i64 v = r.readVarI();
    if (v < std::numeric_limits<i32>::min() || v > std::numeric_limits<i32>::max()) {
        r.fail();
        return 0;
    }
    return static_cast<i32>(v);
}

NetEntityId readTarget(BitReader& r) {
    return static_cast<NetEntityId>(r.readVarU(0xFFFF'FFFFull));
}

void writeComponent(BitWriter& w, const cmd::ComponentValue& c) {
    w.writeVarU(c.stableId);
    writeJson(w, c.value);
}

bool readComponent(BitReader& r, cmd::ComponentValue& c) {
    c.stableId = r.readVarU();
    return readJson(r, c.value);
}

} // namespace

void writePayload(BitWriter& w, const cmd::CommandPayload& payload) {
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, cmd::CreateEntity>) {
                w.writeU8(static_cast<u8>(PayloadTag::CreateEntity));
                writeVec2(w, p.position);
                w.writeString(p.prefab);
                w.writeVarU(p.components.size());
                for (const auto& c : p.components) {
                    writeComponent(w, c);
                }
            } else if constexpr (std::is_same_v<T, cmd::DeleteEntity>) {
                w.writeU8(static_cast<u8>(PayloadTag::DeleteEntity));
                writeTargets(w, p.targets);
            } else if constexpr (std::is_same_v<T, cmd::MoveEntity>) {
                w.writeU8(static_cast<u8>(PayloadTag::MoveEntity));
                writeTargets(w, p.targets);
                writeVec2(w, p.value);
                w.writeBool(p.absolute);
            } else if constexpr (std::is_same_v<T, cmd::AddComponent>) {
                w.writeU8(static_cast<u8>(PayloadTag::AddComponent));
                w.writeVarU(p.target);
                writeComponent(w, p.component);
            } else if constexpr (std::is_same_v<T, cmd::RemoveComponent>) {
                w.writeU8(static_cast<u8>(PayloadTag::RemoveComponent));
                w.writeVarU(p.target);
                w.writeVarU(p.stableId);
            } else if constexpr (std::is_same_v<T, cmd::ChangeComponent>) {
                w.writeU8(static_cast<u8>(PayloadTag::ChangeComponent));
                w.writeVarU(p.target);
                w.writeVarU(p.stableId);
                writeJson(w, p.patch);
            } else if constexpr (std::is_same_v<T, cmd::PaintTerrain>) {
                w.writeU8(static_cast<u8>(PayloadTag::PaintTerrain));
                w.writeString(p.materialId);
                w.writeVarU(p.cells.size());
                for (const Vec2i c : p.cells) {
                    w.writeVarI(c.x);
                    w.writeVarI(c.y);
                }
                w.writeVarI(p.center.x);
                w.writeVarI(p.center.y);
                w.writeU8(static_cast<u8>(p.shape));
                w.writeVarU(p.radius);
            } else if constexpr (std::is_same_v<T, cmd::PauseSimulation>) {
                w.writeU8(static_cast<u8>(PayloadTag::PauseSimulation));
            } else if constexpr (std::is_same_v<T, cmd::ResumeSimulation>) {
                w.writeU8(static_cast<u8>(PayloadTag::ResumeSimulation));
            } else if constexpr (std::is_same_v<T, cmd::StepSimulation>) {
                w.writeU8(static_cast<u8>(PayloadTag::StepSimulation));
                w.writeVarU(p.ticks);
            } else if constexpr (std::is_same_v<T, cmd::SetSimulationSpeed>) {
                w.writeU8(static_cast<u8>(PayloadTag::SetSimulationSpeed));
                w.writeF32(p.speed);
            } else {
                static_assert(sizeof(T) == 0, "새 명령의 와이어 형식을 정하십시오 (PayloadTag 에 새 값)");
            }
        },
        payload);
}

bool readPayload(BitReader& r, cmd::CommandPayload& out) {
    const auto tag = static_cast<PayloadTag>(r.readU8());
    if (r.error()) {
        return false;
    }
    switch (tag) {
    case PayloadTag::CreateEntity: {
        cmd::CreateEntity p;
        p.position = readVec2(r);
        p.prefab = r.readString(kMaxContentIdBytes);
        const u64 n = r.readVarU(sim::kMaxCommandComponents);
        for (u64 i = 0; i < n && !r.error(); ++i) {
            cmd::ComponentValue c;
            if (readComponent(r, c)) {
                p.components.push_back(std::move(c));
            }
        }
        out = std::move(p);
        break;
    }
    case PayloadTag::DeleteEntity:
        out = cmd::DeleteEntity{readTargets(r)};
        break;
    case PayloadTag::MoveEntity: {
        cmd::MoveEntity p;
        p.targets = readTargets(r);
        p.value = readVec2(r);
        p.absolute = r.readBool();
        out = std::move(p);
        break;
    }
    case PayloadTag::AddComponent: {
        cmd::AddComponent p;
        p.target = readTarget(r);
        readComponent(r, p.component);
        out = std::move(p);
        break;
    }
    case PayloadTag::RemoveComponent: {
        cmd::RemoveComponent p;
        p.target = readTarget(r);
        p.stableId = r.readVarU();
        out = p;
        break;
    }
    case PayloadTag::ChangeComponent: {
        cmd::ChangeComponent p;
        p.target = readTarget(r);
        p.stableId = r.readVarU();
        readJson(r, p.patch);
        out = std::move(p);
        break;
    }
    case PayloadTag::PaintTerrain: {
        cmd::PaintTerrain p;
        p.materialId = r.readString(kMaxContentIdBytes);
        const u64 n = r.readVarU(sim::kMaxPaintTiles);
        if (r.remainingBits() < n * 16) { // 칸마다 최소 2바이트
            r.fail();
        }
        for (u64 i = 0; i < n && !r.error(); ++i) {
            Vec2i c;
            c.x = readI32(r);
            c.y = readI32(r);
            p.cells.push_back(c);
        }
        p.center.x = readI32(r);
        p.center.y = readI32(r);
        const u8 shape = r.readU8();
        if (shape > static_cast<u8>(cmd::BrushShape::Circle)) {
            r.fail();
        }
        p.shape = static_cast<cmd::BrushShape>(shape);
        p.radius = static_cast<u32>(r.readVarU(sim::kMaxBrushRadius));
        out = std::move(p);
        break;
    }
    case PayloadTag::PauseSimulation:
        out = cmd::PauseSimulation{};
        break;
    case PayloadTag::ResumeSimulation:
        out = cmd::ResumeSimulation{};
        break;
    case PayloadTag::StepSimulation:
        out = cmd::StepSimulation{static_cast<u32>(r.readVarU(sim::kMaxStepTicks))};
        break;
    case PayloadTag::SetSimulationSpeed: {
        const f32 speed = r.readF32();
        if (!std::isfinite(speed)) {
            r.fail();
        }
        out = cmd::SetSimulationSpeed{speed};
        break;
    }
    default:
        r.fail();
        break;
    }
    return !r.error();
}

} // namespace sbx::net
