#pragma once
// 게임 UI 의 ImGui 백엔드 (v0.7 M3-T3 — ADR-0079 4). 배경 그리기 목록 (게임 화면 위 · 도구 패널 아래) 에 도형 · 글자만
// — ImGui 위젯 · ID · 상태는 쓰지 않는다. 좌표는 ImGui 의 논리 단위 (창 크기 — DPI 는 ImGui 가 프레임버퍼 배율로
// 그린다). image 는 v0.7 M3-T3 에서 material 이름의 고정 색 사각형 — 텍스처는 [계획] (ImGui 텍스처 id ↔ 아틀라스)

#include <imgui.h>

#include "apps/client/ui/FontFallback.hpp"
#include "ui/layout/UiLayout.hpp"
#include "ui/render/UiDraw.hpp"

namespace sbx::client {

class ImGuiUiRenderer final : public ui::IUiRenderer {
public:
    // glyphs = 사슬에 없는 글자를 대체 글자로 (v0.7 M4-T2 (2) — 없으면 글자 그대로)
    explicit ImGuiUiRenderer(ImDrawList* list, GlyphCoverage* glyphs = nullptr) : m_list(list), m_glyphs(glyphs) {}
    void fillRect(const ui::UiRect& rect, ui::UiColor color, const ui::UiRect& clip) override;
    void strokeRect(const ui::UiRect& rect, ui::UiColor color, f32 width, const ui::UiRect& clip) override;
    void text(const ui::UiRect& rect, std::string_view text, f32 fontSize, ui::UiColor color,
              const ui::UiRect& clip) override;
    void image(const ui::UiRect& rect, std::string_view material, const ui::UiRect& clip) override;

private:
    ImDrawList* m_list;
    GlyphCoverage* m_glyphs;
};

// ImGui 의 지금 글꼴로 잰다 (ImGui 프레임 안에서 — 글꼴이 준비된 뒤)
class ImGuiTextMeasure final : public ui::ITextMeasure {
public:
    explicit ImGuiTextMeasure(GlyphCoverage* glyphs = nullptr) : m_glyphs(glyphs) {}
    // 그리기와 같은 글자 (대체 글자를 넣은 것) 로 잰다
    [[nodiscard]] std::array<f32, 2> measure(std::string_view text, f32 fontSize) const override;

private:
    GlyphCoverage* m_glyphs;
};

} // namespace sbx::client
