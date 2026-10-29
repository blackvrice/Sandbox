#include "tools/sbx/Process.hpp"

#include "platform/process/Process.hpp"

namespace sbx::tool {

Expected<package::ProcessResult> runProcess(std::span<const std::string> argv, const std::filesystem::path& cwd) {
    auto r = proc::runProcess(argv, cwd);
    if (!r) {
        return std::unexpected(r.error());
    }
    return package::ProcessResult{r->exitCode, std::move(r->output)};
}

} // namespace sbx::tool
