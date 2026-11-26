#pragma once
// GraphicsUI 응용 호스트 (UI-R1-02, ADR-0174). GraphicsUI Core 의 트리 · 상태 · 입력을 한 UI 스레드에서 소유하고, 프레임마다
//   feed(이벤트)  → 직전 프레임의 배치로 hit-test → InputDispatcher (그린 자리와 누르는 자리가 같다)
//   frame(크기)   → 작업자 결과 (StateSender) 적용 → 화면의 배치 설명 → measure · arrange → 그리기 설명 → DisplayList
// 를 돈다. 화면 (IUiScreen) 이 트리 모양 · 배치 · 그리기를 정하고, 호스트는 수명 · 순서 · 오류만 맡는다.
//
// 무엇을 모르나: GPU · OS 창 · ImGui · Sandbox Core (ECS). DisplayList 를 픽셀로 만드는 것은 렌더 어댑터 (지금은 과도기 —
// apps/client/ui/GraphicsUiImGuiBridge, 목표는 GraphicsUI 의 SDK 렌더러 — UPSTREAM GUI-UP-01). 글자 크기는 ITextMeasure.
// 스레드: GraphicsUI Core 는 만든 스레드에서만 쓴다 — 호스트를 만든 스레드가 feed · frame · 파괴를 부른다. 다른 스레드는
// sender() 의 StateSender 로 값만 보낸다 (다음 frame 이 순서대로 적용).

#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <graphics-ui/core/controls.hpp>
#include <graphics-ui/core/input_dispatcher.hpp>
#include <graphics-ui/core/layout.hpp>
#include <graphics-ui/core/render.hpp>
#include <graphics-ui/core/state_store.hpp>

#include "foundation/types/Error.hpp"
#include "foundation/types/Types.hpp"
#include "platform/common/PlatformEvent.hpp"

