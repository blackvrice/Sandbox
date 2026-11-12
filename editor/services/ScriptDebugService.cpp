#include "editor/services/ScriptDebugService.hpp"

#include <algorithm>
#include <format>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::editor::services {

namespace fs = std::filesystem;

std::vector<RecordingEntry> listRecordings(const fs::path& savesRoot) {
    std::vector<RecordingEntry> out;
    std::error_code ec;
    if (!fs::is_directory(savesRoot, ec)) {
        return out;
    }
    for (const auto& e : fs::directory_iterator(savesRoot, ec)) {
        if (e.is_directory(ec) && fs::is_regular_file(e.path() / "replay.sbxr", ec) &&
            fs::is_directory(e.path() / "start", ec)) {
            out.push_back({io::displayPath(e.path().filename()), e.path()}); // UTF-8 (한글 이름)
        }
    }
    std::sort(out.begin(), out.end(), [](const RecordingEntry& a, const RecordingEntry& b) { return a.name < b.name; });
    return out;
}

ScriptDebugService::ScriptDebugService() = default;

ScriptDebugService::~ScriptDebugService() {
    close();
}

void ScriptDebugService::close() {
    m_session.reset(); // 디버그 스레드를 그치고 BRK 를 거둔다 — 월드보다 먼저
    m_world.reset();
    m_replay.reset();
    m_content.reset();
    m_catalog.reset();
    m_pending.clear();
    for (DebugBreakpoint& b : m_breakpoints) {
        b.sites = 0;
    }
}

u64 ScriptDebugService::calls() const noexcept {
    return m_replay ? m_replay->calls : 0;
}

Expected<void> ScriptDebugService::open(const fs::path& recording, const fs::path& contentRoot) {
    close();
    m_recording = recording;
    m_contentRoot = contentRoot;
    return load();
}

Expected<void> ScriptDebugService::restart() {
    if (m_recording.empty()) {
        return makeError(ErrorCode::InvalidArgument, "연 기록이 없다");
    }
    close();
    return load();
}

Expected<void> ScriptDebugService::load() {
    Expected<replay::Replay> rp = replay::readReplay(m_recording / "replay.sbxr");
    if (!rp) {
        return std::unexpected(rp.error());
    }
    m_replay = std::make_unique<replay::Replay>(std::move(*rp));
    // 로컬 서버의 재생과 같은 입력 — 엔진 컴포넌트 + 리플레이의 팩 (라이브 월드의 콘텐츠 · 오버레이와 따로)
    m_catalog = std::make_unique<ecs::ComponentCatalog>();
    if (Expected<void> r = comp::registerCoreComponents(*m_catalog); !r) {
        close();
        return std::unexpected(r.error());
    }
    Expected<content::ContentDatabase> db = content::loadContent(m_contentRoot, m_replay->header.packs, *m_catalog);
    if (!db) {
        close();
        return std::unexpected(db.error());
    }
    m_content = std::make_unique<content::ContentDatabase>(std::move(*db));
    Expected<persist::LoadResult> loaded =
        persist::loadWorld(*m_catalog, *m_content, m_recording / m_replay->header.startWorld);
    if (!loaded) {
        close();
        return std::unexpected(loaded.error());
    }
    m_world = std::move(loaded->world);
    Expected<std::unique_ptr<script::ScriptDebugSession>> s = script::ScriptDebugSession::create(*m_world, *m_replay);
    if (!s) {
        close();
        return std::unexpected(s.error());
    }
    m_session = std::move(*s);
    // 세션은 새로 — 남아 있던 중단점 · 감시를 모두 넣는다 (Ready)
    m_pending.clear();
    for (DebugBreakpoint& b : m_breakpoints) {
        b.sites = m_session->setBreakpoint(b.pack, b.file, b.line, b.log);
    }
    m_session->setWatches(m_watches);
    return {};
}

void ScriptDebugService::start(std::optional<sim::Tick> stopAtTick) {
    if (!m_session || m_session->state() != script::DebugSessionState::Ready) {
        return;
    }
    m_session->setStopAtTick(stopAtTick);
    m_session->start();
}

bool ScriptDebugService::canEdit() const {
    if (!m_session) {
        return false;
    }
    const script::DebugSessionState st = m_session->state();
    return st == script::DebugSessionState::Ready || st == script::DebugSessionState::Paused;
}

void ScriptDebugService::apply(Pending& p) {
    if (p.set) {
        const usize sites = m_session->setBreakpoint(p.bp.pack, p.bp.file, p.bp.line, p.bp.log);
        if (DebugBreakpoint* b = find(p.bp.pack, p.bp.file, p.bp.line)) {
            b->sites = sites;
        }
    } else {
        (void)m_session->clearBreakpoint(p.bp.pack, p.bp.file, p.bp.line);
    }
}

void ScriptDebugService::update() {
    if (m_pending.empty() || !canEdit()) {
        return;
    }
    for (Pending& p : m_pending) {
        apply(p);
    }
    m_pending.clear();
}

DebugBreakpoint* ScriptDebugService::find(std::string_view pack, std::string_view file, u32 line) {
    const auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(), [&](const DebugBreakpoint& b) {
        return b.pack == pack && b.file == file && b.line == line;
    });
    return it != m_breakpoints.end() ? &*it : nullptr;
}

void ScriptDebugService::setBreakpoint(std::string_view pack, std::string_view file, u32 line, std::string log) {
    DebugBreakpoint* b = find(pack, file, line);
    if (b == nullptr) {
        m_breakpoints.push_back({std::string(pack), std::string(file), line, {}, 0});
        b = &m_breakpoints.back();
    }
    b->log = std::move(log);
    Pending p{true, *b};
    if (canEdit()) {
        apply(p);
    } else if (m_session) {
        m_pending.push_back(std::move(p)); // 재생이 도는 중 — 멈추면 넣는다
    }
}

bool ScriptDebugService::removeBreakpoint(std::string_view pack, std::string_view file, u32 line) {
    const auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(), [&](const DebugBreakpoint& b) {
        return b.pack == pack && b.file == file && b.line == line;
    });
    if (it == m_breakpoints.end()) {
        return false;
    }
    Pending p{false, *it};
    m_breakpoints.erase(it);
    if (canEdit()) {
        apply(p);
    } else if (m_session) {
        m_pending.push_back(std::move(p));
    }
    return true;
}

bool ScriptDebugService::toggleBreakpoint(std::string_view pack, std::string_view file, u32 line) {
    if (removeBreakpoint(pack, file, line)) {
        return false;
    }
    setBreakpoint(pack, file, line);
    return true;
}

std::vector<u32> ScriptDebugService::breakpointLines(std::string_view pack, std::string_view file) const {
    std::vector<u32> out;
    for (const DebugBreakpoint& b : m_breakpoints) {
        if (b.pack == pack && b.file == file) {
            out.push_back(b.line);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void ScriptDebugService::setWatches(std::vector<std::string> watches) {
    m_watches = std::move(watches);
    if (m_session) {
        m_session->setWatches(m_watches); // 세션이 잠금으로 지킨다 — 멈췄으면 지금 스냅숏도 다시
    }
}

std::optional<DebugLocation> ScriptDebugService::pausedAt() const {
    if (!m_session) {
        return std::nullopt;
    }
    const std::optional<script::DebugSnapshot> s = m_session->snapshot();
    if (!s) {
        return std::nullopt;
    }
    return DebugLocation{s->pack, s->file, s->line};
}

} // namespace sbx::editor::services
