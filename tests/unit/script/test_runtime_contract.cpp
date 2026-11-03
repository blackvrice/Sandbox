// Runtime v2 계약 동결 (v0.5 M2-T7 — ADR-0086 6, docs/22-RUNTIME-V2.md 3장): 콜백 표 (script/api/Callbacks) — 예약
// 이름은 검사기 S426 "[계획 vX]", 두지 않는 onPostPhysics, 지금 쓰는 콜백 = 검사기가 받는 콜백, Host 표의 capability 열
// — 지금 함수는 그 도메인의 쓰는 콜백이 허락하는 묶음 안, 지문에 든다.
#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "script/api/Callbacks.hpp"
#include "script/api/HostApi.hpp"
#include "script/lang/Checker.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

constexpr std::string_view kHead = "import { defineSystem } from \"sandbox\"\n";

DiagnosticList check(std::string_view body) {
    DiagnosticList diags;
    const std::vector<SourceFile> files{{"scripts/a.ts", std::string(kHead) + std::string(body)}};
    (void)checkProgram(files, diags);
    return diags;
}

bool has(const DiagnosticList& d, std::string_view code, std::string_view part) {
    return std::any_of(d.items().begin(), d.items().end(), [&](const Diagnostic& x) {
        return x.code == code && x.message.find(part) != std::string::npos;
    });
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("runtime contract: 예약된 콜백 이름은 지금 쓰지 못한다 — [계획 vX], onPostPhysics 는 두지 않는다") {
        struct Case {
            std::string_view src;
            std::string_view part;
        };
        const Case cases[] = {
            {"export default defineSystem({ id: \"a.b\", onPrePhysics(ctx) { } })\n",
             "onPrePhysics 는 server 시스템의 예약된 콜백 이름이다 — [계획 v0.6]"},
            {"export default defineSystem({ id: \"a.b\", domain: \"client\", onRender(ctx) { } })\n",
             "onRender 는 client 시스템의 예약된 콜백 이름이다 — [계획 v0.6]"},
            {"export default defineSystem({ id: \"a.b\", domain: \"client\", onAction(ctx) { } })\n", "[계획 v0.6]"},
            {"export default defineSystem({ id: \"a.b\", domain: \"client\", onLateUpdate(ctx) { } })\n",
             "[계획 v0.6]"},
            {"export default defineSystem({ id: \"a.b\", domain: \"client\", onStart(ctx) { } })\n",
             "onStart 는 client 시스템의 예약된 콜백 이름이다 — [계획 v0.6]"},
            {"export default defineSystem({ id: \"a.b\", domain: \"client\", onEvent(ctx, e) { } })\n", "[계획 v0.6]"},
            {"export default defineSystem({ id: \"a.b\", domain: \"client\", onTick(ctx) { } })\n", "[계획 v0.7]"},
            // 다른 도메인의 예약 이름
            {"export default defineSystem({ id: \"a.b\", onRender(ctx) { } })\n",
             "onRender 는 client 시스템의 예약된 콜백 이름이다 — [계획 v0.6]"},
            {"export default defineSystem({ id: \"a.b\", onPostPhysics(ctx) { } })\n", "onPostPhysics 는 두지 않는다"},
        };
        for (const Case& c : cases) {
            CAPTURE(c.src);
            const DiagnosticList d = check(c.src);
            CAPTURE(d.describeAll());
            CHECK(has(d, "S426", c.part));
        }
        // 지금 쓰는 콜백은 그대로
        CHECK(
            check("export default defineSystem({ id: \"a.b\", onTick(ctx) { }, onStart(ctx) { }, onStop(ctx) { } })\n")
                .ok());
        CHECK(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onUpdate(ctx) { } })\n").ok());
    }

    TEST_CASE(
        "runtime contract: 콜백 표 — 지금 쓰는 것 = 검사기가 받는 것, 이름 · 도메인 겹침 없음, 예약은 버전이 있다") {
        std::set<std::pair<std::string_view, bool>> seen;
        std::set<std::pair<std::string_view, bool>> active;
        for (const api::CallbackInfo& c : api::kCallbacks) {
            CAPTURE(c.name);
            CHECK(seen.insert({c.name, c.client}).second);
            CHECK(c.since.starts_with("v0."));
            CHECK(c.capabilities != 0);
            CHECK_FALSE(c.doc.empty());
            if (c.status == api::CallbackStatus::Active) {
                active.insert({c.name, c.client});
            }
        }
        const std::set<std::pair<std::string_view, bool>> want{
            {"onTick", false}, {"onStart", false}, {"onStop", false}, {"onEvent", false}, {"onUpdate", true}};
        CHECK(active == want); // 검사기 (Checker::declareSystem) 가 콜백으로 받는 다섯
        CHECK(api::findCallback("onRender", true)->status == api::CallbackStatus::Planned);
        CHECK(api::findCallback("onRender", false) == nullptr);
        CHECK(api::isCallbackName("onPrePhysics"));
        CHECK_FALSE(api::isCallbackName("onPostPhysics"));
    }

    TEST_CASE("runtime contract: Host 표의 capability — 지금 함수는 그 도메인의 쓰는 콜백이 허락하는 묶음 안, 이름은 "
              "겹치지 않는다") {
        std::set<std::string_view> names;
        for (u32 i = 0; i < api::kCapabilityCount; ++i) {
            CHECK(names.insert(api::capabilityName(static_cast<api::HostCapability>(i))).second);
        }
        for (const api::HostFunction& f : api::kHostFunctions) {
            CAPTURE(f.id);
            const bool client = f.domain == api::HostDomain::Client;
            bool allowed = false;
            for (const api::CallbackInfo& c : api::kCallbacks) {
                if (c.client == client && c.status == api::CallbackStatus::Active &&
                    (c.capabilities & api::capBit(f.capability)) != 0) {
                    allowed = true;
                }
            }
            CHECK(allowed);
            // 지문에 든다
            CHECK(api::canonicalSignature(f).find(api::capabilityName(f.capability)) != std::string::npos);
        }
    }
}
