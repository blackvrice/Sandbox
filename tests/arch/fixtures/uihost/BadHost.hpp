#pragma once
// 위반 2: module-direction (render) + banned-header (imgui) — 통합 계층은 GPU · ImGui 를 모른다 (UI-R1, ADR-0174)
#include "render/rhi/RenderDevice.hpp"
#include <imgui.h>
