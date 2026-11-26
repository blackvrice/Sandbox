// UI-R1-04 · 05 (ADR-0174): GraphicsUI Studio 홈 화면 (최근 목록 · 단추 → 같은 StudioHomeAction 의 뜻) · DisplayList → ImDrawList
// 과도기 브리지 · ImGui 글꼴 측정. 창 · GPU 없이 (HeadlessWindow + ImGuiLayer — 렌더러 없음).
#include <doctest/doctest.h>

#include <array>

#include <imgui.h>

#include "apps/client/studio/GuiStudioHome.hpp"
#include "apps/client/ui/GraphicsUiImGuiBridge.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

GuiHomeModel model(usize n) {
    GuiHomeModel m;
    m.version = "0.7.0";
    for (usize i = 0; i < n; ++i) {
        m.recent.push_back({std::format("게임 {}", i), std::format("me.game{} · D:/p/{}", i, i),
                            std::filesystem::path(std::format("D:/p/{}", i))});
    }
    return m;
}

// 배치된 상자의 가운데 (논리 좌표) — 게시 순서 index 의 컴포넌트
PlatformEvent at(const uihost::UiHost& host, usize index, bool press) {
    const auto& box = host.lastLayout()->boxes()[index];
    const Vec2 c{static_cast<f32>(box.bounds.x + box.bounds.width / 2),
                 static_cast<f32>(box.bounds.y + box.bounds.height / 2)};
    if (press) {
        return MouseButtonDown{MouseButton::Left, c};
    }
    return MouseButtonUp{MouseButton::Left, c};
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE("gui studio home: recent rows open, switch and quit buttons, keyboard activation") {
        uihost::ApproxTextMeasure text;
        auto host = uihost::UiHost::create(text);
        REQUIRE(host);
        GuiStudioHome home;
        home.setModel(model(3));
        REQUIRE((*host)->setScreen(&home));
        CHECK(home.rowCount() == 3);
        const auto d = (*host)->frame({1280, 720});
        REQUIRE(d);
        REQUIRE(*d != nullptr);
        // 루트 · 제목 · 판 · 목록 제목 · 목록 · 행 3 · 단추 줄 · 단추 2 · 알림 = 12
        CHECK((*host)->stats().components == 12);
        CHECK_FALSE(home.takeIntent());

        // 첫 포커스 = 첫 행 — Enter 가 그것을 연다
        const std::array enter{PlatformEvent{KeyDown{Key::Enter, 0, {}, false}}};
        (*host)->feed(enter);
        const auto first = home.takeIntent();
        REQUIRE(first);
        CHECK(first->kind == GuiHomeIntent::Kind::OpenRecent);
        CHECK(first->dir == std::filesystem::path("D:/p/0"));
        CHECK_FALSE(home.takeIntent()); // 한 번 읽으면 비운다

        // 둘째 행 (게시 순서 6) 을 누르면 그 프로젝트를 연다 (누르면 포커스도 그 행으로 — Button 계약)
        const std::array click{at(**host, 6, true), at(**host, 6, false)};
        (*host)->feed(click);
        const auto open = home.takeIntent();
        REQUIRE(open);
        CHECK(open->dir == std::filesystem::path("D:/p/1"));
        const std::array space{PlatformEvent{KeyDown{Key::Space, 0, {}, false}}};
        (*host)->feed(space);
        CHECK(home.takeIntent()->dir == std::filesystem::path("D:/p/1"));

        // 단추: 기존 UI 로 (게시 순서 9 = 단추 줄 8 의 첫째) · 종료 (10)
        const std::array sw{at(**host, 9, true), at(**host, 9, false)};
        (*host)->feed(sw);
        CHECK(home.takeIntent()->kind == GuiHomeIntent::Kind::SwitchToClassic);
        const std::array quit{at(**host, 10, true), at(**host, 10, false)};
        (*host)->feed(quit);
        CHECK(home.takeIntent()->kind == GuiHomeIntent::Kind::Quit);
    }

    TEST_CASE("gui studio home: empty list shows a hint, long lists show the first rows, remount keeps working") {
        uihost::ApproxTextMeasure text;
        auto host = uihost::UiHost::create(text);
        REQUIRE(host);
        GuiStudioHome home;
        home.setModel(model(0));
        REQUIRE((*host)->setScreen(&home));
        REQUIRE((*host)->frame({800, 600}));
        CHECK(home.rowCount() == 0);
        CHECK((*host)->stats().components == 10); // 행 대신 빈 글 하나

        home.setModel(model(GuiStudioHome::kMaxRows + 5));
        REQUIRE((*host)->setScreen(&home)); // 다시 마운트 (Application 이 최근 목록이 바뀌면 한다)
        REQUIRE((*host)->frame({800, 600}));
        CHECK(home.rowCount() == GuiStudioHome::kMaxRows);
        CHECK((*host)->stats().components == 9 + GuiStudioHome::kMaxRows);
        // 좁은 화면 (행 폭이 0 까지 준다) 에서도 배치가 실패하지 않는다
        const auto narrow = (*host)->frame({40, 600});
        INFO((narrow ? std::string() : narrow.error().describe()));
        REQUIRE(narrow);
    }

    TEST_CASE("gui bridge: display list becomes ImDrawList commands with clips, ImGui font measures text") {
        HeadlessWindow window(WindowDesc{.width = 900, .height = 600});
        ImGuiLayer layer(window);
        PlatformEventQueue q;
        window.pollEvents(q);
        layer.feed(q);
        layer.beginFrame(1.0 / 60.0);

        GuiImGuiTextMeasure measure;
        const auto narrow = measure.measure("ab", 16);
        const auto wide = measure.measure("abcdefgh", 16);
        CHECK(narrow.width > 0.0);
        CHECK(wide.width > narrow.width);
        CHECK(measure.measure("", 16).width == 0.0);

        auto host = uihost::UiHost::create(measure);
        REQUIRE(host);
        GuiStudioHome home;
        home.setModel(model(2));
        REQUIRE((*host)->setScreen(&home));
        const auto d = (*host)->frame({900, 600});
        REQUIRE(d);
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const int before = dl->VtxBuffer.Size;
        const GuiBridgeStats st = drawDisplayList(**d, *dl);
        CHECK(st.rects >= 5);   // 배경 · 행 2 · 단추 2 (+ 포커스 띠)
        CHECK(st.texts >= 7);   // 제목 · 판 · 목록 제목 · 행 2 × 2 · 단추 2 · 알림
        CHECK(st.clips >= 1);   // 루트 clip · 행 clip
        CHECK(dl->VtxBuffer.Size > before);
        CHECK(dl->_ClipRectStack.Size == 1); // clip 짝이 맞는다 (배경 목록의 기본 clip 하나만 남는다)
        (void)layer.endFrame(CursorShape::Arrow);
        layer.acknowledgeTextures();
        REQUIRE((*host)->setScreen(nullptr)); // 화면을 먼저 놓는다 (호스트보다 화면이 먼저 사라지지 않게)
    }
}
