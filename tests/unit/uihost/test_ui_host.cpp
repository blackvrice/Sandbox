// GraphicsUI 통합 계층 (UI-R1-02 · 03, ADR-0174): 입력 변환 표 · 응용 호스트의 프레임 수명 (마운트 → 입력 → 배치 → 그리기) ·
// 지난 세대 입력 · 작업자 전달 · 화면 바꾸기. GPU · 창 없이 (GraphicsUI Core 만).
#include <doctest/doctest.h>

#include <array>
#include <set>
#include <thread>

#include <graphics-ui/core/controls.hpp>

#include "uihost/InputRouting.hpp"
#include "uihost/UiHost.hpp"

using namespace sbx;
using namespace sbx::uihost;
using platform::Key;

namespace {

// 루트 (column) 아래 단추 둘 — 각 단추는 64 × 24 고정. 눌린 수를 센다
struct TwoButtons final : IUiScreen {
    gui::ComponentHandle root, a, b;
    std::unique_ptr<gui::Button> ba, bb;
    int pressedA = 0, pressedB = 0, mounts = 0;
    gui::StateField counter;

    Expected<void> mount(UiHost& host) override {
        ++mounts;
        const std::array<gui::ComponentSpec, 3> specs{{{std::nullopt, 1, 0}, {0, 2, 1}, {0, 2, 2}}};
        if (!host.tree().commit(specs)) {
            return makeError(ErrorCode::Unknown, "commit");
        }
        const auto snap = host.tree().snapshot();
        root = snap->components[0].handle;
        a = snap->components[1].handle;
        b = snap->components[2].handle;
        ba = std::make_unique<gui::Button>(host.input(), a);
        bb = std::make_unique<gui::Button>(host.input(), b);
        (void)ba->setEnabled(true);
        (void)bb->setEnabled(true);
        (void)host.input().setHandler(a, [this](const gui::RoutedEvent& e) {
            const auto r = ba->handle(e);
            pressedA += r && r->activated ? 1 : 0;
            return r ? r->reply : gui::EventReply{};
        });
        (void)host.input().setHandler(b, [this](const gui::RoutedEvent& e) {
            const auto r = bb->handle(e);
            pressedB += r && r->activated ? 1 : 0;
            return r ? r->reply : gui::EventReply{};
        });
        auto f = host.state().define(std::int64_t{0});
        if (!f) {
            return makeError(ErrorCode::Unknown, "define");
        }
        counter = *f;
        return {};
    }
    Expected<void> layout(UiHost&, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) override {
        gui::LayoutNode r;
        r.component = root;
        r.style.kind = gui::LayoutKind::column;
        r.style.gap = 8;
        r.style.padding = {10, 10, 10, 10};
        out.push_back(r);
        for (auto h : {a, b}) {
            gui::LayoutNode n;
            n.component = h;
            n.style.preferred_width = 64;
            n.style.preferred_height = 24;
            out.push_back(n);
        }
        return {};
    }
    Expected<void> paint(UiHost&, const gui::LayoutFrame& frame, PaintList& out) override {
        for (const gui::LayoutBox& box : frame.boxes()) {
            out.add(box.component, {gui::FillRect{{0, 0, box.bounds.width, box.bounds.height}, {40, 40, 40, 255}}});
        }
        return {};
    }
};

platform::PlatformEvent down(f32 x, f32 y) {
    return platform::MouseButtonDown{platform::MouseButton::Left, {x, y}};
}
platform::PlatformEvent up(f32 x, f32 y) {
    return platform::MouseButtonUp{platform::MouseButton::Left, {x, y}};
}

} // namespace

