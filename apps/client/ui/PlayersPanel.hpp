#pragma once
// "접속자" 패널 (Phase 12C, docs/10-EDITOR.md 2 · 7장, ADR-0030): 접속자 · 역할 표. 역할을 바꿀 수 있는 사람(owner ·
// admin)에게는 다른 사람의 역할 콤보 — 고르면 서버에 SetRole (판단은 서버, 결과는 아래 한 줄). 자기 줄은 "(나)".
// ImGui:: 호출만 한다 — 요청은 돌려주고 Application 이 세션에 넘긴다.

#include <optional>
#include <string_view>

#include "apps/client/WorldSession.hpp"

namespace sbx::client {

struct RoleRequest {
    u16 clientId = 0;
    u8 role = 0;
};

// 역할 번호(net::Role 값) → "owner (주인)" 처럼
[[nodiscard]] std::string_view roleLabel(u8 role) noexcept;
// me 역할이 target 역할인 다른 사람을 role 로 바꿀 수 있나 — 서버와 같은 함수 (net::roleCanAssign, 10-EDITOR 7장)
[[nodiscard]] bool canAssignRole(u8 me, u8 target, u8 role) noexcept;

[[nodiscard]] std::optional<RoleRequest> drawPlayersPanel(const PlayersInfo& info);

} // namespace sbx::client
