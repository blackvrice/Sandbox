#include "network/server/CommandValidator.hpp"

#include <algorithm>
#include <format>
#include <type_traits>

namespace sbx::net {

CommandClass classifyCommand(const cmd::CommandPayload& payload) noexcept {
    return std::visit(
        [](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, cmd::PaintTerrain>) {
                return CommandClass::TerrainEdit;
            } else if constexpr (std::is_same_v<T, cmd::PauseSimulation> || std::is_same_v<T, cmd::ResumeSimulation> ||
                                 std::is_same_v<T, cmd::StepSimulation> || std::is_same_v<T, cmd::SetSimulationSpeed>) {
                return CommandClass::SimulationControl;
            } else {
                return CommandClass::EntityEdit;
            }
        },
        payload);
}

bool roleAllows(Role role, CommandClass c) noexcept {
    switch (c) {
    case CommandClass::PlayerAction:
        return role >= Role::Player;
    case CommandClass::EntityEdit:
    case CommandClass::TerrainEdit:
    case CommandClass::ContentEdit:
        return role >= Role::Editor;
    case CommandClass::SimulationControl:
        return role >= Role::Admin;
    }
    return false;
}

Expected<void> CommandValidator::check(ClientCommandState& state, Role role, u32 sequence,
                                       const cmd::CommandPayload& payload, f64 nowSeconds) const {
    if (sequence <= state.lastSequence) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("명령 순번이 줄었습니다 ({} ≤ {})", sequence, state.lastSequence));
    }
    state.lastSequence = sequence;

    if (state.tokens < 0) {
        state.tokens = m_desc.burst;
        state.lastRefillSeconds = nowSeconds;
    }
    const f64 elapsed = std::max(0.0, nowSeconds - state.lastRefillSeconds);
    state.tokens = std::min(m_desc.burst, state.tokens + elapsed * m_desc.ratePerSecond);
    state.lastRefillSeconds = nowSeconds;
    if (state.tokens < 1.0) {
        return makeError(ErrorCode::RateLimited,
                         std::format("초당 명령 상한 {:.0f} 을 넘었습니다", m_desc.ratePerSecond));
    }
    state.tokens -= 1.0;

    if (!roleAllows(role, classifyCommand(payload))) {
        return makeError(ErrorCode::PermissionDenied, std::format("{} 역할은 {} 명령을 쓸 수 없습니다 (10-EDITOR 7장)",
                                                                  roleName(role), cmd::commandName(payload)));
    }
    return {};
}

} // namespace sbx::net
