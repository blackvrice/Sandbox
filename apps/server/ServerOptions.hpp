#pragma once
// SandboxServer 명령줄. 전체 목록(계획)은 docs/15-BUILD.md 7장.
// Phase 1 에서는 --help / --version / --log-level 만 구현한다. 나머지는 해당 Phase 에서 추가한다.

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "foundation/log/Log.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::server {

struct ServerOptions {
    bool showHelp = false;
    bool showVersion = false;
    std::optional<log::Level> logLevel;
};

[[nodiscard]] Expected<ServerOptions> parseServerOptions(std::span<const std::string_view> args);

[[nodiscard]] std::string serverUsage();

} // namespace sbx::server
