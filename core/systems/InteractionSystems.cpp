// InteractionSystem (Stage 10) · ResolveIntentsSystem (Stage 11). docs/03-SIMULATION.md 6.2·6.3, 02-ECS E1.
//
// Interaction: interact(action) 을 낸 엔티티(ai.behavior.pendingAction)마다 대상(ai.behavior.target)에 대해
//   rulesForAction 순서(priority 내림, 정의 순)로 첫 번째로 맞는 Rule 하나 — source 태그 · target 태그 ·
//   거리 ≤ range · conditions 모두 — 를 Intent 로 쌓는다. 상태를 읽기만 하므로 순회 순서와 무관하다.
//
// Resolve: Intent 를 (target saveId, priority 내림, rule 정의 순, source saveId) 로 정렬한 뒤 차례로
//   - 이번 틱에 이미 파괴된(destroy 효과) source·target 의 Intent 는 건너뛴다
//   - 배타 Rule 은 target 당 첫 번째만 (destroy(target) 이 있으면 로더가 배타로 만든다)
//   - 효과를 정의 순서로 적용: field.add/set (FieldMeta 범위로 자름), destroy (ECB + Died{Killed}),
//     spawn (SpawnQueue — 위치는 who 주변 ±0.5, 난수 Interaction 스트림), tag.add/remove, event (EventStream.Custom)
//   조건은 Stage 10 에서 한 번만 본다 — 같은 틱 앞선 Intent 의 효과로 조건이 바뀌어도 다시 보지 않는다.

#include <algorithm>
#include <optional>
#include <unordered_set>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/systems/AiCommon.hpp"
#include "core/systems/AiSystems.hpp"

