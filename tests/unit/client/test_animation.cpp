// 2D 애니메이션 클립 · 상태 기계 (v0.12 RT-06-14, ADR-0157): animations.json 해석 (클립 · 애니메이터 · 거절은 그 항목만
// · 뒤 팩이 이긴다 · 다른 팩의 클립), 프레임 = 렌더 틱 × fps / 30 (반복은 감고 · 아니면 마지막에 멈춤 · 끝), 전이
// (moving · still · end · "*" · 정의 차례 첫째 · 프레임마다 한 번), 속도로 좌우 뒤집기 (멈추면 그대로), 같은 입력 =
// 같은 결과.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "apps/client/NetworkSession.hpp"
#include "apps/client/presentation/Animation.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/renderer/RenderWorld.hpp"

using namespace sbx;
using namespace sbx::client;

namespace {

constexpr std::string_view kHero = R"({
  "clips": {
    "hero.idle": { "material": "hero/sheet", "grid": [4, 2], "frames": [[0, 0], [1, 0]], "fps": 2 },
    "hero.walk": { "material": "hero/sheet", "grid": [4, 2], "frames": [[0, 1], [1, 1], [2, 1], [3, 1]], "fps": 10 },
    "hero.hit":  { "material": "hero/sheet", "grid": [4, 2], "frames": [[2, 0], [3, 0]], "fps": 10, "loop": false },
    "bad.grid":  { "material": "x", "grid": [0, 1], "frames": [[0, 0]] },
    "bad.cell":  { "material": "x", "grid": [2, 2], "frames": [[2, 0]] },
    "bad.fps":   { "material": "x", "frames": [[0, 0]], "fps": 120 }
  },
  "animators": {
    "hero": {
      "initial": "idle",
      "states": { "idle": "hero.idle", "walk": "hero.walk", "hit": "hero.hit" },
      "transitions": [
        { "from": "idle", "to": "walk", "when": "moving" },
        { "from": "walk", "to": "idle", "when": "still" },
        { "from": "hit", "to": "idle", "when": "end" }
      ],
      "flipWithVelocity": true
    },
    "broken": { "states": { "a": "hero.idle" }, "transitions": [{ "to": "nope" }] }
  }
})";

