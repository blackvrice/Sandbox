#pragma once
// 게임 UI 의 ImGui 백엔드 (v0.7 M3-T3 — ADR-0079 4). 배경 그리기 목록 (게임 화면 위 · 도구 패널 아래) 에 도형 · 글자만
// — ImGui 위젯 · ID · 상태는 쓰지 않는다. 좌표는 ImGui 의 논리 단위 (창 크기 — DPI 는 ImGui 가 프레임버퍼 배율로
// 그린다). image 는 v0.7 M3-T3 에서 material 이름의 고정 색 사각형 — 텍스처는 [계획] (ImGui 텍스처 id ↔ 아틀라스)

#include <imgui.h>

#include "ui/layout/UiLayout.hpp"
#include "ui/render/UiDraw.hpp"

namespace sbx::client {

class ImGuiUiRenderer final : public ui::IUiRenderer {
public:
    explicit ImGuiUiRenderer(ImDrawList* list) : m_list(list) {}
    void fillRect(const ui::UiRect& rect, ui::UiColor color, const ui::UiRect& clip) override;
    void strokeRect(const ui::UiRect& rect, ui::UiColor color, f32 width, const ui::UiRect& clip) override;
    void text(const ui::UiRect& rect, std::string_view text, f32 fontSize, ui::UiColor color,
              const ui::UiRect& clip) override;
    void image(const ui::UiRect& rect, std::string_view material, const ui::UiRect& clip) override;

private:
    ImDrawList* m_list;
};

// ImGui 의 지금 글꼴로 잰다 (ImGui 프레임 안에서 — 글꼴이 준비된 뒤)
class ImGuiTextMeasure final : public ui::ITextMeasure {
public:
    [[nodiscard]] std::array<f32, 2> measure(std::string_view text, f32 fontSize) const override;
};

} // namespace sbx::client
