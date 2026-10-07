// Phase 8C 클라이언트 UI: ImGuiLayer(이벤트 → ImGuiIO · 수정자 · 커서 · 클립보드 · IME 켜기 · 헤드리스 텍스처) ·
// 입력 가로채기(I1) · 패널 · F1. docs/06-RENDERING.md 10장, docs/07-PLATFORM.md, ADR-0023.
#include <doctest/doctest.h>

#include <set>
#include <string>

#include <imgui.h>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/ui/DebugPanels.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

ActionMap defaults() {
    auto m = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
    REQUIRE(m.has_value());
    return std::move(*m);
}

// 한 프레임: 이벤트 → NewFrame → ui() → Render (렌더러 없음)
template <class F>
void uiFrame(ImGuiLayer& layer, HeadlessWindow& window, F&& ui) {
    PlatformEventQueue q;
    window.pollEvents(q);
    layer.feed(q);
    layer.beginFrame(1.0 / 60.0);
    ui();
    (void)layer.endFrame(CursorShape::Arrow);
    layer.acknowledgeTextures();
}

struct WorldStub final : IWorldSession {
    int picks = 0, pauses = 0;
    void update(f64) override {}
    void extract(render::RenderWorld&) override {}
    [[nodiscard]] render::WorldRect bounds() const override { return {{0, 0}, {64, 64}}; }
    void togglePause() override { ++pauses; }
    void stepOnce() override {}
    void changeSpeed(int) override {}
    [[nodiscard]] std::string status() const override { return "stub"; }
    void selectAt(Vec2, bool) override { ++picks; }
    [[nodiscard]] WorldInfo info() const override {
        WorldInfo i;
        i.name = "stub";
        i.tick = 42;
        i.entities = 7;
        i.ticksPerSecond = 30;
        i.targetTicksPerSecond = 30;
        return i;
    }
};

} // namespace

