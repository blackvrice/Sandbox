#pragma once
// 게임 UI 포인터 라우팅 (v0.7 M3-T3 — ADR-0079 7, docs/20-CREATOR-ALPHA.md 2.4).
//
//   차례   modal → menu → 도구 패널 (ImGui 가 잡으면) → hud 의 버튼 → 게임 (카메라 · 선택 · 행동 · 에디터)
//   modal  보이는 modal 이 하나라도 있으면 포인터를 모두 잡는다 (밖을 눌러도 게임에 가지 않는다)
//   menu   보이는 menu 의 root 사각형 안이면 잡는다
//   hud    버튼 · 입력 컨트롤 위일 때만 잡는다 — 나머지는 게임으로 통과
// 같은 층의 문서는 뒤의 것이 위. 노드는 사각형 ∩ clip 안이 맞음, 겹치면 나중 (자식) 이 위

#include <functional>
#include <span>
#include <vector>

#include "ui/layout/UiLayout.hpp"

namespace sbx::ui {

struct UiDocumentLayout {
    const UiDocument* document = nullptr;
    std::vector<UiLayoutNode> nodes; // layoutDocument 결과 (앞 순서 — nodes[0] = root)
    bool visible = true;
};

struct UiPointerRoute {
    enum class Owner : u8 { Game, Modal, Menu, Tools, Hud };
    Owner owner = Owner::Game;
    bool captured = false;                 // 게임 UI 가 잡았다 — 게임 · 도구 패널에 주지 않는다
    usize document = 0;                    // 잡은 문서 (docs 의 번호 — captured 일 때)
    const UiLayoutNode* control = nullptr; // 포인터 아래 버튼 · 입력 컨트롤 (isControl — v0.7 NG-06 (1))
};

[[nodiscard]] UiPointerRoute routePointer(std::span<const UiDocumentLayout> docs, i32 x, i32 y, bool toolsWantMouse);

[[nodiscard]] bool contains(const UiRect& r, i32 x, i32 y) noexcept;

// 키보드 포커스 (v0.7 NG-06 (1) — ADR-0103 4 · 5). ui/ 는 플랫폼 키를 모른다 — 앱이 Tab · Shift+Tab · 방향 ·
// Enter/Space · Esc 를 UiKey 로 넘긴다. Backspace · Delete · Home · End 는 글자 칸 편집 (v0.7 NG-06 (2) — ADR-0104)
enum class UiKey : u8 { Next, Previous, Up, Down, Left, Right, Activate, Release, Backspace, Delete, Home, End };

struct UiFocusable {
    const UiLayoutNode* node = nullptr;
    usize document = 0; // docs 의 번호
};

// 보이는 문서의 켜진 컨트롤 — 차례 = tabIndex (작은 것 먼저), 같으면 문서 · 배치 차례. disabled 가 참이면 뺀다
[[nodiscard]] std::vector<UiFocusable>
focusOrder(std::span<const UiDocumentLayout> docs,
           const std::function<bool(const UiNode& node, i32 listIndex)>& disabled);

// from 에서 그 방향 (Up · Down · Left · Right) 의 가장 가까운 것 — 점수 = 주 축 거리 + 옆 축 거리 × 2 (가운데끼리).
// 그 방향에 없으면 -1
[[nodiscard]] i32 nearestFocus(std::span<const UiFocusable> order, usize from, UiKey direction);

} // namespace sbx::ui
