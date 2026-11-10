#pragma once
// Studio 빌드 (v0.8 M6-T3 — docs/25-STUDIO-ARCHITECTURE.md 3장 BuildService · 5장 ⑩, docs/23-ROADMAP.md 1.1, ADR-0112).
// 검사 · 빌드 — UI 를 모른다 (빌드 화면이 부른다, 시험은 헤드리스).
//
// 검사 (check) = validateProject (sbx check 와 같은 것 — 엔진 에셋 루트 포함): 문제를 오류 · 경고로 나눠 돌려준다. 누락
// 에셋 (팩 materials.json 이 부르는 없는 에셋 — A6) · id 없는 에셋 (A2) 은 오류라 빌드를 막고, 어디서도 부르지 않는
// 에셋 (A8) 은 경고. 빌드 (start) = 검사 → 오류가 없으면 package::buildPackage (sbx build 와 같은 것 — 빌드 스냅숏 ·
// 원자적 쓰기). 일꾼 스레드에서 돌고 (편집 · 틱을 막지 않는다 — 25 S5) 결과는 메인 스레드가 poll() 로 가져간다. 한 번에
// 하나 (돌고 있으면 start 거절). 보고 = 패키지 경로 · gameId · 버전 · 파일 수 · 바이트 · digest · 오류 · 경고 · 에셋
// 그룹 줄 · 걸린 시간.

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::ecs {
class ComponentCatalog;
}

namespace sbx::editor::services {

struct BuildRequest {
    std::filesystem::path project;
    std::filesystem::path out; // 비면 <프로젝트>/build/<gameId>-<version>.sbpkg
    std::string profile;       // Export Profile 이름 (export/<이름>.json — 비면 없이)
};

struct BuildReport {
    bool checkOnly = false; // 검사만 했다
    bool ok = false;        // 검사 오류 없음 (빌드면 패키지를 썼다)
    std::filesystem::path file;
    std::string gameId;
    std::string version;
    usize files = 0;
    u64 bytes = 0;
    std::string digest;
    std::vector<std::string> errors;   // 검사 오류 · 빌드 실패 문장
    std::vector<std::string> warnings; // 검사 경고 · 프로필 미검증 · 부르지 않는 에셋
    std::vector<std::string> groups;   // "이름 — N 개 · B 바이트"
    f64 seconds = 0;
};

class BuildService {
public:
    // catalog 는 서비스보다 오래 산다. engineAssets = sbx check 와 같은 U8 검사 (비면 보지 않는다)
    BuildService(const ecs::ComponentCatalog& catalog, SemVer engine, std::filesystem::path engineAssets = {});
    ~BuildService(); // 돌고 있으면 끝날 때까지 기다린다
    BuildService(const BuildService&) = delete;
    BuildService& operator=(const BuildService&) = delete;

    [[nodiscard]] BuildReport check(const std::filesystem::path& project) const;
    // 일꾼 스레드에서 검사 → 빌드. 이미 돌고 있거나 끝난 보고를 아직 poll 로 가져가지 않았으면 false
    bool start(BuildRequest request);
    [[nodiscard]] bool running() const noexcept { return m_running.load(); }
    // 끝난 빌드의 보고 (메인 스레드 — 한 번만 돌려준다)
    [[nodiscard]] std::optional<BuildReport> poll();
    // 같은 스레드에서 (시험 · 헤드리스)
    [[nodiscard]] BuildReport buildNow(const BuildRequest& request) const;

private:
    void join();

    const ecs::ComponentCatalog& m_catalog;
    SemVer m_engine;
    std::filesystem::path m_engineAssets;
    std::thread m_worker;
    std::atomic<bool> m_running{false};
    std::unique_ptr<BuildReport> m_done; // 일꾼이 쓰고 (m_running = false 전에) 메인이 poll 로 가져간다
};

} // namespace sbx::editor::services
