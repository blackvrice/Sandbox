#pragma once
// 기본 키 바인딩 (settings/input.json 형식, docs/07-PLATFORM.md 5장).
// 실행 파일에 넣어 두어 경로 문제가 없다. 사용자 파일(--input)은 이 위에 액션 단위로 덮어쓴다 (ActionMap::merge).
// [계획] 사용자 설정 경로 platformPaths()/settings/input.json 자동 로드 — Phase 8.

#include <string_view>

namespace sbx::client {

[[nodiscard]] std::string_view defaultInputJson() noexcept;

} // namespace sbx::client
