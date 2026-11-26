// UI-R1-04 · 05 · 08, UI-R2-09 (ADR-0174): GraphicsUI Studio 홈 — 검색 글자 칸 · 가상 목록 (휠 · 키 · 클릭) · 새 프로젝트 폼 (제안 ·
// 템플릿 · Enter · 만들기) · 폴더 열기 · 클립보드 · IME 자리, DisplayList → ImDrawList 브리지, Application 의 같은 명령 길.
// 창 · GPU 없이 (HeadlessWindow + ImGuiLayer — 렌더러 없음).
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <vector>

#include <imgui.h>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "editor/gui/StudioHomeScreen.hpp"
#include "apps/client/studio/StudioHome.hpp"
#include "apps/client/ui/GraphicsUiImGuiBridge.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
using Part = StudioHomeScreen::Part;

namespace {

StudioHomeModel model(usize n) {
    StudioHomeModel m;
    m.version = "0.7.0";
    for (usize i = 0; i < n; ++i) {
        editor::services::RecentProject p;
        p.dir = std::filesystem::path(std::format("D:/p/{}", i));
        p.name = std::format("게임 {}", i);
        p.gameId = std::format("me.game{}", i);
        m.recent.push_back(p);
    }
    m.templates.push_back({"empty", "빈 월드", "", {}, ""});
    m.templates.push_back({"minigame", "미니게임", "", {}, ""});
    m.examples.push_back({std::filesystem::path("D:/samples/rabbit_guard"), "Rabbit Guard", "sample.rabbit", "1.0.0", ""});
    m.examples.push_back({std::filesystem::path("D:/samples/orbs"), "Orbs", "sample.orbs", "1.0.0", ""});
    return m;
}

// 컴포넌트의 배치 상자 (직전 frame)
gui::LayoutRect boxOf(const uihost::UiHost& host, gui::ComponentHandle h) {
    for (const gui::LayoutBox& b : host.lastLayout()->boxes()) {
        if (b.component == h) {
            return b.bounds;
        }
    }
    FAIL("상자가 없다");
    return {};
}

// 상자 안 (dx, dy) 를 누르고 뗀다 (음수 = 가운데)
std::array<PlatformEvent, 3> clickAt(const uihost::UiHost& host, gui::ComponentHandle h, f32 dx = -1.f,
                                     f32 dy = -1.f) {
    const gui::LayoutRect r = boxOf(host, h);
    const Vec2 p{static_cast<f32>(r.x + (dx < 0 ? r.width / 2 : dx)),
                 static_cast<f32>(r.y + (dy < 0 ? r.height / 2 : dy))};
    return {MouseMove{p, {0, 0}}, MouseButtonDown{MouseButton::Left, p}, MouseButtonUp{MouseButton::Left, p}};
}

std::string pathText8(const std::filesystem::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

std::vector<PlatformEvent> typed(std::u32string_view s) {
    std::vector<PlatformEvent> out;
    for (const char32_t c : s) {
        out.push_back(TextInput{c});
    }
    return out;
}

PlatformEvent key(Key k, u8 mods = 0) {
    return KeyDown{k, 0, Modifiers{mods}, false};
}

struct Fixture {
    uihost::ApproxTextMeasure text;
    std::unique_ptr<uihost::UiHost> host;
    StudioHomeScreen home;
    std::string clipboard;

    explicit Fixture(usize recent, gui::LayoutSize vp = {1280, 720}) {
        auto h = uihost::UiHost::create(text);
        REQUIRE(h);
        host = std::move(*h);
        host->setClipboard({[this] { return clipboard; }, [this](std::string_view t) { clipboard = std::string(t); }});
        home.setModel(model(recent));
        REQUIRE(host->setScreen(&home));
        REQUIRE(host->frame(vp));
    }
    ~Fixture() { (void)host->setScreen(nullptr); }
    template <class R>
    void feed(const R& events) {
        host->feed(std::span<const PlatformEvent>(events.data(), events.size()));
        REQUIRE(host->frame(host->viewport()));
    }
    void feed(PlatformEvent e) {
        const std::array one{e};
        feed(one);
    }
    gui::ComponentHandle h(Part p) const {
        const auto r = home.handleOf(p);
        REQUIRE(r);
        return *r;
    }
};

} // namespace

TEST_SUITE("client") {

    TEST_CASE("gui studio home: search field filters the virtual list, arrows and Enter open, IME follows the caret") {
        Fixture f(3);
        CHECK(f.home.listedCount() == 3);
        // 첫 포커스 = 검색 칸 — 글자 입력을 켤 자리 (캐럿) 가 칸 안
        const uihost::TextInputState ti = f.host->textInput();
        CHECK(ti.active);
        REQUIRE(ti.caret);
        const gui::LayoutRect sb = boxOf(*f.host, f.h(Part::Search));
        CHECK(ti.caret->x >= sb.x);
        CHECK(ti.caret->x <= sb.x + sb.width);
        CHECK(ti.caret->y >= sb.y);

        f.feed(typed(U"game2"));
        CHECK(f.home.fieldText(Part::Search) == "game2");
        CHECK(f.home.listedCount() == 1);
        CHECK(f.home.selectedRecent() == std::optional<usize>(2));
        f.feed(key(Key::Enter));
        auto open = f.home.takeIntent();
        REQUIRE(open);
        CHECK(open->kind == StudioHomeIntent::Kind::OpenRecent);
        CHECK(open->dir == std::filesystem::path("D:/p/2"));

        // 지우면 다시 전부 — ↓ 두 번은 목록을 움직인다 (검색 칸을 떠나지 않는다)
        const std::array erase{key(Key::Backspace), key(Key::Backspace), key(Key::Backspace), key(Key::Backspace),
                               key(Key::Backspace)};
        f.feed(erase);
        CHECK(f.home.fieldText(Part::Search).empty());
        CHECK(f.home.listedCount() == 3);
        const std::array down{key(Key::Down), key(Key::Down)};
        f.feed(down);
        CHECK(f.home.selectedRecent() == std::optional<usize>(2));
        CHECK(f.host->textFocused());
    }

    TEST_CASE("gui studio home: list click opens, keys move, wheel scrolls a long list, Tab leaves the text field") {
        Fixture f(40);
        CHECK(f.home.listedCount() == 40);
        // 둘째 행 (목록 위에서 1.5 행) 을 누르면 연다 — 누르면 목록이 포커스
        f.feed(clickAt(*f.host, f.h(Part::List), 30.f, static_cast<f32>(StudioHomeScreen::kRowHeight * 1.5)));
        const auto open = f.home.takeIntent();
        REQUIRE(open);
        CHECK(open->dir == std::filesystem::path("D:/p/1"));
        CHECK_FALSE(f.host->textFocused());
        CHECK_FALSE(f.host->textInput().active); // 목록은 글자 대상이 아니다 — IME 를 끈다
        const std::array keys{key(Key::Down), key(Key::Down), key(Key::End)};
        f.feed(keys);
        CHECK(f.home.selectedRecent() == std::optional<usize>(39));
        CHECK(f.home.listOffset() > 0.0); // End 가 마지막 행을 보이게 밀었다
        f.feed(key(Key::Home));
        CHECK(f.home.selectedRecent() == std::optional<usize>(0));
        CHECK(f.home.listOffset() == doctest::Approx(0.0));

        // 휠 (포인터가 목록 위) — 아래로 한 칸 = 세 줄
        const gui::LayoutRect lb = boxOf(*f.host, f.h(Part::List));
        const std::array wheel{
            PlatformEvent{MouseMove{{static_cast<f32>(lb.x + 10), static_cast<f32>(lb.y + 10)}, {}}},
            PlatformEvent{MouseWheel{{0.f, -1.f}}}};
        f.feed(wheel);
        CHECK(f.home.listOffset() == doctest::Approx(StudioHomeScreen::kRowHeight * 3.0));
        // 목록 밖 (제목) 의 휠은 아무것도 밀지 않는다
        const std::array outside{PlatformEvent{MouseMove{{5.f, 5.f}, {}}}, PlatformEvent{MouseWheel{{0.f, -1.f}}}};
        f.feed(outside);
        CHECK(f.home.listOffset() == doctest::Approx(StudioHomeScreen::kRowHeight * 3.0));

        // 검색 칸에서 Tab → 목록 (포커스 순회는 호스트)
        f.feed(clickAt(*f.host, f.h(Part::Search)));
        CHECK(f.host->textFocused());
        f.feed(key(Key::Tab));
        CHECK_FALSE(f.host->textFocused());
    }

    TEST_CASE("gui studio home: new project form suggests gameId and folder, template buttons, Create and Enter") {
        Fixture f(1);
        f.feed(clickAt(*f.host, f.h(Part::TemplateNext))); // ‹ › 넘기기 — 미니게임
        CHECK(f.home.templateIndex() == 1);
        f.feed(clickAt(*f.host, f.h(Part::TemplateNext))); // 끝에서 처음으로 돈다
        CHECK(f.home.templateIndex() == 0);
        f.feed(clickAt(*f.host, f.h(Part::TemplatePrev)));
        CHECK(f.home.templateIndex() == 1);
        f.feed(clickAt(*f.host, f.h(Part::Name)));
        CHECK(f.host->textFocused());
        f.feed(typed(U"Rabbit Run"));
        CHECK(f.home.fieldText(Part::Name) == "Rabbit Run");
        CHECK(f.home.fieldText(Part::GameId) == "me.rabbit_run"); // ImGui 홈과 같은 제안 (suggestGameId)
        CHECK(f.home.fieldText(Part::Dir).ends_with("RabbitRun")); // suggestProjectDir — 영숫자만

        // gameId 를 손으로 고치면 이름을 바꿔도 따라가지 않는다
        f.feed(clickAt(*f.host, f.h(Part::GameId)));
        const std::array selectAll{key(Key::A, Modifiers::kCtrl)};
        f.feed(selectAll);
        f.feed(typed(U"studio.rabbit"));
        CHECK(f.home.fieldText(Part::GameId) == "studio.rabbit");
        f.feed(clickAt(*f.host, f.h(Part::Name)));
        const std::array end{key(Key::End)};
        f.feed(end);
        f.feed(typed(U"2"));
        CHECK(f.home.fieldText(Part::GameId) == "studio.rabbit");

        f.feed(clickAt(*f.host, f.h(Part::Create)));
        auto made = f.home.takeIntent();
        REQUIRE(made);
        CHECK(made->kind == StudioHomeIntent::Kind::NewProject);
        CHECK(made->name == "Rabbit Run2");
        CHECK(made->gameId == "studio.rabbit");
        CHECK(made->templateId == "minigame");
        CHECK_FALSE(made->dir.empty());
        // 폼 칸의 Enter = 만들기
        f.feed(clickAt(*f.host, f.h(Part::GameId)));
        f.feed(key(Key::Enter));
        const auto again = f.home.takeIntent();
        REQUIRE(again);
        CHECK(again->kind == StudioHomeIntent::Kind::NewProject);

        // 다시 마운트 (최근 목록이 바뀜) 해도 쓰던 글자가 남는다
        f.home.setModel(model(2));
        REQUIRE(f.host->setScreen(&f.home));
        REQUIRE(f.host->frame({1280, 720}));
        CHECK(f.home.fieldText(Part::Name) == "Rabbit Run2");
    }

    TEST_CASE("gui studio home: open folder, clipboard copy and paste, caret placed by click, narrow layout") {
        Fixture f(0, {700, 1200}); // 좁다 — 한 칸으로 쌓는다 (세로 스크롤이 없어 내용이 다 들어가는 높이 — UI-R2 남은 일)
        CHECK(f.home.listedCount() == 0);
        f.feed(clickAt(*f.host, f.h(Part::OpenDir)));
        f.clipboard = "C:/Games/Mine";
        const std::array paste{key(Key::V, Modifiers::kCtrl)};
        f.feed(paste);
        CHECK(f.home.fieldText(Part::OpenDir) == "C:/Games/Mine");
        // 칸의 맨 앞을 누르면 캐럿이 처음으로 — 쓴 글자가 앞에 들어간다
        f.feed(clickAt(*f.host, f.h(Part::OpenDir), 2.f));
        f.feed(typed(U"X"));
        CHECK(f.home.fieldText(Part::OpenDir) == "XC:/Games/Mine");
        const std::array copy{key(Key::A, Modifiers::kCtrl), key(Key::C, Modifiers::kCtrl)};
        f.feed(copy);
        CHECK(f.clipboard == "XC:/Games/Mine");
        f.feed(clickAt(*f.host, f.h(Part::Open)));
        const auto open = f.home.takeIntent();
        REQUIRE(open);
        CHECK(open->kind == StudioHomeIntent::Kind::OpenFolder);
        CHECK(open->dir == std::filesystem::path("XC:/Games/Mine"));
        // 단추: 기존 UI 로 · 종료
        f.feed(clickAt(*f.host, f.h(Part::Switch)));
        const auto sw = f.home.takeIntent();
        REQUIRE(sw);
        CHECK(sw->kind == StudioHomeIntent::Kind::SwitchToClassic);
        f.feed(clickAt(*f.host, f.h(Part::Quit)));
        const auto quit = f.home.takeIntent();
        REQUIRE(quit);
        CHECK(quit->kind == StudioHomeIntent::Kind::Quit);
        // 아주 좁은 화면에서도 배치가 실패하지 않는다
        REQUIRE(f.host->frame({40, 300}));
    }

    TEST_CASE("gui studio home: forget from the list, recovery badge, example copy, density toggle") {
        StudioHomeModel m = model(3);
        m.recovery = {"", "2026-11-20T08:00:00Z", ""};
        uihost::ApproxTextMeasure text;
        auto host = uihost::UiHost::create(text);
        REQUIRE(host);
        StudioHomeScreen home;
        home.setModel(m);
        REQUIRE((*host)->setScreen(&home));
        const auto d = (*host)->frame({1280, 720});
        REQUIRE(d);
        REQUIRE(*d != nullptr);
        // 둘째 행에 "복구 가능 (2026-11-20 08:00 (UTC))" — ImGui 홈의 배지와 같은 뜻 · 같은 시각 표시
        bool badge = false;
        for (const gui::RenderCommand& c : (*d)->commands()) {
            if (const auto* t = std::get_if<gui::DrawText>(&c); t != nullptr && t->utf8.starts_with("복구 가능")) {
                badge = t->utf8.find(displayTime("2026-11-20T08:00:00Z")) != std::string::npos;
            }
        }
        CHECK(badge);

        const auto feed = [&](const auto& events) {
            (*host)->feed(std::span<const PlatformEvent>(events.data(), events.size()));
            REQUIRE((*host)->frame({1280, 720}));
        };
        // 목록에서 빼기 — 단추 (고른 것) · Delete (목록에 포커스)
        feed(clickAt(**host, *home.handleOf(Part::Forget)));
        auto forget = home.takeIntent();
        REQUIRE(forget);
        CHECK(forget->kind == StudioHomeIntent::Kind::ForgetRecent);
        CHECK(forget->dir == std::filesystem::path("D:/p/0"));
        const std::array down{PlatformEvent{MouseMove{{0, 0}, {}}}};
        feed(down);
        const gui::LayoutRect lb = boxOf(**host, *home.handleOf(Part::List));
        feed(clickAt(**host, *home.handleOf(Part::List), 20.f, static_cast<f32>(StudioHomeScreen::kRowHeight * 2.5)));
        (void)home.takeIntent(); // 클릭은 연다 — 버린다
        const std::array del{key(Key::Delete)};
        feed(del);
        forget = home.takeIntent();
        REQUIRE(forget);
        CHECK(forget->dir == std::filesystem::path("D:/p/2"));
        CHECK(lb.height > 0.0);

        // 예제: 넘기면 복사할 곳 제안이 그 예제 이름을 따른다 — 복사해서 열기
        const std::string first = home.fieldText(Part::ExampleDir);
        CHECK(first.ends_with("RabbitGuard"));
        feed(clickAt(**host, *home.handleOf(Part::ExampleNext)));
        CHECK(home.exampleIndex() == 1);
        CHECK(home.fieldText(Part::ExampleDir).ends_with("Orbs"));
        feed(clickAt(**host, *home.handleOf(Part::CopyExample)));
        const auto copy = home.takeIntent();
        REQUIRE(copy);
        CHECK(copy->kind == StudioHomeIntent::Kind::CopyExample);
        CHECK(copy->dir == std::filesystem::path("D:/samples/orbs"));
        CHECK(pathText8(copy->dest).ends_with("Orbs"));

        // 간격 — 지금의 반대를 요청한다 (적용은 Application — ImGui 홈의 Density 와 같은 길)
        feed(clickAt(**host, *home.handleOf(Part::Density)));
        const auto dens = home.takeIntent();
        REQUIRE(dens);
        CHECK(dens->kind == StudioHomeIntent::Kind::Density);
        CHECK(dens->compact);
        home.setCompact(true);
        REQUIRE((*host)->frame({1280, 720}));
        CHECK(home.rowHeight() == doctest::Approx(StudioHomeScreen::kCompactRowHeight));
        REQUIRE((*host)->setScreen(nullptr));
    }

    TEST_CASE("gui bridge: display list becomes ImDrawList commands with clips, ImGui font measures text") {
        HeadlessWindow window(WindowDesc{.width = 1280, .height = 720});
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
        StudioHomeScreen home;
        home.setModel(model(2));
        REQUIRE((*host)->setScreen(&home));
        const auto d = (*host)->frame({1280, 720});
        REQUIRE(d);
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const int before = dl->VtxBuffer.Size;
        const GuiBridgeStats st = drawDisplayList(**d, *dl);
        CHECK(st.rects >= 10); // 배경 · 칸 5 (+ 밑줄) · 목록 · 행 2 · 단추
        CHECK(st.texts >= 12); // 제목 · 판 · 제목들 · 칸 5 · 행 2 × 2 · 단추
        CHECK(st.clips >= 1);
        CHECK(dl->VtxBuffer.Size > before);
        CHECK(dl->_ClipRectStack.Size == 1); // clip 짝이 맞는다 (배경 목록의 기본 clip 하나만 남는다)
        // 배율 (UI-R1-09): 같은 목록을 배율 2 로 그리면 꼭짓점이 두 배 자리 (UI 단위 → 논리)
        const auto maxX = [&](int from) {
            float m = 0.f;
            for (int i = from; i < dl->VtxBuffer.Size; ++i) {
                m = std::max(m, dl->VtxBuffer[i].pos.x);
            }
            return m;
        };
        const float x1 = maxX(before);
        const int mid = dl->VtxBuffer.Size;
        (void)drawDisplayList(**d, *dl, nullptr, 0.f, 0.f, 2.f);
        CHECK(maxX(mid) == doctest::Approx(x1 * 2.f).epsilon(0.02));
        CHECK(dl->_ClipRectStack.Size == 1);
        (void)layer.endFrame(CursorShape::Arrow);
        layer.acknowledgeTextures();
        REQUIRE((*host)->setScreen(nullptr)); // 화면을 먼저 놓는다 (호스트보다 화면이 먼저 사라지지 않게)
    }

    TEST_CASE("app (--ui graphicsui, 헤드리스): GraphicsUI 홈에서 새 프로젝트 — 같은 명령 · 결과 줄 · IME 켜고 끄기") {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / std::format("sbx_gui_home_{}", std::random_device{}());
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        editor::services::ProjectService projects(catalog, SemVer{0, 8, 0, {}, {}}, SBX_CONTENT_DIR,
                                                  root / "studio.json", SBX_ASSETS_DIR, SBX_SAMPLES_DIR);
        HeadlessWindow window(WindowDesc{.width = 1280, .height = 720});
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.fixedDt = 1.0 / 60.0;
        cfg.ui = &imgui;
        cfg.projects = &projects;
        cfg.engineVersion = "0.8.0";
        cfg.graphicsUiHome = true;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        REQUIRE(app.frame()); // Boot → MainMenu
        REQUIRE(app.frame()); // 홈을 처음 그린다
        REQUIRE(app.graphicsUiHome() != nullptr);
        CHECK(app.graphicsUiHomeActive());
        CHECK(window.textInputActive()); // 첫 포커스 = 검색 칸 → 창의 글자 입력이 켜졌다

        const uihost::UiHost& host = *app.graphicsUiHost();
        const StudioHomeScreen& home = *app.graphicsUiHome();
        const auto inject = [&](const auto& events) {
            for (const PlatformEvent& e : events) {
                window.inject(e);
            }
            REQUIRE(app.frame());
        };
        // 이름 · 폴더를 쓰고 만들기 — gameId 는 제안 (me.gui_made)
        inject(clickAt(host, *home.handleOf(Part::Name)));
        inject(typed(U"Gui Made"));
        CHECK(home.fieldText(Part::GameId) == "me.gui_made");
        inject(clickAt(host, *home.handleOf(Part::Dir)));
        const std::array selectAll{key(Key::A, Modifiers::kCtrl)};
        inject(selectAll);
        const std::u8string dir = (root / "GuiMade").generic_u8string();
        std::u32string dir32;
        for (const char8_t c : dir) {
            dir32.push_back(static_cast<char32_t>(c)); // 임시 폴더 경로 — ASCII 라고 본다
        }
        inject(typed(dir32));
        CHECK(home.fieldText(Part::Dir) == std::string(dir.begin(), dir.end()));
        inject(clickAt(host, *home.handleOf(Part::Create)));
        REQUIRE(app.frame());
        // ImGui 홈과 같은 명령으로 만들었다 — 최근 목록 · 폴더. 이 실행은 세션을 열 길이 없어 홈에 남는다
        CHECK(fs::exists(root / "GuiMade" / "sandbox.project.json"));
        REQUIRE_FALSE(projects.recent().empty());
        CHECK(projects.recent().front().gameId == "me.gui_made");
        CHECK(app.state() == AppState::MainMenu);

        // 나쁜 gameId 는 명령이 꺼져 있다 — 만들지 않는다
        inject(clickAt(host, *home.handleOf(Part::Dir)));
        inject(selectAll);
        const std::u8string dir2 = (root / "GuiMade2").generic_u8string();
        std::u32string dir2_32;
        for (const char8_t c : dir2) {
            dir2_32.push_back(static_cast<char32_t>(c));
        }
        inject(typed(dir2_32));
        inject(clickAt(host, *home.handleOf(Part::GameId)));
        inject(selectAll);
        inject(typed(U"Bad Id"));
        inject(clickAt(host, *home.handleOf(Part::Create)));
        REQUIRE(app.frame());
        CHECK_FALSE(fs::exists(root / "GuiMade2" / "sandbox.project.json"));

        // 예제 복사 — ImGui 홈과 같은 처리 (ProjectService::copyExample · 최근 목록)
        if (!projects.examples().empty()) {
            inject(clickAt(host, *home.handleOf(Part::ExampleDir)));
            inject(selectAll);
            const std::u8string ex = (root / "Copied").generic_u8string();
            std::u32string ex32;
            for (const char8_t c : ex) {
                ex32.push_back(static_cast<char32_t>(c));
            }
            inject(typed(ex32));
            inject(clickAt(host, *home.handleOf(Part::CopyExample)));
            REQUIRE(app.frame());
            CHECK(fs::exists(root / "Copied" / "sandbox.project.json"));
        }
        // 목록에서 빼기 — 첫 행 (가장 최근) 을 고르고 단추
        const usize before = projects.recent().size();
        REQUIRE(before > 0);
        inject(clickAt(host, *home.handleOf(Part::Forget)));
        REQUIRE(app.frame());
        CHECK(projects.recent().size() == before - 1);

        // 기존 UI 로 — 우리가 켠 글자 입력을 끈다, 트리를 비운다
        inject(clickAt(host, *home.handleOf(Part::Switch)));
        REQUIRE(app.frame());
        CHECK_FALSE(app.graphicsUiHomeActive());
        CHECK_FALSE(window.textInputActive());
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST_CASE("app (--ui graphicsui, DPI 1.5): GraphicsUI 홈이 ImGui 와 같은 배율 — 창 좌표 클릭이 UI 단위 단추에 닿는다") {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / std::format("sbx_gui_scale_{}", std::random_device{}());
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        editor::services::ProjectService projects(catalog, SemVer{0, 8, 0, {}, {}}, SBX_CONTENT_DIR,
                                                  root / "studio.json", SBX_ASSETS_DIR, SBX_SAMPLES_DIR);
        HeadlessWindow window(WindowDesc{.width = 1600, .height = 1000}, 1.5f);
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.fixedDt = 1.0 / 60.0;
        cfg.ui = &imgui;
        cfg.projects = &projects;
        cfg.graphicsUiHome = true;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        REQUIRE(app.graphicsUiHost() != nullptr);
        const uihost::UiHost& host = *app.graphicsUiHost();
        CHECK(host.scale() == doctest::Approx(ImGui::GetStyle().FontScaleDpi));
        CHECK(host.scale() == doctest::Approx(1.5f));
        CHECK(host.viewport().width == doctest::Approx(1600.0 / 1.5));
        // 종료 단추의 가운데 (UI 단위) × 배율 = 창 좌표
        const gui::LayoutRect q = boxOf(host, *app.graphicsUiHome()->handleOf(Part::Quit));
        const Vec2 at{static_cast<f32>((q.x + q.width / 2) * 1.5), static_cast<f32>((q.y + q.height / 2) * 1.5)};
        CHECK(host.hitsUi(at.x, at.y));
        window.inject(MouseMove{at, {0, 0}});
        window.inject(MouseButtonDown{MouseButton::Left, at});
        window.inject(MouseButtonUp{MouseButton::Left, at});
        (void)app.frame(); // 종료가 이 프레임에 적용된다 (false = 끝)
        CHECK(app.state() == AppState::Shutdown);
        std::error_code ec;
        fs::remove_all(root, ec);
    }
}
