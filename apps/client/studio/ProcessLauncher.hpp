#pragma once
// Studio 테스트 세션의 프로세스 런처 (v0.8 ST-06-9 — ADR-0115). TestService (editor/services — 프로세스를 모른다) 의
// IProcessLauncher 를 SandboxProcess 의 spawnProcess 로. 출력은 계획의 로그 파일로, 런처가 없어지면 남은 자식을 끝낸다
// (ChildProcess 소멸자 — Windows 는 Job 객체로 앱이 충돌해도).

#include <map>
#include <memory>

#include "editor/services/TestService.hpp"
#include "platform/process/Process.hpp"

namespace sbx::client {

class ProcessLauncher final : public editor::services::IProcessLauncher {
public:
    Expected<u32> launch(const editor::services::TestLaunch& spec) override;
    std::optional<int> exitCode(u32 handle) override;
    void stop(u32 handle) override;

private:
    std::map<u32, std::unique_ptr<proc::ChildProcess>> m_children;
    u32 m_next = 1;
};

} // namespace sbx::client
