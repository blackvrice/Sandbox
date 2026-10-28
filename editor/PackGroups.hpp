#pragma once
// 콘텐츠 항목을 팩별로 묶기 (v0.4 M4-T3, docs/10-EDITOR.md 2 · 8장) — "편집" 패널의 배치 Prefab 목록과 "콘텐츠" 창의
// 목록이 쓴다. ImGui 를 모른다 (시험이 바로 부른다).
//
//   팩       항목 id 의 첫 마디 ("inv.crate" → inv — 콘텐츠 id 는 "<팩>.<이름>", 11 1장). 읽은 팩에 없으면 "기타" 묶음
//   출처     그 팩을 읽은 자리 (ContentSourcePack::folder — 프로젝트 열기가 붙인 접두사, ADR-0073 3):
//            "content/…" = 프로젝트의 팩, ".sandbox/packages/…" = 설치한 패키지, 그 밖 (시나리오 · 세이브 폴더) = 팩
//   차례     프로젝트 → 설치한 패키지 → 팩 → 기타, 같은 출처 안에서는 팩 id 순. 묶음 안의 항목은 받은 차례 그대로

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::content {
class ContentDatabase;
}

namespace sbx::editor {

enum class PackOrigin : u8 { Project, Installed, Pack, Unknown };

struct PackGroup {
    std::string pack; // 팩 id (Unknown 이면 빈 문자열)
    PackOrigin origin = PackOrigin::Unknown;
    std::string label;        // 묶음 머리 — "eco · 프로젝트" · "inv 1.0.0 · MIT · 설치한 패키지" · "eco" · "기타"
    std::vector<usize> items; // ids 의 자리 (받은 차례)
};

[[nodiscard]] std::vector<PackGroup> groupByPack(const content::ContentDatabase& db,
                                                 std::span<const std::string_view> ids);

} // namespace sbx::editor
