#pragma once
// Studio 설정 — GXML 로 쓴 첫 Studio 화면 (UI-R2-02, ADR-0175). 구조 · 스타일은 editor/gui/gxml/studio_settings.{gxml,gcss}
// (빌드 때 C++ 로 — cmake/SbxGxml.cmake), 실행은 uihost::DocumentView (GraphicsUI DocumentSession).
//
// 화면은 지금 값을 보여 주고 (status) 고른 일 (의도) 만 돌려준다 — 일은 Application 이 기존 명령 (view.theme-* · view.developer-panels) ·
// 간격 행동으로 한다 (ImGui 셸의 보기 메뉴와 같은 길 · 같은 studio.json 저장).

#include <memory>
#include <optional>
#include <string>

#include "editor/services/StudioTokens.hpp"
#include "foundation/types/Error.hpp"
#include "uihost/DocumentView.hpp"

namespace sbx::editor::gui {

namespace gui = graphics_ui::core;

struct StudioSettingsState {
    std::string theme; // "dark" · "light" · "high-contrast" (editor::theme::themeName)
    bool compact = false;
    bool developerPanels = false;
};

struct StudioSettingsIntent {
    enum class Kind : u8 { Theme, ToggleDensity, ToggleDeveloper, Close };
    Kind kind = Kind::Close;
    std::string theme; // Theme — "dark" · "light" · "high-contrast"
};

class StudioSettingsScreen {
public:
    // theme = 색 변형 (빌드가 테마마다 컴파일한 문서 — studio_tokens.json 의 토큰, UI-R2-03). 테마가 바뀌면 다시 만든다 (상태가 작다).
    // GXML 이 빠진 빌드 (Python ≥ 3.11 없음 — SBX_HAS_GXML 없음) 면 Unsupported
    [[nodiscard]] static Expected<std::unique_ptr<StudioSettingsScreen>> create(
        services::StudioTheme theme = services::StudioTheme::Dark);
    [[nodiscard]] services::StudioTheme theme() const noexcept { return m_theme; }

    // 지금 값 → 상태 줄 (바뀔 때만 상태 게시 — 다음 frame 의 그리기에 반영)
    void setState(const StudioSettingsState& state);
    [[nodiscard]] const std::string& statusText() const noexcept { return m_statusText; }
    [[nodiscard]] std::optional<StudioSettingsIntent> takeIntent();
    [[nodiscard]] uihost::DocumentView& view() noexcept { return *m_view; }

private:
    StudioSettingsScreen() = default;

    std::unique_ptr<uihost::DocumentView> m_view;
    gui::StateStore* m_store = nullptr; // DocumentSession 이 소유 (Setup 이 빌려준다) — m_view 와 수명이 같다
    gui::StateField m_status;
    std::string m_statusText;
    std::optional<StudioSettingsIntent> m_intent;
    services::StudioTheme m_theme = services::StudioTheme::Dark;
};

} // namespace sbx::editor::gui