namespace sbx::uihost {

namespace gui = graphics_ui::core;

// GraphicsUI 오류 → Sandbox 오류 (what = 어느 호출이었나)
[[nodiscard]] Error toError(gui::ErrorCode code, std::string_view what);
[[nodiscard]] std::string_view guiErrorName(gui::ErrorCode code) noexcept;

// 글자 크기 — 배치가 묻는다 (호스트의 글꼴 서비스). 논리 단위
class ITextMeasure {
public:
    virtual ~ITextMeasure() = default;
    [[nodiscard]] virtual gui::LayoutSize measure(std::string_view utf8, double emSize) const = 0;
};

// 글꼴 없는 어림 (헤드리스 · 시험): ASCII 0.55 em, 그 밖 1 em, 줄 높이 1.25 em. 픽셀 정확도가 아니라 배치 흐름 확인용
class ApproxTextMeasure final : public ITextMeasure {
public:
    [[nodiscard]] gui::LayoutSize measure(std::string_view utf8, double emSize) const override;
};

// 그리기 설명 — RenderNode::draws 가 가리키는 명령을 buildDisplayList 가 복사할 때까지 살린다
class PaintList {
public:
    void add(gui::ComponentHandle component, std::vector<gui::DrawCommand> draws, bool clipToBounds = false);
    [[nodiscard]] std::span<const gui::RenderNode> nodes() const noexcept { return m_nodes; }
    void clear();

private:
    std::deque<std::vector<gui::DrawCommand>> m_storage; // deque — 앞 원소의 주소가 바뀌지 않는다
    std::vector<gui::RenderNode> m_nodes;
};

// 글자 입력 대상 (UI-R1-08) — 포커스가 이 컴포넌트일 때 창의 확정 글자 (IME 조합이 끝난 글자 포함) 를 받는다.
// 조합 중 미리보기는 OS 입력기 창이 그린다 (PlatformEvent 에 조합 이벤트가 없다 — UPSTREAM S-04), 그 자리는 caret 이 정한다
struct TextTarget {
    std::function<void(std::string_view utf8)> insert;              // 한 번에 도착한 글자 묶음 (한 편집 · 한 실행 취소 단계)
    std::function<std::optional<gui::LayoutRect>()> caret;          // 컴포넌트 기준 논리 좌표의 캐럿 (IME 후보 창 자리)
};

// 휠 — dx · dy 는 칸 수 (PlatformEvent::MouseWheel — y 양수 = 위로). 처리했으면 true (조상에게 가지 않는다)
using WheelHandler = std::function<bool(f32 dx, f32 dy)>;

// 클립보드 — 호스트 (창) 의 것. 비어 있으면 GraphicsUI 편집의 복사 · 붙여넣기가 실패를 돌려준다
struct Clipboard {
    std::function<std::string()> read;
    std::function<void(std::string_view)> write;
};

// 창에 알릴 글자 입력 상태 — frame 뒤에 읽는다
struct TextInputState {
    bool active = false;                // 포커스가 글자 대상이다 → 창의 글자 입력 (IME) 을 켠다
    std::optional<gui::LayoutRect> caret; // 화면 논리 좌표 (후보 창 자리)
};

// 논리 x (글자 시작 기준) 에 가장 가까운 그래핌 경계의 UTF-8 바이트 위치 — 글자 칸을 눌러 캐럿을 놓을 때
[[nodiscard]] usize caretFromX(const ITextMeasure& text, std::string_view utf8, double emSize, double x);

class UiHost;

// 화면 하나. 호스트가 트리 · 입력 · 상태 · 글자 측정을 빌려준다
class IUiScreen {
public:
    virtual ~IUiScreen() = default;
    // setScreen 때 한 번 — 트리를 게시하고 입력 핸들러 · 포커스를 건다
    virtual Expected<void> mount(UiHost& host) = 0;
    // 프레임마다 — 마운트된 컴포넌트마다 LayoutNode 하나
    virtual Expected<void> layout(UiHost& host, const gui::ComponentSnapshot& tree,
                                  std::vector<gui::LayoutNode>& out) = 0;
    // 배치가 끝난 뒤 — 보이는 컴포넌트의 그리기 (컴포넌트 기준 논리 좌표)
    virtual Expected<void> paint(UiHost& host, const gui::LayoutFrame& frame, PaintList& out) = 0;
};

struct UiFrameStats {
    u64 frames = 0;
    u64 treeRevision = 0;
    usize components = 0;   // 배치한 상자
    usize commands = 0;     // DisplayList 명령 (clip 포함)
    usize inputEvents = 0;  // 이번 프레임에 GraphicsUI 로 보낸 입력
    usize inputRejected = 0; // 누적 — 지난 세대 대상 · 바쁨 (버린 입력)
    usize workerErrors = 0;  // 누적 — 적용하지 못한 작업자 메시지
    f64 layoutMs = 0.0;     // 배치 (measure + arrange) — 마지막 frame
    f64 paintMs = 0.0;      // 그리기 설명 + DisplayList — 마지막 frame
    f64 layoutMsTotal = 0.0; // 누적 (평균 = 합 / frames)
    f64 paintMsTotal = 0.0;
    f64 layoutMsMax = 0.0;   // 가장 긴 배치 (첫 frame 은 글꼴 굽기 · 측정 캐시가 차가워 길다)
};

class UiHost {
public:
    [[nodiscard]] static Expected<std::unique_ptr<UiHost>> create(const ITextMeasure& text);
    ~UiHost();
    UiHost(const UiHost&) = delete;
    UiHost& operator=(const UiHost&) = delete;

    [[nodiscard]] gui::ComponentTree& tree() noexcept { return *m_tree; }
    [[nodiscard]] gui::InputDispatcher& input() noexcept { return *m_input; }
    [[nodiscard]] gui::StateStore& state() noexcept { return *m_state; }
    [[nodiscard]] const ITextMeasure& text() const noexcept { return m_text; }
    // 이번 frame 의 화면 크기 (논리 단위) — 화면의 layout 이 읽는다
    [[nodiscard]] gui::LayoutSize viewport() const noexcept { return m_viewport; }
    // 작업자 스레드에 넘길 복사본 (값만 — 다음 frame 이 UI 스레드에서 차례대로 적용)
    [[nodiscard]] Expected<gui::StateSender> sender() const;