TEST_SUITE("uihost") {

    TEST_CASE("input routing: keys map to distinct GraphicsUI codes, control keys match Core constants") {
        std::set<gui::KeyCode> seen;
        for (usize i = 1; i < platform::kKeyCount; ++i) {
            const Key k = static_cast<Key>(i);
            const gui::KeyCode c = toGuiKey(k);
            INFO(platform::keyName(k));
            const bool unmapped = platform::modifierBitOf(k) != 0 || k == Key::Lang1 || k == Key::Lang2 ||
                                  k == Key::KpEqual;
            if (unmapped) {
                CHECK(c == 0);
                continue;
            }
            CHECK(c != 0);
            // Enter 와 키패드 Enter 는 같은 활성화 키다 — 나머지는 서로 다르다
            if (k != Key::KpEnter) {
                CHECK(seen.insert(c).second);
            }
        }
        CHECK(toGuiKey(Key::Enter) == gui::enter_key);
        CHECK(toGuiKey(Key::KpEnter) == gui::enter_key);
        CHECK(toGuiKey(Key::Space) == gui::space_key);
        CHECK(toGuiKey(Key::Escape) == gui::escape_key);
        CHECK(toGuiKey(Key::Left) == gui::left_key);
        CHECK(toGuiKey(Key::Delete) == gui::delete_key);
        CHECK(toGuiKey(Key::A) == 'A');
        CHECK(toGuiKey(Key::Digit7) == '7');
        CHECK(toGuiKey(Key::Unknown) == 0);

        platform::Modifiers m;
        m.bits = platform::Modifiers::kCtrl | platform::Modifiers::kShift;
        CHECK(toGuiModifiers(m) == (gui::KeyModifiers::control | gui::KeyModifiers::shift));
        CHECK(toGuiButton(platform::MouseButton::Left) == gui::PointerButton::primary);
        CHECK(toGuiButton(platform::MouseButton::Right) == gui::PointerButton::secondary);
    }

    TEST_CASE("input routing: events translate, non-input events do not") {
        const auto k = translate(platform::KeyDown{Key::S, 0, {platform::Modifiers::kCtrl}, true}, {});
        REQUIRE(k);
        const auto& ke = std::get<gui::KeyEvent>(*k);
        CHECK(ke.action == gui::KeyAction::down);
        CHECK(ke.chord.key == 'S');
        CHECK(ke.chord.modifiers == gui::KeyModifiers::control);
        CHECK(ke.repeat);
        const auto p = translate(down(3.5f, 4.f), {platform::Modifiers::kShift});
        REQUIRE(p);
        const auto& pe = std::get<gui::PointerEvent>(*p);
        CHECK(pe.action == gui::PointerAction::down);
        CHECK(pe.x == doctest::Approx(3.5));
        CHECK(pe.button == gui::PointerButton::primary);
        CHECK(pe.modifiers == gui::KeyModifiers::shift);
        CHECK_FALSE(translate(platform::TextInput{U'가'}, {}));
        CHECK_FALSE(translate(platform::MouseWheel{{0, 1}}, {}));
        CHECK_FALSE(translate(platform::KeyDown{Key::LeftShift, 0, {}, false}, {})); // 수정자 키 자신
    }

    TEST_CASE("host: mount, layout, paint and click through the last frame's hit test") {
        ApproxTextMeasure text;
        auto host = UiHost::create(text);
        REQUIRE(host);
        TwoButtons screen;
        REQUIRE((*host)->setScreen(&screen));
        CHECK(screen.mounts == 1);

        // 화면이 아직 배치되지 않았다 — 입력은 대상 없이 간다 (눌리지 않는다)
        const std::array early{down(20, 20), up(20, 20)};
        (*host)->feed(early);
        CHECK(screen.pressedA == 0);

        const auto d = (*host)->frame({200, 120});
        REQUIRE(d);
        REQUIRE(*d != nullptr);
        CHECK((*host)->stats().components == 3);
        CHECK((*host)->stats().commands >= 3); // 사각형 셋 (+ 루트 clip)
        // 첫 단추 (10,10)-(74,34), 둘째 (10,42)-(74,66)
        CHECK((*host)->hitsUi(20, 20));
        CHECK_FALSE((*host)->hitsUi(150, 100)); // 루트 배경 — UI 가 아니다

        const std::array clickA{down(20, 20), up(20, 20)};
        (*host)->feed(clickA);
        CHECK(screen.pressedA == 1);
        CHECK(screen.pressedB == 0);
        const std::array clickB{down(20, 50), up(20, 50)};
        (*host)->feed(clickB);
        CHECK(screen.pressedB == 1);
        // 밖에서 떼면 활성화되지 않는다 (캡처 — 누른 단추가 떼는 것도 받는다)
        const std::array dragOut{down(20, 20), up(150, 100)};
        (*host)->feed(dragOut);
        CHECK(screen.pressedA == 1);
    }

    TEST_CASE("host: keyboard activation, Tab moves focus, focus loss cancels a press") {
        ApproxTextMeasure text;
        auto host = UiHost::create(text);
        REQUIRE(host);
        TwoButtons screen;
        REQUIRE((*host)->setScreen(&screen));
        REQUIRE((*host)->frame({200, 120}));
        REQUIRE((*host)->input().setFocus(screen.a));
        const std::array enter{platform::PlatformEvent{platform::KeyDown{Key::Enter, 0, {}, false}},
                               platform::PlatformEvent{platform::KeyUp{Key::Enter, 0, {}}}};
        (*host)->feed(enter);
        CHECK(screen.pressedA == 1);
        const std::array tab{platform::PlatformEvent{platform::KeyDown{Key::Tab, 0, {}, false}}};
        (*host)->feed(tab);
        const auto f = (*host)->input().focus();
        REQUIRE(f);
        CHECK(*f == std::optional<gui::ComponentHandle>(screen.b));
        // 누른 채 창 포커스를 잃으면 취소 — 다시 돌아와 떼도 눌리지 않는다
        const std::array lost{down(20, 50), platform::PlatformEvent{platform::FocusLost{}}, up(20, 50)};
        (*host)->feed(lost);
        CHECK(screen.pressedB == 0);
    }

    TEST_CASE("host: worker values arrive on the next frame, stale targets are rejected") {
        ApproxTextMeasure text;
        auto host = UiHost::create(text);
        REQUIRE(host);
        TwoButtons screen;
        REQUIRE((*host)->setScreen(&screen));
        auto sender = (*host)->sender();
        REQUIRE(sender);
        std::thread worker([&] {
            const std::array w{gui::StateWrite{screen.counter, std::int64_t{42}}};
            CHECK(sender->post(screen.a, w));
        });
        worker.join();
        CHECK(std::get<std::int64_t>(*(*host)->state().read(screen.counter)) == 0); // 아직 UI 스레드가 적용하지 않았다
        REQUIRE((*host)->frame({200, 120}));
        CHECK(std::get<std::int64_t>(*(*host)->state().read(screen.counter)) == 42);
        CHECK((*host)->stats().workerErrors == 0);

        // 대상이 사라진 (다시 마운트한) 뒤 도착한 메시지는 버린다
        const gui::ComponentHandle oldA = screen.a;
        const gui::StateField oldField = screen.counter;
        REQUIRE((*host)->setScreen(&screen)); // 새 세대
        const std::array w{gui::StateWrite{oldField, std::int64_t{7}}};
        (void)sender->post(oldA, w);
        REQUIRE((*host)->frame({200, 120}));
        CHECK((*host)->stats().workerErrors == 1);
    }

    TEST_CASE("host: switching screens drops the old tree; frame without a screen draws nothing") {
        ApproxTextMeasure text;
        auto host = UiHost::create(text);
        REQUIRE(host);
        TwoButtons screen;
        REQUIRE((*host)->setScreen(&screen));
        REQUIRE((*host)->frame({200, 120}));
        const gui::ComponentHandle oldA = screen.a;
        REQUIRE((*host)->setScreen(nullptr));
        CHECK_FALSE((*host)->tree().resolve(oldA)); // 지난 세대
        const auto d = (*host)->frame({200, 120});
        REQUIRE(d);
        CHECK(*d == nullptr);
        CHECK_FALSE((*host)->hitsUi(20, 20));
        const std::array click{down(20, 20), up(20, 20)};
        (*host)->feed(click); // 화면이 없으면 아무 일도 없다
        CHECK(screen.pressedA == 0);
        CHECK_FALSE((*host)->frame({-1, 10})); // 음수 크기
    }

    TEST_CASE("approx text measure: ASCII narrower than Hangul, height follows em") {
        ApproxTextMeasure m;
        const auto ascii = m.measure("abcd", 10);
        const auto hangul = m.measure("가나다라", 10);
        CHECK(ascii.width == doctest::Approx(22));
        CHECK(hangul.width == doctest::Approx(40));
        CHECK(hangul.height == doctest::Approx(12.5));
        CHECK(guiErrorName(gui::ErrorCode::stale_handle) == "stale_handle");
        CHECK(toError(gui::ErrorCode::queue_full, "post").code == ErrorCode::OutOfRange);
    }
}
