#include "apps/client/studio/ProcessLauncher.hpp"

#include <system_error>

namespace sbx::client {

Expected<u32> ProcessLauncher::launch(const editor::services::TestLaunch& spec) {
    std::error_code ec;
    std::filesystem::create_directories(spec.log.parent_path(), ec);
    Expected<std::unique_ptr<proc::ChildProcess>> child = proc::spawnProcess(spec.argv, {{}, spec.log});
    if (!child) {
        return std::unexpected(child.error());
    }
    const u32 handle = m_next++;
    m_children.emplace(handle, std::move(*child));
    return handle;
}

std::optional<int> ProcessLauncher::exitCode(u32 handle) {
    const auto it = m_children.find(handle);
    return it != m_children.end() ? it->second->poll() : std::optional<int>(-1);
}

void ProcessLauncher::stop(u32 handle) {
    if (const auto it = m_children.find(handle); it != m_children.end()) {
        it->second->terminate();
        (void)it->second->waitFor(2000);
    }
}

} // namespace sbx::client
