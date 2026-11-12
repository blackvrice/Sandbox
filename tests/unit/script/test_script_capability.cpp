// 호출 토큰 · Capability 실행 검사 (v0.10 RT-06-1 — ScriptHost · ClientScriptHost · script/api/Callbacks, ADR-0137):
// 지금의 콜백은 그 도메인의 모든 Host 함수를 허락한다 (기존 게임이 막히지 않는다 — 표 전수), 계획 콜백 · 표에 없는
// 이름의 호출 토큰은 그 밖의 Host 함수를 HostError 로 (검사기를 거치지 않은 · 고친 바이트코드 대비 — 인터프리터로 부른
// 모듈), 호출이 끝나면 토큰의 권한도 끝, client 쪽도 같다, 어댑터가 콜백 이름을 넘긴다.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/script/ClientScriptHost.hpp"
#include "core/script/ScriptHost.hpp"
#include "foundation/log/Log.hpp"
#include "script/api/Callbacks.hpp"
#include "script/api/HostApi.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Interpreter.hpp"

using namespace sbx;
using namespace sbx::script;
using vm::Trap;
using vm::Value;

namespace {

struct QuietLog {
    QuietLog() {
        log::setSink([](log::Level, std::string_view, std::string_view) {});
    }
    ~QuietLog() { log::setSink(nullptr); }
    QuietLog(const QuietLog&) = delete;
    QuietLog& operator=(const QuietLog&) = delete;
};

constexpr std::string_view kScript = R"(import { i32, ServerContext } from "sandbox"
export function quiet(ctx: ServerContext): i32 {
  ctx.log("hi")
  return ctx.tick
}
export function grow(ctx: ServerContext): void {
  ctx.log("before")
  const e = ctx.spawn("t.wisp", 0, 0)
}
)";

