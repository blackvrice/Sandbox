// Phase 12D 클라이언트 (docs/10-EDITOR.md 9장, ADR-0031): 싱글플레이(로컬 서버)에서 저장 · 목록 · 불러오기 → 새 월드 ·
// 에디터 기록 비우기, 기록 → 재생 중 일시정지가 재생을 멈춘다, "월드" 패널 그리기 · 앱의 Ctrl+S, --saves.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <random>

#include <imgui.h>

#include "apps/client/Application.hpp"
#include "apps/client/ClientOptions.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "apps/client/ui/WorldPanel.hpp"
#include "editor/Editor.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
namespace fs = std::filesystem;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_{}_{:x}", tag, std::random_device{}());
        fs::remove_all(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::unique_ptr<NetworkSession> local(render::MaterialLibrary& mats, const fs::path& saves) {
    NetworkSessionDesc d;
    d.world = "ecosystem_small";
    d.contentRoot = SBX_CONTENT_DIR;
    d.inlineServer = true;
    d.savesRoot = saves;
    auto ns = NetworkSession::create(d, mats);
    REQUIRE(ns.has_value());
    return std::move(*ns);
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE("app: Ctrl+S saves under the world panel's name") {
        TempDir dir("client_ctrl_s");
        render::MaterialLibrary mats;
        auto ns = local(mats, dir.path);
        platform::HeadlessWindow window;
        platform::NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        auto map = platform::ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        window.inject(platform::KeyDown{platform::Key::LeftCtrl, 0, {}, false});
        window.inject(platform::KeyDown{platform::Key::S, 0, {}, false});
        window.inject(platform::KeyUp{platform::Key::S, 0, {}});
        REQUIRE(app.frame());
        window.inject(platform::KeyUp{platform::Key::LeftCtrl, 0, {}});
        for (int i = 0; i < 60 && !fs::is_regular_file(dir.path / "quicksave" / "world.json"); ++i) {
            REQUIRE(app.frame());
        }
        CHECK(fs::is_regular_file(dir.path / "quicksave" / "world.json"));
        for (int i = 0; i < 10; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(ns->worldOps());
        CHECK(ns->worldOps()->message.find("저장 'quicksave'") != std::string::npos);
        REQUIRE(ns->worldOps()->saved.size() == 1);
    }

    TEST_CASE("world (local server): save · list · load — a new world, the editor's history is dropped") {
        TempDir dir("client_ops");
        render::MaterialLibrary mats;
        auto ns = local(mats, dir.path);
        editor::Editor ed;
        f64 now = 0;
        const auto frame = [&] {
            ns->update(kDt);
            render::RenderWorld w;
            ns->extract(w);
            editor::EditorInput in;
            in.now = (now += kDt);
            in.cursor = {1e4f, 1e4f};
            ed.update(in, *ns);
        };
        for (int i = 0; i < 120 && !ns->ready(); ++i) {
            frame();
        }
        REQUIRE(ns->ready());
        for (int i = 0; i < 5; ++i) {
            frame();
        }
        auto ops = ns->worldOps();
        REQUIRE(ops);
        CHECK(ops->canChange); // 싱글플레이는 owner
        CHECK(ops->listed);    // 접속하면 목록을 한 번
        CHECK(ops->saved.empty());
        CHECK(ops->generation == 0);
        CHECK(ns->worldGeneration() == 0);

        // 콘텐츠 하나를 바꾸고 (기록 한 칸) 저장
        ecs::Json rule = ns->content()->findRule("eco.rabbit_grazes")->raw;
        rule["effects"][0]["value"] = 33;
        REQUIRE(ed.editContent(*ns, content::ContentKind::Rule, "eco.rabbit_grazes", rule));
        for (int i = 0; i < 30 && !ed.canUndo(); ++i) {
            frame();
        }
        REQUIRE(ed.canUndo());
        ns->worldOp(WorldOp::Save, "a");
        for (int i = 0; i < 30 && ns->worldOps()->saved.empty(); ++i) {
            frame();
        }
        ops = ns->worldOps();
        REQUIRE(ops->saved.size() == 1); // 받아들여진 결과 뒤 목록을 다시
        CHECK(ops->saved[0].name == "a");
        CHECK_FALSE(ops->saved[0].replay);
        CHECK(ops->message.find("저장 'a'") != std::string::npos);
        CHECK(fs::is_directory(dir.path / "a" / "overlay"));

        // 되돌려 오버레이를 비운 뒤 불러오면 세이브의 오버레이가 돌아온다
        ed.undo(*ns);
        for (int i = 0; i < 30 && !ns->content()->overlay().empty(); ++i) {
            frame();
        }
        REQUIRE(ns->content()->overlay().empty());
        ns->worldOp(WorldOp::Load, "a");
        for (int i = 0; i < 30 && ns->worldGeneration() == 0; ++i) {
            frame();
        }
        REQUIRE(ns->worldGeneration() == 1);
        for (int i = 0; i < 30 && (!ns->ready() || ns->content()->overlay().empty()); ++i) {
            frame();
        }
        CHECK(ns->ready());
        CHECK(ns->content()->findRule("eco.rabbit_grazes")->effects[0].value == doctest::Approx(33));
        CHECK(ed.undoDepth() == 0); // 옛 월드의 기록은 버렸다
        CHECK(ed.redoDepth() == 0);
        CHECK(ns->worldOps()->generation == 1);
        CHECK(ns->worldOps()->message.find("불러오기 'a'") != std::string::npos);
        CHECK(ns->selection().empty());

        // 없는 세이브 — 거절 사유가 패널 줄에
        ns->worldOp(WorldOp::Load, "zzz");
        for (int i = 0; i < 10; ++i) {
            frame();
        }
        CHECK(ns->worldOps()->message.find("거절") != std::string::npos);
        CHECK(ns->worldGeneration() == 1);
    }

    TEST_CASE("world (local server): record → play, pause stops the playback, the status line shows it") {
        TempDir dir("client_replay");
        render::MaterialLibrary mats;
        auto ns = local(mats, dir.path);
        const auto frame = [&] {
            ns->update(kDt);
            render::RenderWorld w;
            ns->extract(w);
        };
        for (int i = 0; i < 120 && !ns->ready(); ++i) {
            frame();
        }
        REQUIRE(ns->ready());
        ns->worldOp(WorldOp::RecordStart, "r");
        for (int i = 0; i < 40; ++i) {
            frame();
        }
        CHECK(ns->worldOps()->mode == 1);
        CHECK(ns->status().find("기록 중 r") != std::string::npos);
        ns->worldOp(WorldOp::RecordStop, {});
        for (int i = 0; i < 10; ++i) {
            frame();
        }
        REQUIRE(ns->worldOps()->saved.size() == 1);
        CHECK(ns->worldOps()->saved[0].replay);

        ns->worldOp(WorldOp::Play, "r");
        for (int i = 0; i < 45 && !ns->playingReplay(); ++i) {
            frame();
        }
        REQUIRE(ns->playingReplay());
        CHECK(ns->worldGeneration() == 1);
        CHECK(ns->status().find("재생 r") != std::string::npos);
        ns->togglePause(); // 재생을 멈춘다 (월드 일시정지가 아니라)
        for (int i = 0; i < 5; ++i) {
            frame();
        }
        const u64 at = ns->localServer()->host().stats().replayCall;
        for (int i = 0; i < 40; ++i) {
            frame();
        }
        CHECK(ns->localServer()->host().stats().replayCall == at);
        CHECK(ns->worldOps()->replayPaused);
        ns->stepOnce(); // 멈춘 동안 한 call
        for (int i = 0; i < 5; ++i) {
            frame();
        }
        CHECK(ns->localServer()->host().stats().replayCall == at + 1);
        ns->togglePause();
        for (int i = 0; i < 200 && ns->localServer()->host().stats().mode == 2; ++i) {
            frame();
        }
        CHECK(ns->localServer()->host().stats().mode == 0);
        for (int i = 0; i < 5; ++i) {
            frame();
        }
        CHECK(ns->worldOps()->message.find("일치") != std::string::npos);
    }

    TEST_CASE("world panel: draws every state, the mode line, and --saves is single-player only") {
        platform::HeadlessWindow window;
        ImGuiLayer layer(window);
        const auto uiFrame = [&](auto&& ui) {
            platform::PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            ui();
            (void)layer.endFrame(platform::CursorShape::Arrow);
            layer.acknowledgeTextures();
        };
        WorldPanelState st;
        WorldOpsInfo info;
        std::optional<WorldPanelRequest> req;
        uiFrame([&] { req = drawWorldPanel(info, st); }); // 처음엔 접혀 있다
        CHECK_FALSE(req);
        uiFrame([&] {
            ImGui::SetWindowCollapsed("###world", false); // 펼친다 (사용자가 제목 줄을 누른 것처럼)
            req = drawWorldPanel(info, st);               // 목록 전 · 권한 없음
        });
        CHECK_FALSE(req);
        info.canChange = true;
        info.listed = true;
        info.saved = {{"a", false, 300, 50}, {"r", true, 0, 50}};
        st.selected = "r";
        uiFrame([&] { req = drawWorldPanel(info, st); });
        info.mode = 1;
        info.modeName = "rec";
        info.replayCall = 120;
        CHECK(worldModeLine(info) == "기록 중 'rec' — 120 call");
        uiFrame([&] { req = drawWorldPanel(info, st); });
        info.mode = 2;
        info.modeName = "r";
        info.replayCall = 300;
        info.replayCalls = 608;
        info.hashesChecked = 10;
        info.replayPaused = true;
        info.message = "리플레이 재생 'r': 608 call 재생 시작";
        CHECK(worldModeLine(info) == "재생 'r' 300/608 call · 해시 10 일치 · 멈춤");
        uiFrame([&] { req = drawWorldPanel(info, st); });
        info.mismatch = true;
        info.replayPaused = false;
        info.replaySpeed = 2;
        CHECK(worldModeLine(info) == "재생 'r' 300/608 call · 해시 10 불일치! · ×2");
        info.mode = 0;
        CHECK(worldModeLine(info) == "보통");
        CHECK(st.name == "quicksave");

        const std::array<std::string_view, 4> ok{"--world", "ecosystem_small", "--saves", "my_saves"};
        CHECK(parseClientOptions(ok)->saves == "my_saves");
        const std::array<std::string_view, 4> remote{"--connect", "h:1", "--saves", "x"};
        CHECK_FALSE(parseClientOptions(remote).has_value());
        CHECK(clientUsage().find("--saves") != std::string::npos);
    }
}
