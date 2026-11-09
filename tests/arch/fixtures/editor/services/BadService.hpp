#pragma once
// 일부러 틀린 픽스처 (컴파일되지 않는다): editor/services 는 render · editor/ (툴 · 패널) · ImGui 를 모른다 (v0.8 ST-06-1)
#include <imgui.h>

#include "editor/Editor.hpp"
#include "render/renderer/Camera2D.hpp"
