#pragma once
// 콘텐츠 창의 항목 (v0.12 ST-06-10 에서 ContentPanel.cpp 밖으로 — GraphicsUI 콘텐츠 패널과 같이 쓴다, ADR-0182). ImGui 를 모른다.
//
//   항목     종류 (규칙 · 행동 · Prefab · 트리거 · 스크립트) 의 id 와 오버레이 여부 — 콘텐츠 차례 (트리거는 실행 순서, 스크립트는
//            팩의 scripts/*.ts + 오버레이로 더한 파일, id 정렬)
//   지금 값  JSON 종류는 원본 (raw) 의 dump(2), 스크립트는 소스 (오버레이 → 팩 원본)

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentOverlay.hpp"
#include "core/serialization/JsonVisitor.hpp"

namespace sbx::content {
class ContentDatabase;
}

namespace sbx::editor {

struct ContentItem {
    std::string id;
    bool overlay = false;
    const ecs::Json* raw = nullptr; // 스크립트는 없다 (소스 — scriptSourceOf)
};

// 스크립트 오버레이 id 의 팩 파일 (v0.3 M5-T2) — 팩 폴더 · 원본 텍스트. 팩에 없는 (오버레이로 더한) 파일이면 text 가 없다
struct ScriptFile {
    std::string display; // "<팩 폴더>/scripts/a.ts" (콘텐츠 문제의 file 과 같다)
    const std::string* text = nullptr;
    std::filesystem::path disk; // 이 컴퓨터의 그 파일 (팩 폴더를 모르면 비었다)
    bool installed = false;     // 설치한 패키지 (.sandbox/packages/ — [저장] 하지 않는다)
};

[[nodiscard]] std::optional<ScriptFile> scriptFileOf(const content::ContentDatabase& db, std::string_view id);
// 지금 소스 (오버레이 → 팩 원본). 없으면 nullopt
[[nodiscard]] std::optional<std::string> scriptSourceOf(const content::ContentDatabase& db, std::string_view id);
[[nodiscard]] std::vector<ContentItem> contentItems(const content::ContentDatabase& db, content::ContentKind kind);
// JSON 종류의 원본 (스크립트 · 없는 id 는 nullptr)
[[nodiscard]] const ecs::Json* contentRawOf(const content::ContentDatabase& db, content::ContentKind kind,
                                            std::string_view id);
// 고르는 항목의 지금 값 — JSON 종류는 dump(2), 스크립트는 소스. 없으면 nullopt
[[nodiscard]] std::optional<std::string> currentContentText(const content::ContentDatabase& db,
                                                            content::ContentKind kind, std::string_view id);

} // namespace sbx::editor
