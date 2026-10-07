// BehaviorSystem (Stage 7). docs/03-SIMULATION.md 5장, docs/11-CONTENT-SCHEMA.md 4장.
//
// 엔티티마다 (자기 컴포넌트에만 쓴다 → 순회 순서와 무관)
//   0. pendingAction = 0. 아직 시작 전이면 initial 로 들어간다 (onEnter).
//   1. 전이: (priority 내림, 정의 순) 으로 정렬된 전이 중 from 이 지금 상태이거나 "*" 이고 to 가 지금 상태가 아닌
//      첫 참인 것 하나 → onExit → 상태 바꿈 · enteredTick = tick → onEnter.
//      ("*" → 지금 상태 자신으로의 전이는 건너뛴다 — 매 틱 다시 들어가 stateTime 이 0 으로 돌아가지 않게.)
//   2. 지금 상태의 onTick 노드를 순서대로.
// 난수는 엔티티당 틱마다 스트림 하나(Behavior, saveId)를 열어 노드 평가 순서대로 꺼낸다.
//
// 이동 목표 (setMoveGoal)
//   core.movement.goal 을 바꾸고, ai.path 가 있으면:
//   - 진행 중인 요청·경로의 목표와 1 이내면 그대로 둔다 (움직이는 대상을 쫓을 때 매 틱 다시 요청하지 않게)
//   - 지금 위치에서 목표까지 직선이 막히지 않으면 경로 없이 곧장 (state None)
//   - 막혔으면 Pending → Stage 8 이 Job 을 제출한다

#include <cmath>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Movement.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/life/Life.hpp"
#include "core/path/PathGrid.hpp"
#include "core/systems/AiCommon.hpp"
#include "core/systems/AiSystems.hpp"

namespace sbx::sys {
namespace {

using content::ActionNode;
using content::Condition;

constexpr f32 kRepathDistance = 1.0f; // 목표가 이만큼 움직여야 경로를 다시 요청한다
constexpr u32 kWanderTries = 8;

class Evaluator {
public:
    Evaluator(sim::SystemContext& ctx, ecs::EntityId e, SaveId self, const content::BehaviorGraph& g, Vec2 position)
        : m_ctx(ctx), m_e(e), m_g(g), m_pos(position), m_sensor(ctx.reg.tryRead<comp::Sensor>(e)),
          m_rng(ctx.random.stream(rnd::Purpose::Behavior, self)) {}

    void run() {
        comp::Behavior& b = m_ctx.reg.write<comp::Behavior>(m_e);
        b.pendingAction = 0;
        if (b.state == comp::kBehaviorNotStarted || b.state >= m_g.states.size()) {
            enter(b, m_g.initial);
        }
        for (const content::Transition& t : m_g.transitions) {
            if ((t.from != content::kAnyState && t.from != b.state) || t.to == b.state) {
                continue;
            }
            if (eval(b, t.when)) {
                runActions(b, m_g.states[b.state].onExit);
                enter(b, t.to);
                break;
            }
        }
        runActions(b, m_g.states[b.state].onTick);
    }

private:
    void enter(comp::Behavior& b, u16 state) {
        b.state = state;
        b.enteredTick = m_ctx.tick;
        runActions(b, m_g.states[state].onEnter);
    }

    [[nodiscard]] const comp::SensedSlot* slot(u8 query) const noexcept {
        if (m_sensor == nullptr || query >= m_g.queries.size() || m_sensor->sensed[query].count == 0) {
            return nullptr;
        }
        return &m_sensor->sensed[query];
    }

    [[nodiscard]] bool eval(const comp::Behavior& b, const Condition& c) {
        using K = Condition::Kind;
        switch (c.kind) {
        case K::True:
            return true;
        case K::EnergyBelow:
        case K::EnergyAbove: {
            const comp::Energy* en = m_ctx.reg.tryRead<comp::Energy>(m_e);
            if (en == nullptr) {
                return false;
            }
            return c.kind == K::EnergyBelow ? en->value < c.value : en->value > c.value;
        }
        case K::HealthBelow: {
            const comp::Health* h = m_ctx.reg.tryRead<comp::Health>(m_e);
            return h != nullptr && h->value < c.value;
        }
        case K::Sensed:
            return slot(c.query) != nullptr;
        case K::TargetValid:
            return ai::resolveSave(m_ctx, b.target).valid();
        case K::TargetInRange: {
            const ecs::EntityId t = ai::resolveSave(m_ctx, b.target);
            if (!t.valid()) {
                return false;
            }
            const comp::Transform* tt = m_ctx.reg.tryRead<comp::Transform>(t);
            const f64 r = c.value;
            return tt != nullptr && static_cast<f64>((tt->position - m_pos).lengthSquared()) <= r * r;
        }
        case K::StateTime: {
            const f64 seconds = static_cast<f64>(m_ctx.tick - b.enteredTick) / static_cast<f64>(sim::kTickRate);
            return content::compare(seconds, c.op, c.value);
        }
        case K::Random:
            return static_cast<f64>(m_rng.unitF32()) < c.value;
        case K::And:
            for (const Condition& child : c.children) {
                if (!eval(b, child)) {
                    return false;
                }
            }
            return true;
        case K::Or:
            for (const Condition& child : c.children) {
                if (eval(b, child)) {
                    return true;
                }
            }
            return false;
        case K::Not:
            return !c.children.empty() && !eval(b, c.children.front());
        }
        return false;
    }

