// Phase 12A: 에디터 툴이 로컬 서버(LocalServerHost, Inline)까지 — 배치 · 이동(EditPreview) · 지형 · 지우기가 서버
// 월드를 바꾸고 복제본으로 돌아온다. Application 의 숫자 키 · 클릭 · Delete 도 같은 길 (docs/10-EDITOR.md 10장,
// ADR-0028).
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "core/world/WorldGrid.hpp"
#include "editor/Editor.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

std::unique_ptr<NetworkSession> localSession(render::MaterialLibrary& mats) {
    NetworkSessionDesc d;
    d.world = "ecosystem_small";
    d.contentRoot = SBX_CONTENT_DIR;
    d.inlineServer = true;
    auto ns = NetworkSession::create(d, mats);
    REQUIRE_MESSAGE(ns.has_value(), (ns ? std::string() : ns.error().describe()));
    return std::move(*ns);
}

struct EditRig {
    render::MaterialLibrary mats;
    std::unique_ptr<NetworkSession> ns = localSession(mats);
    editor::Editor ed;
    f64 now = 0;

    editor::EditorInput input(Vec2 cursor) {
        editor::EditorInput in;
        in.now = (now += kDt);
        in.cursor = cursor;
        return in;
    }
    // 한 프레임: 세션 진행 → 그릴 거리(고르기 · preview 가 쓰는 스냅숏) → 에디터
    void frame(editor::EditorInput in) {
        ns->update(kDt);
        render::RenderWorld w;
        ns->extract(w);
        ed.update(in, *ns);
    }
    void idle() { frame(input({1e4f, 1e4f})); }
    template <class Pred>
    void until(Pred pred, int frames = 120) {
        for (int i = 0; i < frames && !pred(); ++i) {
            idle();
        }
        REQUIRE(pred());
    }
    [[nodiscard]] std::optional<Vec2> drawnAt(NetEntityId id) const {
        for (const SnapshotSprite& s : ns->lastSnapshot().sprites) {
            if (s.id == id) {
                return s.position;
            }
        }
        return std::nullopt;
    }
};

ActionMap defaults() {
    auto m = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
    REQUIRE(m.has_value());
    return std::move(*m);
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE("editor on a local server: place · move with preview · paint · erase come back through replication") {
        EditRig r;
        r.until([&] { return r.ns->ready(); });
        r.ns->togglePause(); // 움직이지 않게 (일시정지 편집 — 명령은 그대로 적용된다)
        r.until([&] { return r.ns->paused(); });
        REQUIRE(r.ns->editorHost() == r.ns.get());
        REQUIRE(r.ns->content() != nullptr);

        // 배치
        r.ed.setTool(editor::Tool::Place);
        r.ed.settings().prefab = "eco.rabbit";
        const Vec2 at{2.5f, 3.5f};
        auto in = r.input(at);
        in.primaryPressed = true;
        r.frame(in);
        r.until([&] { return !r.ed.lastCreated().empty(); });
        const NetEntityId id = r.ed.lastCreated().front();
        r.until([&] { return r.drawnAt(id).has_value(); });
        CHECK(r.drawnAt(id)->x == doctest::Approx(at.x));
        CHECK(r.drawnAt(id)->y == doctest::Approx(at.y));

        // 이동: 끄는 동안 preview 로 옮겨 그리고, 놓으면 서버가 옮긴다 → preview 를 걷어도 같은 자리
        r.ed.setTool(editor::Tool::Move);
        in = r.input(at);
        in.primaryPressed = in.primaryDown = true;
        r.frame(in);
        CHECK(r.ns->selection().size() == 1);
        in = r.input(at + Vec2{3, 0});
        in.primaryDown = true;
        r.frame(in);
        r.frame(r.input(at + Vec2{3, 0})); // 놓기
        REQUIRE(r.ed.preview().size() == 1);
        CHECK(r.drawnAt(id)->x == doctest::Approx(at.x + 3)); // 확정 전에도 옮긴 자리 (preview)
        r.until([&] { return r.ed.preview().empty(); });
        r.idle();
        CHECK(r.drawnAt(id)->x == doctest::Approx(at.x + 3)); // 서버 값
        CHECK(r.ed.stats().rejected == 0);

        // 지형: 바탕이 아닌 머티리얼로 한 칸
        r.ed.setTool(editor::Tool::TerrainBrush);
        r.ed.settings().material = "core.water";
        r.ed.settings().brushRadius = 0;
        const Vec2i tile{-5, -6};
        in = r.input({-4.5f, -5.5f});
        in.primaryPressed = true;
        r.frame(in);
        const auto water = *r.ns->content()->findMaterial("core.water");
        r.until([&] { return r.ns->grid()->materialAt(tile) == water; });

        // 지우기 (Delete 키 = 선택 전부)
        in = r.input({1e4f, 1e4f});
        in.deleteSelection = true;
        r.frame(in);
        r.until([&] { return !r.drawnAt(id).has_value(); });
        CHECK(r.ns->selection().empty());
        CHECK(r.ed.stats().sent == 4);
        CHECK(r.ed.stats().accepted == 4);
    }

    TEST_CASE("app: number keys pick the tool, a left click places, the title shows the tool") {
        render::MaterialLibrary mats;
        auto ns = localSession(mats);
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.titleEveryFrames = 1;
        Application app(window, defaults(), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        CHECK(window.title().find("툴 선택") != std::string::npos);
        window.inject(KeyDown{Key::Digit3, 0, {}, false});
        window.inject(KeyUp{Key::Digit3, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.editor().tool() == editor::Tool::Place);
        app.editor().settings().prefab = "eco.rabbit";
        REQUIRE(app.frame());
        CHECK(window.title().find("툴 배치 eco.rabbit") != std::string::npos);

        const usize before = ns->clientWorld()->entityCount();
        window.inject(MouseMove{{800, 450}, {0, 0}});
        window.inject(MouseButtonDown{MouseButton::Left, {800, 450}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {800, 450}});
        for (int i = 0; i < 60 && app.editor().lastCreated().empty(); ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.editor().lastCreated().size() == 1);
        CHECK(app.editor().stats().sent == 1);
        CHECK(ns->clientWorld()->entityCount() >= before); // 생태계가 움직이니 수는 정확히 비교하지 않는다
        CHECK(ns->clientWorld()->find(app.editor().lastCreated().front()) != ecs::kNullEntity);

        // 4 = 지형 툴, 1 = 다시 선택
        window.inject(KeyDown{Key::Digit4, 0, {}, false});
        window.inject(KeyUp{Key::Digit4, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.editor().tool() == editor::Tool::TerrainBrush);
        CHECK(app.renderWorld().selection.lines().size() >= 3); // 브러시 원
        window.inject(KeyDown{Key::Digit1, 0, {}, false});
        window.inject(KeyUp{Key::Digit1, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.editor().tool() == editor::Tool::Select);
    }
}
