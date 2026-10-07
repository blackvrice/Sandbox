#pragma once
// sbx_sim_check 명령줄. docs/04-DETERMINISM.md 2장, docs/13-TESTING.md 5장.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/simulation/SimConstants.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::simcheck {

// 종료 코드
inline constexpr int kExitOk = 0;
inline constexpr int kExitMismatch = 1; // 해시 불일치 (repeat · save-at · golden)
inline constexpr int kExitBadArgs = 2;  // 잘못된 인자·파일·시나리오 오류
inline constexpr int kExitNoGolden = 3;

inline constexpr u32 kMaxThreads = 64; // 골든 파일에 이 툴체인 항목이 없다 (CTest 는 SKIP 으로 본다)

struct SimCheckOptions {
    bool showHelp = false;
    bool listScenarios = false;
    std::optional<std::string> scenario; // 없으면 random_walk_1k (골든 파일이 있으면 파일 값)
    std::optional<u64> seed;             // 없으면 1 (골든 파일이 있으면 파일 값)
    std::optional<sim::Tick> ticks;      // 없으면 시나리오 기본값 (골든 파일이 있으면 파일 값)
    std::optional<sim::Tick> hashEvery;  // 없으면 30
    std::vector<sim::Tick> hashAt;       // 비어 있지 않으면 hashEvery 대신 이 틱들
    bool printHash = false;
    u32 repeat = 1; // ≥ 2 면 같은 입력의 월드를 나란히 돌려 매 체크포인트를 비교한다 (D1)
    std::string goldenPath; // 비교할 골든 파일
    std::string recordPath; // 기록할 골든 파일 (이 툴체인 항목을 쓰거나 바꾼다)
    bool profile = false;   // System 별 평균 시간 출력
    // D2: 이 틱이 끝난 뒤 첫 실행을 저장하고, 로드한 월드를 나란히 끝까지 돌려 비교한다
    std::optional<sim::Tick> saveAt;
    std::string saveDir; // 비어 있으면 임시 폴더
    // 콘텐츠: 팩을 찾을 루트 (비면 빌드 때 정한 저장소의 content/)
    std::string contentRoot;
    // --validate-content <팩,팩>: 시뮬레이션 없이 콘텐츠 검증만 (11 8장). 오류가 있으면 종료 코드 1
    std::vector<std::string> validatePacks;
    // Worker 수. 첫 값으로 기본 실행(들)을 돌리고, 나머지 값마다 실행을 하나씩 더해 나란히 비교한다 (D5)
    std::vector<u32> threads{0};
    // D3: 시작 세이브 + 적용된 명령을 리플레이로 기록하고, 세이브를 로드해 재생하며 해시를 비교한다
    bool replayRoundtrip = false;
    std::string replayDir; // 리플레이(start/ + replay.sbxr)를 둘 폴더 (비면 임시 폴더)
    // 기록된 리플레이 파일을 재생해 검사만 한다 (시나리오를 돌리지 않는다)
    std::string replayFile;
    // 끝났을 때 이 Prefab 들이 하나 이상 살아 있어야 한다 (없으면 종료 코드 1). 밸런스 검사용
    std::vector<std::string> requirePrefabs;
    // 하네스 자체 시험: 이 틱에 두 번째 실행에만 작은 이동 명령을 넣어 불일치·진단 경로를 확인한다 (--repeat ≥ 2)
    std::optional<sim::Tick> injectDivergence;
};

[[nodiscard]] Expected<SimCheckOptions> parseSimCheckOptions(std::span<const std::string_view> args);
[[nodiscard]] std::string simCheckUsage();

} // namespace sbx::simcheck
