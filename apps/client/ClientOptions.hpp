#pragma once
// SandboxClient 명령줄. 전체 목록(계획)은 docs/15-BUILD.md 7장.
// Phase 6: 빈 창 + 앱 상태기계. Phase 7A: --rhi-* · --vsync · --frames-in-flight. Phase 8A: --seed · --threads ·
// --content · --assets. Phase 8C: --font · --no-ui. Phase 10B: --world · --connect · --name (--direct-sim 은 지웠다).

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "foundation/log/Log.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::client {

struct ClientOptions {
    bool showHelp = false;
    bool showVersion = false;
    std::optional<log::Level> logLevel;

    bool console = false;      // Windows: 콘솔을 붙인다 (GUI 서브시스템 실행 파일)
    bool headless = false;     // OS 창 없이 HeadlessWindow 로 (CI·창 없는 OS)
    std::optional<u64> frames; // 이만큼 돌고 끝 (없으면 창을 닫을 때까지)
    std::optional<double> fps; // 프레임 페이싱 Hz. 0 = 페이싱 없음. 기본: 창 60, --headless 0
    std::string inputFile;     // 기본 바인딩 위에 덮어쓸 settings/input.json
    bool logInput = false;     // 입력 이벤트를 로그로 (QA)
    u32 width = 1600;          // 논리 단위
    u32 height = 900;

    // 렌더링 (Phase 7A, docs/06-RENDERING.md 13장)
    bool noRender = false;  // --no-render: 창만 (Phase 6 동작)
    bool rhiDebug = false;  // --rhi-debug: D3D12 Debug Layer
    bool rhiGbv = false;    // --rhi-gbv: GPU-Based Validation (Debug Layer 포함)
    bool rhiWarp = false;   // --rhi-warp: 소프트웨어 디바이스
    bool rhiFl11 = false;   // --rhi-fl11: FL 11_0 어댑터도 허용 (오래된 GPU · Wine 시험)
    bool vsync = true;      // --vsync on|off
    u32 framesInFlight = 2; // --frames-in-flight 2|3

    // 월드 (Phase 10B — 8A 의 --direct-sim 을 대신한다)
    std::optional<std::string> world;   // --world <시나리오|세이브 폴더>: 같은 프로세스의 서버(LocalServerHost)
    std::optional<std::string> connect; // --connect host:port: 원격 SandboxServer
    std::string name = "player";        // --name: 접속 이름
    u64 seed = 1;                       // --seed (--world)
    std::optional<u32> simThreads;      // --threads: 로컬 서버의 시뮬레이션 Worker 수 (없으면 코어 수로 정한다)
    std::string contentRoot;            // --content (비면 빌드 때 정한 저장소의 content/)
    std::string assetRoot;              // --assets (비면 저장소의 assets/)

    // UI (Phase 8C)
    std::string font;  // --font <ttf|ttc>: ImGui 폰트 (비면 OS 한글 폰트 → 내장)
    bool noUi = false; // --no-ui: ImGui 패널 없이
};

[[nodiscard]] Expected<ClientOptions> parseClientOptions(std::span<const std::string_view> args);

[[nodiscard]] std::string clientUsage();

} // namespace sbx::client
