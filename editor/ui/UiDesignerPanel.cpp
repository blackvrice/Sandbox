#include "editor/ui/UiDesignerPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <imgui.h>

#include "core/asset/AssetRefs.hpp"
#include "editor/ui/AssetBrowserPanel.hpp"
#include "editor/ui/StudioWidgets.hpp"

namespace sbx::editor {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

std::vector<fs::path> uiDocumentFiles(const fs::path& projectDir) {
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::path content = projectDir / "content";
    if (!fs::is_directory(content, ec)) {
        return out;
    }
    for (fs::directory_iterator pack(content, ec), end; !ec && pack != end; pack.increment(ec)) {
        const fs::path ui = pack->path() / "presentation" / "ui";
        if (!fs::is_directory(ui, ec)) {
            continue;
        }
        for (fs::directory_iterator it(ui, ec), e; !ec && it != e; it.increment(ec)) {
            const fs::path& p = it->path();
            if (it->is_regular_file(ec) && p.extension() == ".json" && p.filename() != "styles.json") {
                out.push_back(p);
            }
        }
    }
    std::ranges::sort(out);
    return out;
}

UiPreviewFit fitPreview(f32 canvasW, f32 canvasH, i32 width, i32 height) {
    UiPreviewFit f;
    if (width <= 0 || height <= 0 || canvasW <= 0 || canvasH <= 0) {
        return f;
    }
    f.zoom = std::min(1.0f, std::min(canvasW / static_cast<f32>(width), canvasH / static_cast<f32>(height)));
    f.x = (canvasW - static_cast<f32>(width) * f.zoom) * 0.5f;
    f.y = (canvasH - static_cast<f32>(height) * f.zoom) * 0.5f;
    return f;
}

std::optional<services::UiNodePath> pickNode(const std::vector<services::UiDesignRect>& rects, f32 x, f32 y) {
    std::optional<services::UiNodePath> best;
    u32 bestDepth = 0;
    for (const services::UiDesignRect& r : rects) {
        const bool inside = x >= static_cast<f32>(r.rect.x) && y >= static_cast<f32>(r.rect.y) &&
                            x < static_cast<f32>(r.rect.x + r.rect.w) && y < static_cast<f32>(r.rect.y + r.rect.h);
        if (inside && (!best || r.depth >= bestDepth)) {
            best = r.path;
            bestDepth = r.depth;
        }
    }
    return best;
}

std::array<f32, 4> dragOffset(const std::array<f32, 4>& offset, f32 dx, f32 dy, f32 scale) {
    // 배치는 오프셋에 배율을 곱한다 — 화면에서 끈 거리를 배율로 나눠 문서 값으로
    const f32 s = scale > 0 ? scale : 1;
    const f32 ox = std::round(dx / s);
    const f32 oy = std::round(dy / s);
    return {offset[0] + ox, offset[1] + oy, offset[2] + ox, offset[3] + oy};
}

namespace {

std::string pathKey(const services::UiNodePath& path) {
    std::string s = "r";
    for (const u32 i : path) {
        s += std::format(".{}", i);
    }
    return s;
}

int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<usize>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

std::array<f32, 2> pair(const Json& node, const char* key, const char* sub) {
    if (node.contains(key) && node[key].is_object() && node[key].contains(sub) && node[key][sub].is_array() &&
        node[key][sub].size() == 2) {
        return {node[key][sub][0].get<f32>(), node[key][sub][1].get<f32>()};
    }
    return {0, 0};
}

std::array<f32, 4> offsetOf(const Json& node) {
    std::array<f32, 4> o{};
    if (node.contains("offset") && node["offset"].is_array() && node["offset"].size() == 4) {
        for (usize i = 0; i < 4; ++i) {
            o[i] = node["offset"][i].get<f32>();
        }
    }
    return o;
}

void report(UiDesignerPanelState& state, const Expected<void>& r, std::string_view ok) {
    state.status = r ? std::string(ok) : r.error().describe();
}

} // namespace

Expected<void> dropAssetOnImage(UiDesignerPanelState& state, services::UiDesignService& design, std::string_view id) {
    // 문서 = <프로젝트>/content/<팩 폴더>/presentation/ui/<이름>.json
    const fs::path packDir = state.file.parent_path().parent_path().parent_path();
    const fs::path projectDir = packDir.parent_path().parent_path();
    const asset::AssetScan scan = asset::scanProjectAssets(projectDir);
    const asset::AssetEntry* e = scan.find(id);
    if (e == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("에셋 id '{}' 가 프로젝트에 없다", id));
    }
    const std::string name = std::format("{}/{}", packDir.filename().string(), fs::path(e->path).stem().string());
    if (auto r = asset::ensureAssetMaterial(projectDir, packDir.filename().string(), id, name); !r) {
        return r;
    }
    return design.setProperty(state.selected, "material", name);
}

