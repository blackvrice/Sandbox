#pragma once
// SandboxServer 명령줄. 전체 목록은 docs/15-BUILD.md 7장.
// Phase 1: --help / --version / --log-level.  Phase 3: --scenario / --ticks / --seed / --realtime (헤드리스 실행).
// Phase 9: --world / --port / --bind / --max-clients / --default-role / --threads / --ticks N --exit (네트워크 서버).
// [계획] --autosave · --record-replay · --metrics-csv.

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "foundation/log/Log.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::server {

inline constexpr u16 kDefaultPort = 7777;

struct ServerOptions {
    bool showHelp = false;
    bool showVersion = false;
    std::optional<log::Level> logLevel;

    // Phase 3 헤드리스 실행: 시나리오를 돌리고 최종 해시와 시간을 출력한다
    std::optional<std::string> scenario;
    std::optional<u64> ticks; // --scenario: 진행할 틱 (없으면 시나리오 기본값). --world: --exit 와 함께 멈출 틱
    u64 seed = 1;
    bool realtime = false;   // true 면 30 Hz 로 페이싱 (기본은 최대 속도)
    std::string contentRoot; // 콘텐츠 팩 루트 (비면 빌드 때 정한 저장소의 content/)

    // Phase 9 네트워크 서버: 시나리오 이름 또는 세이브 폴더(world.json 이 있는 곳)
    std::optional<std::string> world;
    u16 port = kDefaultPort; // 0 = 아무 빈 포트 (실제 포트를 출력한다)
    std::string bind;        // 비면 모든 인터페이스
    u32 maxClients = 16;
    std::string defaultRole = "editor";
    std::optional<u32> threads; // 시뮬레이션 Worker 수 (없으면 코어 수 - 2 를 1 ~ 4)
    bool exitAtTicks = false;   // --exit: --ticks 에 도달하면 끝낸다 (CI · 벤치)
    u32 snapshotKBps = 256;     // 클라이언트당 복제 예산 KB/s (0 = 제한 없음, 08 6.3)
};

[[nodiscard]] Expected<ServerOptions> parseServerOptions(std::span<const std::string_view> args);

[[nodiscard]] std::string serverUsage();

} // namespace sbx::server
