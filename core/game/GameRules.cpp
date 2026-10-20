#include "core/game/GameRules.hpp"

#include <algorithm>
#include <format>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/simulation/SimulationWorld.hpp"

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

// 세션 엔티티의 점수를 0 으로 (없으면 붙인다 — M2 의 세이브에는 game.score 가 없다)
void resetScore(ecs::Registry& reg, ecs::EntityId session) {
    if (comp::GameScore* sc = reg.tryWrite<comp::GameScore>(session)) {
        *sc = comp::GameScore{};
    } else {
        reg.emplace<comp::GameScore>(session, comp::GameScore{});
    }
}

} // namespace

ecs::EntityId findSessionEntity(const ecs::Registry& reg) noexcept {
    const auto* pool = reg.findPool<comp::GameSession>();
    if (pool == nullptr || pool->empty()) {
        return ecs::kNullEntity;
    }
    // 하나뿐인 것이 보통이다. 둘 이상이면 saveId 가 가장 작은 것 (dense 순서는 세이브/로드에 따라 다르다).
    // saveId 가 없는 것은 saveId 가 있는 것이 하나도 없을 때만 — 클라이언트 복제 월드 (persist.persistence 는 복제하지
    // 않는다, 표시만 한다)
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
    return best != ecs::kNullEntity ? best : pool->entities().front();
}

const comp::GameSession* findSession(const ecs::Registry& reg) noexcept {
    const ecs::EntityId e = findSessionEntity(reg);
    return e == ecs::kNullEntity ? nullptr : reg.tryRead<comp::GameSession>(e);
}

const comp::GameScore* findScore(const ecs::Registry& reg) noexcept {
    const ecs::EntityId e = findSessionEntity(reg);
    return e == ecs::kNullEntity ? nullptr : reg.tryRead<comp::GameScore>(e);
}

bool addScore(ecs::Registry& reg, const content::ContentDatabase& content, const content::RuleEffect& fx,
              ecs::EntityId who) {
    const ecs::EntityId e = findSessionEntity(reg);
    if (e == ecs::kNullEntity || reg.tryRead<comp::GameScore>(e) == nullptr) {
        return false;
    }
    const comp::GameSession& s = reg.read<comp::GameSession>(e);
    if (s.gameState() != comp::GameState::Running) {
        return false; // 카운트다운 · 대기 · 끝난 뒤의 점수는 세지 않는다
    }
    const content::GameMode* mode = content.findMode(std::string_view(s.mode.view()));
    if (mode == nullptr) {
        return false;
    }
    usize slot = 0;
    if (!fx.team.empty()) {
        const auto it = std::find_if(mode->teams.begin(), mode->teams.end(),
                                     [&](const content::GameTeam& t) { return t.id == fx.team; });
        if (it == mode->teams.end()) {
            return false; // 이 모드에는 그 팀이 없다 (로더 V2 경고)
        }
        slot = static_cast<usize>(it - mode->teams.begin()) + 1;
    } else if (const comp::Tags* tags = reg.tryRead<comp::Tags>(who)) {
        slot = content::teamOf(mode->teams, tags->set);
    }
    if (slot >= comp::kScoreSlots) {
        return false;
    }
    const i64 sum = i64{reg.read<comp::GameScore>(e).points[slot]} + static_cast<i64>(fx.value);
    reg.write<comp::GameScore>(e).points[slot] =
        static_cast<i32>(std::clamp<i64>(sum, -comp::kMaxScore, comp::kMaxScore));
    return true;
}

std::optional<usize> scoreSlot(const content::GameMode* mode, std::string_view team) noexcept {
    if (team.empty()) {
        return usize{0};
    }
    if (mode == nullptr) {
        return std::nullopt;
    }
    const auto it =
        std::find_if(mode->teams.begin(), mode->teams.end(), [&](const content::GameTeam& t) { return t.id == team; });
    if (it == mode->teams.end()) {
        return std::nullopt;
    }
    return static_cast<usize>(it - mode->teams.begin()) + 1;
}

bool finishGame(ecs::Registry& reg, const content::ContentDatabase& content, content::GameResult result,
                std::string_view team, sim::Tick now) {
    const ecs::EntityId e = findSessionEntity(reg);
    if (e == ecs::kNullEntity || reg.read<comp::GameSession>(e).gameState() != comp::GameState::Running) {
        return false;
    }
    const content::GameMode* mode = content.findMode(std::string_view(reg.read<comp::GameSession>(e).mode.view()));
    const auto slot = team.empty() ? std::nullopt : scoreSlot(mode, team);
    comp::GameSession& w = reg.write<comp::GameSession>(e);
    w.state = static_cast<u8>(comp::GameState::Finished);
    w.stateTick = now;
    w.result = static_cast<u8>(outcomeOf(result));
    w.team = slot ? static_cast<u8>(*slot) : u8{0};
    w.rule = 0; // 끝 규칙이 아니라 트리거가 끝냈다
    return true;
}

