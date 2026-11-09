#include "apps/client/ui/GameUiImGui.hpp"

#include <cfloat>

#include "foundation/hash/Fnv1a.hpp"

namespace sbx::client {
namespace {

ImU32 rgba(ui::UiColor c) {
    return IM_COL32(c[0], c[1], c[2], c[3]);
}

ImVec2 topLeft(const ui::UiRect& r) {
    return {static_cast<float>(r.x), static_cast<float>(r.y)};
}

ImVec2 bottomRight(const ui::UiRect& r) {
    return {static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h)};
}

// clip 을 걸고 그린다 (ImGui 는 자를 사각형을 쌓는다 — intersect = true 로 바깥 clip 과 겹침)
template <class F>
void clipped(ImDrawList* list, const ui::UiRect& clip, F&& draw) {
    if (clip.w <= 0 || clip.h <= 0) {
        return;
    }
    list->PushClipRect(topLeft(clip), bottomRight(clip), true);
    draw();
    list->PopClipRect();
}

} // namespace

void ImGuiUiRenderer::fillRect(const ui::UiRect& rect, ui::UiColor color, const ui::UiRect& clip) {
    clipped(m_list, clip, [&] { m_list->AddRectFilled(topLeft(rect), bottomRight(rect), rgba(color)); });
}

void ImGuiUiRenderer::strokeRect(const ui::UiRect& rect, ui::UiColor color, f32 width, const ui::UiRect& clip) {
    clipped(m_list, clip, [&] { m_list->AddRect(topLeft(rect), bottomRight(rect), rgba(color), 0.0f, width); });
}

void ImGuiUiRenderer::text(const ui::UiRect& rect, std::string_view text, f32 fontSize, ui::UiColor color,
                           const ui::UiRect& clip) {
    const std::string shown = m_glyphs != nullptr ? m_glyphs->cover(text) : std::string(text);
    clipped(m_list, clip, [&] {
        m_list->AddText(ImGui::GetFont(), fontSize, topLeft(rect), rgba(color), shown.data(),
                        shown.data() + shown.size());
    });
}

void ImGuiUiRenderer::image(const ui::UiRect& rect, std::string_view material, const ui::UiRect& clip) {
    // 텍스처 [계획] — material 이름마다 같은 색 (MaterialLibrary 의 대체 색처럼 이름 해시)
    const u64 h = fnv1a64(material);
    const ui::UiColor c{static_cast<u8>(80 + (h & 0x7F)), static_cast<u8>(80 + ((h >> 8) & 0x7F)),
                        static_cast<u8>(80 + ((h >> 16) & 0x7F)), 255};
    fillRect(rect, c, clip);
}

std::array<f32, 2> ImGuiTextMeasure::measure(std::string_view text, f32 fontSize) const {
    const std::string shown = m_glyphs != nullptr ? m_glyphs->cover(text) : std::string(text);
    const ImVec2 s =
        ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, shown.data(), shown.data() + shown.size());
    return {s.x, s.y};
}

} // namespace sbx::client
