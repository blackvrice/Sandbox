#pragma once
// 패키지 (= 콘텐츠 팩) 틀 — sbx new --pack (v0.4 M4-T2, ADR-0071 7). docs/15-BUILD.md 7장.
//
//   pack.json           {"id", "version": "0.1.0", "license", "description"}
//   scripts/main.ts     컴포넌트 <팩>.counter + export function bump (다른 팩은 "@<팩>/main" — ADR-0075)
//   scripts/tick.ts     server 시스템 <팩>.tick (30 틱마다 counter 를 올린다)
//   prefabs/thing.json  <팩>.thing (core.transform + <팩>.counter)
//
// 폴더는 없거나 비어 있어야 한다. 옆의 <폴더>.sbx-new 에 만들어 콘텐츠 검사 (스크립트 컴파일 · 검증 포함 — 오류 · 경고
// 0) 와 패키지 훑기 (digest) 를 통과해야 제자리로 옮긴다 (sbx new 와 같은 태도 — 실패하면 아무것도 남기지 않는다).

#include <filesystem>
#include <string>
#include <string_view>

#include "core/ecs/ComponentCatalog.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::package {

// 라이선스를 적지 않으면 — 배포 전에 바꾼다 (SPDX 식, ADR-0071 1)
inline constexpr std::string_view kDefaultPackLicense = "LicenseRef-proprietary";

struct NewPackResult {
    std::string id;
    std::string license;
    std::string digest; // 만든 팩의 패키지 digest
    usize files = 0;
};

// id = 팩 id (소문자 · 숫자 · '_', core 아님), license = SPDX 식 (비면 kDefaultPackLicense)
[[nodiscard]] Expected<NewPackResult> createPack(const std::filesystem::path& dir, std::string_view id,
                                                 std::string_view license, const ecs::ComponentCatalog& catalog);

} // namespace sbx::package