f64 uiNumberOr(const NetworkSession& ns, std::string_view key) {
    return ns.uiData() != nullptr ? ns.uiData()->var(key).number : 0.0;
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE("animation (v0.12 RT-06-14): animations.json — 클립 · 애니메이터, 틀린 항목은 그것만 빼고 경고") {
        AnimationLibrary lib;
        const std::vector<std::string> w = lib.load(kHero, "hero/presentation/animations.json");
        CHECK(w.size() == 4); // bad.grid · bad.cell · bad.fps · broken
        CHECK(lib.clipCount() == 3);
        CHECK(lib.animatorCount() == 1);
        const auto hero = lib.findAnimator("hero");
        REQUIRE(hero.has_value());
        const Animator* an = lib.animator(*hero);
        REQUIRE(an != nullptr);
        CHECK(an->clips.size() == 3);
        CHECK(an->states[an->initial] == "idle");
        CHECK(lib.unresolved().empty());

        // 다른 팩의 클립을 쓰는 애니메이터 — 그 팩이 오기 전에는 못 푼다
        AnimationLibrary two;
        (void)two.load(R"({"animators": {"slime": {"states": {"jump": "slime.jump"}}}})", "a");
        CHECK(two.unresolved().size() == 1);
        (void)two.load(R"({"clips": {"slime.jump": {"material": "s", "frames": [[0, 0]]}}})", "b");
        CHECK(two.unresolved().empty());
        CHECK(two.animator(*two.findAnimator("slime"))->clips.size() == 1);
        // 같은 이름은 뒤가 이긴다
        (void)two.load(R"({"clips": {"slime.jump": {"material": "s2", "frames": [[0, 0]]}}})", "c");
        CHECK(two.clip(0)->material == "s2");
    }

    TEST_CASE("animation (v0.12 RT-06-14): 프레임 = 렌더 틱 × fps / 30 — 반복 · 멈춤 · 끝") {
        AnimationClip loop;
        loop.frames = {{0, 0}, {1, 0}, {2, 0}};
        loop.fps = 10; // 3 틱마다
        CHECK(clipFrameAt(loop, 0) == 0);
        CHECK(clipFrameAt(loop, 2.9) == 0);
        CHECK(clipFrameAt(loop, 3) == 1);
        CHECK(clipFrameAt(loop, 9) == 0); // 감는다
        CHECK_FALSE(clipEnded(loop, 1000));
        AnimationClip once = loop;
        once.loop = false;
        CHECK(clipFrameAt(once, 100) == 2); // 마지막에 멈춘다
        CHECK_FALSE(clipEnded(once, 8.9));
        CHECK(clipEnded(once, 9));
        CHECK(clipFrameAt(once, -5) == 0);
    }

    TEST_CASE(
        "animation (v0.12 RT-06-14): 전이 — moving · still · end, 프레임마다 한 번, 뒤집기, 같은 입력 = 같은 결과") {
        AnimationLibrary lib;
        (void)lib.load(kHero, "t");
        AnimationState s{*lib.findAnimator("hero"), 999, 0, false}; // 처음 = initial
        AnimationFrame f = stepAnimation(lib, s, 10, 0.f, 0.f);
        CHECK(lib.animator(s.animator)->states[s.state] == "idle"); // states 는 이름 차례 (hit · idle · walk)
        CHECK(s.start == 10);
        REQUIRE(f.clip != nullptr);
        CHECK(f.clip->material == "hero/sheet");
        CHECK(f.sub[0] == 0.f);
        CHECK(f.sub[3] == doctest::Approx(0.5f));
        f = stepAnimation(lib, s, 30, 0.f, 0.f); // idle 2 fps — 20 틱 지나 1 칸
        CHECK(f.sub[0] == doctest::Approx(0.25f));

        f = stepAnimation(lib, s, 31, 3.f, -3.f); // 움직이면 walk (그 틱부터), 왼쪽 → 뒤집기
        CHECK(lib.animator(s.animator)->states[s.state] == "walk");
        CHECK(s.start == 31);
        CHECK(f.flipX);
        CHECK(f.sub[1] == doctest::Approx(0.5f)); // 둘째 줄
        f = stepAnimation(lib, s, 37, 3.f, 0.f);  // 6 틱 = 둘째 다음 칸 (10 fps — 3 틱마다), 좌우 0 이면 뒤집기 그대로
        CHECK(f.sub[0] == doctest::Approx(0.5f));
        CHECK(f.flipX);
        f = stepAnimation(lib, s, 38, 0.f, 0.f); // 멈추면 idle
        CHECK(lib.animator(s.animator)->states[s.state] == "idle");

        // 스크립트가 hit 로 (전이 없이) → 끝나면 idle
        s.state = *lib.animator(s.animator)->stateOf("hit");
        s.start = 40;
        f = stepAnimation(lib, s, 42, 0.f, 0.f);
        CHECK(lib.animator(s.animator)->states[s.state] == "hit");
        CHECK(f.sub[0] == doctest::Approx(0.5f));
        f = stepAnimation(lib, s, 46, 0.f, 0.f); // 2 프레임 × 3 틱 = 6 틱 → 끝
        CHECK(lib.animator(s.animator)->states[s.state] == "idle");

        // 같은 입력 = 같은 결과 (클라이언트마다 같은 프레임)
        const auto run = [&] {
            AnimationState a{*lib.findAnimator("hero"), 999, 0, false};
            std::vector<f32> trace;
            for (int t = 0; t < 120; ++t) {
                const f32 v = (t / 20) % 2 == 0 ? 0.f : 2.f;
                const AnimationFrame fr = stepAnimation(lib, a, t, v, v);
                trace.push_back(fr.sub[0] + 10.f * static_cast<f32>(a.state));
            }
            return trace;
        };
        CHECK(run() == run());

        // 없는 애니메이터 — 그리지 않는다
        AnimationState none{99, 0, 0, false};
        CHECK(stepAnimation(lib, none, 0, 0.f, 0.f).clip == nullptr);
    }
}

