#pragma once
// executeTick 으로 정렬해 꺼내는 명령 큐. docs/03-SIMULATION.md 4장 M5.

#include <vector>

#include "core/command/SimCommand.hpp"

namespace sbx::cmd {

class CommandQueue {
public:
    void push(SimCommand command) { m_pending.push_back(std::move(command)); }

    // executeTick <= tick 인 명령을 (executeTick, issuer, sequence) 순으로 꺼낸다.
    // 같은 키가 둘이면 넣은 순서를 유지한다 (stable) — 정상 경로에서는 생기지 않는다.
    [[nodiscard]] std::vector<SimCommand> takeUpTo(sim::Tick tick);

    [[nodiscard]] usize size() const noexcept { return m_pending.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_pending.empty(); }
    void clear() noexcept { m_pending.clear(); }

private:
    std::vector<SimCommand> m_pending;
};

} // namespace sbx::cmd
