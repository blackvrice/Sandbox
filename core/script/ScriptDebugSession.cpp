#include "core/script/ScriptDebugSession.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "core/script/ScriptSystemAdapter.hpp"

namespace sbx::script {

std::string_view debugSessionStateName(DebugSessionState s) noexcept {
    switch (s) {
    case DebugSessionState::Ready:
        return "준비";
    case DebugSessionState::Running:
        return "실행 중";
    case DebugSessionState::Paused:
        return "멈춤";
    case DebugSessionState::Finished:
        return "끝";
    case DebugSessionState::Failed:
        return "실패";
    }
    return "?";
}

Expected<std::unique_ptr<ScriptDebugSession>> ScriptDebugSession::create(sim::SimulationWorld& world,
                                                                         const replay::Replay& replay) {
    ScriptSystemAdapter* adapter = world.scriptSystems();
    if (adapter == nullptr || !adapter->programs()) {
        return makeError(ErrorCode::InvalidArgument, "이 월드에는 서버 스크립트 시스템이 없다 — 디버그할 것이 없다");
    }
    Expected<std::unique_ptr<replay::ReplayPlayer>> player = replay::ReplayPlayer::start(world, replay);
    if (!player) {
        return std::unexpected(player.error());
    }
    std::unique_ptr<ScriptDebugSession> s(new ScriptDebugSession(world, replay));
    s->m_player = std::move(*player);
    const content::ScriptPrograms& programs = *adapter->programs();
    for (usize pi = 0; pi < programs.packs.size(); ++pi) {
        const bc::Module& module = programs.packs[pi].module;
        const bool server = std::any_of(module.systems.begin(), module.systems.end(),
                                        [](const bc::SystemDesc& d) { return d.domain == api::HostDomain::Server; });
        if (!server) {
            continue; // 클라이언트 시스템만 — 재생 (서버) 에서 돌지 않는다
        }
        bc::Module* copy = adapter->debugModule(pi);
        if (copy == nullptr) {
            continue;
        }
        PackDebugger d{programs.packs[pi].pack, pi, std::make_unique<vm::Debugger>(*copy)};
        const usize k = s->m_debuggers.size();
        ScriptDebugSession* self = s.get();
        d.debugger->setStopHandler([self, k](const vm::DebugStop& stop) { return self->onStop(stop, k); });
        adapter->attachDebugger(pi, d.debugger.get(), &d.debugger->patches());
        s->m_debuggers.push_back(std::move(d));
    }
    if (s->m_debuggers.empty()) {
        return makeError(ErrorCode::InvalidArgument, "서버 스크립트 시스템이 있는 팩이 없다");
    }
    return s;
}

ScriptDebugSession::~ScriptDebugSession() {
    abort();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // 월드는 세션보다 오래 산다 — BRK 를 거두고 훅을 뗀다 (어댑터의 사본 Interpreter 는 그대로 원래 코드로 돈다)
    if (ScriptSystemAdapter* adapter = m_world.scriptSystems()) {
        for (PackDebugger& d : m_debuggers) {
            d.debugger->clearBreakpoints();
            d.debugger->armStep(vm::StepAction::Continue);
            adapter->attachDebugger(d.index, nullptr, nullptr);
        }
    }
}

ScriptDebugSession::PackDebugger* ScriptDebugSession::find(std::string_view pack) {
    const auto it =
        std::find_if(m_debuggers.begin(), m_debuggers.end(), [&](const PackDebugger& d) { return d.pack == pack; });
    return it != m_debuggers.end() ? &*it : nullptr;
}

std::vector<std::string> ScriptDebugSession::packs() const {
    std::vector<std::string> out;
    for (const PackDebugger& d : m_debuggers) {
        out.push_back(d.pack);
    }
    return out;
}

// --- UI 스레드 ---

usize ScriptDebugSession::setBreakpoint(std::string_view pack, std::string_view file, u32 line, std::string log) {
    PackDebugger* d = find(pack);
    if (d == nullptr) {
        return 0;
    }
    const DebugSessionState st = state();
    if (st == DebugSessionState::Ready) {
        return d->debugger->setBreakpoint(file, line, std::move(log));
    }
    usize placed = 0;
    (void)onDebugThread([&](const vm::Interpreter&) { placed = d->debugger->setBreakpoint(file, line, log); });
    return placed;
}

bool ScriptDebugSession::clearBreakpoint(std::string_view pack, std::string_view file, u32 line) {
    PackDebugger* d = find(pack);
    if (d == nullptr) {
        return false;
    }
    if (state() == DebugSessionState::Ready) {
        return d->debugger->clearBreakpoint(file, line);
    }
    bool cleared = false;
    (void)onDebugThread([&](const vm::Interpreter&) { cleared = d->debugger->clearBreakpoint(file, line); });
    return cleared;
}

void ScriptDebugSession::setWatches(std::vector<std::string> exprs) {
    {
        const std::lock_guard lock(m_mutex);
        m_watches = std::move(exprs);
    }
    // 멈춘 동안이면 지금 스냅숏의 감시도 (디버그 스레드가 — 잠금 안에서 돈다)
    (void)onDebugThread([this](const vm::Interpreter& vm) {
        if (m_snapshot && vm.frameCount() > 0) {
            m_snapshot->watches.clear();
            for (const std::string& w : m_watches) {
                m_snapshot->watches.push_back(vm::evaluateWatch(vm, vm.frameCount() - 1, w));
            }
        }
    });
}

void ScriptDebugSession::setStopAtTick(std::optional<sim::Tick> tick) {
    const std::lock_guard lock(m_mutex);
    if (m_state == DebugSessionState::Ready) {
        m_stopAtTick = tick;
    }
}

void ScriptDebugSession::start() {
    const std::lock_guard lock(m_mutex);
    if (m_state != DebugSessionState::Ready) {
        return;
    }
    m_state = DebugSessionState::Running;
    m_thread = std::thread([this] { run(); });
}

DebugSessionState ScriptDebugSession::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mutex);
    m_cv.wait_for(lock, timeout, [this] { return m_state != DebugSessionState::Running; });
    return m_state;
}

