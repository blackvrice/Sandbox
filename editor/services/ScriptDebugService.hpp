#pragma once
// 스크립트 디버거 서비스 (v0.9 ST-06-7 조각 3 — docs/27-SCRIPT-IDE.md 6장, ADR-0134). Studio 의 "스크립트 디버거" 창
// (editor/ui/ScriptDebugPanel) 과 스크립트 편집기의 줄 번호 칸 (중단점 · 멈춘 줄) 이 함께 쓴다. UI 를 모른다.
//
// 기록 열기: 기록 폴더 (start/ + replay.sbxr — 세이브 폴더 아래) → 리플레이의 팩을 콘텐츠 폴더에서 읽고 (로컬 서버의
// 재생과 같은 입력 — 라이브 월드의 콘텐츠 · 오버레이와 따로), 시작 세이브를 로드해 ScriptDebugSession 을 만든다
// (Ready).
// 중단점: (팩, 파일, 줄, logpoint 글) — 세션 사이에 남는다 (처음부터 · 다른 기록). 재생이 도는 동안 바꾼 것은 멈추거나
// 다시 시작할 때 세션에 넣는다 (세션은 Running 중에 코드를 고치지 않는다 — ADR-0133).
// 처음부터: 같은 기록을 다시 로드해 새 세션 (같은 중단점 · 감시).
//
// 세션의 월드는 디버그 스레드의 것 — 이 서비스는 세션 API 로만 본다.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/script/ScriptDebugSession.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::ecs {
class ComponentCatalog;
}
namespace sbx::content {
class ContentDatabase;
}

namespace sbx::editor::services {

struct RecordingEntry {
    std::string name; // 폴더 이름
    std::filesystem::path dir;
};
// savesRoot 아래의 기록 (start/ 와 replay.sbxr 가 있는 폴더) — 이름 순
[[nodiscard]] std::vector<RecordingEntry> listRecordings(const std::filesystem::path& savesRoot);

struct DebugBreakpoint {
    std::string pack;
    std::string file; // "scripts/regen.ts"
    u32 line = 0;
    std::string log; // 비면 멈춘다, 아니면 logpoint
    usize sites = 0; // 지금 세션에 놓인 BRK 수 (0 = 그 줄에 코드 없음 · 아직 넣지 않음)
};

struct DebugLocation {
    std::string pack;
    std::string file;
    u32 line = 0;
};

class ScriptDebugService {
public:
    ScriptDebugService();
    ~ScriptDebugService();
    ScriptDebugService(const ScriptDebugService&) = delete;
    ScriptDebugService& operator=(const ScriptDebugService&) = delete;

    // 기록을 연다 (앞 세션은 닫는다). contentRoot = 팩을 찾을 콘텐츠 폴더 (로컬 서버와 같은 것)
    Expected<void> open(const std::filesystem::path& recording, const std::filesystem::path& contentRoot);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return m_session != nullptr; }
    [[nodiscard]] const std::filesystem::path& recording() const noexcept { return m_recording; }
    [[nodiscard]] u64 calls() const noexcept; // 리플레이의 call 수
    // 같은 기록 · 중단점 · 감시로 새 세션 (Ready)
    Expected<void> restart();
    // Ready → Running (stopAtTick 이면 그 틱의 첫 스크립트 줄에서 멈춘다)
    void start(std::optional<sim::Tick> stopAtTick = std::nullopt);
    // 프레임마다 — 멈췄으면 미뤄 둔 중단점 바꾸기를 세션에 넣는다
    void update();

    // 중단점 — 있으면 지우고 없으면 놓는다 (true = 지금 있다)
    bool toggleBreakpoint(std::string_view pack, std::string_view file, u32 line);
    void setBreakpoint(std::string_view pack, std::string_view file, u32 line, std::string log = {});
    bool removeBreakpoint(std::string_view pack, std::string_view file, u32 line);
    [[nodiscard]] const std::vector<DebugBreakpoint>& breakpoints() const noexcept { return m_breakpoints; }
    [[nodiscard]] std::vector<u32> breakpointLines(std::string_view pack, std::string_view file) const;
    [[nodiscard]] bool pendingChanges() const noexcept { return !m_pending.empty(); }

    void setWatches(std::vector<std::string> watches);
    [[nodiscard]] const std::vector<std::string>& watches() const noexcept { return m_watches; }

    [[nodiscard]] script::ScriptDebugSession* session() noexcept { return m_session.get(); }
    [[nodiscard]] const script::ScriptDebugSession* session() const noexcept { return m_session.get(); }
    // 멈춘 자리 (멈췄을 때만) — 편집기가 그 줄을 칠한다
    [[nodiscard]] std::optional<DebugLocation> pausedAt() const;

private:
    struct Pending {
        bool set = true;
        DebugBreakpoint bp;
    };
    Expected<void> load();
    void apply(Pending& p); // 세션에 (Ready · Paused 일 때만 부른다)
    [[nodiscard]] bool canEdit() const;
    DebugBreakpoint* find(std::string_view pack, std::string_view file, u32 line);

    std::filesystem::path m_recording;
    std::filesystem::path m_contentRoot;
    std::unique_ptr<ecs::ComponentCatalog> m_catalog;
    std::unique_ptr<content::ContentDatabase> m_content;
    std::unique_ptr<replay::Replay> m_replay;
    std::unique_ptr<sim::SimulationWorld> m_world;
    std::unique_ptr<script::ScriptDebugSession> m_session; // 월드 · 리플레이보다 먼저 없앤다 (뒤에 둔다)
    std::vector<DebugBreakpoint> m_breakpoints;
    std::vector<std::string> m_watches;
    std::vector<Pending> m_pending; // 재생이 도는 동안 바꾼 것
};

} // namespace sbx::editor::services