bool gameHoldsSimulation(const ecs::Registry& reg) noexcept {
    const comp::GameSession* s = findSession(reg);
    return s != nullptr && (s->gameState() == comp::GameState::Lobby || s->gameState() == comp::GameState::Finished);
}

bool endConditionHolds(const content::GameEndCondition& when, sim::Tick elapsed, const ecs::Registry& reg) noexcept {
    if (!when.hasTime && !when.hasCount && !when.hasScore) {
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
    if (when.hasScore) {
        // game.score 가 없으면 모든 칸이 0 인 것으로 (i32 · 조건 값 모두 f64 로 정확하다)
        const comp::GameScore* sc = findScore(reg);
        const i32 v = sc != nullptr && when.scoreSlot < comp::kScoreSlots ? sc->points[when.scoreSlot] : 0;
        if (!content::compare(static_cast<f64>(v), when.scoreOp, static_cast<f64>(when.scoreValue))) {
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

Expected<void> applyGameControl(ecs::Registry& reg, const content::ContentDatabase& content, cmd::GameControlOp op,
                                sim::Tick now) {
    const ecs::EntityId e = findSessionEntity(reg);
    if (e == ecs::kNullEntity) {
        return makeError(ErrorCode::NotFound, "게임 세션이 없다 (게임 모드가 있는 프로젝트를 여십시오)");
    }
    const comp::GameSession& s = reg.read<comp::GameSession>(e);
    const comp::GameState state = s.gameState();
    switch (op) {
    case cmd::GameControlOp::Start: {
        if (state != comp::GameState::Lobby) {
            return makeError(
                ErrorCode::InvalidArgument,
                std::format("{} 에서는 시작할 수 없다 (Lobby 에서만 — 끝난 게임은 재시작)", gameStateName(state)));
        }
        if (content.findMode(std::string_view(s.mode.view())) == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("게임 모드 '{}' 가 콘텐츠에 없다", s.mode.view()));
        }
        comp::GameSession& w = reg.write<comp::GameSession>(e);
        w.state = static_cast<u8>(comp::GameState::Starting);
        w.stateTick = now;
        w.result = 0;
        w.team = 0;
        w.rule = 0;
        resetScore(reg, e);
        return {};
    }
    case cmd::GameControlOp::Abort: {
        if (state != comp::GameState::Starting && state != comp::GameState::Running) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} 에서는 중단할 수 없다 (Starting · Running 에서만)", gameStateName(state)));
        }
        comp::GameSession& w = reg.write<comp::GameSession>(e);
        w.state = static_cast<u8>(comp::GameState::Finished);
        w.stateTick = now;
        w.result = static_cast<u8>(comp::GameOutcome::None);
        w.team = 0;
        w.rule = 0;
        return {};
    }
    }
    return makeError(ErrorCode::InvalidArgument, "모르는 GameControl");
}

Expected<void> resetSession(sim::SimulationWorld& world, std::string_view mode) {
    if (world.content().findMode(mode) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("게임 모드 '{}' 가 콘텐츠에 없다", mode));
    }
    ContentId id;
    if (!id.assign(mode)) {
        return makeError(ErrorCode::InvalidArgument, std::format("게임 모드 id 가 너무 길다: {}", mode));
    }
    comp::GameSession fresh;
    fresh.mode = id;
    fresh.state = static_cast<u8>(comp::GameState::Lobby);
    fresh.stateTick = world.currentTick();
    ecs::Registry& reg = world.registry();
    if (const ecs::EntityId e = findSessionEntity(reg); e != ecs::kNullEntity) {
        reg.write<comp::GameSession>(e) = fresh;
        resetScore(reg, e);
        return {};
    }
    const ecs::EntityId e = reg.create();
    reg.emplace<comp::GameSession>(e, fresh);
    reg.emplace<comp::GameScore>(e, comp::GameScore{});
    world.assignPendingIdentities();
    return {};
}

std::string_view gameOutcomeName(comp::GameOutcome o) noexcept {
    switch (o) {
    case comp::GameOutcome::None:
        return "none";
    case comp::GameOutcome::Win:
        return "win";
    case comp::GameOutcome::Lose:
        return "lose";
    case comp::GameOutcome::Draw:
        return "draw";
    }
    return "?";
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
