#include "editor/ui/ScriptDebugPanel.hpp"

#include <charconv>
#include <cstring>
#include <format>
#include <utility>

#include <imgui.h>

#include "editor/ui/StudioTheme.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::editor {

namespace {

using script::DebugSessionState;
using script::vm::StepAction;

[[nodiscard]] f32 dpi() {
    return ImGui::GetStyle().FontScaleDpi;
}

[[nodiscard]] std::optional<u64> parseNumber(const char* text) {
    u64 v = 0;
    const char* end = text + std::strlen(text);
    const auto [p, ec] = std::from_chars(text, end, v);
    if (ec != std::errc() || p != end || p == text) {
        return std::nullopt;
    }
    return v;
}

void drawControls(services::ScriptDebugService& svc, ScriptDebugPanelState& st) {
    script::ScriptDebugSession* s = svc.session();
    if (s == nullptr) {
        return;
    }
    const DebugSessionState state = s->state();
    switch (state) {
    case DebugSessionState::Ready:
        if (ImGui::Button("시작")) {
            const std::optional<u64> tick = st.stopTick[0] != '\0' ? parseNumber(st.stopTick.data()) : std::nullopt;
            svc.start(tick ? std::optional<sim::Tick>(*tick) : std::nullopt);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.f * dpi());
        ImGui::InputTextWithHint("##stoptick", "틱에서 멈춤", st.stopTick.data(), st.stopTick.size(),
                                 ImGuiInputTextFlags_CharsDecimal);
        break;
    case DebugSessionState::Paused:
        if (ImGui::Button("계속")) {
            s->resume(StepAction::Continue);
        }
        ImGui::SameLine();
        if (ImGui::Button("한 줄")) {
            s->resume(StepAction::Over);
        }
        ImGui::SameLine();
        if (ImGui::Button("들어가기")) {
            s->resume(StepAction::Into);
        }
        ImGui::SameLine();
        if (ImGui::Button("나가기")) {
            s->resume(StepAction::Out);
        }
        ImGui::SameLine();
        if (ImGui::Button("그만")) {
            s->abort();
        }
        break;
    case DebugSessionState::Running:
        ImGui::TextDisabled("재생 중…");
        ImGui::SameLine();
        if (ImGui::Button("그만")) {
            s->abort();
        }
        break;
    case DebugSessionState::Finished:
    case DebugSessionState::Failed:
        break;
    }
    ImGui::SameLine();
    if (ImGui::Button("처음부터")) {
        if (Expected<void> r = svc.restart(); !r) {
            st.message = r.error().describe();
        } else {
            st.message.clear();
        }
    }
}

void drawResult(const script::ScriptDebugSession& s) {
    const theme::Tokens& tk = theme::tokens();
    const replay::PlaybackResult r = s.result();
    if (s.state() == DebugSessionState::Failed) {
        ImGui::TextColored(tk.error, "재생 실패: %s", s.error().c_str());
        return;
    }
    if (s.aborted()) {
        ImGui::TextDisabled("그쳤다 — call %llu 까지 재생", static_cast<unsigned long long>(r.calls));
        return;
    }
    if (r.mismatch) {
        ImGui::TextColored(tk.error, "해시가 다르다 — call %llu (tick %llu)",
                           static_cast<unsigned long long>(r.mismatch->call),
                           static_cast<unsigned long long>(r.mismatch->tick));
    } else {
        ImGui::TextColored(tk.success, "끝 — call %llu · 해시 기록 %llu 개 일치 · 끝 해시 %016llx",
                           static_cast<unsigned long long>(r.calls), static_cast<unsigned long long>(r.hashesCompared),
                           static_cast<unsigned long long>(s.finalHash()));
    }
}

void drawVariables(const std::vector<script::vm::DebugVariable>& vars) {
    if (vars.empty()) {
        ImGui::TextDisabled("(없음)");
        return;
    }
    if (ImGui::BeginTable("##vars", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("이름");
        ImGui::TableSetupColumn("타입");
        ImGui::TableSetupColumn("값", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const auto& v : vars) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", v.type.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", v.value.c_str());
        }
        ImGui::EndTable();
    }
}

} // namespace

