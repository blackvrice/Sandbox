#pragma once
// sbx_net_probe 명령줄. 서버에 접속해 핸드셰이크 · 명령 · 통계를 확인하는 도구 (Phase 9 — 스냅숏이 없어 화면은 없다).
// docs/15-BUILD.md 7장, docs/13-TESTING.md.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "foundation/log/Log.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::probe {

struct NetProbeOptions {
    bool showHelp = false;
    std::optional<log::Level> logLevel;
    std::string connect = "127.0.0.1:7777";
    std::string name = "probe";
    std::string contentRoot;                   // 비면 저장소의 content/
    f64 seconds = 2.0;                         // 명령 결과를 받은 뒤 통계를 보며 머무는 시간
    f64 connectTimeoutSeconds = 10.0;          // 서버가 늦게 뜨는 경우까지 (ENet 이 다시 보낸다)
    std::vector<cmd::CommandPayload> commands; // 접속 뒤 이 순서로 보낸다
};

[[nodiscard]] Expected<NetProbeOptions> parseNetProbeOptions(std::span<const std::string_view> args);
[[nodiscard]] std::string netProbeUsage();

} // namespace sbx::probe
