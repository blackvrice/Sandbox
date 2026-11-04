#pragma once
// 게임 UI 바인딩 평가 (v0.7 M3-T4 — ADR-0079 5, docs/20-CREATOR-ALPHA.md 2.3).
//
// "var:<키>" = client 스크립트 ctx.ui.set 의 값 (수 · 글자 · 참거짓). "session:<칸>" = state (Lobby · Starting ·
// Running · Finished — 글자) · tick · time (초) · team · score[i]. "component:<컴포넌트>.<칸>@owned" = 내 플레이어
// (game.owner == 내 clientId) 의 개체 중 netId 가 가장 작은 것의 수, "…@selected" = 선택한 개체 (여럿이면 첫째) 의 수.
// 값을 모르면 (없는 키 · 개체 · 칸) None — 글자 자리는 "-", progress 는 0. 데이터는 IUiDataSource 로 받는다 (Foundation
// 만 — 클라이언트가 복제 월드 · 세션 · 스크립트 값으로 구현한다). 표시 값 = 복제된 값 (서버의 것을 바꾸지 않는다)

#include <optional>
#include <string>
#include <string_view>

#include "ui/model/UiDocument.hpp"

namespace sbx::ui {

struct UiScalar {
    enum class Kind : u8 { None, Number, Text, Bool };
    Kind kind = Kind::None;
    f64 number = 0;
    std::string text;
    bool flag = false;

    [[nodiscard]] static UiScalar of(f64 v) { return {Kind::Number, v, {}, false}; }
    [[nodiscard]] static UiScalar of(std::string v) { return {Kind::Text, 0, std::move(v), false}; }
    [[nodiscard]] static UiScalar ofBool(bool v) { return {Kind::Bool, v ? 1.0 : 0.0, {}, v}; }
    [[nodiscard]] bool known() const noexcept { return kind != Kind::None; }
    // 보이는 글자 — 수는 정수면 정수, 아니면 짧은 십진 (std::format 의 최단 표현), 참거짓은 "true" · "false", 없음 "-"
    [[nodiscard]] std::string display() const;
    // progress · list 수 — 수 · 참거짓 (1 · 0), 글자 · 없음 = nullopt
    [[nodiscard]] std::optional<f64> asNumber() const;
    bool operator==(const UiScalar&) const = default;
};

enum class UiTarget : u8 { Owned, Selected };

class IUiDataSource {
public:
    virtual ~IUiDataSource() = default;
    [[nodiscard]] virtual UiScalar var(std::string_view key) const = 0;
    [[nodiscard]] virtual UiScalar session(std::string_view field) const = 0;
    // componentField = "<컴포넌트 id>.<칸>" (점이 여럿이면 마지막 점이 칸을 가른다)
    [[nodiscard]] virtual UiScalar component(std::string_view componentField, UiTarget target) const = 0;
};

// 바인딩 식 하나 (모양이 틀리면 None — 검사는 checkBinding · sbx check 의 몫)
[[nodiscard]] UiScalar evaluate(std::string_view bind, const IUiDataSource& source);
[[nodiscard]] UiScalar evaluate(const UiValue& value, const IUiDataSource* source); // 고정 수 또는 바인딩

// node.text 의 "{0}" ~ "{9}" 를 args 의 값으로 (없는 인자는 빈 글자, 모르는 값은 "-"). source 가 없으면 바인딩은 "-"
[[nodiscard]] std::string formatText(const UiNode& node, const IUiDataSource* source);
// progress 의 채움 value / max (0 ~ 1 로 자름, max ≤ 0 · 모르는 값 = 0)
[[nodiscard]] f32 progressOf(const UiNode& node, const IUiDataSource* source);
// list 의 항목 수 — items 바인딩의 수 (0 ~ 256 으로 자름, 모르면 0)
[[nodiscard]] usize listCountOf(const UiNode& node, const IUiDataSource* source);

inline constexpr usize kMaxListItems = 256;

} // namespace sbx::ui
