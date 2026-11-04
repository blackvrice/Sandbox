#pragma once
// 게임 UI 포인터 라우팅 (v0.7 M3-T3 — ADR-0079 7, docs/20-CREATOR-ALPHA.md 2.4).
//
//   차례   modal → menu → 도구 패널 (ImGui 가 잡으면) → hud 의 버튼 → 게임 (카메라 · 선택 · 행동 · 에디터)
//   modal  보이는 modal 이 하나라도 있으면 포인터를 모두 잡는다 (밖을 눌러도 게임에 가지 않는다)
//   menu   보이는 menu 의 root 사각형 안이면 잡는다
//   hud    버튼 위일 때만 잡는다 — 나머지는 게임으로 통과
// 같은 층의 문서는 뒤의 것이 위. 노드는 사각형 ∩ clip 안이 맞음, 겹치면 나중 (자식) 이 위

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
    bool captured = false;                // 게임 UI 가 잡았다 — 게임 · 도구 패널에 주지 않는다
    usize document = 0;                   // 잡은 문서 (docs 의 번호 — captured 일 때)
    const UiLayoutNode* button = nullptr; // 포인터 아래 버튼 (누르면 그 이벤트)
};

[[nodiscard]] UiPointerRoute routePointer(std::span<const UiDocumentLayout> docs, i32 x, i32 y, bool toolsWantMouse);

[[nodiscard]] bool contains(const UiRect& r, i32 x, i32 y) noexcept;

} // namespace sbx::ui
