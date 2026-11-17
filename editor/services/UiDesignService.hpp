#pragma once
// Game UI Designer 의 모델 (v0.10 ST-06-4 조각 1 — docs/25-STUDIO-ARCHITECTURE.md 8장, ADR-0145). ImGui 를 모른다 —
// 패널 (editor/ui) 이 이것을 부른다.
//
// 편집하는 것은 팩의 UI 문서 JSON (presentation/ui/<이름>.json — ADR-0079) 그 자체다 (런타임과 디자이너 사이에 다른
// 형식이 없다). 문서는 DocumentService 로 열고, 편집 하나 = 문서 전체를 다시 쓴 글 (setText — 되돌리기 한 칸). 다시 쓴
// 글은 키 차례를 지킨 JSON (들여쓰기 2 · 끝 줄바꿈) — 손으로 맞춘 한 줄 배치는 처음 편집 때 풀린다. 편집하지 않고
// 저장하면 바이트 그대로.
//
//   노드 경로   root = {} · root 의 children[i] = {i} · 그 아래 = {i, j} … (문서 차례)
//   목록        nodes() — 앞 순서 (부모 먼저), 깊이 · 종류 · id · 표시 이름
//   편집        setProperty · removeProperty (노드의 키 하나) · addChild (요소 라이브러리의 기본값, 겹치지 않는 id) ·
//   remove ·
//               move (형제 사이) · setPlacement (앵커 · 오프셋 — 미리보기의 끌기가 값으로)
//   미리보기    layout (해상도 · 배율 — ui/ 배치 엔진 그대로, 글자 재기는 고정 폭) → 노드 경로마다 사각형
//   저장        문서 검사 (parseUiDocument — 오류가 있으면 쓰지 않는다) → DocumentService::save (바깥 변경 거절 ·
//   원자적 쓰기)

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "editor/services/DocumentService.hpp"
#include "foundation/types/Error.hpp"
#include "ui/layout/UiLayout.hpp"
#include "ui/model/UiDocument.hpp"

namespace sbx::editor::services {

using UiNodePath = std::vector<u32>;

struct UiDesignNode {
    UiNodePath path;
    std::string kind; // "panel" · "text" …
    std::string id;   // 없으면 빈 글자
    u32 depth = 0;
    std::string label; // 목록 표시 — "button #ring '울리기'"
};

struct UiDesignRect {
    UiNodePath path;
    sbx::ui::UiRect rect;
    u32 depth = 0;
};

// 요소 라이브러리 (25 8장) — addChild 의 kind
inline constexpr std::array<std::string_view, 13> kUiLibrary{"panel",     "text",     "image",    "button", "progress",
                                                             "list",      "scroll",   "checkbox", "toggle", "slider",
                                                             "textinput", "dropdown", "tabs"};

// 자식을 가질 수 있는 요소 (panel · scroll · list (항목 틀 하나) · tabs)
[[nodiscard]] bool uiKindHasChildren(std::string_view kind) noexcept;

class UiDesignService {
public:
    explicit UiDesignService(DocumentService& documents);
    UiDesignService(const UiDesignService&) = delete;
    UiDesignService& operator=(const UiDesignService&) = delete;

    // UI 문서를 연다 (이미 열린 문서면 그것 — DocumentService 가 같은 경로를 한 번만 연다). 앞에 연 것은 놓는다 (닫지
    // 않는다)
    Expected<void> open(const std::filesystem::path& file);
    void release() noexcept { m_doc.reset(); }
    [[nodiscard]] bool isOpen() const noexcept { return m_doc.has_value(); }
    [[nodiscard]] std::optional<DocId> document() const noexcept { return m_doc; }

    // 지금 글 (DocumentService 의 것 — 되돌리기 · 다른 편집기의 편집도 본다) 의 결과
    [[nodiscard]] const std::vector<UiDesignNode>& nodes();
    [[nodiscard]] const std::vector<sbx::ui::UiIssue>& issues(); // 문서 검사 (U1 ~ U8 — 콘텐츠 표 없이)
    [[nodiscard]] const std::string& parseError();               // JSON 이 깨졌으면 이유 (아니면 빈 글자)
    [[nodiscard]] std::optional<nlohmann::ordered_json> node(const UiNodePath& path);

    Expected<void> setProperty(const UiNodePath& path, std::string_view key, nlohmann::ordered_json value);
    Expected<void> removeProperty(const UiNodePath& path, std::string_view key);
    // 노드 JSON 을 통째로 (속성 창의 JSON 칸) — kind 가 있어야 한다, children · item 을 적지 않으면 지금 것을 지킨다
    Expected<void> setNode(const UiNodePath& path, nlohmann::ordered_json node);
    // parent 의 children 끝에 kind 의 기본 노드 — 돌려준 경로가 새 노드
    Expected<UiNodePath> addChild(const UiNodePath& parent, std::string_view kind);
    Expected<void> remove(const UiNodePath& path);
    // 형제 사이에서 delta 만큼 (끝을 넘으면 끝) — 돌려준 경로가 옮긴 자리
    Expected<UiNodePath> move(const UiNodePath& path, i32 delta);
    Expected<void> setPlacement(const UiNodePath& path, std::array<f32, 2> anchorMin, std::array<f32, 2> anchorMax,
                                std::array<f32, 4> offset);

    // 미리보기 배치 (문서 검사에 U1 · U3 · U7 오류가 있으면 빈 목록). 앞 순서 — list 항목은 항목마다 같은 경로
    [[nodiscard]] std::vector<UiDesignRect> layout(i32 width, i32 height, f32 scale);

    // 오류가 있으면 쓰지 않는다 (ValidationFailed — 첫 오류를 메시지에)
    Expected<void> save(bool force = false);

private:
    void refresh();
    Expected<nlohmann::ordered_json> current() const;
    Expected<void> commit(const nlohmann::ordered_json& root, std::string label);
    [[nodiscard]] std::string stem() const;

    DocumentService& m_docs;
    std::optional<DocId> m_doc;
    std::string m_seen; // nodes · issues 를 만든 글
    bool m_fresh = false;
    std::vector<UiDesignNode> m_nodes;
    std::vector<sbx::ui::UiIssue> m_issues;
    std::string m_parseError;
};

} // namespace sbx::editor::services
