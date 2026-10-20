#include "core/game/GameRules.hpp"

#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"

namespace sbx::game {
namespace {

// 결과 → 컴포넌트 값 (content::GameResult 는 0 부터, GameOutcome 은 0 = 없음)
comp::GameOutcome outcomeOf(content::GameResult r) noexcept {
    switch (r) {
    case content::GameResult::Win:
        return comp::GameOutcome::Win;
    case content::GameResult::Lose:
        return comp::GameOutcome::Lose;
    case content::GameResult::Draw:
        return comp::GameOutcome::Draw;
    }
    return comp::GameOutcome::None;
}

} // namespace

ecs::EntityId findSessionEntity(const ecs::Registry& reg) noexcept {
    const auto* pool = reg.findPool<comp::GameSession>();
    if (pool == nullptr || pool->empty()) {
        return ecs::kNullEntity;
    }
    // 하나뿐인 것이 보통이다. 둘 이상이면 saveId 가 가장 작은 것 (dense 순서는 세이브/로드에 따라 다르다).
    // 정체성이 아직 없는 것(saveId 0)은 고르지 않는다
    ecs::EntityId best = ecs::kNullEntity;
    SaveId bestId = kInvalidSaveId;
    for (const ecs::EntityId e : pool->entities()) {
        const auto* p = reg.tryRead<comp::Persistence>(e);
        const SaveId id = p != nullptr ? p->saveId : kInvalidSaveId;
        if (id != kInvalidSaveId && (bestId == kInvalidSaveId || id < bestId)) {
            best = e;
            bestId = id;
        }
    }
    return best;
}

const comp::GameSession* findSession(const ecs::Registry& reg) noexcept {
    const ecs::EntityId e = findSessionEntity(reg);
    return e == ecs::kNullEntity ? nullptr : reg.tryRead<comp::GameSession>(e);
}

bool gameHoldsSimulation(const ecs::Registry& reg) noexcept {
    const comp::GameSession* s = findSession(reg);
    return s != nullptr && (s->gameState() == comp::GameState::Lobby || s->gameState() == comp::GameState::Finished);
}

bool endConditionHolds(const content::GameEndCondition& when, sim::Tick elapsed, const ecs::Registry& reg) noexcept {
    if (!when.hasTime && !when.hasCount) {
        return false; // 로더가 막는다 (V8) — 빈 조건은 맞지 않는 것으로
    }
    if (when.hasTime && !content::compare(static_cast<f64>(elapsed), when.timeOp, static_cast<f64>(when.timeTicks))) {
        return false;
    }
    if (when.hasCount) {
        u64 n = 0;
        if (const auto* pool = reg.findPool<comp::Tags>(); pool != nullptr) {
            for (const comp::Tags& t : pool->data()) { // 수 세기는 순서와 무관하다
                if (when.countTags.matches(t.set)) {
                    ++n;
                }
            }
        }
        if (!content::compare(static_cast<f64>(n), when.countOp, static_cast<f64>(when.countValue))) {
            return false;
        }
    }
    return true;
}

std::optional<comp::GameState> stepGameRules(ecs::Registry& reg, const content::ContentDatabase& content,
                                             sim::Tick now) {
    const ecs::EntityId e = findSessionEntity(reg);
    if (e == ecs::kNullEntity) {
        return std::nullopt;
    }
    const comp::GameSession& s = reg.read<comp::GameSession>(e);
    const content::GameMode* mode = content.findMode(std::string_view(s.mode.view()));
    if (mode == nullptr) {
        return std::nullopt; // 콘텐츠에 없는 모드 — 진행하지 않는다 (프로젝트 검사 P5 가 미리 막는다)
    }
    const sim::Tick elapsed = now >= s.stateTick ? now - s.stateTick : 0;

    switch (s.gameState()) {
    case comp::GameState::Starting:
        if (elapsed >= mode->countdownTicks) {
            comp::GameSession& w = reg.write<comp::GameSession>(e);
            w.state = static_cast<u8>(comp::GameState::Running);
            w.stateTick = now;
            return comp::GameState::Running;
        }
        return std::nullopt;
    case comp::GameState::Running:
        for (usize i = 0; i < mode->end.size(); ++i) {
            const content::GameEndRule& rule = mode->end[i];
            if (!endConditionHolds(rule.when, elapsed, reg)) {
                continue;
            }
            comp::GameSession& w = reg.write<comp::GameSession>(e);
            w.state = static_cast<u8>(comp::GameState::Finished);
            w.stateTick = now;
            w.result = static_cast<u8>(outcomeOf(rule.result));
            w.team = rule.team;
            w.rule = static_cast<u8>(i + 1);
            return comp::GameState::Finished;
        }
        return std::nullopt;
    case comp::GameState::Lobby:
    case comp::GameState::Finished:
        return std::nullopt; // 시뮬레이션이 멈춰 있어 여기 오지 않는다 (상태를 직접 바꾼 시험 · 도구)
    }
    return std::nullopt; // 범위 밖 값 (0~3 밖) — 그대로 둔다
}

std::string_view gameStateName(comp::GameState s) noexcept {
    switch (s) {
    case comp::GameState::Lobby:
        return "Lobby";
    case comp::GameState::Starting:
        return "Starting";
    case comp::GameState::Running:
        return "Running";
    case comp::GameState::Finished:
        return "Finished";
    }
    return "?";
}

} // namespace sbx::game