    // 화면 바꾸기: 앞 화면의 트리를 비우고 (구독 · 포커스 · 캡처가 풀린다) 새 화면을 마운트한다. nullptr = 빈 화면.
    // 화면은 호스트보다 오래 살거나 setScreen(nullptr) 뒤에 사라져야 한다 (핸들러가 화면을 가리킨다)
    Expected<void> setScreen(IUiScreen* screen);

    // 이번 프레임 이벤트 (도착 순서). 직전 frame 의 배치로 hit-test 한다 — 그 뒤 트리가 바뀌었으면 대상 없이 보낸다
    void feed(std::span<const platform::PlatformEvent> events);
    // 작업자 결과 적용 → 배치 → 그리기. 반환 포인터는 다음 frame · setScreen 까지 유효. 화면이 없으면 nullptr
    Expected<const gui::DisplayList*> frame(gui::LayoutSize viewport);

    [[nodiscard]] const UiFrameStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] const gui::LayoutFrame* lastLayout() const noexcept { return m_layout.get(); }
    // 글자 대상 · 휠 · 클립보드 (UI-R1-08). 빈 함수 = 해제. 화면 바꾸기 (setScreen) 가 모두 지운다
    Expected<void> setTextTarget(gui::ComponentHandle target, TextTarget handler);
    Expected<void> setWheelHandler(gui::ComponentHandle target, WheelHandler handler);
    void setClipboard(Clipboard clipboard) { m_clipboard = std::move(clipboard); }
    // GraphicsUI 편집 콜백 — publish 는 화면의 값 저장, 클립보드는 호스트의 것
    [[nodiscard]] gui::TextEditCallbacks editCallbacks(std::function<gui::Result<void>(std::string_view)> publish) const;
    [[nodiscard]] TextInputState textInput() const;
    // 포커스가 글자 대상인가 (앱 단축키를 막는다 — ImGui 의 WantTextInput 과 같은 뜻)
    [[nodiscard]] bool textFocused() const;

    // 마지막 포인터 이벤트가 가리킨 컴포넌트 (hover 표시 — Button::visualState 에 넘긴다)
    [[nodiscard]] std::optional<gui::ComponentHandle> pointerHit() const noexcept { return m_hover; }
    // 논리 좌표가 루트가 아닌 컴포넌트 위인가 (게임 입력과 나누기 — 빈 배경은 UI 가 아니다)
    [[nodiscard]] bool hitsUi(f32 x, f32 y) const noexcept;

private:
    explicit UiHost(const ITextMeasure& text) noexcept : m_text(text) {}

    const ITextMeasure& m_text;
    std::unique_ptr<gui::ComponentTree> m_tree;
    std::unique_ptr<gui::StateStore> m_state;
    std::unique_ptr<gui::InputDispatcher> m_input;
    IUiScreen* m_screen = nullptr;
    gui::LayoutCache m_cache;
    std::unique_ptr<gui::LayoutFrame> m_layout;
    std::unique_ptr<gui::DisplayList> m_display;
    std::vector<gui::LayoutNode> m_nodes;
    PaintList m_paint;
    platform::Modifiers m_mods;
    std::optional<gui::ComponentHandle> m_hover;
    gui::LayoutSize m_viewport;
    UiFrameStats m_stats;
    std::vector<std::pair<gui::ComponentHandle, TextTarget>> m_textTargets;
    std::vector<std::pair<gui::ComponentHandle, WheelHandler>> m_wheelHandlers;
    Clipboard m_clipboard;

    void deliverText(std::string& pending);
    void deliverWheel(f32 dx, f32 dy);
    [[nodiscard]] const TextTarget* focusedTextTarget() const;
};

} // namespace sbx::uihost
