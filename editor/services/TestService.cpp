#include "editor/services/TestService.hpp"

#include <algorithm>
#include <format>

namespace sbx::editor::services {

namespace {

std::string pathArg(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return {u.begin(), u.end()};
}

} // namespace

std::string NetSimSettings::arg() const {
    return std::format("latency={},jitter={},loss={}", latencyMs, jitterMs, lossRate);
}

Expected<std::vector<TestLaunch>> planTestSession(const TestPlan& plan) {
    if (plan.clients < 1 || plan.clients > TestService::kMaxClients) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("클라이언트 수 {} — 1 ~ {}", plan.clients, TestService::kMaxClients));
    }
    if (plan.port == 0) {
        return makeError(ErrorCode::InvalidArgument,
                         "테스트 세션의 포트는 1 ~ 65535 (클라이언트가 같은 포트로 접속한다)");
    }
    if (plan.project.empty() || plan.serverExe.empty() || plan.clientExe.empty() || plan.logDir.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "테스트 세션 — 프로젝트 · 서버 · 클라이언트 · 로그 폴더가 필요하다");
    }
    const NetSimSettings& n = plan.netSim;
    const auto inRange = [](f64 v, f64 hi) { return v >= 0 && v <= hi; };
    if (!inRange(n.latencyMs, 2000) || !inRange(n.jitterMs, 2000) || !inRange(n.lossRate, 0.5)) {
        return makeError(ErrorCode::InvalidArgument, "지연 · 흔들림 0 ~ 2000 ms, 손실 0 ~ 0.5");
    }
    std::vector<TestLaunch> out;
    TestLaunch server{"서버",
                      {pathArg(plan.serverExe), "--world", pathArg(plan.project), "--port", std::to_string(plan.port),
                       "--bind", "127.0.0.1"},
                      plan.logDir / "server.log"};
    if (n.active()) {
        server.argv.insert(server.argv.end(), {"--net-sim", n.arg()});
    }
    if (plan.headlessFrames > 0) {
        // 클라이언트가 F 프레임 (60 fps — F/60 초) 뒤 끝난다. 서버는 30 TPS 로 그보다 10 초 더 — 접속 · 끝 여유
        const u64 ticks = u64{plan.headlessFrames} / 2 + 300;
        server.argv.insert(server.argv.end(), {"--ticks", std::to_string(ticks), "--exit"});
    }
    out.push_back(std::move(server));
    for (u32 i = 1; i <= plan.clients; ++i) {
        const std::string name = std::format("player-{}", i);
        TestLaunch c{name,
                     {pathArg(plan.clientExe), "--connect", std::format("127.0.0.1:{}", plan.port), "--name", name},
                     plan.logDir / (name + ".log")};
        if (n.active()) {
            c.argv.insert(c.argv.end(), {"--net-sim", n.arg()});
        }
        if (plan.headlessFrames > 0) {
            c.argv.insert(c.argv.end(), {"--headless", "--frames", std::to_string(plan.headlessFrames), "--fps", "60"});
        }
        out.push_back(std::move(c));
    }
    return out;
}

TestService::~TestService() {
    stopAll();
}

Expected<void> TestService::start(const TestPlan& plan) {
    if (running()) {
        return makeError(ErrorCode::AlreadyExists, "테스트 세션이 이미 돌고 있다 — 먼저 모두 끝낸다");
    }
    Expected<std::vector<TestLaunch>> launches = planTestSession(plan);
    if (!launches) {
        return std::unexpected(launches.error());
    }
    m_processes.clear();
    for (const TestLaunch& l : *launches) {
        Expected<u32> h = m_launcher.launch(l);
        if (!h) {
            stopAll();
            return makeError(h.error().code, std::format("{} 을 띄우지 못했다 — {}", l.label, h.error().message),
                             h.error().context);
        }
        m_processes.push_back({l.label, l.log, *h, TestProcessState::Running, 0});
    }
    return {};
}

std::vector<std::string> TestService::poll() {
    std::vector<std::string> events;
    for (TestProcess& p : m_processes) {
        if (p.state != TestProcessState::Running) {
            continue;
        }
        if (const std::optional<int> code = m_launcher.exitCode(p.handle)) {
            p.state = TestProcessState::Exited;
            p.exitCode = *code;
            events.push_back(std::format("{} 끝남 (종료 코드 {}) — 로그 {}", p.label, *code, pathArg(p.log)));
        }
    }
    return events;
}

void TestService::stopAll() {
    for (TestProcess& p : m_processes) {
        if (p.state == TestProcessState::Running) {
            m_launcher.stop(p.handle);
            p.state = TestProcessState::Stopped;
        }
    }
}

bool TestService::running() const noexcept {
    return std::ranges::any_of(m_processes, [](const TestProcess& p) { return p.state == TestProcessState::Running; });
}

bool TestService::succeeded() const noexcept {
    return !m_processes.empty() && std::ranges::all_of(m_processes, [](const TestProcess& p) {
        return p.state == TestProcessState::Exited && p.exitCode == 0;
    });
}

} // namespace sbx::editor::services
