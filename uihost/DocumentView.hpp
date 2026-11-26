#pragma once
// GXML 화면의 호스트 (UI-R2-02, ADR-0175) — GraphicsUI 의 DocumentSession (GXML · GCSS 를 컴파일한 Document 를 실행 — 자기 트리 ·
// 상태 · 입력) 을 UiHost 와 같은 창 계약으로 감싼다: 포인터 · 키 (InputRouting 과 같은 변환), UI 배율, 확정 글자 → 포커스된 TextBox,
// IME 자리, frame → DisplayList (UI 단위 — 렌더 어댑터가 배율을 곱한다).
//
// DocumentSession 의 공개 API (pointer · key · editText(id) · find · render) 로만 한다 — 그래서 어림하는 것이 있다 (GUI-UP-14):
//   포커스된 TextBox   pointer down 의 대상이 TextBox 면 그것 (Core 에 포커스 조회가 없다), 다른 곳을 누르면 없음
//   IME 자리          그 TextBox 상자의 왼쪽 아래 (캐럿 위치를 Core 가 알려 주지 않는다)
//   Tab · 휠          보내지 않는다 (DocumentSession 에 포커스 이동 · 스크롤이 없다)

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <graphics-ui/core/document.hpp>

#include "foundation/types/Error.hpp"
#include "platform/common/PlatformEvent.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::uihost {

class DocumentView {
public:
    // setup = DocumentSession 의 필드 · 명령 등록 (화면이 준다). 문서 검증 실패 · 등록 불일치는 오류
    [[nodiscard]] static Expected<std::unique_ptr<DocumentView>> create(gui::Document document,
                                                                        gui::DocumentSession::Setup setup);

    void setScale(f32 scale) noexcept { m_scale = scale > 0.f ? scale : 1.f; }
    [[nodiscard]] f32 scale() const noexcept { return m_scale; }

    void feed(std::span<const platform::PlatformEvent> events);
    // logical = 창의 논리 크기. 실패하면 앞 frame 의 그리기가 남는다 (DocumentSession 계약)
    Expected<const gui::DisplayList*> frame(gui::LayoutSize logical);

    [[nodiscard]] TextInputState textInput() const;
    [[nodiscard]] bool textFocused() const noexcept { return m_focusText.has_value(); }
    [[nodiscard]] const std::optional<std::string>& focusedTextId() const noexcept { return m_focusText; }
    [[nodiscard]] gui::DocumentSession& session() noexcept { return *m_session; }
    [[nodiscard]] const UiFrameStats& stats() const noexcept { return m_stats; }
    // 창 좌표가 루트가 아닌 노드 위인가
    [[nodiscard]] bool hitsUi(f32 x, f32 y) const noexcept;

private:
    DocumentView() = default;
    [[nodiscard]] std::optional<std::string> textBoxOf(gui::ComponentHandle h) const;

    std::unique_ptr<gui::DocumentSession> m_session;
    std::vector<std::string> m_textBoxes; // 문서의 TextBox id
    std::optional<std::string> m_focusText;
    platform::Modifiers m_mods;
    f32 m_scale = 1.f;
    bool m_rendered = false;
    UiFrameStats m_stats;
};

} // namespace sbx::uihost
