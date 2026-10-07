#pragma once
// SandboxServer 명령줄. 전체 목록(계획)은 docs/15-BUILD.md 7장.
// Phase 1: --help / --version / --log-level.  Phase 3: --scenario / --ticks / --seed / --realtime (헤드리스 실행).
// 월드 파일·포트(--world, --port …)는 Phase 9 에서 추가한다.

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

    // Phase 3 헤드리스 실행: 시나리오를 돌리고 최종 해시와 시간을 출력한다
    std::optional<std::string> scenario;
    std::optional<u64> ticks; // 없으면 시나리오 기본값
    u64 seed = 1;
    bool realtime = false;   // true 면 30 Hz 로 페이싱 (기본은 최대 속도)
    std::string contentRoot; // 콘텐츠 팩 루트 (비면 빌드 때 정한 저장소의 content/)
};

[[nodiscard]] Expected<ServerOptions> parseServerOptions(std::span<const std::string_view> args);

[[nodiscard]] std::string serverUsage();

} // namespace sbx::server
