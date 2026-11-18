#pragma once
// "UI 디자이너" 창 (v0.10 ST-06-4 조각 2 — docs/25-STUDIO-ARCHITECTURE.md 8장, ADR-0145). 왼쪽 문서 계층 · 요소
// 라이브러리, 가운데 미리보기 (해상도 · 배율 — ui/ 배치 엔진, 누르면 고르기 · 끌면 오프셋), 오른쪽 속성 (자주 쓰는 키 ·
// JSON 그대로), 아래 검사 결과 · 저장. 편집은 모두 UiDesignService (문서 = DocumentService — 되돌리기 한 줄) 로 — 이
// 창은 파일을 쓰지 않는다.

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "editor/services/UiDesignService.hpp"

namespace sbx::editor {

// 프로젝트의 UI 문서 — content/<팩>/presentation/ui/*.json 중 styles.json 이 아닌 것 (팩 · 이름 순)
[[nodiscard]] std::vector<std::filesystem::path> uiDocumentFiles(const std::filesystem::path& projectDir);

// 미리보기 해상도
struct UiPreviewSize {
    const char* name;
    i32 width;
    i32 height;
};
inline constexpr std::array<UiPreviewSize, 4> kUiPreviewSizes{
    {{"1280 × 720", 1280, 720}, {"1920 × 1080", 1920, 1080}, {"800 × 600", 800, 600}, {"390 × 844 (세로)", 390, 844}}};

// 화면 (canvasW × canvasH) 에 해상도를 맞춘 비율과 왼쪽 위 (가운데 맞춤 — 비율 < 1 이면 줄인다)
struct UiPreviewFit {
    f32 zoom = 1;
    f32 x = 0;
    f32 y = 0;
};
[[nodiscard]] UiPreviewFit fitPreview(f32 canvasW, f32 canvasH, i32 width, i32 height);

// 미리보기 점 (해상도 좌표) 아래의 가장 깊은 노드 — 같으면 나중 (위에 그려진) 것. 없으면 nullopt
[[nodiscard]] std::optional<services::UiNodePath> pickNode(const std::vector<services::UiDesignRect>& rects, f32 x,
                                                           f32 y);

// 끌기 (해상도 좌표 dx · dy, 배율) → 새 오프셋 — 네 칸을 같이 옮긴다 (크기 그대로). 반올림
[[nodiscard]] std::array<f32, 4> dragOffset(const std::array<f32, 4>& offset, f32 dx, f32 dy, f32 scale);

struct UiDesignerPanelState {
    std::filesystem::path file; // 연 문서 (비면 고르기)
    int size = 0;               // kUiPreviewSizes
    f32 scale = 1;
    services::UiNodePath selected;
    // 끌기 — 누른 자리 (해상도 좌표) 와 그때의 오프셋
    bool dragging = false;
    f32 dragX = 0;
    f32 dragY = 0;
    std::array<f32, 4> dragStart{};
    std::string jsonText; // 고른 노드의 JSON (오른쪽 편집 칸)
    std::string jsonFor;  // jsonText 를 만든 경로 · 글 (바뀌면 다시)
    std::string status;   // 마지막 결과 한 줄
};

// 에셋 (id) 을 고른 image 노드에 — 문서의 팩 materials.json 에 "<팩 폴더>/<파일 이름>" = {"sprite": "asset:<id>"}
// (없으면 만든다) 를 두고 노드의 material 을 그 이름으로 (끌어다 놓기 — v0.10 ST-06-3)
Expected<void> dropAssetOnImage(UiDesignerPanelState& state, services::UiDesignService& design, std::string_view id);

// 창을 그린다. files = uiDocumentFiles(프로젝트). 문서를 고르면 design.open, 편집 · 저장은 design 으로
void drawUiDesignerPanel(UiDesignerPanelState& state, services::UiDesignService& design,
                         const std::vector<std::filesystem::path>& files, bool* open = nullptr);

} // namespace sbx::editor
