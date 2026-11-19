#include "editor/ui/ProjectExplorerPanel.hpp"

#include <cstring>
#include <format>
#include <utility>

#include <imgui.h>

#include "core/asset/AssetDatabase.hpp"
#include "editor/ui/AssetBrowserPanel.hpp"
#include "editor/ui/StudioWidgets.hpp"

namespace sbx::editor {

namespace fs = std::filesystem;
using services::ProjectFileEntry;
using services::ProjectFileKind;

namespace {

constexpr u32 kRescanFrames = 120; // 밖 (탐색기 · 다른 편집기) 에서 바뀐 파일 — 2 초쯤마다

const char* kindTag(ProjectFileKind k) noexcept {
    switch (k) {
    case ProjectFileKind::Folder:
        return "";
    case ProjectFileKind::Script:
        return "TS";
    case ProjectFileKind::Image:
        return "IMG";
    case ProjectFileKind::Audio:
        return "SND";
    case ProjectFileKind::Json:
        return "{}";
    case ProjectFileKind::Other:
        return "·";
    }
    return "";
}

void setStatus(ProjectExplorerState& st, std::string text, bool error) {
    st.status = std::move(text);
    st.statusError = error;
}

void ask(ProjectExplorerState& st, ProjectExplorerState::Ask what, std::string target, std::string_view initial) {
    st.ask = what;
    st.askOpen = true;
    st.askTarget = std::move(target);
    st.askInput.fill('\0');
    std::memcpy(st.askInput.data(), initial.data(), std::min(initial.size(), st.askInput.size() - 1));
    st.forceTrash = false;
}

// 끌어다 놓은 것을 이 폴더로 — 파일 경로 또는 에셋 id
std::optional<ProjectExplorerAction> acceptDrop(ProjectExplorerState& st, const fs::path& projectDir,
                                                const std::string& folder) {
    if (!ImGui::BeginDragDropTarget()) {
        return std::nullopt;
    }
    std::string from;
    std::string assetId;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kFilePayload)) {
        from = static_cast<const char*>(p->Data);
    } else if (const ImGuiPayload* a = ImGui::AcceptDragDropPayload(kAssetPayload)) {
        assetId = static_cast<const char*>(a->Data);
        for (const auto& [path, id] : st.assetIds) {
            if (id == assetId) {
                from = path;
            }
        }
    }
    ImGui::EndDragDropTarget();
    if (from.empty() || from == folder) {
        return std::nullopt;
    }
    const auto r = services::moveProjectEntry(projectDir, from, folder);
    st.stale = true;
    if (!r) {
        setStatus(st, r.error().message, true);
        return std::nullopt;
    }
    setStatus(st, std::format("옮겼다: {} → {}", from, *r), false);
    st.selected = *r;
    return ProjectExplorerAction{ProjectExplorerAction::Kind::FilesChanged, *r, from, assetId};
}

void dragSource(const ProjectExplorerState& st, const ProjectFileEntry& e) {
    if (!ImGui::BeginDragDropSource()) {
        return;
    }
    if (const auto it = st.assetIds.find(e.rel); it != st.assetIds.end()) {
        ImGui::SetDragDropPayload(kAssetPayload, it->second.c_str(), it->second.size() + 1); // 월드 · UI 디자이너도 받는다
    } else {
        ImGui::SetDragDropPayload(kFilePayload, e.rel.c_str(), e.rel.size() + 1);
    }
    ImGui::TextUnformatted(e.rel.c_str());
    ImGui::EndDragDropSource();
}

