#pragma once
// Studio 홈의 도우미 — UI 와 무관 (UI-R2-01, ADR-0175). ImGui 홈 (apps/client/studio/StudioHome) 과 GraphicsUI 홈
// (editor/gui/StudioHomeScreen) 이 같은 검색 · 제안 · 시각 표시를 쓴다 (v0.8 UI-2 에서 옮겨 왔다 — 동작 그대로).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "editor/services/ProjectService.hpp"

namespace sbx::editor::services {

// 검색: 이름 · gameId · 경로에 query 가 (ASCII 대소문자 무시) 들어 있는 최근 목록의 차례. 빈 query = 전부
[[nodiscard]] std::vector<usize> filterRecent(const std::vector<RecentProject>& recent, std::string_view query);
// 새 프로젝트 · 예제 복사의 기본 위치: 가장 최근 프로젝트의 부모 폴더 (없으면 지금 작업 폴더) / 폴더 이름.
// 폴더 이름은 name 의 영숫자 · '-' · '_' (나머지 빼고 — 비면 "project"). 비지 않은 폴더가 있으면 -2, -3 …
[[nodiscard]] std::filesystem::path suggestProjectDir(const std::vector<RecentProject>& recent, std::string_view name);
// gameId 제안 — "me.<이름에서 소문자 · 숫자 · '_'>" (비면 "me.game")
[[nodiscard]] std::string suggestGameId(std::string_view name);
// "2026-11-10T09:12:03Z" → "2026-11-10 09:12 (UTC)" (모르면 그대로)
[[nodiscard]] std::string displayTime(std::string_view utc);

} // namespace sbx::editor::services
