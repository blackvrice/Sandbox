#pragma once
// 팩 매니페스트 pack.json 의 패키지 키 (v0.4 M1-T1, ADR-0071). docs/11-CONTENT-SCHEMA.md 1.3.
//
// license       SPDX 식 — 식별자 (MIT · Apache-2.0 · LicenseRef-<이름> …, 끝에 "+" 가능) 를 AND · OR ·
//               WITH <예외> · 괄호로 잇는다 (연산자는 대문자). 목록에 있는 식별자인지는 보지 않는다 (모양만)
// dependencies  {팩 id: 범위 | {version?, path} | {version?, git, rev}} — 범위만 적으면 소스는 콘텐츠 루트
//               (requires 와 같다). path · git 은 기록만 — 받기는 v0.4 M2 [계획]
//
// 이 모듈은 모양만 본다. 범위가 실제 팩 version 을 만족하는지는 로더가 팩을 다 읽은 뒤 본다 (V12).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "foundation/version/SemVer.hpp"

namespace sbx::content {

enum class PackSource : u8 { Root, Path, Git };

struct PackDependency {
    std::string pack;
    std::optional<VersionRange> range; // 없으면 범위를 보지 않는다
    PackSource source = PackSource::Root;
    std::string path; // Path — 그 팩 폴더에서의 상대 경로 ('/' 로)
    std::string git;  // Git — 저장소 URL
    std::string rev;  // Git — 태그 · 커밋
};

// SPDX 식의 모양 — 맞으면 nullopt, 아니면 이유
[[nodiscard]] std::optional<std::string> checkSpdxExpression(std::string_view text);

// dependencies 의 값 하나 (키 = 팩 id 는 부르는 쪽이 본다). 오류면 (JSON 포인터 꼬리 — "" 또는 "/version" 등, 이유)
struct PackDependencyError {
    std::string pointer;
    std::string message;
};
[[nodiscard]] std::optional<PackDependencyError> parsePackDependency(const nlohmann::json& value, PackDependency& out);

} // namespace sbx::content
