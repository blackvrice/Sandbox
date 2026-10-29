#pragma once
// 사용자 설정 · 안전 모드 (v0.5 M1-T3 — docs/20-CREATOR-ALPHA.md 7.5). 게임 데이터가 아니라 사람의 것 — 프로젝트 ·
// 패키지 밖의 설정 폴더 (ADR-0083):
//
//   설정 폴더   SBX_CONFIG_DIR, 아니면 Windows %APPDATA%\sandbox, macOS ~/Library/Application Support/sandbox,
//               그 밖 $XDG_CONFIG_HOME/sandbox · ~/.config/sandbox
//     client.json   {"schemaVersion": 1, "uiScale": 1.0, "updateCheck": true, "updateFeed": "<파일>"} — 모두 선택
//                   uiScale 0.5~3 (ImGui — DPI 배율에 곱한다), updateCheck false 면 새 버전 알림을 끈다, updateFeed =
//                   릴리스 피드 파일 (ReleaseFeed.hpp — --update-feed 가 이긴다). 모르는 키는 경고 (새 클라이언트가
//                   쓴 키일 수 있다)
//     input.json    키 바인딩 (settings/input.json 형식 — 기본 바인딩 위, --input 아래)
//
//   깨진 파일 (JSON 이 아니다 · 타입 · 범위) 은 같은 폴더의 "<이름>.broken-<UTC yyyyMMdd-HHmmss>" 로 옮기고 알림,
//   기본값으로 시작한다. 더 새 schemaVersion 은 옮기지 않고 읽지도 않는다 (새 클라이언트의 파일 — 알림만).
//   --safe-mode: 둘 다 읽지 않는다 (옮기지도 않는다), UI 배율 1, 패키지 캐시는 빈 임시 폴더 (main 이 정한다).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"
#include "platform/common/ActionMap.hpp"

namespace sbx::client {

inline constexpr std::string_view kClientSettingsFile = "client.json";
inline constexpr std::string_view kUserInputFile = "input.json";
inline constexpr u32 kClientSettingsVersion = 1;
inline constexpr f32 kMinUiScale = 0.5f;
inline constexpr f32 kMaxUiScale = 3.f;

struct ClientSettings {
    f32 uiScale = 1.f;
    bool updateCheck = true;
    std::string updateFeed; // 비면 없음
};

struct ClientSettingsParse {
    ClientSettings settings;           // errors 가 비었을 때만 믿는다
    std::vector<std::string> errors;   // 깨짐 — 옮긴다
    std::vector<std::string> warnings; // 모르는 키
    bool newerVersion = false;         // 더 새 schemaVersion — 옮기지 않고 기본값
};

[[nodiscard]] ClientSettingsParse parseClientSettings(std::string_view jsonText);

[[nodiscard]] std::filesystem::path defaultConfigRoot();

// 깨진 설정 파일을 같은 폴더의 "<이름>.broken-<UTC>" 로 옮긴다 (같은 초면 -2 …). 옮긴 경로
[[nodiscard]] Expected<std::filesystem::path> moveAside(const std::filesystem::path& file);

struct UserSettings {
    ClientSettings settings;
    std::optional<platform::ActionMap> input; // <설정 폴더>/input.json 이 있고 성했을 때
    std::vector<std::string> notices;         // 사람에게 보일 알림 (깨진 파일을 옮김 · 더 새 판 · 모르는 키)
};

// 설정 폴더를 읽는다. 없는 파일은 기본값 (알림 없음)
[[nodiscard]] UserSettings loadUserSettings(const std::filesystem::path& configRoot);

} // namespace sbx::client
