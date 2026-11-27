#pragma once
// GraphicsUI DisplayList → ImDrawList — 과도기 렌더 경로 (UI-R1-04, ADR-0174).
//
// 왜 과도기인가: GraphicsUI 의 GPU 렌더러 (GraphicsUI::SdkRenderer) 는 GraphicsUI 가 고정한 다른 GraphicsSDK 커밋 (9ae01bb7) 에
// 묶여 있고 D3D12 · DirectWrite 만 있다 — Sandbox 의 GraphicsSDK (95967d2e, D3D12 · Vulkan · Metal) 와 한 프로세스에 둘 수 없다
// (같은 gfx:: 심볼 두 벌). UPSTREAM GUI-UP-01 (공급자가 준 GraphicsSDK 타깃 · 장치로 그리기) 이 풀릴 때까지 GraphicsUI 의
// 그리기 명령을 이미 세 백엔드에서 도는 Sandbox 의 ImGui 렌더러 (render/imgui — 글꼴 아틀라스 · 한글 대체 글꼴 포함) 로 낸다.
// UI-R10 에서 지운다 (ImGui 와 함께). GPU 자원 · 펜스를 새로 만들지 않는다 — ImGui 렌더러의 것을 그대로 쓴다.
//
// 좌표: GraphicsUI · ImGui 모두 논리 단위 (ImGuiLayer — DisplaySize 논리, 프레임버퍼 배율은 렌더러). 색: RenderColor 는 곱하지
// 않은 RGBA8 = IM_COL32 그대로.

#include <span>
#include <string_view>

#include <graphics-ui/core/render.hpp>

#include "foundation/types/Types.hpp"
#include "uihost/UiHost.hpp"

struct ImDrawList;
struct ImFont;

namespace sbx::client {

namespace gui = graphics_ui::core;

struct GuiBridgeStats {
    usize rects = 0;
    usize texts = 0;
    usize gradients = 0;
    usize shadows = 0;
    usize clips = 0; // PushClip 수
};

// list 를 dl 에 그린다. list 는 UI 단위 — scale 을 곱해 논리 좌표로 (UiHost::scale — UI-R1-09), origin = 논리 좌표 원점.
// font 가 없으면 ImGui 기본 글꼴. PushClip/PopClip 짝이 맞지 않으면 남은 clip 을
// 닫는다 (DisplayList 는 짝을 보장하지만 렌더러 쪽에서도 ImDrawList 의 clip 스택을 깨뜨리지 않는다)
GuiBridgeStats drawDisplayList(const gui::DisplayList& list, ImDrawList& dl, ImFont* font = nullptr,
                               f32 originX = 0.f, f32 originY = 0.f, f32 scale = 1.f);
// 같은 것 — DisplayList 가 아닌 명령 묶음 (Dock 작업 공간의 탭 · 분할선 — UI-R2-05). 좌표는 UI 단위
GuiBridgeStats drawCommands(std::span<const gui::RenderCommand> commands, ImDrawList& dl, ImFont* font = nullptr,
                            f32 originX = 0.f, f32 originY = 0.f, f32 scale = 1.f);

// ImGui 글꼴로 글자 크기 재기 — ImGui 프레임 안 (NewFrame ~ Render) 에서만 (1.92 의 동적 아틀라스가 그 크기를 굽는다)
class GuiImGuiTextMeasure final : public uihost::ITextMeasure {
public:
    explicit GuiImGuiTextMeasure(ImFont* font = nullptr) noexcept : m_font(font) {}
    [[nodiscard]] gui::LayoutSize measure(std::string_view utf8, double emSize) const override;

private:
    ImFont* m_font;
};

} // namespace sbx::client
