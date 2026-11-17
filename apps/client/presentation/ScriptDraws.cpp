#include "apps/client/presentation/ScriptDraws.hpp"

#include "render/renderer/RenderWorld.hpp"

namespace sbx::client {

u32 scriptColorToRgba8(u32 c) noexcept {
    return render::packRgba8(static_cast<u8>(c >> 24), static_cast<u8>(c >> 16), static_cast<u8>(c >> 8),
                             static_cast<u8>(c));
}

void appendScriptDraws(std::span<const script::ClientScriptDraw> draws, render::DebugDrawList& lines,
                       std::vector<ScriptTextView>* texts) {
    using Kind = script::ClientScriptDraw::Kind;
    for (const script::ClientScriptDraw& d : draws) {
        const u32 color = scriptColorToRgba8(d.color);
        switch (d.kind) {
        case Kind::Line:
            lines.line(d.a, d.b, color, kScriptLineWidth);
            break;
        case Kind::Rect:
            lines.rect(d.a, d.b, color, kScriptLineWidth);
            break;
        case Kind::Circle:
            if (d.radius > 0) {
                lines.circle(d.a, d.radius, color, kScriptLineWidth, kScriptCircleSegments);
            }
            break;
        case Kind::Text:
            if (texts != nullptr) {
                texts->push_back(ScriptTextView{d.a, d.text, color});
            }
            break;
        }
    }
}

} // namespace sbx::client