TEST_SUITE("client") {

    TEST_CASE("imgui keys: every physical key except language keys maps to a distinct ImGuiKey, cursors map") {
        std::set<int> seen;
        for (usize i = 1; i < kKeyCount; ++i) {
            const Key k = static_cast<Key>(i);
            const int g = toImGuiKey(k);
            if (k == Key::Lang1 || k == Key::Lang2) {
                CHECK(g == ImGuiKey_None);
                continue;
            }
            INFO(keyName(k));
            CHECK(g != ImGuiKey_None);
            CHECK(seen.insert(g).second);
        }
        CHECK(toImGuiKey(Key::Unknown) == ImGuiKey_None);
        CHECK(toImGuiKey(Key::A) == ImGuiKey_A);
        CHECK(toImGuiKey(Key::Digit7) == ImGuiKey_7);
        CHECK(toImGuiKey(Key::F12) == ImGuiKey_F12);
        CHECK(toImGuiKey(Key::Kp5) == ImGuiKey_Keypad5);
        CHECK(fromImGuiCursor(ImGuiMouseCursor_TextInput) == CursorShape::TextInput);
        CHECK(fromImGuiCursor(ImGuiMouseCursor_ResizeNWSE) == CursorShape::ResizeNWSE);
        CHECK(fromImGuiCursor(ImGuiMouseCursor_Hand) == CursorShape::Hand);
        CHECK(fromImGuiCursor(ImGuiMouseCursor_None) == CursorShape::Hidden);
        CHECK(fromImGuiCursor(ImGuiMouseCursor_Arrow) == CursorShape::Arrow);
    }

    TEST_CASE("imgui layer: events reach ImGuiIO, modifiers, wheel, characters, display size, clipboard") {
        HeadlessWindow window(WindowDesc{.width = 800, .height = 600}, 1.5f);
        ImGuiLayer layer(window);
        // ImGui 는 한 프레임에 같은 장치의 상태 변화를 하나씩 풀어 준다(trickle) — 이 시험은 한 프레임에 다 보려고 끈다
        layer.io().ConfigInputTrickleEventQueue = false;
        window.inject(MouseMove{{100, 50}, {0, 0}});
        window.inject(MouseButtonDown{MouseButton::Right, {100, 50}});
        window.inject(MouseWheel{{0, 2}});
        window.inject(KeyDown{Key::LeftShift, 0, {}, false});
        window.inject(KeyDown{Key::A, 0, {}, false});
        window.inject(TextInput{U'한'});
        PlatformEventQueue q;
        window.pollEvents(q);
        layer.feed(q);
        layer.beginFrame(1.0 / 60.0);
        const ImGuiIO& io = layer.io();
        CHECK(io.MousePos.x == 100.f);
        CHECK(io.MousePos.y == 50.f);
        CHECK(io.MouseDown[1]);
        CHECK(io.MouseWheel == 2.f);
        CHECK(io.KeyShift);
        CHECK(ImGui::IsKeyDown(ImGuiKey_A));
        CHECK(io.DisplaySize.x == 800.f);
        CHECK(io.DisplayFramebufferScale.x == doctest::Approx(1.5f)); // 프레임버퍼 = 논리 × 1.5
        REQUIRE(io.InputQueueCharacters.Size == 1);
        CHECK(io.InputQueueCharacters[0] == static_cast<ImWchar>(U'한'));
        // 클립보드는 창으로
        ImGui::SetClipboardText("붙여넣기");
        CHECK(window.clipboardText() == "붙여넣기");
        window.setClipboardText("복사");
        CHECK(std::string(ImGui::GetClipboardText()) == "복사");
        (void)layer.endFrame(CursorShape::Arrow);
        layer.acknowledgeTextures();
        // Shift 를 떼면 수정자도 풀린다
        window.inject(KeyUp{Key::LeftShift, 0, {}});
        uiFrame(layer, window, [] {});
        CHECK_FALSE(layer.io().KeyShift);
        CHECK_FALSE(layer.fontName().empty());
    }

    TEST_CASE("imgui layer: hovering a window captures the mouse, a text field turns IME on and off") {
        HeadlessWindow window;
        ImGuiLayer layer(window);
        const auto panel = [] {
            ImGui::SetNextWindowPos({10, 10});
            ImGui::SetNextWindowSize({200, 100});
            ImGui::Begin("panel");
            ImGui::TextUnformatted("x");
            ImGui::End();
        };
        window.inject(MouseMove{{50, 50}, {0, 0}});
        uiFrame(layer, window, panel);
        uiFrame(layer, window, panel); // 창이 생긴 다음 프레임부터 그 위를 안다
        CHECK(layer.wantMouse());
        window.inject(MouseMove{{700, 500}, {0, 0}});
        uiFrame(layer, window, panel);
        CHECK_FALSE(layer.wantMouse());

        // 글자 칸에 포커스 → WantTextInput → 창의 글자 입력(IME)을 켠다, 나오면 끈다
        static char buf[32] = "";
        bool focus = true;
        const auto textField = [&] {
            ImGui::SetNextWindowPos({10, 10});
            ImGui::Begin("edit");
            if (focus) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputText("name", buf, sizeof(buf));
            ImGui::End();
        };
        CHECK_FALSE(window.textInputActive());
        uiFrame(layer, window, textField);
        focus = false;
        for (int i = 0; i < 5 && !layer.wantText();
             ++i) { // 포커스 요청 → 다음 프레임에 활성 → 그다음 NewFrame 이 알린다
            uiFrame(layer, window, textField);
        }
        uiFrame(layer, window, textField);
        CHECK(layer.wantText());
        CHECK(window.textInputActive());
        CHECK(layer.wantKeyboard());
        window.inject(KeyDown{Key::Escape, 0, {}, false}); // 글자 칸에서 나온다
        uiFrame(layer, window, textField);
        window.inject(KeyUp{Key::Escape, 0, {}});
        for (int i = 0; i < 5 && layer.wantText(); ++i) {
            uiFrame(layer, window, textField);
        }
        uiFrame(layer, window, textField);
        CHECK_FALSE(layer.wantText());
        CHECK_FALSE(window.textInputActive());
    }

    TEST_CASE("app with ui: panels drawn, clicks over a panel do not reach the world (I1), F1 hides panels") {
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        ImGuiLayer layer(window);
        WorldStub world;
        AppConfig cfg;
        cfg.world = &world;
        cfg.ui = &layer;
        cfg.fixedDt = 1.0 / 60.0;
        Application app(window, defaults(), audio, cfg);
        for (int i = 0; i < 4; ++i) {
            REQUIRE(app.frame()); // Boot → … → InWorld, 패널이 한 번 그려진다
        }
        REQUIRE(app.state() == AppState::InWorld);
        CHECK(app.panels().visible);
        CHECK(app.panels().count >= 1);

        // 시뮬레이션 패널(왼쪽 위 12, 12) 위 클릭: ImGui 가 가져간다 → 월드 선택 없음
        window.inject(MouseMove{{40, 40}, {0, 0}});
        REQUIRE(app.frame());
        window.inject(MouseButtonDown{MouseButton::Left, {40, 40}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {40, 40}});
        REQUIRE(app.frame());
        CHECK(world.picks == 0);
        // 패널 밖(가운데) 클릭은 월드로
        window.inject(MouseMove{{800, 450}, {0, 0}});
        REQUIRE(app.frame());
        window.inject(MouseButtonDown{MouseButton::Left, {800, 450}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {800, 450}});
        REQUIRE(app.frame());
        CHECK(world.picks == 1);

        // F1: 패널 숨기기 → 같은 자리 클릭이 월드로 간다
        window.inject(KeyDown{Key::F1, 0, {}, false});
        window.inject(KeyUp{Key::F1, 0, {}});
        REQUIRE(app.frame());
        CHECK_FALSE(app.panels().visible);
        window.inject(MouseMove{{40, 40}, {0, 0}});
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        window.inject(MouseButtonDown{MouseButton::Left, {40, 40}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {40, 40}});
        REQUIRE(app.frame());
        CHECK(world.picks == 2);
    }

    TEST_CASE("panels: actions come back instead of being applied, hidden panels return the inputs") {
        HeadlessWindow window;
        ImGuiLayer layer(window);
        PanelState st;
        st.pushFrame(16.6);
        st.pushFrame(17.2);
        CHECK(st.count == 2);
        for (int i = 0; i < 200; ++i) {
            st.pushFrame(10);
        }
        CHECK(st.count == PanelState::kHistory);
        WorldInfo w;
        w.name = "x";
        w.paused = true;
        PanelInputs in;
        in.world = &w;
        in.grid = true;
        in.details = false;
        PanelActions act;
        uiFrame(layer, window, [&] { act = drawDebugPanels(st, in); });
        CHECK(act.grid);
        CHECK_FALSE(act.details);
        CHECK_FALSE(act.togglePause);
        st.visible = false;
        in.grid = false;
        uiFrame(layer, window, [&] { act = drawDebugPanels(st, in); });
        CHECK_FALSE(act.grid);
        // 월드 없음(메뉴)도 그린다
        st.visible = true;
        in.world = nullptr;
        uiFrame(layer, window, [&] { act = drawDebugPanels(st, in); });
        CHECK_FALSE(act.step);
    }
}