    void runActions(comp::Behavior& b, const std::vector<ActionNode>& actions) {
        for (const ActionNode& a : actions) {
            runAction(b, a);
        }
    }

    void runAction(comp::Behavior& b, const ActionNode& a) {
        using K = ActionNode::Kind;
        switch (a.kind) {
        case K::Seek: {
            const comp::SensedSlot* s = slot(a.query);
            if (s == nullptr) {
                b.target = kInvalidSaveId; // 보이지 않으면 대상을 놓는다
                stop();
                return;
            }
            b.target = s->nearestSaveId;
            if (const comp::Transform* tt = m_ctx.reg.tryRead<comp::Transform>(s->nearest)) {
                setMoveGoal(tt->position);
            }
            return;
        }
        case K::Flee: {
            const comp::SensedSlot* s = slot(a.query);
            if (s == nullptr) {
                return;
            }
            const comp::Transform* tt = m_ctx.reg.tryRead<comp::Transform>(s->nearest);
            if (tt == nullptr) {
                return;
            }
            Vec2 away = m_pos - tt->position;
            const f32 l2 = away.lengthSquared();
            away = l2 > 1.0e-12f ? away / std::sqrt(l2) : Vec2{1.f, 0.f};
            // 목표가 물·바위 위면 닿을 수 없는 목표로 A* 가 확장 상한까지 헤맨다 → 거리를 줄여 가며 통행 가능한 점을
            // 고른다
            for (const f32 k : {1.f, 0.75f, 0.5f, 0.25f}) {
                const Vec2 goal = m_ctx.grid.clampPoint(m_pos + away * (a.distance * k));
                if (ai::passable(m_ctx.grid, path::tileOf(goal))) {
                    setMoveGoal(goal);
                    return;
                }
            }
            return;
        }
        case K::Wander: {
            const auto interval =
                static_cast<u64>(std::max(1.0, std::floor(static_cast<f64>(a.interval) * sim::kTickRate + 0.5)));
            if ((m_ctx.tick - b.enteredTick) % interval != 0) {
                return;
            }
            for (u32 i = 0; i < kWanderTries; ++i) {
                const Vec2 off{m_rng.rangeF32(-1.f, 1.f), m_rng.rangeF32(-1.f, 1.f)};
                if (off.lengthSquared() > 1.f) {
                    continue;
                }
                const Vec2 goal = m_ctx.grid.clampPoint(m_pos + off * a.radius);
                if (ai::passable(m_ctx.grid, path::tileOf(goal))) {
                    setMoveGoal(goal);
                    return;
                }
            }
            return;
        }
        case K::Interact:
            b.pendingAction = a.actionId;
            stop();
            return;
        case K::Idle:
            stop();
            return;
        case K::SetBlackboard:
            b.blackboard[a.slot] = static_cast<f32>(a.value);
            return;
        }
    }

    void stop() {
        if (comp::Movement* mv = m_ctx.reg.tryWrite<comp::Movement>(m_e); mv != nullptr && mv->hasGoal) {
            mv->hasGoal = false;
        }
        if (comp::Path* p = m_ctx.reg.tryWrite<comp::Path>(m_e);
            p != nullptr && (p->state != comp::PathState::None || !p->waypoints.empty())) {
            p->state = comp::PathState::None;
            p->waypoints.clear();
            p->cursor = 0;
            p->partial = false;
        }
    }

    void setMoveGoal(Vec2 goal) {
        comp::Movement* mv = m_ctx.reg.tryWrite<comp::Movement>(m_e);
        if (mv == nullptr) {
            return;
        }
        mv->goal = goal;
        mv->hasGoal = true;
        comp::Path* p = m_ctx.reg.tryWrite<comp::Path>(m_e);
        if (p == nullptr) {
            return;
        }
        const bool near = (p->goal - goal).lengthSquared() <= kRepathDistance * kRepathDistance;
        const bool active = p->state == comp::PathState::Pending || p->state == comp::PathState::Submitted ||
                            p->state == comp::PathState::Following;
        if (near && (active || p->state == comp::PathState::Failed)) {
            return; // 진행 중인 경로를 그대로 쓴다 (실패한 목표를 매 틱 다시 요청하지도 않는다)
        }
        const auto open = [this](Vec2i t) { return ai::passable(m_ctx.grid, t); };
        p->goal = goal;
        if (path::tileLineClear(m_pos, goal, open)) {
            p->state = comp::PathState::None;
            p->waypoints.clear();
            p->cursor = 0;
            p->partial = false;
        } else {
            p->state = comp::PathState::Pending; // 새 결과가 올 때까지 옛 경유점을 계속 따라간다
        }
    }

    sim::SystemContext& m_ctx;
    ecs::EntityId m_e;
    const content::BehaviorGraph& m_g;
    Vec2 m_pos;
    const comp::Sensor* m_sensor;
    rnd::CounterRng m_rng;
};

} // namespace

void BehaviorSystem::run(sim::SystemContext& ctx) {
    for (auto [e, beh, id, tr] :
         ctx.reg.view<ecs::Read<comp::Behavior>, ecs::Read<comp::Persistence>, ecs::Read<comp::Transform>>()) {
        const content::BehaviorGraph* g = ai::graphOf(ctx.content, beh);
        if (g == nullptr || g->states.empty()) {
            continue;
        }
        Evaluator(ctx, e, id.saveId, *g, tr.position).run();
    }
}

} // namespace sbx::sys