// 로컬 세션 (v0.12 RT-06-14): 생태계 팩 사본의 토끼에 render.animator — 추출이 시트의 칸 (sub) 을 고르고, client
// 스크립트의 ctx.playAnimation 이 상태를 바꾼다 (hit — 끝나면 idle 로). 늑대 · 풀 (애니메이터 없음) 은 그림 전체
// 그대로.
TEST_SUITE("client") {
    TEST_CASE(
        "animation (local server, v0.12 RT-06-14): render.animator 개체의 칸 · ctx.playAnimation → hit → 끝나면 idle") {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "animation" / "session";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
        fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", root / "ecosystem", fs::copy_options::recursive);
        const auto put = [&](const std::string& rel, std::string_view text) {
            fs::create_directories((root / rel).parent_path());
            std::ofstream(root / rel, std::ios::binary) << text;
        };
        put("ecosystem/presentation/animations.json", R"({
  "clips": {
    "eco.rabbit.idle": { "material": "eco/rabbit", "grid": [4, 2], "frames": [[0, 0]] },
    "eco.rabbit.run":  { "material": "eco/rabbit", "grid": [4, 2], "frames": [[0, 1], [1, 1], [2, 1], [3, 1]], "fps": 10 },
    "eco.rabbit.hit":  { "material": "eco/rabbit", "grid": [4, 2], "frames": [[3, 0]], "fps": 2, "loop": false }
  },
  "animators": {
    "eco.rabbit": {
      "initial": "idle",
      "states": { "idle": "eco.rabbit.idle", "run": "eco.rabbit.run", "hit": "eco.rabbit.hit" },
      "transitions": [
        { "from": "idle", "to": "run", "when": "moving" },
        { "from": "run", "to": "idle", "when": "still" },
        { "from": "hit", "to": "idle", "when": "end" }
      ],
      "flipWithVelocity": true
    }
  }
})");
        {
            nlohmann::json rabbit =
                nlohmann::json::parse(std::ifstream(root / "ecosystem" / "prefabs" / "rabbit.json"));
            rabbit["components"]["render.animator"] = {{"animator", "eco.rabbit"}};
            put("ecosystem/prefabs/rabbit.json", rabbit.dump(2));
        }
        // 틱 30 부터 한 번 — 모든 토끼를 hit 로 (없는 상태 "nope" 는 아무 일 없음)
        put("ecosystem/scripts/anim.ts", R"(import { defineSystem, Velocity } from "sandbox"
export default defineSystem({
  id: "eco.anim",
  domain: "client",
  onUpdate(ctx) {
    if (ctx.tick >= 30 && ctx.uiVar("eco.hit") === 0) {
      for (const e of ctx.query(Velocity)) { ctx.playAnimation(e, "nope"); ctx.playAnimation(e, "hit") }
      ctx.setUi("eco.hit", ctx.tick)
    }
  },
})
)");

        render::MaterialLibrary mats;
        NetworkSessionDesc d;
        d.world = "ecosystem_small";
        d.contentRoot = root;
        d.inlineServer = true;
        auto made = NetworkSession::create(d, mats);
        REQUIRE_MESSAGE(made.has_value(), (made ? std::string() : made.error().describe()));
        NetworkSession& ns = **made;
        const auto frame = [&] {
            ns.update(1.0 / 30.0);
            render::RenderWorld w;
            ns.extract(w);
        };
        const auto count = [&](f32 u0, f32 v0) {
            usize n = 0;
            for (const SnapshotSprite& s : ns.lastSnapshot().sprites) {
                n += s.sub[0] == doctest::Approx(u0) && s.sub[1] == doctest::Approx(v0) &&
                             s.sub[2] - s.sub[0] == doctest::Approx(0.25f)
                         ? 1
                         : 0;
            }
            return n;
        };
        const auto animated = [&] {
            usize n = 0;
            for (const SnapshotSprite& s : ns.lastSnapshot().sprites) {
                n += s.sub[2] - s.sub[0] == doctest::Approx(0.25f) ? 1 : 0;
            }
            return n;
        };
        for (int i = 0; i < 120 && !ns.ready(); ++i) {
            frame();
        }
        REQUIRE(ns.ready());
        frame();
        const usize rabbits = animated();
        CHECK(rabbits > 0);                                // 토끼만 칸 (4 × 2 시트)
        CHECK(ns.lastSnapshot().sprites.size() > rabbits); // 늑대 · 풀은 그림 전체
        CHECK(count(0.75f, 0.f) == 0);                     // 아직 hit 없음

        for (int i = 0; i < 60 && uiNumberOr(ns, "eco.hit") == 0; ++i) {
            frame();
        }
        REQUIRE(uiNumberOr(ns, "eco.hit") >= 30);
        frame();
        CHECK(count(0.75f, 0.f) > 0);  // hit 칸 [3, 0]
        for (int i = 0; i < 40; ++i) { // hit 은 2 fps 한 칸 = 15 틱 — 끝나면 idle (또는 움직이면 run)
            frame();
        }
        CHECK(count(0.75f, 0.f) == 0);
        CHECK(animated() > 0);
    }
}
