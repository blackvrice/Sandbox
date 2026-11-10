#include "apps/client/studio/StudioShell.hpp"

#include <algorithm>
#include <array>
#include <format>

#include <imgui.h>
#include <imgui_internal.h>

#include "editor/ui/StudioWidgets.hpp"

namespace sbx::client {

namespace {

namespace th = editor::theme;
namespace ui = editor::ui;

// 도크 창 이름 (창 제목 그대로 — "###id" 가 붙은 창은 id 로)
constexpr const char* kHierarchy = "목록";
constexpr const char* kWorld = "###world";
constexpr const char* kInspector = "인스펙터";
constexpr const char* kTools = "편집";
constexpr const char* kConsole = "콘솔";
constexpr const char* kContent = "콘텐츠";
constexpr const char* kBuild = "빌드";
constexpr const char* kTest = "테스트";
constexpr const char* kGame = "###game";
constexpr const char* kActions = "###actions";
constexpr const char* kNotices = "알림";

float uiScale() {
    return ImGui::GetStyle().FontScaleDpi > 0.f ? ImGui::GetStyle().FontScaleDpi : 1.f;
}

// 배치 프리셋 (29 5장) — 비율이라 화면 크기 · DPI 와 무관
void buildPreset(ImGuiID dock, std::string_view name, const ImGuiViewport* vp) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dock, vp->WorkPos);
    ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
    ImGuiID center = dock;
    const auto split = [&](ImGuiDir dir, float ratio) {
        return ImGui::DockBuilderSplitNode(center, dir, ratio, nullptr, &center);
    };
    if (name == "scripting") {
        const ImGuiID left = split(ImGuiDir_Left, 0.16f);
        const ImGuiID right = split(ImGuiDir_Right, 0.22f / 0.84f);
        const ImGuiID bottom = split(ImGuiDir_Down, 0.48f);
        ImGui::DockBuilderDockWindow(kHierarchy, left);
        ImGui::DockBuilderDockWindow(kInspector, right);
        ImGui::DockBuilderDockWindow(kTools, right);
        for (const char* w : {kContent, kConsole, kBuild, kNotices}) {
            ImGui::DockBuilderDockWindow(w, bottom);
        }
    } else if (name == "play") {
        const ImGuiID right = split(ImGuiDir_Right, 0.24f);
        const ImGuiID bottom = split(ImGuiDir_Down, 0.22f);
        for (const char* w : {kGame, kActions, kWorld}) {
            ImGui::DockBuilderDockWindow(w, right);
        }
        for (const char* w : {kConsole, kTest, kNotices}) {
            ImGui::DockBuilderDockWindow(w, bottom);
        }
    } else { // scene (기본)
        const ImGuiID left = split(ImGuiDir_Left, 0.20f);
        const ImGuiID right = split(ImGuiDir_Right, 0.24f / 0.80f);
        const ImGuiID bottom = split(ImGuiDir_Down, 0.26f);
        ImGuiID rightBottom = 0;
        const ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.62f, nullptr, &rightBottom);
        ImGui::DockBuilderDockWindow(kHierarchy, left);
        ImGui::DockBuilderDockWindow(kWorld, left);
        ImGui::DockBuilderDockWindow(kInspector, rightTop);
        ImGui::DockBuilderDockWindow(kTools, rightBottom);
        for (const char* w : {kConsole, kContent, kBuild, kTest, kGame, kActions, kNotices}) {
            ImGui::DockBuilderDockWindow(w, bottom);
        }
    }
    ImGui::DockBuilderFinish(dock);
}

bool menuItem(const char* label, const char* shortcut, bool enabled = true, bool selected = false) {
    return ImGui::MenuItem(label, shortcut, selected, enabled);
}

