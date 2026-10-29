#pragma once
// 실행 파일 기준 데이터 폴더 (v0.5 M1-T4 — 배포 묶음, docs/20-CREATOR-ALPHA.md 7.7). 빌드는 저장소의 content/ · assets/
// 절대 경로를 실행 파일에 넣는다 (SBX_DEFAULT_*_DIR) — 그대로 zip 으로 옮기면 다른 PC 에서 찾지 못한다. 배포 묶음은
// 실행 파일 옆에 content/ · assets/ 를 둔다:
//
//   dataDir("content", 빌드 때 경로)  =  <실행 파일 폴더>/content 가 폴더면 그것, 아니면 빌드 때 경로 (개발 빌드)
//
// 실행 파일 경로: Windows GetModuleFileNameW (windows.h 없이 선언 — Console.cpp 와 같다), Linux /proc/self/exe, macOS
// _NSGetExecutablePath. 얻지 못하면 빈 경로 (그러면 늘 빌드 때 경로).

#include <filesystem>
#include <string_view>

namespace sbx::io {

[[nodiscard]] std::filesystem::path executablePath();

[[nodiscard]] std::filesystem::path dataDir(std::string_view name, const std::filesystem::path& buildDefault);

} // namespace sbx::io
