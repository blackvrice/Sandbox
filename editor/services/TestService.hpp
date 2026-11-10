#pragma once
// Studio 테스트 세션 — 서버 + N 클라이언트 (v0.8 ST-06-9 — docs/25-STUDIO-ARCHITECTURE.md 3장 TestService, ADR-0115).
// 프로세스를 모른다 (editor/services 는 platform 을 모른다 — ADR-0108): 무엇을 어떤 인자로 띄울지 (plan) 와 상태 (돌고
// 있음 · 끝남 · 끝냄 · 못 띄움) 를 맡고, 띄우기 · 끝내기는 앱이 넘긴 IProcessLauncher (SandboxProcess 의 spawnProcess)
// 가 한다.
//
//   서버      SandboxServer --world <프로젝트> --port <p> --bind 127.0.0.1 [--net-sim …] [--ticks T --exit]
//             (저장한 프로젝트 — 디스크의 시작 월드. 저장하지 않은 편집은 들어가지 않는다)
//   클라이언트 SandboxClient --connect 127.0.0.1:<p> --name player-<i> [--net-sim …] [--headless --frames F --fps 60]
//             (1 ~ 4 개. 지연 · 손실은 양쪽 보내는 쪽에 — 한 방향 값)
//   자동 시험 headlessFrames > 0 이면 클라이언트는 헤드리스로 F 프레임 (60 fps 로 F/60 초) 뒤 끝, 서버는 그보다 길게
//             (T = F/2 + 300 틱 — 30 TPS) 돌고 끝 — 세션이 스스로 끝난다 (CTest studio_test_session)
//   끝내기    stopAll = 모두 끝낸다 (Roblox End Session 과 같은 사용 모양 — 벤치마크 E2). 앱이 끝나면 런처가
//             남은 자식을 끝낸다

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "foundation/types/Error.hpp"

namespace sbx::editor::services {

struct NetSimSettings {
    f64 latencyMs = 0; // 한 방향 · 0 ~ 2000
    f64 jitterMs = 0;  // 0 ~ 2000
    f64 lossRate = 0;  // 0 ~ 0.5 (Snapshot 채널 — 신뢰 채널은 늦어질 뿐)
    [[nodiscard]] bool active() const noexcept { return latencyMs > 0 || jitterMs > 0 || lossRate > 0; }
    // --net-sim 의 글 ("latency=80,jitter=10,loss=0.05")
    [[nodiscard]] std::string arg() const;
};

struct TestPlan {
    std::filesystem::path project;
    u32 clients = 2;
    u16 port = 7790;
    NetSimSettings netSim;
    u32 headlessFrames = 0; // > 0 = 자동 시험 (위)
    std::filesystem::path serverExe;
    std::filesystem::path clientExe;
    std::filesystem::path logDir; // 프로세스마다 <이름>.log
};

struct TestLaunch {
    std::string label; // "서버" · "player-1"
    std::vector<std::string> argv;
    std::filesystem::path log;
};

// 띄울 차례 (서버 먼저). 틀린 계획 (클라이언트 수 · 포트 0 · 흉내 범위 · 빈 경로) 은 InvalidArgument
[[nodiscard]] Expected<std::vector<TestLaunch>> planTestSession(const TestPlan& plan);

class IProcessLauncher {
public:
    virtual ~IProcessLauncher() = default;
    virtual Expected<u32> launch(const TestLaunch& spec) = 0; // 손잡이 (0 이 아니다)
    virtual std::optional<int> exitCode(u32 handle) = 0;      // 끝났으면 종료 코드
    virtual void stop(u32 handle) = 0;                        // 강제로 끝낸다 (끝났으면 아무 일 없음)
};

enum class TestProcessState : u8 { Running, Exited, Stopped };

struct TestProcess {
    std::string label;
    std::filesystem::path log;
    u32 handle = 0;
    TestProcessState state = TestProcessState::Running;
    int exitCode = 0;
};

class TestService {
public:
    static constexpr u32 kMaxClients = 4;

    explicit TestService(IProcessLauncher& launcher) : m_launcher(launcher) {}
    ~TestService(); // 남은 프로세스를 끝낸다
    TestService(const TestService&) = delete;
    TestService& operator=(const TestService&) = delete;

    // 띄운다 — 돌고 있으면 거절 (AlreadyExists). 하나라도 못 띄우면 띄운 것을 끝내고 그 오류
    Expected<void> start(const TestPlan& plan);
    // 끝난 프로세스를 거둔다 — 사람이 읽을 일 ("player-1 끝남 (종료 코드 0)")
    [[nodiscard]] std::vector<std::string> poll();
    void stopAll();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool started() const noexcept { return !m_processes.empty(); }
    // 모두 스스로 끝났고 종료 코드 0 (자동 시험의 성공 — 끝낸 것 · 0 이 아닌 것이 있으면 false)
    [[nodiscard]] bool succeeded() const noexcept;
    [[nodiscard]] const std::vector<TestProcess>& processes() const noexcept { return m_processes; }

private:
    IProcessLauncher& m_launcher;
    std::vector<TestProcess> m_processes;
};

} // namespace sbx::editor::services