ScriptDebugPanelResult drawScriptDebugPanel(services::ScriptDebugService& svc, ScriptDebugPanelState& st,
                                            const std::filesystem::path& savesRoot,
                                            const std::filesystem::path& contentRoot, bool& open) {
    ScriptDebugPanelResult out;
    if (!open) {
        st.wasOpen = false;
        return out;
    }
    if (!std::exchange(st.wasOpen, true)) {
        ImGui::SetNextWindowFocus();
        st.scanned = false; // 열 때마다 기록을 다시 찾는다
    }
    ImGui::SetNextWindowSize({520.f * dpi(), 560.f * dpi()}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("스크립트 디버거", &open)) {
        ImGui::End();
        return out;
    }
    svc.update();
    const theme::Tokens& tk = theme::tokens();

    // 기록
    if (!st.scanned) {
        st.recordings = services::listRecordings(savesRoot);
        st.scanned = true;
        st.selected = std::min(st.selected, st.recordings.empty() ? usize{0} : st.recordings.size() - 1);
    }
    if (st.recordings.empty()) {
        ImGui::TextDisabled(
            "기록이 없습니다 — 테스트 플레이를 기록하거나 (--record) 세이브 폴더에 start/ + replay.sbxr");
    } else {
        ImGui::SetNextItemWidth(220.f * dpi());
        if (ImGui::BeginCombo("기록", st.recordings[st.selected].name.c_str())) {
            for (usize i = 0; i < st.recordings.size(); ++i) {
                if (ImGui::Selectable(st.recordings[i].name.c_str(), i == st.selected)) {
                    st.selected = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("열기")) {
            if (Expected<void> r = svc.open(st.recordings[st.selected].dir, contentRoot); !r) {
                st.message = r.error().describe();
            } else {
                st.message.clear();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("다시 찾기")) {
        st.scanned = false;
    }
    if (!st.message.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(tk.error, "%s", st.message.c_str());
        ImGui::PopTextWrapPos();
    }
    script::ScriptDebugSession* s = svc.session();
    if (s == nullptr) {
        ImGui::TextDisabled(
            "기록을 열면 그 시작 세이브에서 서버 스크립트를 다시 돌리며 중단점에서 멈춘다 (라이브 월드는 그대로)");
        ImGui::End();
        return out;
    }
    ImGui::TextDisabled("%s · call %llu · %s", io::displayPath(svc.recording().filename()).c_str(),
                        static_cast<unsigned long long>(svc.calls()),
                        std::string(script::debugSessionStateName(s->state())).c_str());
    drawControls(svc, st);
    s = svc.session(); // 처음부터 — 새 세션
    if (s == nullptr) {
        ImGui::End();
        return out;
    }
    const DebugSessionState state = s->state();
    if (state == DebugSessionState::Finished || state == DebugSessionState::Failed) {
        drawResult(*s);
    }

    const std::optional<script::DebugSnapshot> snap = s->snapshot();
    if (snap) {
        if (snap->stop != st.frameStop) {
            st.frameStop = snap->stop;
            st.frame = 0;
            st.frameWatchKey = static_cast<usize>(-1);
        }
        ImGui::Separator();
        ImGui::TextColored(tk.accent, "%s %s:%u (팩 %s) — tick %llu · call %llu",
                           snap->reason == script::vm::StopReason::Breakpoint ? "중단점" : "단계", snap->file.c_str(),
                           snap->line, snap->pack.c_str(), static_cast<unsigned long long>(snap->tick),
                           static_cast<unsigned long long>(snap->call));
        ImGui::SameLine();
        if (ImGui::SmallButton("편집기에서 보기")) {
            out.showInEditor = services::DebugLocation{snap->pack, snap->file, snap->line};
        }
        if (ImGui::CollapsingHeader("호출 스택", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (usize i = 0; i < snap->frames.size(); ++i) {
                const std::string label = std::format("{}##frame{}", snap->frames[i].site, i);
                if (ImGui::Selectable(label.c_str(), i == st.frame)) {
                    st.frame = i;
                }
            }
        }
        st.frame = std::min(st.frame, snap->frames.empty() ? usize{0} : snap->frames.size() - 1);
        if (ImGui::CollapsingHeader("지역", ImGuiTreeNodeFlags_DefaultOpen) && !snap->frames.empty()) {
            drawVariables(snap->frames[st.frame].locals);
        }
    }

    if (ImGui::CollapsingHeader("감시", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 안쪽 프레임은 스냅숏의 것, 다른 프레임은 디버그 스레드에 묻는다 (멈춤 · 프레임 · 식 수가 바뀔 때만)
        const std::vector<script::vm::WatchResult>* shown = snap ? &snap->watches : nullptr;
        if (snap && st.frame != 0) {
            const usize key = static_cast<usize>(snap->stop) * 4096 + st.frame * 64 + svc.watches().size();
            if (key != st.frameWatchKey) {
                st.frameWatchKey = key;
                st.frameWatches.clear();
                for (const std::string& w : svc.watches()) {
                    st.frameWatches.push_back(s->evaluate(st.frame, w));
                }
            }
            shown = &st.frameWatches;
        }
        std::optional<usize> remove;
        for (usize i = 0; i < svc.watches().size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("x")) {
                remove = i;
            }
            ImGui::SameLine();
            const std::string& expr = svc.watches()[i];
            if (shown != nullptr && i < shown->size()) {
                const script::vm::WatchResult& w = (*shown)[i];
                if (w.ok) {
                    ImGui::Text("%s = %s", expr.c_str(), w.var.value.c_str());
                } else {
                    ImGui::TextColored(tk.warning, "%s — %s", expr.c_str(), w.var.value.c_str());
                }
            } else {
                ImGui::TextDisabled("%s", expr.c_str());
            }
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(220.f * dpi());
        const bool enter = ImGui::InputTextWithHint("##watch", "m.current · xs[0] · name.length", st.watchInput.data(),
                                                    st.watchInput.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("감시 더하기") || enter) && st.watchInput[0] != '\0') {
            std::vector<std::string> w = svc.watches();
            w.emplace_back(st.watchInput.data());
            svc.setWatches(std::move(w));
            st.watchInput[0] = '\0';
            st.frameWatchKey = static_cast<usize>(-1);
        }
        if (remove) {
            std::vector<std::string> w = svc.watches();
            w.erase(w.begin() + static_cast<std::ptrdiff_t>(*remove));
            svc.setWatches(std::move(w));
            st.frameWatchKey = static_cast<usize>(-1);
        }
    }

    if (ImGui::CollapsingHeader("중단점", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::optional<services::DebugBreakpoint> remove;
        for (usize i = 0; i < svc.breakpoints().size(); ++i) {
            const services::DebugBreakpoint& b = svc.breakpoints()[i];
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("x")) {
                remove = b;
            }
            ImGui::SameLine();
            const std::string where = std::format("{} {}:{}", b.pack, b.file, b.line);
            if (b.sites == 0) {
                ImGui::TextDisabled("%s — 그 줄에 코드 없음%s", where.c_str(),
                                    svc.pendingChanges() ? " (멈추면 넣는다)" : "");
            } else if (!b.log.empty()) {
                ImGui::Text("%s — logpoint \"%s\"", where.c_str(), b.log.c_str());
            } else {
                ImGui::TextUnformatted(where.c_str());
            }
            ImGui::PopID();
        }
        if (remove) {
            (void)svc.removeBreakpoint(remove->pack, remove->file, remove->line);
        }
        ImGui::TextDisabled("편집기의 줄 번호 칸을 눌러도 놓는다");
        ImGui::SetNextItemWidth(60.f * dpi());
        ImGui::InputTextWithHint("##bppack", "팩", st.bpPack.data(), st.bpPack.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.f * dpi());
        ImGui::InputTextWithHint("##bpfile", "scripts/a.ts", st.bpFile.data(), st.bpFile.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.f * dpi());
        ImGui::InputTextWithHint("##bpline", "줄", st.bpLine.data(), st.bpLine.size(),
                                 ImGuiInputTextFlags_CharsDecimal);
        ImGui::SetNextItemWidth(220.f * dpi());
        ImGui::InputTextWithHint("##bplog", "logpoint 글 (비면 멈춤) — x={m.current}", st.bpLog.data(),
                                 st.bpLog.size());
        ImGui::SameLine();
        const std::optional<u64> line = parseNumber(st.bpLine.data());
        ImGui::BeginDisabled(st.bpPack[0] == '\0' || st.bpFile[0] == '\0' || !line || *line == 0);
        if (ImGui::Button("놓기")) {
            svc.setBreakpoint(st.bpPack.data(), st.bpFile.data(), static_cast<u32>(*line), st.bpLog.data());
        }
        ImGui::EndDisabled();
    }

    const std::vector<std::string> logs = s->logs();
    if (!logs.empty() && ImGui::CollapsingHeader("logpoint 글")) {
        ImGui::BeginChild("##logs", {0, 120.f * dpi()}, ImGuiChildFlags_Borders);
        for (const std::string& l : logs) {
            ImGui::TextUnformatted(l.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
    return out;
}

} // namespace sbx::editor