void drawUiDesignerPanel(UiDesignerPanelState& state, services::UiDesignService& design,
                         const std::vector<fs::path>& files) {
    if (!ImGui::Begin("UI 디자이너")) {
        ImGui::End();
        return;
    }
    // 문서 고르기
    const std::string chosen =
        state.file.empty() ? std::string("(UI 문서를 고른다)")
                           : std::format("{} — {}", state.file.filename().string(),
                                         state.file.parent_path().parent_path().parent_path().filename().string());
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("문서", chosen.c_str())) {
        for (const fs::path& f : files) {
            const std::string label = std::format("{} — {}", f.filename().string(),
                                                  f.parent_path().parent_path().parent_path().filename().string());
            if (ImGui::Selectable(label.c_str(), f == state.file)) {
                const auto r = design.open(f);
                state.file = r ? f : fs::path();
                state.selected.clear();
                state.jsonFor.clear();
                report(state, r, "열었다");
            }
        }
        ImGui::EndCombo();
    }
    if (files.empty()) {
        ui::emptyState("UI 문서가 없다", "팩의 presentation/ui/<이름>.json 을 만들면 여기서 고친다 (25 8장)");
        ImGui::End();
        return;
    }
    if (!design.isOpen()) {
        ImGui::TextDisabled("위에서 문서를 고른다");
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    if (ImGui::BeginCombo("해상도", kUiPreviewSizes[static_cast<usize>(state.size)].name)) {
        for (usize i = 0; i < kUiPreviewSizes.size(); ++i) {
            if (ImGui::Selectable(kUiPreviewSizes[i].name, static_cast<usize>(state.size) == i)) {
                state.size = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("배율", &state.scale, 0.75f, 2.0f, "%.2f");
    ImGui::SameLine();
    if (ui::primaryButton("저장")) {
        report(state, design.save(), "저장했다");
    }
    if (!state.status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", state.status.c_str());
    }
    if (!design.parseError().empty()) {
        ui::statusText(theme::Status::Error, design.parseError());
    }

    const UiPreviewSize& size = kUiPreviewSizes[static_cast<usize>(state.size)];
    const std::vector<services::UiDesignRect> rects = design.layout(size.width, size.height, state.scale);
    const auto selectedNode = design.node(state.selected);
    if (!selectedNode) {
        state.selected.clear();
    }
    if (ImGui::BeginTable("##uidesigner", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("계층", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("미리보기", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("속성", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableNextRow();

        // --- 계층 · 라이브러리 ---
        ImGui::TableNextColumn();
        ui::sectionHeader("계층");
        for (const services::UiDesignNode& n : design.nodes()) {
            ImGui::Indent(static_cast<f32>(n.depth) * 12.0f + 1.0f);
            const std::string label = std::format("{}##{}", n.label, pathKey(n.path));
            if (ImGui::Selectable(label.c_str(), n.path == state.selected)) {
                state.selected = n.path;
            }
            ImGui::Unindent(static_cast<f32>(n.depth) * 12.0f + 1.0f);
        }
        ImGui::BeginDisabled(state.selected.empty());
        if (ImGui::SmallButton("위로")) {
            if (const auto m = design.move(state.selected, -1)) {
                state.selected = *m;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("아래로")) {
            if (const auto m = design.move(state.selected, 1)) {
                state.selected = *m;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("지우기")) {
            const auto r = design.remove(state.selected);
            report(state, r, "지웠다");
            if (r) {
                state.selected.pop_back();
            }
        }
        ImGui::EndDisabled();
        ui::sectionHeader("요소 더하기");
        const std::string parentKind = selectedNode ? selectedNode->value("kind", std::string()) : std::string();
        const bool container = parentKind == "panel" || parentKind == "scroll";
        if (!container) {
            ImGui::TextDisabled("panel · scroll 을 고르면 그 안에 더한다");
        }
        ImGui::BeginDisabled(!container);
        for (const std::string_view kind : services::kUiLibrary) {
            if (ImGui::SmallButton(std::string(kind).c_str())) {
                const auto made = design.addChild(state.selected, kind);
                if (made) {
                    state.selected = *made;
                    state.status = std::format("{} 를 더했다", kind);
                } else {
                    state.status = made.error().describe();
                }
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::EndDisabled();

        // --- 미리보기 (누르면 고르기 · 끌면 오프셋) ---
        ImGui::TableNextColumn();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 canvas{std::max(avail.x, 64.0f), std::max(avail.y - 8.0f, 160.0f)};
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##preview", canvas);
        const UiPreviewFit fit = fitPreview(canvas.x, canvas.y, size.width, size.height);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 screen0{origin.x + fit.x, origin.y + fit.y};
        dl->AddRectFilled(
            screen0,
            {screen0.x + static_cast<f32>(size.width) * fit.zoom, screen0.y + static_cast<f32>(size.height) * fit.zoom},
            IM_COL32(24, 28, 36, 255));
        for (const services::UiDesignRect& r : rects) {
            const ImVec2 a{screen0.x + static_cast<f32>(r.rect.x) * fit.zoom,
                           screen0.y + static_cast<f32>(r.rect.y) * fit.zoom};
            const ImVec2 b{a.x + static_cast<f32>(r.rect.w) * fit.zoom, a.y + static_cast<f32>(r.rect.h) * fit.zoom};
            const bool chosenRect = r.path == state.selected;
            if (chosenRect) {
                dl->AddRectFilled(a, b, IM_COL32(80, 140, 255, 60));
            }
            dl->AddRect(a, b, chosenRect ? IM_COL32(110, 170, 255, 255) : IM_COL32(150, 150, 160, 140));
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const f32 px = (mouse.x - screen0.x) / fit.zoom;
        const f32 py = (mouse.y - screen0.y) / fit.zoom;
        if (ImGui::IsItemActivated()) {
            if (const auto picked = pickNode(rects, px, py)) {
                state.selected = *picked;
                if (const auto n = design.node(state.selected)) {
                    state.dragging = true;
                    state.dragX = px;
                    state.dragY = py;
                    state.dragStart = offsetOf(*n);
                }
            }
        }
        if (state.dragging && ImGui::IsItemDeactivated()) {
            state.dragging = false;
            const f32 dx = px - state.dragX;
            const f32 dy = py - state.dragY;
            if (std::abs(dx) >= 1.0f || std::abs(dy) >= 1.0f) {
                if (const auto n = design.node(state.selected)) {
                    report(state,
                           design.setPlacement(state.selected, pair(*n, "anchor", "min"), pair(*n, "anchor", "max"),
                                               dragOffset(state.dragStart, dx, dy, state.scale)),
                           "옮겼다 (쌓기 안의 노드는 쌓기를 따른다)");
                }
            }
        }

        // --- 속성 (노드 JSON 그대로) ---
        ImGui::TableNextColumn();
        ui::sectionHeader("속성");
        if (const auto n = design.node(state.selected)) {
            const std::string key = pathKey(state.selected) + "|" + n->dump();
            if (state.jsonFor != key) {
                state.jsonFor = key;
                Json shown = *n;
                shown.erase("children"); // 자식은 계층에서 — 여기서는 노드 자신의 키만
                shown.erase("item");
                state.jsonText = shown.dump(2);
            }
            ImGui::TextDisabled("%s", n->value("kind", std::string("?")).c_str());
            ImGui::InputTextMultiline("##node", state.jsonText.data(), state.jsonText.capacity() + 1,
                                      {-1.0f, ImGui::GetTextLineHeight() * 16}, ImGuiInputTextFlags_CallbackResize,
                                      resizeCallback, &state.jsonText);
            if (ui::secondaryButton("적용")) {
                const Json edited = Json::parse(state.jsonText, nullptr, false);
                if (edited.is_discarded()) {
                    state.status = "JSON 이 아니다 — 고친 뒤 다시";
                } else {
                    report(state, design.setNode(state.selected, edited), "고쳤다");
                    state.jsonFor.clear();
                }
            }
            ImGui::TextDisabled("자리 (anchor · offset) 는 미리보기에서 끌어도 된다");
            if (n->value("kind", std::string()) == "image") {
                // 에셋 창에서 끌어 놓기 — 팩 materials.json 에 "<팩 폴더>/<파일 이름>" = asset:<id> (없으면) 를 만들고
                // material 로
                ui::secondaryButton("여기에 에셋을 놓는다", true);
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetPayload)) {
                        const std::string id(static_cast<const char*>(p->Data));
                        report(state, dropAssetOnImage(state, design, id), "머티리얼을 넣었다");
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        } else {
            ImGui::TextDisabled("계층이나 미리보기에서 노드를 고른다");
        }
        ImGui::EndTable();
    }
    // --- 검사 (U1 ~ U8 — 콘텐츠 표 없이, 저장은 오류가 없어야) ---
    for (const sbx::ui::UiIssue& i : design.issues()) {
        ui::statusText(i.severity == sbx::ui::UiIssue::Severity::Error ? theme::Status::Error : theme::Status::Warning,
                       std::format("{} {} — {}", i.rule, i.pointer, i.message));
    }
    ImGui::End();
}

} // namespace sbx::editor
