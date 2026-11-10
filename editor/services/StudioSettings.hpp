#pragma once
// Studio 설정 파일 (v0.8 M6-T6 — docs/25-STUDIO-ARCHITECTURE.md 4장 S4, ADR-0114). 사용자 설정 폴더의 studio.json —
// 게임 클라이언트 설정 (client.json — UserSettings) 과 나눈다 (Studio 의 것은 Studio 만 읽는다).
//
//   {"schemaVersion": 1, "recentProjects": [...] (ProjectService), "layout": "<이름>" (LayoutService — 지금 레이아웃),
//    "autosaveSeconds": 180 (AutosaveService 주기 — --autosave-seconds 가 이긴다, 0 = 끔),
//    "developerPanels": false · "density": "comfortable" | "compact" (Studio 셸 — v0.8 UI-1)}
//
// 여러 서비스가 같은 파일의 자기 키만 쓴다 — 쓰기는 읽고 · 한 키를 바꾸고 · 원자적으로 쓴다 (다른 키 · 모르는 키는
// 그대로). 깨진 파일 (JSON 객체가 아니다) 은 옆 "studio.json.broken" 으로 옮기고 기본값 + 경고 (M1-T3 의 규칙 — 먼저
// 본 서비스가 옮긴다). 키 하나의 타입 · 범위가 틀리면 그 키만 무시하고 경고 (파일은 다른 서비스의 것도 들고 있다). 더
// 새 schemaVersion 은 읽기만 하고 쓰지 않는다 (새 Studio 의 파일 — 경고).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"

namespace sbx::editor::services {

inline constexpr u32 kStudioSettingsVersion = 1;
inline constexpr double kMaxAutosaveSeconds = 86400.0; // --autosave-seconds 와 같은 범위

struct StudioSettings {
    std::string layout;                    // 비면 기본 레이아웃
    std::optional<double> autosaveSeconds; // 없으면 서비스 기본값
    bool developerPanels = false;          // 보기 › 개발자 창 (v0.8 UI-1 — ADR-0118)
    bool compactDensity = false;           // "density": "compact" (v0.8 UI-1)
};

struct StudioSettingsLoad {
    StudioSettings settings;
    std::vector<std::string> warnings;
    bool newerVersion = false; // 쓰지 않는다
};

// 읽는다 (없는 파일 = 기본값). 깨진 파일은 옆으로 옮긴다
[[nodiscard]] StudioSettingsLoad loadStudioSettings(const std::filesystem::path& file);

// key 의 값을 JSON 텍스트 (예: "\"scene\"", "180", "[...]") 로 바꿔 쓴다. 다른 키는 그대로, schemaVersion 은 1 로.
// 깨진 파일이면 옆으로 옮기고 새로 쓴다. 더 새 schemaVersion 이면 쓰지 않고 오류 (Unsupported). file 이 비면 아무 것도
// 하지 않는다 (안전 모드)
[[nodiscard]] Expected<void> writeStudioSetting(const std::filesystem::path& file, std::string_view key,
                                                std::string_view jsonValue);

// 깨진 studio.json 을 "<파일>.broken" 으로 옮긴다 (있던 .broken 은 바꾼다). 사람에게 보일 경고 문장
[[nodiscard]] std::string moveStudioFileAside(const std::filesystem::path& file);

} // namespace sbx::editor::services
