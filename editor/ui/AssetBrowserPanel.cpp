#include "editor/ui/AssetBrowserPanel.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>

#include <imgui.h>
#include <imgui_internal.h> // RegisterUserTexture (ImGui 1.92 — 사용자 텍스처, 실험)

#include "editor/ui/StudioWidgets.hpp"
#include "render/asset/Image.hpp"

namespace sbx::editor {

namespace fs = std::filesystem;

std::vector<const asset::AssetEntry*> filterAssets(const asset::AssetScan& scan, std::string_view filter) {
    const auto lower = [](std::string s) {
        std::ranges::transform(s, s.begin(),
                               [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
        return s;
    };
    const std::string f = lower(std::string(filter));
    std::vector<const asset::AssetEntry*> out;
    for (const asset::AssetEntry& e : scan.assets) {
        if (f.empty() || lower(e.path).find(f) != std::string::npos || e.id.find(f) != std::string::npos) {
            out.push_back(&e);
        }
    }
    std::ranges::sort(out, [](const asset::AssetEntry* a, const asset::AssetEntry* b) { return a->path < b->path; });
    return out;
}

usize countReferences(const std::vector<asset::AssetRef>& refs, std::string_view path) {
    return static_cast<usize>(std::ranges::count_if(refs, [&](const asset::AssetRef& r) { return r.path == path; }));
}

std::array<u32, 2> thumbnailSize(u32 width, u32 height, u32 max) {
    if (width == 0 || height == 0) {
        return {1, 1};
    }
    if (width <= max && height <= max) {
        return {width, height};
    }
    if (width >= height) {
        return {max, std::max<u32>(1, static_cast<u32>(static_cast<u64>(height) * max / width))};
    }
    return {std::max<u32>(1, static_cast<u32>(static_cast<u64>(width) * max / height)), max};
}

// 썸네일 — 경로 · 내용 해시마다 ImTextureData 하나 (렌더러가 올린다 — 헤드리스는 ImGuiLayer 가 받은 척). 내용이 바뀌면
// 옛 것은 지우기를 요청하고, 지워진 뒤 등록을 푼다
class ThumbnailCache {
public:
    ~ThumbnailCache() {
        if (ImGui::GetCurrentContext() == nullptr) {
            return; // ImGui 가 먼저 없어졌다 — 등록부도 없다
        }
        for (auto& [path, t] : m_thumbs) {
            ImGui::UnregisterUserTexture(t.tex.get());
        }
        for (auto& t : m_dying) {
            ImGui::UnregisterUserTexture(t.get());
        }
    }
    // 없거나 PNG 가 아니면 nullptr
    ImTextureData* get(const fs::path& file, const std::string& key, const std::string& sha) {
        if (const auto it = m_thumbs.find(key); it != m_thumbs.end()) {
            if (it->second.sha == sha) {
                return it->second.tex.get();
            }
            retire(std::move(it->second.tex));
            m_thumbs.erase(it);
        }
        if (file.extension() != ".png" || m_thumbs.size() >= kMax) {
            return nullptr;
        }
        auto img = render::loadPng(file);
        if (!img) {
            return nullptr;
        }
        const auto [w, h] = thumbnailSize(img->width, img->height, kSize);
        auto tex = std::make_unique<ImTextureData>();
        tex->Create(ImTextureFormat_RGBA32, static_cast<int>(w), static_cast<int>(h));
        auto* dst = static_cast<u8*>(tex->GetPixels());
        for (u32 y = 0; y < h; ++y) { // 가장 가까운 픽셀 — 썸네일은 표시만
            for (u32 x = 0; x < w; ++x) {
                const u8* src = img->pixel(x * img->width / w, y * img->height / h);
                std::memcpy(dst + (static_cast<usize>(y) * w + x) * 4, src, 4);
            }
        }
        tex->UseColors = true;
        ImGui::RegisterUserTexture(tex.get());
        ImTextureData* raw = tex.get();
        m_thumbs[key] = Thumb{std::move(tex), sha};
        return raw;
    }
    // 지우기를 마친 것의 등록을 푼다 (프레임마다)
    void collect() {
        std::erase_if(m_dying, [](const std::unique_ptr<ImTextureData>& t) {
            if (t->Status == ImTextureStatus_Destroyed || t->TexID == ImTextureID_Invalid) {
                ImGui::UnregisterUserTexture(t.get());
                return true;
            }
            return false;
        });
    }

private:
    static constexpr u32 kSize = 48;
    static constexpr usize kMax = 512;
    struct Thumb {
        std::unique_ptr<ImTextureData> tex;
        std::string sha;
    };
    void retire(std::unique_ptr<ImTextureData> tex) {
        tex->WantDestroyNextFrame = true;
        m_dying.push_back(std::move(tex));
    }
    std::map<std::string, Thumb> m_thumbs;
    std::vector<std::unique_ptr<ImTextureData>> m_dying;
};

AssetBrowserState::AssetBrowserState() : thumbs(std::make_unique<ThumbnailCache>()) {}
AssetBrowserState::~AssetBrowserState() = default;

namespace {

int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<usize>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}
bool inputString(const char* label, std::string& text) {
    return ImGui::InputText(label, text.data(), text.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resizeCallback,
                            &text);
}

} // namespace

std::optional<AssetChange> drawAssetBrowser(AssetBrowserState& state, const fs::path& projectDir) {
    std::optional<AssetChange> change;
    if (state.project != projectDir) {
        state.project = projectDir;
        state.stale = true;
        state.selected.clear();
    }
    if (state.stale) {
        state.stale = false;
        state.scan = asset::scanProjectAssets(projectDir);
        state.refs = asset::findAssetReferences(projectDir, state.scan);
    }
    state.thumbs->collect();
    if (!ImGui::Begin("에셋")) {
        ImGui::End();
        return change;
    }
    if (ui::secondaryButton("다시 훑기")) {
        state.stale = true;
    }
    ImGui::SameLine();
    const usize missing = state.scan.unidentified.size();
    if (ui::secondaryButton(std::format("가져오기 (id 없음 {})", missing), missing == 0)) {
        const auto r = asset::importAssets(projectDir);
        state.status = r ? std::format("{} 개에 id 를 붙였다 (.meta)", *r) : r.error().describe();
        state.stale = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    inputString("거르기", state.filter);
    if (!state.status.empty()) {
        ImGui::TextDisabled("%s", state.status.c_str());
    }
    for (const content::ContentIssue& i : state.scan.issues) {
        ui::statusText(i.severity == content::ContentIssue::Severity::Error ? theme::Status::Error
                                                                            : theme::Status::Warning,
                       i.describe());
    }
    const std::vector<const asset::AssetEntry*> shown = filterAssets(state.scan, state.filter);
    if (state.scan.assets.empty()) {
        ui::emptyState("에셋이 없다", "프로젝트 assets/ 에 PNG 를 넣고 [가져오기] — id (.meta) 가 붙는다");
    }
    if (ImGui::BeginTable("##assets", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                          {0, ImGui::GetTextLineHeightWithSpacing() * 12})) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 52);
        ImGui::TableSetupColumn("경로");
        ImGui::TableSetupColumn("크기", ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("참조", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableHeadersRow();
        for (const asset::AssetEntry* e : shown) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImTextureData* t = state.thumbs->get(projectDir / fs::path(e->path), e->path, e->sha256)) {
                ImGui::Image(t->GetTexRef(), {static_cast<f32>(t->Width), static_cast<f32>(t->Height)});
            } else {
                ImGui::TextDisabled("—");
            }
            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::format("{}##{}", e->path, e->id).c_str(), state.selected == e->path,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                state.selected = e->path;
                state.moveTo = e->path;
                state.forceDelete = false;
            }
            if (ImGui::BeginDragDropSource()) { // 끌기 원천 — UI 디자이너의 image 가 받는다
                ImGui::SetDragDropPayload(kAssetPayload, e->id.c_str(), e->id.size() + 1);
                ImGui::TextUnformatted(e->path.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.1f KB", static_cast<double>(e->size) / 1024.0);
            ImGui::TableNextColumn();
            ImGui::Text("%zu", countReferences(state.refs, e->path));
        }
        ImGui::EndTable();
    }
    for (const std::string& p : state.scan.unidentified) {
        ImGui::TextDisabled("id 없음: %s", p.c_str());
    }

    const asset::AssetEntry* sel = nullptr;
    for (const asset::AssetEntry& e : state.scan.assets) {
        if (e.path == state.selected) {
            sel = &e;
        }
    }
    if (sel != nullptr) {
        ui::sectionHeader(sel->path);
        ImGui::TextDisabled("id %s · %llu 바이트", sel->id.c_str(), static_cast<unsigned long long>(sel->size));
        const std::vector<asset::AssetRef> uses = asset::referencesTo(state.refs, sel->path);
        if (uses.empty()) {
            ImGui::TextDisabled("쓰는 곳 없음");
        }
        for (const asset::AssetRef& r : uses) {
            ImGui::BulletText("%s%s (%s)", r.file.c_str(), r.pointer.c_str(), r.byId ? "id" : "경로");
        }
        ImGui::SetNextItemWidth(260);
        inputString("새 경로", state.moveTo);
        ImGui::SameLine();
        if (ui::secondaryButton("옮기기", state.moveTo == sel->path || state.moveTo.empty())) {
            const auto r = asset::moveAsset(projectDir, sel->path, state.moveTo);
            if (r) {
                state.status = std::format("옮겼다 — 경로 참조 {} 개를 고쳤다", r->rewritten);
                change = AssetChange{sel->id, r->to, r->from};
                state.selected = r->to;
            } else {
                state.status = r.error().describe();
            }
            state.stale = true;
        }
        if (ui::secondaryButton("다시 읽기")) { // Hot Reload — 원본을 밖에서 고친 뒤
            change = AssetChange{sel->id, sel->path, {}};
            state.status = "다시 올린다";
        }
        ImGui::SameLine();
        if (!uses.empty()) {
            ImGui::Checkbox("쓰는 곳이 있어도 지운다", &state.forceDelete);
            ImGui::SameLine();
        }
        if (ui::secondaryButton("지우기", !uses.empty() && !state.forceDelete)) {
            const auto r = asset::deleteAsset(projectDir, sel->path, state.forceDelete);
            state.status =
                r ? std::format("휴지통으로 옮겼다 — {}", r->trashed.generic_string()) : r.error().describe();
            state.selected.clear();
            state.stale = true;
        }
    }
    ImGui::End();
    return change;
}

} // namespace sbx::editor
