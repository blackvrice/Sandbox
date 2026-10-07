#include "core/command/CommandQueue.hpp"

#include <algorithm>

namespace sbx::cmd {

std::string_view commandName(const CommandPayload& payload) noexcept {
    struct Namer {
        std::string_view operator()(const CreateEntity&) const noexcept { return "CreateEntity"; }
        std::string_view operator()(const DeleteEntity&) const noexcept { return "DeleteEntity"; }
        std::string_view operator()(const MoveEntity&) const noexcept { return "MoveEntity"; }
        std::string_view operator()(const AddComponent&) const noexcept { return "AddComponent"; }
        std::string_view operator()(const RemoveComponent&) const noexcept { return "RemoveComponent"; }
        std::string_view operator()(const ChangeComponent&) const noexcept { return "ChangeComponent"; }
        std::string_view operator()(const PaintTerrain&) const noexcept { return "PaintTerrain"; }
        std::string_view operator()(const PauseSimulation&) const noexcept { return "PauseSimulation"; }
        std::string_view operator()(const ResumeSimulation&) const noexcept { return "ResumeSimulation"; }
        std::string_view operator()(const StepSimulation&) const noexcept { return "StepSimulation"; }
        std::string_view operator()(const SetSimulationSpeed&) const noexcept { return "SetSimulationSpeed"; }
    };
    return std::visit(Namer{}, payload);
}

std::vector<SimCommand> CommandQueue::takeUpTo(sim::Tick tick) {
    std::vector<SimCommand> ready;
    std::vector<SimCommand> later;
    ready.reserve(m_pending.size());
    for (SimCommand& c : m_pending) {
        (c.header.executeTick <= tick ? ready : later).push_back(std::move(c));
    }
    m_pending = std::move(later);
    std::stable_sort(ready.begin(), ready.end(), [](const SimCommand& a, const SimCommand& b) {
        if (a.header.executeTick != b.header.executeTick) {
            return a.header.executeTick < b.header.executeTick;
        }
        if (a.header.issuer != b.header.issuer) {
            return a.header.issuer < b.header.issuer;
        }
        return a.header.sequence < b.header.sequence;
    });
    return ready;
}

} // namespace sbx::cmd
