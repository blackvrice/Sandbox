// Studio 테스트 세션 (v0.8 ST-06-9 — editor/services/TestService · platform/process spawnProcess, ADR-0115): 계획의
// 인자 (서버 먼저 · 127.0.0.1 · 흉내는 양쪽 · 자동 시험의 헤드리스 · 서버 틱), 틀린 계획 거절, 상태 (돌고 있음 → 끝남 ·
// 끝냄), 두 번 시작 거절, 못 띄우면 띄운 것을 끝낸다, 성공 판정. 진짜 프로세스: 서버를 띄우고 · 끝내고 (돌던 것) ·
// 스스로 끝나기 (--ticks --exit) · 로그
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <random>
#include <string>

#include "editor/services/TestService.hpp"
#include "foundation/io/FileIo.hpp"
#include "platform/process/Process.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;

namespace {

class FakeLauncher final : public IProcessLauncher {
public:
    std::vector<TestLaunch> launched;
    std::map<u32, std::optional<int>> exits;
    std::vector<u32> stopped;
    usize failAt = 1000; // 이 번째 띄우기를 실패시킨다

    Expected<u32> launch(const TestLaunch& l) override {
        if (launched.size() == failAt) {
            return makeError(ErrorCode::NotFound, "없는 실행 파일");
        }
        launched.push_back(l);
        const u32 h = static_cast<u32>(launched.size());
        exits[h] = std::nullopt;
        return h;
    }
    std::optional<int> exitCode(u32 h) override { return exits[h]; }
    void stop(u32 h) override {
        stopped.push_back(h);
        if (!exits[h]) {
            exits[h] = 1;
        }
    }
};

TestPlan basePlan() {
    TestPlan p;
    p.project = "C:/Games/My Game";
    p.serverExe = "bin/SandboxServer.exe";
    p.clientExe = "bin/SandboxClient.exe";
    p.logDir = "logs";
    return p;
}

bool has(const std::vector<std::string>& argv, std::string_view a, std::string_view b) {
    for (usize i = 0; i + 1 < argv.size(); ++i) {
        if (argv[i] == a && argv[i + 1] == b) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("테스트 세션: 계획 — 서버 먼저 · 127.0.0.1 · 흉내 양쪽 · 자동 시험 (v0.8 ST-06-9)") {
    TestPlan p = basePlan();
    Expected<std::vector<TestLaunch>> l = planTestSession(p);
    REQUIRE(l.has_value());
    REQUIRE(l->size() == 3);
    const TestLaunch& server = l->front();
    CHECK(server.label == "서버");
    CHECK(server.argv.front() == "bin/SandboxServer.exe");
    CHECK(has(server.argv, "--world", "C:/Games/My Game")); // 빈칸 있는 경로도 한 인자 (셸이 없다)
    CHECK(has(server.argv, "--port", "7790"));
    CHECK(has(server.argv, "--bind", "127.0.0.1"));
    CHECK(std::ranges::find(server.argv, "--net-sim") == server.argv.end());
    CHECK(std::ranges::find(server.argv, "--exit") == server.argv.end()); // 사람이 끝낸다
    CHECK(server.log == fs::path("logs") / "server.log");
    CHECK((*l)[1].label == "player-1");
    CHECK(has((*l)[2].argv, "--connect", "127.0.0.1:7790"));
    CHECK(has((*l)[2].argv, "--name", "player-2"));
    CHECK(std::ranges::find((*l)[2].argv, "--headless") == (*l)[2].argv.end());

    // 흉내 · 자동 시험
    p.clients = 4;
    p.port = 9000;
    p.netSim = {80, 10, 0.05};
    p.headlessFrames = 600;
    l = planTestSession(p);
    REQUIRE(l->size() == 5);
    CHECK(has(l->front().argv, "--net-sim", "latency=80,jitter=10,loss=0.05"));
    CHECK(has(l->front().argv, "--ticks", "600")); // 600 / 2 + 300
    CHECK(std::ranges::find(l->front().argv, "--exit") != l->front().argv.end());
    for (usize i = 1; i < l->size(); ++i) {
        CHECK(has((*l)[i].argv, "--net-sim", "latency=80,jitter=10,loss=0.05"));
        CHECK(has((*l)[i].argv, "--frames", "600"));
        CHECK(has((*l)[i].argv, "--fps", "60"));
        CHECK(has((*l)[i].argv, "--connect", "127.0.0.1:9000"));
    }

    // 틀린 계획
    TestPlan bad = basePlan();
    bad.clients = 0;
    CHECK(planTestSession(bad).error().code == ErrorCode::InvalidArgument);
    bad.clients = 5;
    CHECK_FALSE(planTestSession(bad).has_value());
    bad = basePlan();
    bad.port = 0;
    CHECK_FALSE(planTestSession(bad).has_value());
    bad = basePlan();
    bad.netSim.lossRate = 0.6;
    CHECK_FALSE(planTestSession(bad).has_value());
    bad = basePlan();
    bad.serverExe.clear();
    CHECK_FALSE(planTestSession(bad).has_value());
}

TEST_CASE("테스트 세션: 상태 · 두 번 시작 · 못 띄움 · 모두 끝내기 · 성공 판정 (v0.8 ST-06-9)") {
    FakeLauncher fake;
    {
        TestService t(fake);
        CHECK_FALSE(t.running());
        CHECK_FALSE(t.succeeded());
        REQUIRE(t.start(basePlan()).has_value());
        CHECK(t.running());
        CHECK(t.processes().size() == 3);
        CHECK(t.start(basePlan()).error().code == ErrorCode::AlreadyExists);
        CHECK(t.poll().empty());

        // 스스로 끝남 — 모두 0 이면 성공
        fake.exits[2] = 0;
        std::vector<std::string> e = t.poll();
        REQUIRE(e.size() == 1);
        CHECK(e.front().find("player-1 끝남 (종료 코드 0)") != std::string::npos);
        CHECK(t.running());
        fake.exits[1] = 0;
        fake.exits[3] = 0;
        CHECK(t.poll().size() == 2);
        CHECK_FALSE(t.running());
        CHECK(t.succeeded());
        CHECK(t.poll().empty()); // 한 번만 알린다

        // 다시 시작 — 0 이 아닌 종료는 실패
        REQUIRE(t.start(basePlan()).has_value());
        fake.exits[4] = 0;
        fake.exits[5] = 2;
        fake.exits[6] = 0;
        (void)t.poll();
        CHECK_FALSE(t.succeeded());

        // 모두 끝내기 — 끝낸 것은 성공이 아니다
        REQUIRE(t.start(basePlan()).has_value());
        t.stopAll();
        CHECK_FALSE(t.running());
        CHECK(fake.stopped == std::vector<u32>{7, 8, 9});
        CHECK(t.processes()[0].state == TestProcessState::Stopped);
        CHECK_FALSE(t.succeeded());

        // 남은 것을 두고 서비스가 없어지면 끝낸다
        REQUIRE(t.start(basePlan()).has_value());
    }
    CHECK(fake.stopped.size() == 6);

    // 못 띄우면 (둘째 클라이언트) 띄운 서버 · 첫 클라이언트를 끝내고 그 오류
    FakeLauncher failing;
    failing.failAt = 2;
    TestService t(failing);
    const Expected<void> r = t.start(basePlan());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::NotFound);
    CHECK(r.error().message.find("player-2") != std::string::npos);
    CHECK(failing.stopped == std::vector<u32>{1, 2});
    CHECK_FALSE(t.running());
}

#if defined(SBX_SERVER_EXE)
TEST_CASE("프로세스: 띄우기 · 돌던 것 끝내기 · 스스로 끝나기 · 로그 (v0.8 ST-06-9 spawnProcess)") {
    std::random_device rd;
    const fs::path dir = fs::temp_directory_path() / std::format("sbx_spawn_{:08x}", rd());
    fs::create_directories(dir);

    // 계속 도는 서버 (--port 0 — 빈 포트) → 끝낸다
    {
        const std::vector<std::string> argv{
            SBX_SERVER_EXE, "--world", "ecosystem_small", "--content-root", SBX_CONTENT_DIR,
            "--port",       "0",       "--bind",          "127.0.0.1"};
        Expected<std::unique_ptr<proc::ChildProcess>> c = proc::spawnProcess(argv, {{}, dir / "long.log"});
        REQUIRE(c.has_value());
        CHECK((*c)->pid() != 0);
        CHECK_FALSE((*c)->waitFor(300)); // 아직 돈다
        CHECK_FALSE((*c)->poll().has_value());
        (*c)->terminate();
        REQUIRE((*c)->waitFor(10000));
#if defined(_WIN32)
        CHECK((*c)->poll().value() != 0); // TerminateProcess (1)
#else
        CHECK((*c)->poll().has_value()); // SIGTERM — 서버는 깨끗이 멈춘다 (종료 코드 0 일 수 있다)
#endif
    }

    // 스스로 끝나는 서버 → 종료 코드 0 · 로그
    {
        const std::vector<std::string> argv{
            SBX_SERVER_EXE, "--world", "ecosystem_small", "--content-root", SBX_CONTENT_DIR,
            "--port",       "0",       "--bind",          "127.0.0.1",      "--ticks",
            "15",           "--exit"};
        Expected<std::unique_ptr<proc::ChildProcess>> c = proc::spawnProcess(argv, {{}, dir / "short.log"});
        REQUIRE(c.has_value());
        REQUIRE((*c)->waitFor(60000));
        CHECK((*c)->poll().value() == 0);
        c->reset(); // 끝난 자식의 소멸은 아무 일 없다
        const Expected<std::string> log = io::readFile(dir / "short.log");
        REQUIRE(log.has_value());
        CHECK(log->find("서버 시작") != std::string::npos);
    }

    // 없는 실행 파일
    const std::vector<std::string> missing{(dir / "no-such-program").string()};
    CHECK(proc::spawnProcess(missing, {}).error().code == ErrorCode::NotFound);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
#endif
