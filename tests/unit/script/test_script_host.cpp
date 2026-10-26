#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/script/ScriptHost.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/log/Log.hpp"
#include "script/lang/Codegen.hpp"
#include "script/sdk/SdkGenerator.hpp"

using namespace sbx;
using namespace sbx::script;
using vm::Trap;
using vm::Value;

namespace {

bc::Module compile(std::string_view src) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts", std::string(src)};
    std::optional<bc::Module> m = compileScripts(std::span(&f, 1), d);
    REQUIRE_MESSAGE(d.ok(), d.describeAll());
    return std::move(*m);
}

bool mentions(const std::vector<std::string>& problems, std::string_view needle) {
    for (const std::string& p : problems) {
        if (p.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

vm::HostResult noop(ScriptHost&, std::span<const vm::HostArg>) {
    return {};
}

// 로그를 stderr 대신 모은다 (시험 출력이 지저분해지지 않게)
struct LogCapture {
    std::vector<std::string> lines;
    LogCapture() {
        log::setSink([this](log::Level, std::string_view cat, std::string_view msg) {
            lines.push_back(std::string(cat) + ": " + std::string(msg));
        });
    }
    ~LogCapture() { log::setSink(nullptr); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
};

constexpr std::string_view kScript = R"(import { i32, ServerContext } from "sandbox"
export function tick(ctx: ServerContext): i32 {
  ctx.log(`t=${ctx.tick}`)
  return ctx.tick
}
export function shout(ctx: ServerContext): void {
  let s = "가"
  for (let i = 0; i < 11; i++) { s = s + s }
  ctx.log(s)
}
)";

} // namespace

TEST_SUITE("script") {
    TEST_CASE("Host 바인딩: 엔진의 구현 목록이 Host API 표와 맞는다 (계약 검사)") {
        const std::vector<std::string> problems = checkHostBindings(hostBindings());
        std::string all;
        for (const std::string& p : problems) {
            all += p + "\n";
        }
        CHECK_MESSAGE(problems.empty(), all);
        usize server = 0;
        for (const api::HostFunction& f : api::kHostFunctions) {
            server += f.domain == api::HostDomain::Server ? 1 : 0;
        }
        CHECK(hostBindings().size() == server); // Client 도메인은 ClientScriptHost 의 몫 (ADR-0067)
    }

    TEST_CASE("Host 바인딩: 계약 검사가 잡는 것 — 없음 · 둘 · 표에 없음 · 서명 · 함수 없음") {
        using api::AbiType;
        CHECK(mentions(checkHostBindings({}), "ctx.tick: 구현이 없다"));
        const HostBinding wrongSig[] = {
            {"ctx.tick", AbiType::I32, {AbiType::ServerContext}, 1, &noop},
            {"ctx.log", AbiType::Void, {AbiType::ServerContext, AbiType::I32}, 2, &noop},
        };
        CHECK(mentions(checkHostBindings(wrongSig), "ctx.log: 구현의 서명이 표와 다르다"));
        const HostBinding twice[] = {
            {"ctx.tick", AbiType::I32, {AbiType::ServerContext}, 1, &noop},
            {"ctx.tick", AbiType::I32, {AbiType::ServerContext}, 1, &noop},
            {"ctx.log", AbiType::Void, {AbiType::ServerContext, AbiType::String}, 2, &noop},
            {"ctx.zzz", AbiType::Void, {AbiType::ServerContext}, 1, &noop},
        };
        const auto p = checkHostBindings(twice);
        CHECK(mentions(p, "ctx.tick: 구현이 2 개다"));
        CHECK(mentions(p, "ctx.zzz: 표에 없는 구현"));
        const HostBinding noImpl[] = {
            {"ctx.tick", AbiType::I32, {AbiType::ServerContext}, 1, nullptr},
            {"ctx.log", AbiType::Void, {AbiType::ServerContext, AbiType::String}, 2, &noop},
        };
        CHECK(mentions(checkHostBindings(noImpl), "ctx.tick: 구현 함수가 없다"));
    }

    TEST_CASE("ScriptHost: 스크립트가 엔진의 ctx.tick · ctx.log 를 부른다") {
        LogCapture capture;
        const bc::Module m = compile(kScript);
        ScriptHost host;
        vm::Interpreter v(m, &host);
        const Value ctx = host.beginCall(30, "scripts/a.ts:tick");
        const vm::CallResult r = v.call("scripts/a.ts", "tick", std::vector<Value>{ctx});
        host.endCall(true);
        REQUIRE_MESSAGE(r.ok(), r.message);
        CHECK(r.value.asI32() == 30);
        REQUIRE(host.recentLogs().size() == 1);
        CHECK(host.recentLogs()[0].tick == 30);
        CHECK(host.recentLogs()[0].script == "scripts/a.ts:tick");
        CHECK(host.recentLogs()[0].text == "t=30");
        REQUIRE(capture.lines.size() == 1);
        CHECK(capture.lines[0] == "script: scripts/a.ts:tick t30: t=30");
    }

    TEST_CASE("ScriptHost: 지난 호출 · 위조한 · 열리지 않은 ctx 는 HostError") {
        LogCapture capture;
        const bc::Module m = compile(kScript);
        ScriptHost host;
        vm::Interpreter v(m, &host);
        const Value old = host.beginCall(1, "a");
        host.endCall(true);
        // 열린 호출이 없다
        CHECK(v.call("scripts/a.ts", "tick", std::vector<Value>{old}).trap == Trap::HostError);
        // 새 호출이 열렸어도 지난 ctx 는 아니다
        const Value now = host.beginCall(2, "a");
        CHECK(now.bits != old.bits);
        const vm::CallResult stale = v.call("scripts/a.ts", "tick", std::vector<Value>{old});
        CHECK(stale.trap == Trap::HostError);
        CHECK(stale.message.find("ctx 가 지금 호출의 것이 아니다") != std::string::npos);
        CHECK(v.call("scripts/a.ts", "tick", std::vector<Value>{Value::opaque(12345)}).trap == Trap::HostError);
        CHECK(v.call("scripts/a.ts", "tick", std::vector<Value>{now}).ok());
        host.endCall(true);
    }

    TEST_CASE("ScriptHost: 긴 로그는 UTF-8 글자 경계에서 자른다") {
        LogCapture capture;
        const bc::Module m = compile(kScript);
        ScriptHost host;
        vm::Interpreter v(m, &host);
        const Value ctx = host.beginCall(5, "a");
        REQUIRE(v.call("scripts/a.ts", "shout", std::vector<Value>{ctx}).ok());
        host.endCall(true);
        REQUIRE(host.recentLogs().size() == 1);
        const std::string& line = host.recentLogs()[0].text;
        CHECK(line.size() <= ScriptHost::kMaxLogBytes + 3);
        CHECK(line.ends_with("…"));
        CHECK((line.size() - 3) % 3 == 0); // "가" (3 바이트) 가 반쯤 남지 않았다
    }

    TEST_CASE("SDK: sandbox.d.ts 는 표에서 만든 것과 같다 (골든 — 다르면 sbx sdk sdk)") {
        const std::string generated = sdk::generateSandboxDts();
        CHECK(generated == sdk::generateSandboxDts()); // 결정적
        CHECK(generated.find("declare module \"sandbox\" {") != std::string::npos);
        CHECK(generated.find("    readonly tick: i32;\n") != std::string::npos);
        CHECK(generated.find("    log(message: string): void;\n") != std::string::npos);
        CHECK(generated.find(std::format("{:016x}", api::hostApiFingerprint())) != std::string::npos);
        const Expected<std::string> committed = io::readFile(std::string(SBX_SDK_DIR) + "/sandbox.d.ts");
        REQUIRE_MESSAGE(committed.has_value(), committed.error().describe());
        CHECK_MESSAGE(*committed == generated, "sdk/sandbox.d.ts 가 Host API 표와 다르다 — sbx sdk sdk 로 다시 만든다");
    }
}
