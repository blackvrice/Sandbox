#include "apps/client/ui/GraphicsUiImGuiBridge.hpp"

#include <algorithm>
#include <cfloat>
#include <type_traits>

#include <imgui.h>

namespace sbx::client {

namespace {

ImU32 col(gui::RenderColor c, f32 alphaScale = 1.f) {
    const auto a = static_cast<u32>(std::clamp(static_cast<f32>(c.alpha) * alphaScale, 0.f, 255.f));
    return IM_COL32(c.red, c.green, c.blue, a);
}

} // namespace

GuiBridgeStats drawDisplayList(const gui::DisplayList& list, ImDrawList& dl, ImFont* font, f32 originX, f32 originY,
                               f32 scale) {
    GuiBridgeStats st;
    ImFont* f = font != nullptr ? font : ImGui::GetFont();
    const f32 s = scale > 0.f ? scale : 1.f;
    // UI 단위 → 논리 좌표 (ImGui 의 좌표)
    const auto p = [&](double x, double y) {
        return ImVec2(originX + static_cast<f32>(x) * s, originY + static_cast<f32>(y) * s);
    };
    int depth = 0;
    for (const gui::RenderCommand& cmd : list.commands()) {
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, gui::PushClip>) {
                    dl.PushClipRect(p(c.bounds.x, c.bounds.y),
                                    p(c.bounds.x + c.bounds.width, c.bounds.y + c.bounds.height), true);
                    ++depth;
                    ++st.clips;
                } else if constexpr (std::is_same_v<T, gui::PopClip>) {
                    if (depth > 0) {
                        dl.PopClipRect();
                        --depth;
                    }
                } else if constexpr (std::is_same_v<T, gui::FillRect>) {
                    dl.AddRectFilled(p(c.bounds.x, c.bounds.y),
                                     p(c.bounds.x + c.bounds.width, c.bounds.y + c.bounds.height), col(c.color));
                    ++st.rects;
                } else if constexpr (std::is_same_v<T, gui::LinearGradient>) {
                    const ImU32 a = col(c.start);
                    const ImU32 b = col(c.end);
                    const bool h = c.axis == gui::GradientAxis::horizontal;
                    // 꼭짓점 차례: 왼위 · 오른위 · 오른아래 · 왼아래
                    dl.AddRectFilledMultiColor(p(c.bounds.x, c.bounds.y),
                                               p(c.bounds.x + c.bounds.width, c.bounds.y + c.bounds.height), a,
                                               h ? b : a, b, h ? a : b);
                    ++st.gradients;
                } else if constexpr (std::is_same_v<T, gui::DropShadow>) {
                    // 근사: 진한 사각형 + radius 만큼 바깥으로 번지는 띠 (최대 8 겹, 알파가 선형으로 준다).
                    // GraphicsUI 의 D3D12 그림자와 픽셀이 같지 않다 — 과도기 경로의 차이 (MIGRATION_GUIDE)
                    dl.AddRectFilled(p(c.bounds.x, c.bounds.y),
                                     p(c.bounds.x + c.bounds.width, c.bounds.y + c.bounds.height), col(c.color));
                    const int steps = std::clamp(static_cast<int>(c.radius / 2.0), 1, 8);
                    for (int i = 1; i <= steps; ++i) {
                        const double r = c.radius * i / steps;
                        dl.AddRect(p(c.bounds.x - r, c.bounds.y - r),
                                   p(c.bounds.x + c.bounds.width + r, c.bounds.y + c.bounds.height + r),
                                   col(c.color, 1.f - static_cast<f32>(i) / static_cast<f32>(steps + 1)), 0.f,
                                   static_cast<f32>(c.radius / steps) * s);
                    }
                    ++st.shadows;
                } else if constexpr (std::is_same_v<T, gui::DrawText>) {
                    // 글자 상자로 자른다 (GraphicsUI 의 글자 명령은 상자 안에서 배치된다 — 넘친 부분은 보이지 않는다)
                    const ImVec2 lo = p(c.bounds.x, c.bounds.y);
                    const ImVec2 hi = p(c.bounds.x + c.bounds.width, c.bounds.y + c.bounds.height);
                    const ImVec4 clip(lo.x, lo.y, hi.x, hi.y);
                    dl.AddText(f, static_cast<f32>(c.em_size) * s, lo, col(c.color),
                               c.utf8.data(), c.utf8.data() + c.utf8.size(), 0.f, &clip);
                    ++st.texts;
                }
            },
            cmd);
    }
    while (depth-- > 0) {
        dl.PopClipRect();
    }
    return st;
}

gui::LayoutSize GuiImGuiTextMeasure::measure(std::string_view utf8, double emSize) const {
    ImFont* f = m_font != nullptr ? m_font : ImGui::GetFont();
    if (f == nullptr || utf8.empty()) {
        return {0.0, emSize};
    }
    const ImVec2 s = f->CalcTextSizeA(static_cast<f32>(emSize), FLT_MAX, 0.f, utf8.data(), utf8.data() + utf8.size());
    return {static_cast<double>(s.x), static_cast<double>(s.y)};
}

} // namespace sbx::client
