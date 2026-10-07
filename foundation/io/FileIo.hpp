#pragma once
// 파일 읽기·쓰기. docs/09-SERIALIZATION.md 3.4.
//
// 실패는 Expected 로 돌려준다 (예외를 밖으로 내지 않는다 — std::filesystem 의 error_code 오버로드만 쓴다).
// 시뮬레이션 코드가 직접 부르지 않는다: 세이브·로드·도구 경계에서만 쓴다.

#include <filesystem>
#include <string>
#include <string_view>

#include "foundation/types/Error.hpp"

namespace sbx::io {

// 파일 전체를 바이트 그대로 읽는다
[[nodiscard]] Expected<std::string> readFile(const std::filesystem::path& path);

// path 옆 임시 파일에 쓰고 rename 으로 바꿔 넣는다 — 중간에 실패해도 기존 파일이 반쯤 덮이지 않는다.
[[nodiscard]] Expected<void> writeFileAtomic(const std::filesystem::path& path, std::string_view bytes);

// 디렉터리를 (중간 경로까지) 만든다. 이미 있으면 성공.
[[nodiscard]] Expected<void> createDirectories(const std::filesystem::path& path);

// 디렉터리 교체: staging 을 target 자리에 넣는다. target 이 있으면 target.old 로 옮긴 뒤 지운다.
// 두 rename 사이에 실패하면 target.old 가 남는다 (오류 메시지에 경로를 담는다).
[[nodiscard]] Expected<void> replaceDirectory(const std::filesystem::path& staging,
                                              const std::filesystem::path& target);

// 경로를 UTF-8 문자열로 (오류 메시지·로그용)
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

} // namespace sbx::io
