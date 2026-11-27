// GXML 화면의 호스트 uihost::DocumentView (UI-R2-02, ADR-0175): 빌드 때 컴파일한 문서 (tests/data/gxml/test_form — 글자 칸 · 단추) 를
// DocumentSession 으로 — 포인터 · 배율 · 확정 글자 → 누른 TextBox · 편집 키 · 단추 명령 · IME 자리 · 문서 검증 실패.
#include <doctest/doctest.h>

#include <array>
#include <string>

#include "uihost/DocumentView.hpp"

#if defined(SBX_HAS_GXML)
#include "sandbox/gxml/test_form.hpp"
#endif

using namespace sbx;
using namespace sbx::uihost;
using platform::Key;

#if defined(SBX_HAS_GXML)

namespace {

struct FormModel {
    gui::StateStore* store = nullptr;
    gui::StateField name;
    int oks = 0;
    gui::DocumentSession::Setup setup() {
        return [this](gui::StateStore& s) -> gui::Result<gui::DocumentHost> {
            store = &s;
            // 검증기: 16 바이트까지 (넘는 편집은 문서가 거절 — 값이 그대로)
            auto f = s.define(std::string(), [](const gui::StateValue& v) {
                return std::get<std::string>(v).size() <= 16;
            });
            if (!f) {
                return std::unexpected(f.error());
            }
            name = *f;
            return gui::DocumentHost{
                {{"name", *f}},
                // Edit 처리기가 값을 게시한다 (DocumentSession 계약 — 검증기가 거절하면 편집이 실패한다)
                {{"set_name", gui::DocumentCommand::Edit{[this](std::string_view v) -> gui::Result<void> {
                      const std::array w{gui::StateWrite{name, std::string(v)}};
                      const auto r = store->transact(w);
                      if (!r) {
                          return std::unexpected(r.error());
                      }
                      return {};
                  }},
                  "name"},
                 {"ok", gui::DocumentCommand::Invoke{[this]() -> gui::Result<void> {
                      ++oks;
                      return {};
                  }},
                  {}}}};
        };
    }
    std::string value() const { return std::get<std::string>(*store->read(name)); }
};

gui::LayoutRect boxOf(DocumentView& v, std::string_view id) {
    const auto h = v.session().find(id);
    REQUIRE(h);
    REQUIRE(*h);
    for (const gui::LayoutBox& b : v.session().layout().boxes()) {
        if (b.component == **h) {
            return b.bounds;
        }
    }
    FAIL("상자가 없다");
    return {};
}

// 상자 가운데를 누르고 뗀다 (창 좌표 = UI × 배율)
std::array<platform::PlatformEvent, 3> click(DocumentView& v, std::string_view id) {
    const gui::LayoutRect r = boxOf(v, id);
    const f32 s = v.scale();
    const Vec2 p{static_cast<f32>((r.x + r.width / 2) * s), static_cast<f32>((r.y + r.height / 2) * s)};
    return {platform::MouseMove{p, {}}, platform::MouseButtonDown{platform::MouseButton::Left, p, 1},
            platform::MouseButtonUp{platform::MouseButton::Left, p}};
}

} // namespace

TEST_SUITE("uihost") {

    TEST_CASE("document view: GXML form — click focuses the TextBox, committed text edits, button runs its command") {
        FormModel model;
        auto view = DocumentView::create(graphics_ui::generated::document_test_form(), model.setup());
        REQUIRE(view);
        DocumentView& v = **view;
        // 첫 frame 전에는 누를 자리가 없다
        const std::array early{platform::PlatformEvent{platform::MouseButtonDown{platform::MouseButton::Left, {20, 20}, 1}}};
        v.feed(early);
        REQUIRE(v.frame({400, 300}));
        CHECK(v.stats().components >= 5);
        CHECK_FALSE(v.textFocused());
        CHECK_FALSE(v.textInput().active);

        v.feed(click(v, "name"));
        REQUIRE(v.frame({400, 300}));
        CHECK(v.textFocused());
        CHECK(v.focusedTextId() == std::optional<std::string>("name"));
        const TextInputState ti = v.textInput();
        CHECK(ti.active);
        REQUIRE(ti.caret);
        const gui::LayoutRect nb = boxOf(v, "name");
        CHECK(ti.caret->x == doctest::Approx(nb.x));

        const std::array typed{platform::PlatformEvent{platform::TextInput{U'한'}},
                               platform::PlatformEvent{platform::TextInput{U'글'}}};
        v.feed(typed);
        CHECK(model.value() == "한글");
        const std::array back{platform::PlatformEvent{platform::KeyDown{Key::Backspace, 0, {}, false}}};
        v.feed(back);
        CHECK(model.value() == "한");
        // 문서의 검증기가 넘는 글자를 거절 — 값 그대로, 버린 입력으로 센다
        const std::array tooLong{platform::PlatformEvent{platform::TextInput{U'가'}}, platform::PlatformEvent{platform::TextInput{U'나'}},
                                 platform::PlatformEvent{platform::TextInput{U'다'}}, platform::PlatformEvent{platform::TextInput{U'라'}},
                                 platform::PlatformEvent{platform::TextInput{U'마'}}, platform::PlatformEvent{platform::TextInput{U'바'}}};
        v.feed(tooLong);
        CHECK(model.value() == "한");
        CHECK(v.stats().inputRejected >= 1);

        // 단추를 누르면 명령, 포커스는 글자 칸을 떠난다 (글자 대상 없음)
        v.feed(click(v, "ok"));
        REQUIRE(v.frame({400, 300}));
        CHECK(model.oks == 1);
        CHECK_FALSE(v.textFocused());
        const std::array stray{platform::PlatformEvent{platform::TextInput{U'x'}}};
        v.feed(stray);
        CHECK(model.value() == "한");
    }

    TEST_CASE("document view: UI scale — layout in UI units, clicks and IME area in window units") {
        FormModel model;
        auto view = DocumentView::create(graphics_ui::generated::document_test_form(), model.setup());
        REQUIRE(view);
        DocumentView& v = **view;
        v.setScale(2.f);
        REQUIRE(v.frame({800, 600})); // UI 400 × 300
        const gui::LayoutRect ok = boxOf(v, "ok");
        CHECK(v.hitsUi(static_cast<f32>((ok.x + 1) * 2), static_cast<f32>((ok.y + 1) * 2)));
        v.feed(click(v, "ok"));
        CHECK(model.oks == 1);
        v.feed(click(v, "name"));
        const TextInputState ti = v.textInput();
        REQUIRE(ti.caret);
        CHECK(ti.caret->x == doctest::Approx(boxOf(v, "name").x * 2));
        CHECK_FALSE(v.frame({-1, 10}));
    }

    TEST_CASE("document view: a host that does not register the model's fields is rejected") {
        const auto bad = [](gui::StateStore&) -> gui::Result<gui::DocumentHost> { return gui::DocumentHost{}; };
        CHECK_FALSE(DocumentView::create(graphics_ui::generated::document_test_form(), bad));
    }
}

#endif