void contextMenu(ProjectExplorerState& st, const ProjectFileEntry& e) {
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }
    st.selected = e.rel;
    const bool folder = e.kind == ProjectFileKind::Folder;
    const bool editable = !services::checkProjectEdit(st.project, e.rel, true).has_value();
    if (folder && ImGui::MenuItem("새 폴더…")) {
        ask(st, ProjectExplorerState::Ask::NewFolder, e.rel, "");
    }
    if (folder && canCreateScriptIn(e.rel) && ImGui::MenuItem("새 스크립트…")) {
        ask(st, ProjectExplorerState::Ask::NewScript, e.rel, "");
    }
    if (ImGui::MenuItem("이름 바꾸기…", "F2", false, editable)) {
        ask(st, ProjectExplorerState::Ask::Rename, e.rel, e.name);
    }
    if (ImGui::MenuItem("복제", nullptr, false, editable)) {
        const auto r = services::duplicateProjectEntry(st.project, e.rel);
        setStatus(st, r ? std::format("복제했다: {}", *r) : r.error().message, !r);
        st.stale = true;
    }
    if (ImGui::MenuItem("휴지통으로…", "Delete", false, editable)) {
        ask(st, ProjectExplorerState::Ask::Trash, e.rel, "");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("경로 복사")) {
        ImGui::SetClipboardText(e.rel.c_str());
    }
    ImGui::EndPopup();
}

