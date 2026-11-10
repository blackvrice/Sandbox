#pragma once
// Studio 공통 위젯 (v0.8 UI-1 — docs/29-STUDIO-UI.md 3장, ADR-0118). 토큰 (StudioTheme) 으로 그린다 — 새 창은 이것으로
// 같은 모양이 된다: 페이지 제목 · 묶음 머리 · 주 / 보조 단추 · 툴바 토글 · 배지 · 상태 글 · 빈 상태 · 카드. ImGui
// 프레임 안에서.

#include <string_view>

#include <imgui.h>

#include "editor/ui/StudioTheme.hpp"

namespace sbx::editor::ui {

// 큰 제목 (기본 글자 × 1.5) · 아래 보조 줄 (없으면 생략)
void pageTitle(std::string_view title, std::string_view subtitle = {});
// 묶음 머리 — 글자 + 강조 밑줄 (SeparatorText)
void sectionHeader(std::string_view title);
// 주 단추 (강조 채움 · 어두운 글자) · 보조 단추 (표면). disabled 면 누를 수 없다
bool primaryButton(std::string_view label, bool disabled = false, ImVec2 size = {});
bool secondaryButton(std::string_view label, bool disabled = false, ImVec2 size = {});
// 툴바 토글 — 켜졌으면 강조 채움. tooltip 은 hover 때 (단축키 안내)
bool toolbarToggle(std::string_view label, bool active, std::string_view tooltip = {}, bool disabled = false);
// 작은 배지 (글자 + 둥근 바탕). 색만으로 뜻을 전하지 않게 글자를 같이 쓴다
void badge(std::string_view text, theme::Status status);
// 상태 글 (줄 바꿈)
void statusText(theme::Status status, std::string_view text);
// 빈 상태 — 가운데 제목 · 설명
void emptyState(std::string_view title, std::string_view body);
// 마우스를 올리면 설명 (지금 위젯)
void tooltip(std::string_view text);

// 카드 — 테두리 있는 자식 창. hover · selected 면 강조 테두리. beginCard 가 true 일 때만 안을 그리고, 늘 endCard 를
// 부른다. clicked = 카드 바탕을 눌렀다 (안의 단추가 아닌 곳)
struct CardResult {
    bool visible = false;
    bool hovered = false;
};
CardResult beginCard(std::string_view id, ImVec2 size, bool selected = false);
// 끝 — 바탕 클릭이면 true (안의 위젯이 가져가지 않은 클릭)
bool endCard();

// 둥근 사각형 안 이니셜 (프로젝트 기본 미리보기 — 그림이 없을 때). 이름의 첫 글자 (UTF-8 한 글자)
void initialsTile(std::string_view name, ImVec2 size);

} // namespace sbx::editor::ui