DebugSessionState ScriptDebugSession::state() const {
    const std::lock_guard lock(m_mutex);
    return m_state;
}

std::optional<DebugSnapshot> ScriptDebugSession::snapshot() const {
    const std::lock_guard lock(m_mutex);
    return m_state == DebugSessionState::Paused ? m_snapshot : std::nullopt;
}

void ScriptDebugSession::resume(vm::StepAction action) {
    const std::lock_guard lock(m_mutex);
    if (m_state != DebugSessionState::Paused) {
        return;
    }
    m_resume = action;
    m_state = DebugSessionState::Running; // wait() 가 곧바로 Paused 를 보지 않게 (디버그 스레드가 이어서 돈다)
    m_snapshot.reset();
    m_cv.notify_all();
}

vm::WatchResult ScriptDebugSession::evaluate(usize frame, std::string expr) {
    vm::WatchResult r;
    r.var.name = expr;
    r.var.value = "멈춘 동안에만";
    (void)onDebugThread([&](const vm::Interpreter& vm) {
        if (frame >= vm.frameCount()) {
            r.var.value = std::format("프레임 {} 이 없다 ({} 개)", frame, vm.frameCount());
            return;
        }
        r = vm::evaluateWatch(vm, vm.frameCount() - 1 - frame, expr);
    });
    return r;
}

void ScriptDebugSession::abort() {
    const std::lock_guard lock(m_mutex);
    m_abort = true;
    if (m_state == DebugSessionState::Paused) {
        m_state = DebugSessionState::Running; // 디버그 스레드가 이어서 끝낸다
        m_snapshot.reset();
    }
    m_cv.notify_all();
}

bool ScriptDebugSession::aborted() const {
    const std::lock_guard lock(m_mutex);
    return m_abort;
}

replay::PlaybackResult ScriptDebugSession::result() const {
    const std::lock_guard lock(m_mutex);
    return m_result;
}

u64 ScriptDebugSession::finalHash() const {
    const std::lock_guard lock(m_mutex);
    return m_finalHash;
}

std::string ScriptDebugSession::error() const {
    const std::lock_guard lock(m_mutex);
    return m_error;
}

std::vector<std::string> ScriptDebugSession::logs() const {
    const std::lock_guard lock(m_mutex);
    return m_logs;
}