std::optional<ProjectExplorerAction> askWindow(ProjectExplorerState& st, const fs::path& projectDir) {
    std::optional<ProjectExplorerAction> out;
    if (st.askOpen) {
        ImGui::OpenPopup("##project-ask");
        st.askOpen = false;
    }
    if (!ImGui::BeginPopupModal("##project-ask", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return out;
    }
    using Ask = ProjectExplorerState::Ask;
    if (st.ask == Ask::None) { // 밖에서 끝냈다 (applyExplorerAsk)
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return out;
    }
    bool done = false;
    if (st.ask == Ask::Trash) {
        ImGui::Text("'%s' 를 휴지통으로 옮긴다", st.askTarget.c_str());
        ImGui::TextDisabled("<프로젝트>/.sandbox/trash 에 남는다 — 되살릴 수 있다. 빌드 · 패키지에는 들어가지 않는다");
        ImGui::Checkbox("쓰는 곳 (머티리얼) 이 있는 에셋이어도", &st.forceTrash);
        if (ui::primaryButton("휴지통으로")) {
            out = applyExplorerAsk(st, projectDir);
            done = true;
        }
    } else {
        const bool folder = st.ask == Ask::NewFolder;
        const bool script = st.ask == Ask::NewScript;
        ImGui::TextUnformatted(script ? "새 스크립트 이름" : folder ? "새 폴더 이름" : "새 이름");
        ImGui::TextDisabled("%s", script ? "소문자 · 숫자 · '_' (소문자로 시작 — 예: enemy_ai). 틀로 만들고 편집기에 연다"
                                         : "영문 · 숫자 · '_' · '-' · '.' (패키지가 모든 OS 에서 같게 — P4)");
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool enter = ImGui::InputText("##ask-name", st.askInput.data(), st.askInput.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ui::primaryButton(folder || script ? "만들기" : "바꾸기", st.askInput[0] == '\0') ||
            (enter && st.askInput[0] != '\0')) {
            out = applyExplorerAsk(st, projectDir);
            done = true;
        }
    }
    ImGui::SameLine();
    if (ui::secondaryButton("취소") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        done = true;
    }
    if (done) {
        st.ask = Ask::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return out;
}

} // namespace

std::optional<ProjectExplorerAction> applyExplorerAsk(ProjectExplorerState& st, const fs::path& projectDir) {
    using Ask = ProjectExplorerState::Ask;
    std::optional<ProjectExplorerAction> out;
    const Ask what = std::exchange(st.ask, Ask::None);
    st.stale = true;
    if (what == Ask::Trash) {
        const auto r = services::trashProjectEntry(projectDir, st.askTarget, st.forceTrash);
        setStatus(st, r ? std::format("휴지통으로 옮겼다 — {}", r->generic_string()) : r.error().message, !r);
        if (r) {
            out = ProjectExplorerAction{ProjectExplorerAction::Kind::FilesChanged, {}, st.askTarget, {}};
            st.selected.clear();
        }
        return out;
    }
    if (what == Ask::NewScript) { // 자리만 정한다 — 만들기 · 열기는 앱 (스크립트 편집기의 저장 길)
        const auto r = services::newScriptPath(projectDir, st.askTarget, st.askInput.data());
        if (!r) {
            setStatus(st, r.error().message, true);
            return out;
        }
        setStatus(st, std::format("새 스크립트: {}", *r), false);
        st.selected = *r;
        return ProjectExplorerAction{ProjectExplorerAction::Kind::NewScript, *r, {}, {}};
    }
    if (what != Ask::NewFolder && what != Ask::Rename) {
        return out;
    }
    const bool folder = what == Ask::NewFolder;
    const std::string name = st.askInput.data();
    const auto it = st.assetIds.find(st.askTarget);
    const std::string assetId = folder || it == st.assetIds.end() ? std::string() : it->second;
    const auto r = folder ? services::createProjectFolder(projectDir, st.askTarget, name)
                          : services::renameProjectEntry(projectDir, st.askTarget, name);
    setStatus(st, r ? std::format("{}: {}", folder ? "만들었다" : "이름을 바꿨다", *r) : r.error().message, !r);
    if (r) {
        st.selected = *r;
        out = ProjectExplorerAction{ProjectExplorerAction::Kind::FilesChanged, *r, folder ? std::string() : st.askTarget,
                                    assetId};
    }
    return out;
}

bool canCreateScriptIn(std::string_view folderRel) noexcept {
    // content/<팩> 이거나 content/<팩>/scripts[/…]
    if (!folderRel.starts_with("content/")) {
        return false;
    }
    const std::string_view rest = folderRel.substr(8);
    const usize slash = rest.find('/');
    if (rest.empty() || slash == 0) {
        return false;
    }
    if (slash == std::string_view::npos) {
        return true;
    }
    const std::string_view after = rest.substr(slash + 1);
    return after == "scripts" || after.starts_with("scripts/");
}

std::string projectParentOf(std::string_view rel) {
    const usize slash = rel.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(rel.substr(0, slash));
}

std::optional<ProjectExplorerAction> drawProjectExplorer(ProjectExplorerState& st, const fs::path& projectDir,
                                                         bool* open) {
    std::optional<ProjectExplorerAction> action;
    if (st.project != projectDir) {
        st.project = projectDir;
        st.stale = true;
        st.selected.clear();
    }
    if (st.stale || ++st.age >= kRescanFrames) {
        st.stale = false;
        st.age = 0;
        st.entries = services::listProjectFiles(projectDir);
        st.assetIds.clear();
        const asset::AssetScan scan = asset::scanProjectAssets(projectDir);
        for (const asset::AssetEntry& a : scan.assets) {
            st.assetIds[a.path] = a.id;
            for (const std::string& c : a.copies) {
                st.assetIds[c] = a.id;
            }
        }
    }
    if (!ImGui::Begin("프로젝트", open)) {
        ImGui::End();
        return action;
    }
    // 위 줄: 거르기 · 새 폴더 (고른 폴더 · 고른 파일의 폴더 · 맨 위) · 다시 읽기
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() * 6.f);
    ImGui::InputTextWithHint("##filter", "찾기 (경로 일부)", st.filter.data(), st.filter.size());
    ImGui::SameLine();
    const auto selectedFolder = [&] {
        for (const ProjectFileEntry& e : st.entries) {
            if (e.rel == st.selected) {
                return e.kind == ProjectFileKind::Folder ? e.rel : projectParentOf(e.rel);
            }
        }
        return std::string();
    };
    if (ui::secondaryButton("+ 폴더")) {
        ask(st, ProjectExplorerState::Ask::NewFolder, selectedFolder(), "");
    }
    ui::tooltip("고른 폴더 (없으면 맨 위) 에 새 폴더");
    ImGui::SameLine();
    {
        const std::string folder = selectedFolder();
        if (ui::secondaryButton("+ 스크립트", !canCreateScriptIn(folder))) {
            ask(st, ProjectExplorerState::Ask::NewScript, folder, "");
        }
        ui::tooltip("팩 폴더 (content/<팩> · 그 scripts/) 를 고르면 그 scripts/ 에 새 스크립트");
    }
    ImGui::SameLine();
    if (ui::secondaryButton("다시 읽기")) {
        st.stale = true;
    }
    if (!st.status.empty()) {
        ui::statusText(st.statusError ? theme::Status::Error : theme::Status::Success, st.status);
    }
    ImGui::Separator();

    const std::vector<ProjectFileEntry> shown = services::filterProjectFiles(st.entries, st.filter.data());
    const bool filtering = st.filter[0] != '\0';
    if (st.entries.empty()) {
        ui::emptyState("파일이 없다", "프로젝트 폴더를 읽지 못했거나 비어 있다 — [다시 읽기]");
    } else if (shown.empty()) {
        ImGui::TextDisabled("'%s' 에 맞는 파일이 없다", st.filter.data());
    }
    ImGui::BeginChild("##tree");
    // 깊이 우선 목록 → 트리. openDepth = 지금 펼쳐진 단계 수 (그보다 깊은 항목은 접힌 폴더 안 — 건너뛴다)
    u32 openDepth = 0;
    for (const ProjectFileEntry& e : shown) {
        while (openDepth > e.depth) {
            ImGui::TreePop();
            --openDepth;
        }
        if (e.depth > openDepth) {
            continue;
        }
        const bool folder = e.kind == ProjectFileKind::Folder;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (!folder) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (e.rel == st.selected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (folder && filtering) {
            ImGui::SetNextItemOpen(true);
        }
        const std::string label = folder ? std::format("{}/", e.name) : std::format("{}  {}", kindTag(e.kind), e.name);
        const bool expanded = ImGui::TreeNodeEx(e.rel.c_str(), flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            st.selected = e.rel;
        }
        if (!folder && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (e.kind == ProjectFileKind::Image) {
                action = ProjectExplorerAction{ProjectExplorerAction::Kind::OpenAsset, e.rel, {}, {}};
            } else if (e.kind == ProjectFileKind::Script) {
                action = ProjectExplorerAction{ProjectExplorerAction::Kind::OpenScript, e.rel, {}, {}};
            }
        }
        if (ImGui::IsItemHovered() && !folder && e.size > 0) {
            ui::tooltip(std::format("{} · {:.1f} KB", e.rel, static_cast<double>(e.size) / 1024.0));
        }
        dragSource(st, e);
        if (folder) {
            if (auto dropped = acceptDrop(st, projectDir, e.rel)) {
                action = std::move(dropped);
            }
        }
        contextMenu(st, e);
        if (folder && expanded) {
            ++openDepth;
        }
    }
    while (openDepth > 0) {
        ImGui::TreePop();
        --openDepth;
    }
    // 빈 곳: 맨 위로 놓기 · 맨 위에 새 폴더
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 24.f)));
    if (auto dropped = acceptDrop(st, projectDir, std::string())) {
        action = std::move(dropped);
    }
    if (ImGui::BeginPopupContextItem("##project-bg")) {
        if (ImGui::MenuItem("새 폴더 (맨 위)…")) {
            ask(st, ProjectExplorerState::Ask::NewFolder, std::string(), "");
        }
        if (ImGui::MenuItem("다시 읽기")) {
            st.stale = true;
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    // 고른 항목의 키 — F2 이름 바꾸기 · Delete 휴지통 (창에 초점이 있고 글자 칸이 아닐 때)
    if (!st.selected.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        !ImGui::GetIO().WantTextInput && !services::checkProjectEdit(projectDir, st.selected, true)) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            ask(st, ProjectExplorerState::Ask::Rename, st.selected,
                st.selected.substr(st.selected.rfind('/') == std::string::npos ? 0 : st.selected.rfind('/') + 1));
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            ask(st, ProjectExplorerState::Ask::Trash, st.selected, "");
        }
    }
    if (auto a = askWindow(st, projectDir)) {
        action = std::move(a);
    }
    ImGui::End();
    return action;
}

} // namespace sbx::editor