void mainMenu(ShellState& st, const ShellInputs& in, ShellActions& act) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().accent);
    ImGui::TextUnformatted("Sandbox Studio");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(th::space(th::Space::Md, uiScale()), 0.f));
    if (ImGui::BeginMenu("파일")) {
        if (menuItem("월드 저장", "Ctrl+S", in.canSave)) {
            act.save = true;
        }
        if (menuItem("레이아웃 저장", nullptr, in.layoutsEnabled)) {
            act.command = "studio.save-layout";
        }
        ImGui::Separator();
        if (menuItem("끝내기", "Alt+F4")) {
            act.quit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("편집")) {
        const std::string undo = std::format("되돌리기 {}", in.undoLabel);
        const std::string redo = std::format("다시 하기 {}", in.redoLabel);
        if (menuItem(undo.c_str(), "Ctrl+Z", in.hasEditor && in.canUndo)) {
            act.undo = true;
        }
        if (menuItem(redo.c_str(), "Ctrl+Y", in.hasEditor && in.canRedo)) {
            act.redo = true;
        }
        ImGui::Separator();
        if (menuItem("선택 해제", "Esc", in.selected > 0)) {
            act.clearSelection = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("보기")) {
        ImGui::SeparatorText("창");
        ImGui::MenuItem("목록 (Hierarchy)", nullptr, &st.view.hierarchy);
        ImGui::MenuItem("인스펙터", nullptr, &st.view.inspector);
        ImGui::MenuItem("편집 도구 설정", nullptr, &st.view.tools);
        ImGui::MenuItem("콘텐츠 (Prefab · 규칙 · 스크립트)", nullptr, &st.view.content);
        ImGui::MenuItem("콘솔", nullptr, &st.view.console);
        ImGui::MenuItem("빌드", nullptr, &st.view.build);
        ImGui::MenuItem("테스트 세션", nullptr, &st.view.test);
        ImGui::MenuItem("월드 (세이브 · 리플레이)", nullptr, &st.view.world);
        ImGui::MenuItem("게임 · 행동", nullptr, &st.view.game);
        if (menuItem("알림", nullptr, true, in.notices)) {
            act.toggleNotices = true;
        }
        ImGui::SeparatorText("배치");
        for (const std::string& name : in.layouts) {
            const std::string label = name == "default" ? std::string("default (scene)") : name;
            if (menuItem(label.c_str(), nullptr, true, name == in.layout) && name != in.layout) {
                act.layout = name;
            }
        }
        for (const char* preset : {"scene", "scripting", "play"}) {
            if (std::ranges::find(in.layouts, preset) == in.layouts.end() && menuItem(preset, nullptr)) {
                act.layout = std::string(preset);
            }
        }
        if (menuItem("기본 배치로 되돌리기", nullptr)) {
            act.resetLayout = true;
        }
        ImGui::SeparatorText("월드 그림");
        if (menuItem("격자", "G", true, in.grid)) {
            act.grid = !in.grid;
        }
        if (menuItem("선택한 개체 자세히", "V", true, in.details)) {
            act.details = !in.details;
        }
        if (menuItem("월드 맞춤", "Home")) {
            act.fitCamera = true;
        }
        ImGui::SeparatorText("화면");
        if (menuItem("보통 간격", nullptr, true, st.density == th::Density::Comfortable) &&
            st.density != th::Density::Comfortable) {
            st.density = th::Density::Comfortable;
            act.densityChanged = true;
        }
        if (menuItem("촘촘한 간격", nullptr, true, st.density == th::Density::Compact) &&
            st.density != th::Density::Compact) {
            st.density = th::Density::Compact;
            act.densityChanged = true;
        }
        ImGui::MenuItem("개발자 창 (진단)", nullptr, &st.developerPanels);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("프로젝트")) {
        if (menuItem("검사", nullptr, in.canBuild)) {
            act.command = "studio.check";
        }
        if (menuItem("빌드", "Ctrl+B", in.canBuild)) {
            act.command = "studio.build";
        }
        ImGui::Separator();
        if (menuItem("서버 + 클라이언트 테스트", nullptr, in.canTest)) {
            act.command = "studio.test-start";
        }
        if (menuItem("테스트 모두 끝내기", nullptr, in.testRunning)) {
            act.command = "studio.test-stop";
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("도움말")) {
        ImGui::SeparatorText("단축키");
        for (const auto& [k, v] :
             std::array<std::pair<const char*, const char*>, 9>{{{"1 ~ 5", "툴 (선택 · 이동 · 배치 · 지형 · 지우기)"},
                                                                 {"Space · .", "일시정지 · 한 틱"},
                                                                 {"Ctrl+Z · Ctrl+Y", "되돌리기 · 다시 하기"},
                                                                 {"Ctrl+S", "월드 저장"},
                                                                 {"Ctrl+B", "빌드"},
                                                                 {"Delete", "선택 지우기"},
                                                                 {"G · V · Home", "격자 · 자세히 · 월드 맞춤"},
                                                                 {"F1", "창 모두 숨기기 · 보이기"},
                                                                 {"휠 · 가운데 끌기", "확대 · 이동"}}}) {
            ImGui::MenuItem(v, k, false, false);
        }
        ImGui::Separator();
        const std::string ver = std::format("Sandbox {}", in.version);
        ImGui::MenuItem(ver.c_str(), nullptr, false, false);
        ImGui::EndMenu();
    }
    // 오른쪽: 프로젝트 이름
    const std::string title = in.project.empty() ? in.world : in.project;
    const float w = ImGui::CalcTextSize(title.c_str()).x + th::space(th::Space::Lg, uiScale());
    if (ImGui::GetContentRegionAvail().x > w) {
        ImGui::SameLine(ImGui::GetWindowWidth() - w);
        ImGui::TextDisabled("%s", title.c_str());
    }
    ImGui::EndMainMenuBar();
}

void toolbar(const ShellInputs& in, ShellActions& act) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight() + th::space(th::Space::Md, uiScale()) * 2.f;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::tokens().background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(th::space(th::Space::Md, uiScale()), th::space(th::Space::Md, uiScale())));
    if (ImGui::BeginViewportSideBar("##studio-toolbar", vp, ImGuiDir_Up, h, flags)) {
        // 툴 (Editor — 쓸 수 없는 툴은 끈다)
        constexpr std::array<const char*, 5> kToolLabels{"선택", "이동", "배치", "지형", "지우기"};
        for (usize i = 0; i < kToolLabels.size(); ++i) {
            const auto t = static_cast<editor::Tool>(i);
            const bool allowed = in.hasEditor && (in.toolMask & (1u << i)) != 0;
            const std::string tip = std::format("{} 툴 ({})", kToolLabels[i], i + 1);
            if (ui::toolbarToggle(kToolLabels[i], in.hasEditor && in.tool == t, tip, !allowed)) {
                act.tool = t;
            }
            ImGui::SameLine();
        }
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ui::toolbarToggle("되돌리기", false, "Ctrl+Z", !in.canUndo)) {
            act.undo = true;
        }
        ImGui::SameLine();
        if (ui::toolbarToggle("다시", false, "Ctrl+Y", !in.canRedo)) {
            act.redo = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        // 플레이 (테스트 플레이 — 편집 월드의 복사본, 멈추면 편집 상태 그대로)
        if (in.mode == WorkspaceMode::TestPlay) {
            if (ui::primaryButton("■ 멈춤")) {
                act.stop = true;
            }
            ui::tooltip("테스트 플레이를 멈추고 편집 월드로 돌아간다 (플레이 중 바뀐 것은 버린다)");
        } else if (ui::primaryButton("▶ 플레이", !in.canPlay)) {
            act.play = true;
        } else {
            ui::tooltip(in.canPlay ? "편집 월드의 복사본으로 플레이한다 — 멈추면 편집 상태로"
                                   : "지금은 플레이할 수 없다 (기록 · 재생 중이거나 admin 아래 역할)");
        }
        ImGui::SameLine();
        if (ui::toolbarToggle(in.paused ? "재개" : "일시정지", in.paused, "Space", !in.canControl)) {
            act.togglePause = true;
        }
        ImGui::SameLine();
        if (ui::toolbarToggle("한 틱", false, ". (일시정지 중)", !in.canControl || !in.paused)) {
            act.step = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ui::toolbarToggle("저장", false, "Ctrl+S — 월드 창의 이름으로", !in.canSave)) {
            act.save = true;
        }
        ImGui::SameLine();
        if (ui::toolbarToggle(in.buildRunning ? "빌드 중…" : "빌드", in.buildRunning, "Ctrl+B — 검사 뒤 패키지",
                              !in.canBuild)) {
            act.command = "studio.build";
        }
        ImGui::SameLine();
        if (in.testRunning) {
            if (ui::toolbarToggle("테스트 끝내기", true, "서버 · 클라이언트 프로세스를 모두 끝낸다")) {
                act.command = "studio.test-stop";
            }
        } else if (ui::toolbarToggle("서버 + 클라이언트", false, "테스트 창의 값으로 서버 + N 클라이언트를 띄운다",
                                     !in.canTest)) {
            act.command = "studio.test-start";
        }
        // 오른쪽: 모드 배지
        const std::string_view mode = workspaceModeName(in.mode);
        const float bw = ImGui::CalcTextSize(mode.data(), mode.data() + mode.size()).x + 24.f * uiScale();
        if (ImGui::GetContentRegionAvail().x > bw + 8.f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - bw - th::space(th::Space::Md, uiScale()));
            ui::badge(mode, in.mode == WorkspaceMode::Edit ? th::Status::Muted : th::Status::Accent);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void statusBar(const ShellInputs& in) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, th::tokens().background);
    if (ImGui::BeginViewportSideBar("##studio-status", vp, ImGuiDir_Down, h, flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::TextDisabled("%s", (in.project.empty() ? in.world : in.project).c_str());
            ImGui::TextDisabled("· 배치 %s", in.layout.c_str());
            ImGui::TextDisabled("· 선택 %zu", in.selected);
            ImGui::TextDisabled("· tick %llu", static_cast<unsigned long long>(in.tick));
            if (in.paused) {
                ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().warning);
                ImGui::TextUnformatted("· 일시정지");
                ImGui::PopStyleColor();
            } else if (in.tps > 0) {
                ImGui::TextDisabled("· %.0f TPS · ×%g", in.tps, static_cast<double>(in.speed));
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

// 가운데 노드 (월드 그림) 의 모드 띠 · 플레이 중 테두리 — 창 아래 · 월드 위 (배경 목록)
void modeBand(ImGuiID dock, const ShellInputs& in) {
    const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock);
    if (central == nullptr) {
        return;
    }
    const ImVec2 p0 = central->Pos;
    const ImVec2 p1(central->Pos.x + central->Size.x, central->Pos.y + central->Size.y);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const th::Tokens& t = th::tokens();
    const bool live = in.mode != WorkspaceMode::Edit;
    const std::string_view mode = workspaceModeName(in.mode);
    const float s = uiScale();
    const ImVec2 pad(10.f * s, 4.f * s);
    const ImVec2 ts = ImGui::CalcTextSize(mode.data(), mode.data() + mode.size());
    const ImVec2 b0(p0.x + 8.f * s, p0.y + 8.f * s);
    const ImVec2 b1(b0.x + ts.x + pad.x * 2.f, b0.y + ts.y + pad.y * 2.f);
    ImVec4 bg = live ? t.accent : t.surface;
    bg.w = live ? 0.92f : 0.85f;
    dl->AddRectFilled(b0, b1, th::toU32(bg), 4.f * s);
    dl->AddText(ImVec2(b0.x + pad.x, b0.y + pad.y), th::toU32(live ? t.accentText : t.text), mode.data(),
                mode.data() + mode.size());
    if (live) {
        dl->AddRect(p0, p1, th::toU32(t.accent), 0.f, 2.f * s);
    }
}

} // namespace

std::string_view workspaceModeName(WorkspaceMode m) noexcept {
    switch (m) {
    case WorkspaceMode::TestPlay:
        return "Game — 테스트 플레이";
    case WorkspaceMode::Replay:
        return "재생 — 리플레이";
    case WorkspaceMode::Recording:
        return "Scene — 편집 (기록 중)";
    case WorkspaceMode::Edit:
        break;
    }
    return "Scene — 편집";
}

bool isPresetLayout(std::string_view name) noexcept {
    return name == "default" || name == "scene" || name == "scripting" || name == "play";
}

std::string_view presetFor(std::string_view layoutName) noexcept {
    if (layoutName == "scripting" || layoutName == "play") {
        return layoutName;
    }
    return "scene";
}

ShellActions drawStudioShell(ShellState& state, const ShellInputs& in) {
    ShellActions act;
    mainMenu(state, in, act);
    toolbar(in, act);
    statusBar(in);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiID dock = ImGui::GetID("StudioDock");
    state.dockId = dock;
    if (state.pendingPreset) {
        buildPreset(dock, *state.pendingPreset, vp);
        state.pendingPreset.reset();
    }
    // 가운데는 통과 (월드 그림 · 게임 입력), 그 위에 창을 도킹하지 않는다 (Scene 이 늘 보인다)
    ImGui::DockSpaceOverViewport(dock, vp,
                                 ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);
    modeBand(dock, in);
    return act;
}

} // namespace sbx::client