bc::Module compile() {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts", std::string(kScript)};
    std::optional<bc::Module> m = compileScripts(std::span(&f, 1), d);
    REQUIRE_MESSAGE(d.ok(), d.describeAll());
    return std::move(*m);
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE(
        "capability (v0.10 RT-06-1): 지금의 콜백은 그 도메인의 모든 Host 함수를 허락한다 (기존 게임이 막히지 않는다)") {
        usize checked = 0;
        for (const api::HostFunction& fn : api::kHostFunctions) {
            const bool client = fn.domain == api::HostDomain::Client;
            for (const api::CallbackInfo& cb : api::kCallbacks) {
                if (cb.status != api::CallbackStatus::Active || cb.client != client) {
                    continue;
                }
                CAPTURE(fn.id);
                CAPTURE(cb.name);
                CHECK((cb.capabilities & api::capBit(fn.capability)) != 0);
                ++checked;
            }
        }
        CHECK(checked > 50);
        CHECK(api::callbackCapabilities("onTick", false) == api::findCallback("onTick", false)->capabilities);
        CHECK(api::callbackCapabilities("onUpdate", true) != 0);
        CHECK(api::callbackCapabilities("nope", false) == 0);
        CHECK(api::callbackCapabilities("onUpdate", false) == 0); // 다른 도메인의 이름
        // 계획 콜백은 좁다 — onPrePhysics 에 쓰기 · 구조 변경 없음, onRender 에 Ui · 시간 없음
        const u32 pre = api::callbackCapabilities("onPrePhysics", false);
        CHECK((pre & api::capBit(api::HostCapability::EcsWrite)) == 0);
        CHECK((pre & api::capBit(api::HostCapability::Structural)) == 0);
        CHECK((pre & api::capBit(api::HostCapability::Log)) != 0);
        const u32 render = api::callbackCapabilities("onRender", true);
        CHECK((render & api::capBit(api::HostCapability::Ui)) == 0);
    }

    TEST_CASE("capability (v0.10 RT-06-1): 호출 토큰의 콜백 밖 Host 함수는 HostError — 바이트코드로 불러도") {
        QuietLog quiet;
        const bc::Module m = compile();
        ScriptHost host;
        vm::Interpreter v(m, &host);

        // onTick — 그대로
        Value ctx = host.beginCall(7, "scripts/a.ts:quiet", nullptr, "onTick");
        CHECK(host.callback() == "onTick");
        vm::CallResult r = v.call("scripts/a.ts", "quiet", std::vector<Value>{ctx});
        host.endCall(r.ok());
        REQUIRE_MESSAGE(r.ok(), r.message);
        CHECK(r.value.asI32() == 7);

        // onPrePhysics (계획 — EcsRead · PhysicsCommand · Random · Time · Log) 의 토큰: log · tick 은 되고 spawn
        // (Structural) 은 검사기를 거치지 않은 바이트코드로 불러도 HostError — 구현에 닿기 전에
        ctx = host.beginCall(8, "scripts/a.ts:quiet", nullptr, "onPrePhysics");
        r = v.call("scripts/a.ts", "quiet", std::vector<Value>{ctx});
        host.endCall(r.ok());
        CHECK(r.ok());
        ctx = host.beginCall(8, "scripts/a.ts:grow", nullptr, "onPrePhysics");
        r = v.call("scripts/a.ts", "grow", std::vector<Value>{ctx});
        host.endCall(r.ok());
        CHECK(r.trap == Trap::HostError);
        CAPTURE(r.message);
        CHECK(r.message.find("ctx.spawn") != std::string::npos);
        CHECK(r.message.find("onPrePhysics") != std::string::npos);
        CHECK(r.message.find("Structural") != std::string::npos);

        // 표에 없는 콜백 이름 — 아무것도 부르지 못한다
        ctx = host.beginCall(9, "x", nullptr, "onWhatever");
        CHECK(host.capabilities() == 0);
        r = v.call("scripts/a.ts", "quiet", std::vector<Value>{ctx});
        host.endCall(false);
        CHECK(r.trap == Trap::HostError);
        CHECK(r.message.find("ctx.log") != std::string::npos);

        // 호출이 끝나면 토큰의 권한도 끝
        CHECK(host.capabilities() == 0);
        const api::HostFunction* tick = api::findHostFunction("ctx.tick");
        REQUIRE(tick != nullptr);
        const vm::HostArg stale[] = {{api::AbiType::ServerContext, ctx, {}, {}}};
        CHECK_FALSE(host.call(*tick, stale).ok);
    }

    TEST_CASE("capability (v0.10 RT-06-1): client — onUpdate 는 그대로, 계획 onRender 의 토큰은 Ui · 시간을 막는다") {
        QuietLog quiet;
        ClientScriptHost client;
        client.beginFrame({nullptr, nullptr, 5, 1, 0});
        const api::HostFunction* tick = api::findHostFunction("client.tick");
        const api::HostFunction* setUi = api::findHostFunction("client.setUi");
        REQUIRE(tick != nullptr);
        REQUIRE(setUi != nullptr);

        Value ctx = client.beginCall("t.hud");
        const vm::HostArg tickArgs[] = {{api::AbiType::ClientContext, ctx, {}, {}}};
        CHECK(client.call(*tick, tickArgs).ok); // 기본 onUpdate
        client.endCall(true);

        ctx = client.beginCall("t.hud", "onRender");
        const vm::HostArg renderTick[] = {{api::AbiType::ClientContext, ctx, {}, {}}};
        const vm::HostResult denied = client.call(*tick, renderTick);
        CHECK_FALSE(denied.ok);
        CHECK(denied.error.find("onRender") != std::string::npos);
        CHECK(denied.error.find("Time") != std::string::npos);
        const vm::HostArg ui[] = {{api::AbiType::ClientContext, ctx, {}, {}},
                                  {api::AbiType::String, {}, "hud.hp", {}},
                                  {api::AbiType::Number, Value::f64v(1.0), {}, {}}};
        CHECK_FALSE(client.call(*setUi, ui).ok);
        client.endCall(false);
        CHECK(client.uiOutputs().empty());
    }
}