bool ScriptDebugSession::onDebugThread(const std::function<void(const vm::Interpreter&)>& task) {
    std::unique_lock lock(m_mutex);
    if (m_state != DebugSessionState::Paused) {
        return false;
    }
    m_tasks.push_back(task);
    const u64 ticket = ++m_tasksPosted;
    m_cv.notify_all();
    m_cv.wait(lock, [&] {
        return m_tasksDone >= ticket || m_state == DebugSessionState::Finished || m_state == DebugSessionState::Failed;
    });
    return m_tasksDone >= ticket;
}

// --- 디버그 스레드 ---

void ScriptDebugSession::collectLogs() {
    m_logs.clear();
    for (const PackDebugger& d : m_debuggers) {
        m_logs.insert(m_logs.end(), d.debugger->logs().begin(), d.debugger->logs().end());
    }
}

void ScriptDebugSession::run() {
    for (;;) {
        bool armStop = false;
        {
            const std::lock_guard lock(m_mutex);
            if (m_abort || m_player->done()) {
                break;
            }
            m_call = m_player->call();
            // 그 틱의 첫 스크립트 줄에서 — tick() 이 틱을 올리고 그 틱의 시스템을 돌린다
            if (m_stopAtTick && m_world.currentTick() + 1 >= *m_stopAtTick) {
                m_stopAtTick.reset();
                armStop = true;
            }
        }
        if (armStop) {
            for (PackDebugger& d : m_debuggers) {
                d.debugger->armStep(vm::StepAction::Into);
            }
        }
        if (Expected<void> r = m_player->step(); !r) {
            const std::lock_guard lock(m_mutex);
            m_error = r.error().describe();
            m_result = m_player->result();
            collectLogs();
            m_state = DebugSessionState::Failed;
            m_cv.notify_all();
            return;
        }
    }
    const std::lock_guard lock(m_mutex);
    m_result = m_player->result();
    m_finalHash = m_world.worldHash().value_or(0);
    collectLogs();
    m_state = DebugSessionState::Finished;
    m_snapshot.reset();
    m_cv.notify_all();
}

vm::StepAction ScriptDebugSession::onStop(const vm::DebugStop& s, usize pack) {
    std::unique_lock lock(m_mutex);
    if (m_abort) {
        return vm::StepAction::Continue;
    }
    // 다른 팩에 걸어 둔 단계 (stopAtTick) 는 거둔다 — 멈춤은 하나
    for (usize k = 0; k < m_debuggers.size(); ++k) {
        if (k != pack) {
            m_debuggers[k].debugger->armStep(vm::StepAction::Continue);
        }
    }
    const vm::Interpreter& vm = *s.vm;
    const bc::Module& module = vm.module();
    DebugSnapshot snap;
    snap.stop = ++m_stops;
    snap.call = m_call;
    snap.tick = m_world.currentTick();
    snap.pack = m_debuggers[pack].pack;
    snap.reason = s.reason;
    snap.file = s.file;
    snap.line = s.line;
    snap.column = s.column;
    const usize depth = vm.frameCount();
    for (usize k = 0; k < depth; ++k) {
        const usize i = depth - 1 - k; // 안쪽부터
        const vm::FrameInfo fi = vm.frameAt(i);
        const bc::Function& fn = module.functions[fi.function];
        const bc::LineEntry* at = bc::findLine(fn, fi.pc);
        snap.frames.push_back({std::string(bc::functionShortName(fn.name)),
                               bc::describeSite(module, fi.function, fi.pc), at != nullptr ? at->line : 0,
                               vm::frameLocals(vm, i)});
    }
    for (const std::string& w : m_watches) {
        snap.watches.push_back(vm::evaluateWatch(vm, depth - 1, w));
    }
    m_snapshot = std::move(snap);
    m_resume.reset();
    collectLogs();
    m_state = DebugSessionState::Paused;
    m_cv.notify_all();
    // 멈춘 동안: UI 의 일 (감시 식 · 중단점) 을 여기서 — 이 스레드만 VM 을 읽는다
    for (;;) {
        m_cv.wait(lock, [this] { return m_resume.has_value() || !m_tasks.empty() || m_abort; });
        while (!m_tasks.empty()) {
            const auto task = std::move(m_tasks.front());
            m_tasks.erase(m_tasks.begin());
            task(vm);
            ++m_tasksDone;
            m_cv.notify_all();
        }
        if (m_abort) {
            return vm::StepAction::Continue;
        }
        if (m_resume) {
            const vm::StepAction a = *m_resume;
            m_resume.reset();
            return a;
        }
    }
}

} // namespace sbx::script
