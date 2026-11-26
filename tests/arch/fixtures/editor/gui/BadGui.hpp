#pragma once
// 위반 2: banned-header (imgui) + subpath-direction (render) — GraphicsUI Studio 화면은 ImGui · Render 를 모른다 (UI-R2-01, ADR-0175)
#include <imgui.h>
#include "render/rhi/RenderDevice.hpp"