namespace sbx::sys {
namespace {

[[nodiscard]] content::TagSet tagsOf(const ecs::Registry& reg, ecs::EntityId e) noexcept {
    const comp::Tags* t = reg.tryRead<comp::Tags>(e);
    return t != nullptr ? t->set : content::TagSet{};
}

[[nodiscard]] std::optional<f64> readNumber(const sim::SystemContext& ctx, ecs::EntityId e,
                                            const content::FieldRef& ref) {
    const ecs::ComponentPoolBase* pool = ctx.reg.poolByStableId(ref.component);
    const ecs::ComponentInfo* info = ctx.catalog.find(ref.component);
    if (pool == nullptr || info == nullptr || info->getNumber == nullptr) {
        return std::nullopt;
    }
    const void* raw = pool->getRaw(e);
    return raw != nullptr ? info->getNumber(raw, ref.field) : std::nullopt;
}

void writeNumber(sim::SystemContext& ctx, ecs::EntityId e, const content::FieldRef& ref, f64 value, bool add) {
    ecs::ComponentPoolBase* pool = ctx.reg.poolByStableId(ref.component);
    const ecs::ComponentInfo* info = ctx.catalog.find(ref.component);
    if (pool == nullptr || info == nullptr || info->applyNumber == nullptr || pool->getRaw(e) == nullptr) {
        return; // 그 컴포넌트가 없으면 효과 없음
    }
    (void)info->applyNumber(pool->getRawForWrite(e, ctx.tick), ref.field, value, add);
}

} // namespace

void InteractionSystem::run(sim::SystemContext& ctx) {
    const auto rules = ctx.content.rules();
    for (auto [e, beh, id, tr] :
         ctx.reg.view<ecs::Read<comp::Behavior>, ecs::Read<comp::Persistence>, ecs::Read<comp::Transform>>()) {
        if (beh.pendingAction == 0) {
            continue;
        }
        const ecs::EntityId target = ai::resolveSave(ctx, beh.target);
        if (!target.valid() || target == e) {
            continue;
        }
        const comp::Transform* tt = ctx.reg.tryRead<comp::Transform>(target);
        if (tt == nullptr) {
            continue;
        }
        const content::TagSet srcTags = tagsOf(ctx.reg, e);
        const content::TagSet dstTags = tagsOf(ctx.reg, target);
        const f32 dist2 = (tt->position - tr.position).lengthSquared();
        for (const u32 ri : ctx.content.rulesForAction(beh.pendingAction)) {
            const content::Rule& rule = rules[ri];
            if (!rule.source.matches(srcTags) || !rule.target.matches(dstTags) || dist2 > rule.range * rule.range) {
                continue;
            }
            const bool ok = std::all_of(rule.conditions.begin(), rule.conditions.end(), [&](const auto& c) {
                const auto v = readNumber(ctx, c.who == content::Who::Source ? e : target, c.ref);
                return v.has_value() && content::compare(*v, c.op, c.value);
            });
            if (ok) {
                ctx.intents.push(sim::Intent{&rule, e, target, id.saveId, beh.target});
                break;
            }
        }
    }
}

void ResolveIntentsSystem::run(sim::SystemContext& ctx) {
    auto& items = ctx.intents.items();
    if (items.empty()) {
        return;
    }
    std::sort(items.begin(), items.end(), [](const sim::Intent& a, const sim::Intent& b) {
        if (a.targetSave != b.targetSave) {
            return a.targetSave < b.targetSave;
        }
        if (a.rule->priority != b.rule->priority) {
            return a.rule->priority > b.rule->priority;
        }
        if (a.rule->order != b.rule->order) {
            return a.rule->order < b.rule->order;
        }
        return a.sourceSave < b.sourceSave;
    });

    std::unordered_set<SaveId> destroyed; // 조회 전용 (순회하지 않는다)
    SaveId exclusiveTaken = kInvalidSaveId;
    for (const sim::Intent& in : items) {
        if (destroyed.contains(in.sourceSave) || destroyed.contains(in.targetSave)) {
            continue;
        }
        if (in.rule->exclusive) {
            if (exclusiveTaken == in.targetSave) {
                continue; // 정렬되어 있으므로 같은 target 의 배타 Intent 는 연속해서 온다
            }
            exclusiveTaken = in.targetSave;
        }
        for (const content::RuleEffect& fx : in.rule->effects) {
            const bool toSource = fx.who == content::Who::Source;
            const ecs::EntityId who = toSource ? in.source : in.target;
            const SaveId whoSave = toSource ? in.sourceSave : in.targetSave;
            switch (fx.op) {
            case content::EffectOp::FieldAdd:
            case content::EffectOp::FieldSet:
                writeNumber(ctx, who, fx.field, fx.value, fx.op == content::EffectOp::FieldAdd);
                break;
            case content::EffectOp::Destroy:
                if (destroyed.insert(whoSave).second) {
                    ctx.ecb.destroy(who);
                    ctx.events.push(sim::SimEvent{sim::EventKind::Died, who, whoSave,
                                                  static_cast<u64>(sim::DeathCause::Killed), 0});
                }
                break;
            case content::EffectOp::Spawn: {
                const content::Prefab* prefab = ctx.content.findPrefab(fx.prefab);
                const comp::Transform* t = ctx.reg.tryRead<comp::Transform>(who);
                if (prefab == nullptr || t == nullptr) {
                    break;
                }
                auto rng = ctx.random.stream(rnd::Purpose::Interaction, whoSave);
                for (u32 i = 0; i < fx.count; ++i) {
                    const Vec2 off{rng.rangeF32(-0.5f, 0.5f), rng.rangeF32(-0.5f, 0.5f)};
                    ctx.spawns.push(whoSave, prefab, ctx.grid.clampPoint(t->position + off));
                }
                break;
            }
            case content::EffectOp::TagAdd:
            case content::EffectOp::TagRemove:
                if (comp::Tags* tags = ctx.reg.tryWrite<comp::Tags>(who)) {
                    if (fx.op == content::EffectOp::TagAdd) {
                        tags->set.set(fx.tag);
                    } else {
                        tags->set.reset(fx.tag);
                    }
                }
                break;
            case content::EffectOp::Event:
                ctx.events.push(sim::SimEvent{sim::EventKind::Custom, who, whoSave, fx.eventCode, 0});
                break;
            }
        }
    }
}

} // namespace sbx::sys
