#include "editor/gui/StudioSettingsScreen.hpp"

#include <array>
#include <format>
#include <utility>

#if defined(SBX_HAS_GXML)
#include "sandbox/gxml/studio_settings.hpp" // 빌드 때 생성 — editor/gui/gxml/studio_settings.gxml · .gcss
#endif

namespace sbx::editor::gui {

namespace {

std::string themeTitle(std::string_view name) {
    if (name == "light") {
        return "밝음";
    }
    if (name == "high-contrast") {
        return "고대비";
    }
    return "어두움";
}

} // namespace

Expected<std::unique_ptr<StudioSettingsScreen>> StudioSettingsScreen::create() {
#if defined(SBX_HAS_GXML)
    std::unique_ptr<StudioSettingsScreen> screen(new StudioSettingsScreen());
    StudioSettingsScreen* self = screen.get();
    // 모델 (studio_settings.model.json) 과 같은 이름 · 타입 — 다르면 DocumentSession::create 가 거절한다
    const auto setup = [self](gui::StateStore& store) -> gui::Result<gui::DocumentHost> {
        self->m_store = &store;
        auto status = store.define(std::string());
        if (!status) {
            return std::unexpected(status.error());
        }
        self->m_status = *status;
        const auto intent = [self](StudioSettingsIntent::Kind kind, std::string theme = {}) {
            return gui::DocumentCommand::Invoke{[self, kind, theme]() -> gui::Result<void> {
                self->m_intent = StudioSettingsIntent{kind, theme};
                return {};
            }};
        };
        using K = StudioSettingsIntent::Kind;
        return gui::DocumentHost{{{"status", *status}},
                                 {{"theme_dark", intent(K::Theme, "dark"), {}},
                                  {"theme_light", intent(K::Theme, "light"), {}},
                                  {"theme_contrast", intent(K::Theme, "high-contrast"), {}},
                                  {"toggle_density", intent(K::ToggleDensity), {}},
                                  {"toggle_developer", intent(K::ToggleDeveloper), {}},
                                  {"close", intent(K::Close), {}}}};
    };
    auto view = uihost::DocumentView::create(graphics_ui::generated::document_studio_settings(), setup);
    if (!view) {
        return std::unexpected(view.error());
    }
    screen->m_view = std::move(*view);
    return screen;
#else
    return makeError(ErrorCode::Unsupported, "GXML 화면이 빠진 빌드 (Python ≥ 3.11 없음 — ADR-0175)");
#endif
}

void StudioSettingsScreen::setState(const StudioSettingsState& state) {
    std::string text = std::format("테마 {} · 간격 {} · 개발자 창 {}", themeTitle(state.theme), state.compact ? "촘촘" : "보통",
                                   state.developerPanels ? "켜짐" : "꺼짐");
    if (text == m_statusText || m_store == nullptr) {
        return;
    }
    const std::array writes{gui::StateWrite{m_status, text}};
    if (m_store->transact(writes)) {
        m_statusText = std::move(text);
    }
}

std::optional<StudioSettingsIntent> StudioSettingsScreen::takeIntent() {
    auto out = std::move(m_intent);
    m_intent.reset();
    return out;
}

} // namespace sbx::editor::gui
