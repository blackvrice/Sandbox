#include "editor/ui/ContentPanel.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "core/content/ContentDatabase.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/PackGroups.hpp"
#include "editor/services/ScriptAssist.hpp"
#include "editor/services/ScriptDebugService.hpp"
#include "foundation/text/TextPosition.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::editor {

namespace {

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_None; // 자리는 레이아웃에 (v0.8 M6-T6)

[[nodiscard]] f32 dpi() {
    return ImGui::GetStyle().FontScaleDpi;
}

struct Item {
    std::string id;
    bool overlay = false;
    const ecs::Json* raw = nullptr; // 스크립트는 없다 (소스 — scriptSourceOf)
};

// 스크립트 오버레이 id 의 팩 파일 (v0.3 M5-T2) — 팩 폴더 · 원본 텍스트. 팩에 없는 (오버레이로 더한) 파일이면 text 가
// 없다
struct ScriptFile {
    std::string display; // "<팩 폴더>/scripts/a.ts" (콘텐츠 문제의 file 과 같다)
    const std::string* text = nullptr;
    std::filesystem::path disk; // 이 컴퓨터의 그 파일 (팩 폴더를 모르면 비었다)
    bool installed = false;     // 설치한 패키지 (.sandbox/packages/ — [저장] 하지 않는다)
};
[[nodiscard]] std::optional<ScriptFile> scriptFileOf(const content::ContentDatabase& db, std::string_view id) {
    const auto path = content::scriptOverlayPath(id);
    if (!path || db.sources() == nullptr) {
        return std::nullopt;
    }
    for (const content::ContentSourcePack& sp : db.sources()->packs) {
        if (sp.info.id != path->pack) {
            continue;
        }
        ScriptFile out{sp.folder + "/" + path->rel, nullptr,
                       sp.dir.empty() ? std::filesystem::path() : sp.dir / path->rel,
                       sp.folder.starts_with(".sandbox/packages/")};
        for (const content::ContentSourceFile& f : sp.files) {
            if (f.rel == path->rel) {
                out.text = &f.text;
            }
        }
        return out;
    }
    return std::nullopt;
}

// 지금 소스 (오버레이 → 팩 원본). 없으면 nullopt
[[nodiscard]] std::optional<std::string> scriptSourceOf(const content::ContentDatabase& db, std::string_view id) {
    if (const ecs::Json* v = db.overlay().find(content::ContentKind::Script, id);
        v != nullptr && v->contains("source") && (*v)["source"].is_string()) {
        return (*v)["source"].get<std::string>();
    }
    const auto file = scriptFileOf(db, id);
    if (file && file->text != nullptr) {
        return *file->text;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<Item> itemsOf(const content::ContentDatabase& db, content::ContentKind kind) {
    std::vector<Item> out;
    switch (kind) {
    case content::ContentKind::Prefab:
        for (const content::Prefab& p : db.prefabs()) {
            out.push_back({p.id, p.overlay, &p.raw});
        }
        break;
    case content::ContentKind::Rule:
        for (const content::Rule& r : db.rules()) {
            out.push_back({r.id, r.overlay, &r.raw});
        }
        break;
    case content::ContentKind::Behavior:
        for (const content::BehaviorGraph& g : db.behaviors()) {
            out.push_back({g.id, g.overlay, &g.raw});
        }
        break;
    case content::ContentKind::Trigger:
        for (const content::TriggerDef& t : db.triggers()) { // 실행 순서
            out.push_back({t.id, t.overlay, &t.raw});
        }
        break;
    case content::ContentKind::Script: {
        // 팩의 scripts/*.ts (오버레이 id 로 쓸 수 있는 경로만) + 오버레이로 더한 파일 — id 정렬
        std::vector<std::string> ids;
        if (db.sources() != nullptr) {
            for (const content::ContentSourcePack& sp : db.sources()->packs) {
                for (const content::ContentSourceFile& f : sp.files) {
                    if (const auto id = content::scriptOverlayId(sp.info.id, f.rel)) {
                        ids.push_back(*id);
                    }
                }
            }
        }
        for (const auto& [id, value] : db.overlay().items(content::ContentKind::Script)) {
            (void)value;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        for (std::string& id : ids) {
            const bool overlaid = db.overlay().find(content::ContentKind::Script, id) != nullptr;
            out.push_back({std::move(id), overlaid, nullptr});
        }
        break;
    }
    }
    return out;
}

// 고르는 항목의 지금 값 — JSON 종류는 dump(2), 스크립트는 소스. 없으면 nullopt
[[nodiscard]] std::optional<std::string> currentTextOf(const content::ContentDatabase& db, content::ContentKind kind,
                                                       std::string_view id);

[[nodiscard]] const ecs::Json* rawOf(const content::ContentDatabase& db, content::ContentKind kind,
                                     std::string_view id) {
    switch (kind) {
    case content::ContentKind::Prefab: {
        const content::Prefab* p = db.findPrefab(id);
        return p != nullptr ? &p->raw : nullptr;
    }
    case content::ContentKind::Rule: {
        const content::Rule* r = db.findRule(id);
        return r != nullptr ? &r->raw : nullptr;
    }
    case content::ContentKind::Behavior: {
        const content::BehaviorGraph* g = db.findBehavior(id);
        return g != nullptr ? &g->raw : nullptr;
    }
    case content::ContentKind::Trigger: {
        const content::TriggerDef* t = db.findTrigger(id);
        return t != nullptr ? &t->raw : nullptr;
    }
    case content::ContentKind::Script:
        return nullptr; // 소스 텍스트 — currentTextOf
    }
    return nullptr;
}

std::optional<std::string> currentTextOf(const content::ContentDatabase& db, content::ContentKind kind,
                                         std::string_view id) {
    if (kind == content::ContentKind::Script) {
        return scriptSourceOf(db, id);
    }
    const ecs::Json* raw = rawOf(db, kind, id);
    return raw != nullptr ? std::optional<std::string>(raw->dump(2)) : std::nullopt;
}

[[nodiscard]] bool containsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::search(text, needle, [&](char a, char b) { return lower(a) == lower(b); }).begin() !=
           text.end();
}

// std::string 을 InputTextMultiline 에 (imgui_stdlib 와 같은 크기 바꾸기 콜백)
int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<usize>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

bool inputJson(const char* label, std::string& text, ImVec2 size, bool readOnly) {
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput;
    if (readOnly) {
        flags |= ImGuiInputTextFlags_ReadOnly;
    }
    return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, size, flags, resizeCallback, &text);
}

void summary(const content::ContentDatabase& db, content::ContentKind kind, std::string_view id) {
    switch (kind) {
    case content::ContentKind::Rule:
        if (const content::Rule* r = db.findRule(id)) {
            ImGui::TextDisabled("action %s · 우선순위 %d · 범위 %.1f · 조건 %zu · 효과 %zu%s", r->action.c_str(),
                                r->priority, static_cast<double>(r->range), r->conditions.size(), r->effects.size(),
                                r->exclusive ? " · 배타" : "");
        }
        break;
    case content::ContentKind::Behavior:
        if (const content::BehaviorGraph* g = db.findBehavior(id)) {
            if (ImGui::TreeNode("##graph", "상태 %zu · 전이 %zu", g->states.size(), g->transitions.size())) {
                for (usize i = 0; i < g->states.size(); ++i) {
                    const auto& st = g->states[i];
                    ImGui::BulletText("%s%s  (들어갈 때 %zu · 틱마다 %zu · 나갈 때 %zu)", st.id.c_str(),
                                      i == g->initial ? " ◀ 처음" : "", st.onEnter.size(), st.onTick.size(),
                                      st.onExit.size());
                }
                for (const content::Transition& t : g->transitions) {
                    const std::string from = t.from == content::kAnyState ? std::string("*") : g->states.at(t.from).id;
                    ImGui::BulletText("%s → %s  (우선순위 %d)", from.c_str(), g->states.at(t.to).id.c_str(),
                                      t.priority);
                }
                ImGui::TreePop();
            }
        }
        break;
    case content::ContentKind::Trigger:
        if (const content::TriggerDef* t = db.findTrigger(id)) {
            static constexpr std::array<const char*, 6> kEvents{"게임 시작", "매 틱", "타이머",
                                                                "영역 진입", "점수",  "죽음"};
            if (ImGui::TreeNode("##program", "%s · 조건 %zu · 효과 %zu · 틱마다 %u%s — 실행 표현 %zu 줄",
                                kEvents.at(static_cast<usize>(t->event.kind)), t->conditions.size(), t->effects.size(),
                                t->perTick, t->hasSubject ? " · 대상 있음" : "", t->program.size())) {
                for (const std::string& line : describeProgram(*t)) {
                    ImGui::TextDisabled("%s", line.c_str());
                }
                ImGui::TreePop();
            }
        }
        break;
    case content::ContentKind::Script: {
        // 이 파일이 정의한 시스템 (콘텐츠의 바이트코드 — 마지막으로 받아들여진 소스)
        const auto path = content::scriptOverlayPath(id);
        const auto& programs = db.scriptPrograms();
        if (!path || !programs) {
            break;
        }
        for (const content::ScriptPackProgram& pp : programs->packs) {
            if (pp.pack != path->pack) {
                continue;
            }
            for (const script::bc::SystemDesc& s : pp.module.systems) {
                if (s.module != path->rel) {
                    continue;
                }
                std::string line = std::format("시스템 {}", s.id);
                if (s.onTick != script::api::kNoCallback) {
                    line += s.intervalTicks == 1 ? " · onTick" : std::format(" · onTick ({} 틱마다)", s.intervalTicks);
                }
                line += s.onStart != script::api::kNoCallback ? " · onStart" : "";
                line += s.onEvent != script::api::kNoCallback ? " · onEvent" : "";
                line += s.onStop != script::api::kNoCallback ? " · onStop" : "";
                ImGui::TextDisabled("%s", line.c_str());
            }
        }
        break;
    }
    case content::ContentKind::Prefab:
        if (const content::Prefab* p = db.findPrefab(id)) {
            std::string names;
            for (const content::PrefabComponent& c : p->components) {
                names += (names.empty() ? "" : " · ") + c.name;
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 340.f * dpi());
            ImGui::TextDisabled("%s · 태그 %s · %s", p->name.empty() ? "(이름 없음)" : p->name.c_str(),
                                db.describeTags(p->tags).c_str(), names.c_str());
            ImGui::PopTextWrapPos();
        }
        break;
    }
}

} // namespace

bool checkContentEdit(const IEditorHost& host, content::ContentKind kind, const std::string& id,
                      const std::string& text, std::vector<ContentPanelState::Issue>& out) {
    out.clear();
    const content::ContentDatabase* db = host.content();
    const ecs::ComponentCatalog* catalog = host.catalog();
    if (db == nullptr || catalog == nullptr) {
        out.push_back({true, "접속 전입니다"});
        return false;
    }
    ecs::Json value = contentEditValue(kind, id, text);
    if (value.is_discarded() || !value.is_object()) {
        out.push_back({true, "JSON 객체가 아닙니다 (괄호 · 따옴표 · 쉼표를 확인하십시오)"});
        return false;
    }
    content::ContentOverlay overlay = db->overlay();
    overlay.set(kind, id, std::move(value));
    const content::ContentLoadResult r = content::ContentLoader::applyOverlay(*db, overlay, *catalog);
    std::string file = std::format("overlay/{}/{}.json", content::contentKindFolder(kind), id);
    if (kind == content::ContentKind::Script) {
        // 스크립트 진단은 팩 파일 자리로 온다 ("<팩 폴더>/scripts/a.ts" · 포인터 "줄:열")
        const auto f = scriptFileOf(*db, id);
        file = f ? f->display : file;
    }
    for (const content::ContentIssue& i : r.issues) {
        const bool error = i.severity == content::ContentIssue::Severity::Error;
        if (error || i.file == file) { // 경고는 이 항목 것만 (팩의 다른 경고는 늘 있던 것)
            std::string line = i.describe();
            if (kind == content::ContentKind::Script && i.file == file) {
                line += scriptExcerpt(text, i.pointer);
            }
            if (!i.help.empty()) {
                line += "\n  고치는 법: " + i.help; // v0.9 M6-T4
            }
            out.push_back({error, std::move(line)});
        }
    }
    return r.ok();
}

ScriptSaveResult saveScriptEdit(const content::ContentDatabase& db, services::DocumentService& docs,
                                const std::string& id, const std::string& text,
                                std::map<std::string, SavedScript, std::less<>>& saved, bool force) {
    ScriptSaveResult r;
    const auto file = scriptFileOf(db, id);
    if (!file || file->disk.empty()) {
        r.message = std::format("'{}' 의 팩 폴더를 모릅니다 — 팩 파일에 저장할 수 없습니다", id);
        return r;
    }
    if (file->installed) {
        r.message = std::format(
            "{} — 설치한 패키지의 파일은 고치지 않습니다 (프로젝트 팩에서 고치거나 [적용 (오버레이)])", file->display);
        return r;
    }
    std::optional<std::string_view> base;
    if (const auto it = saved.find(id); it != saved.end()) {
        base = it->second.written;
    } else if (file->text != nullptr) {
        base = *file->text;
    }
    r = saveScriptFile(docs, file->disk, text, base, force);
    if (r.status == ScriptSaveResult::Status::Saved) {
        saved[id] = SavedScript{r.written, text};
    }
    return r;
}

ecs::Json contentEditValue(content::ContentKind kind, const std::string& id, const std::string& text) {
    if (kind == content::ContentKind::Script) {
        return ecs::Json{{"id", id}, {"source", text}}; // 스크립트 오버레이 값 (ADR-0066)
    }
    return ecs::Json::parse(text, nullptr, false);
}

std::string scriptExcerpt(const std::string& source, std::string_view pointer) {
    // pointer = "줄:열" (1 부터 — 열은 코드 포인트, 진단과 같은 단위) → "\n  12 | <그 줄>\n     |     ^" (길면 자른다).
    // 줄 · 열은 텍스트 위치 계약으로 (v0.9 NG-05 — 줄 끝 CRLF · CR · U+2028 도 컴파일러와 같게, 줄 끝 글자는 보이지
    // 않는다). ^ 줄은 화면 칸 (한글 2 칸) 만큼 띄우고 탭은 탭으로 (그 줄과 같은 들여쓰기 — 고정폭 글꼴에서 맞는다)
    const usize colon = pointer.find(':');
    const auto number = [](std::string_view d) {
        u32 n = 0;
        if (d.empty() || d.size() > 9) {
            return u32{0};
        }
        for (const char ch : d) {
            if (ch < '0' || ch > '9') {
                return u32{0};
            }
            n = n * 10 + static_cast<u32>(ch - '0');
        }
        return n;
    };
    const u32 line = number(pointer.substr(0, colon));
    const u32 column = colon == std::string_view::npos ? 0 : number(pointer.substr(colon + 1));
    if (line == 0 || text::lineColAt(source, source.size()).line < line) {
        return {};
    }
    const std::string_view whole = text::lineAt(source, line);
    constexpr usize kMax = 160;
    usize cut = whole.size();
    if (cut > kMax) {
        cut = kMax;
        while (cut > 0 && (static_cast<unsigned char>(whole[cut]) & 0xC0) == 0x80) {
            --cut; // UTF-8 글자 가운데를 자르지 않는다
        }
    }
    std::string out = std::format("\n  {} | {}{}", line, whole.substr(0, cut), cut < whole.size() ? "…" : "");
    if (column >= 1) {
        const usize at = text::offsetAt(whole, {1, column});
        if (at <= cut) {
            std::string pad;
            for (usize i = 0; i < at;) {
                const usize start = i;
                const char32_t cp = utf8::decodeNext(whole, i);
                pad += cp == U'\t' ? std::string("\t")
                                   : std::string(text::displayWidth(whole.substr(start, i - start)), ' ');
            }
            out += std::format("\n  {} | {}^", std::string(std::to_string(line).size(), ' '), pad);
        }
    }
    return out;
}

bool showScriptLine(ContentPanelState& st, const content::ContentDatabase& db, const services::DebugLocation& at) {
    const std::optional<std::string> id = content::scriptOverlayId(at.pack, at.file);
    if (!id) {
        return false;
    }
    if (st.kind != content::ContentKind::Script || st.selected != *id) {
        const std::optional<std::string> text = currentTextOf(db, content::ContentKind::Script, *id);
        if (!text) {
            return false;
        }
        st.kind = content::ContentKind::Script;
        st.selected = *id;
        st.isNew = false;
        st.pendingFrom.reset();
        st.text = st.loaded = *text;
        st.issues.clear();
        st.checked.clear();
        st.saveAsk = ContentPanelState::SaveAsk::None;
    }
    // 줄 → 바이트 (텍스트 위치 계약 — 열 1 은 그 줄의 처음)
    st.script.gotoOffset = text::offsetAt(st.text, {at.line, 1});
    return true;
}

std::string scriptTemplate(const std::string& id) {
    return std::format(R"(import {{ defineSystem }} from "sandbox"

// 19-SANDBOXSCRIPT 17 · 18장 — ctx.query · ctx.read · ctx.write (16장)
export default defineSystem({{
  id: "{}",
  onTick(ctx) {{
  }},
}})
)",
                       id);
}

ecs::Json contentTemplate(content::ContentKind kind, const std::string& id) {
    const std::string pack = id.substr(0, id.find('.'));
    switch (kind) {
    case content::ContentKind::Prefab:
        return ecs::Json{{"id", id},
                         {"name", ""},
                         {"tags", ecs::Json::array()},
                         {"components", ecs::Json{{"core.transform", ecs::Json::object()}}}};
    case content::ContentKind::Rule:
        return ecs::Json{
            {"id", id},
            {"action", "act"},
            {"range", 1.0},
            {"effects", ecs::Json::array({ecs::Json{{"op", "event"}, {"name", pack + ".event"}, {"who", "target"}}})}};
    case content::ContentKind::Trigger:
        return triggerTemplate(id);
    case content::ContentKind::Script:
        return ecs::Json{{"id", id}, {"source", scriptTemplate(id)}};
    case content::ContentKind::Behavior:
        return ecs::Json{
            {"id", id},
            {"initial", "idle"},
            {"states", ecs::Json::array(
                           {ecs::Json{{"id", "idle"}, {"onTick", ecs::Json::array({ecs::Json{{"action", "idle"}}})}}})},
            {"transitions", ecs::Json::array()}};
    }
    return ecs::Json::object();
}

void drawContentPanel(Editor& editor, ContentPanelState& st, IEditorHost& host, bool& open) {
    if (!open) {
        st.wasOpen = false;
        return; // 큰 창 — 필요할 때 연다
    }
    if (!std::exchange(st.wasOpen, true)) {
        ImGui::SetNextWindowFocus(); // 다른 패널에 가리지 않게
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f},
                            ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({600.f * dpi(), 470.f * dpi()}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("콘텐츠", &open, kPanelFlags)) {
        ImGui::End();
        return;
    }
    const content::ContentDatabase* db = host.content();
    if (db == nullptr) {
        ImGui::TextDisabled("접속 전");
        ImGui::End();
        return;
    }
    const bool editable = host.permissions().content;

    // 종류
    const auto kindButton = [&](const char* label, content::ContentKind k) {
        if (ImGui::RadioButton(label, st.kind == k) && st.kind != k) {
            st.kind = k;
            st.saveAsk = ContentPanelState::SaveAsk::None;
            st.selected.clear();
            st.isNew = false;
            st.issues.clear();
        }
    };
    kindButton("규칙 (Rule)", content::ContentKind::Rule);
    ImGui::SameLine();
    kindButton("행동 (Behavior)", content::ContentKind::Behavior);
    ImGui::SameLine();
    kindButton("Prefab", content::ContentKind::Prefab);
    ImGui::SameLine();
    kindButton("트리거", content::ContentKind::Trigger);
    ImGui::SameLine();
    kindButton("스크립트", content::ContentKind::Script);
    ImGui::SameLine();
    ImGui::TextDisabled("· 오버레이 %zu", db->overlay().size());

    // 서버 값이 바뀌면 다시 불러온다: 보낸 것이 받아들여졌거나(pendingFrom 과 달라짐), 고치지 않은 채 남이 바꿨다.
    // 없어진 항목(오버레이로 더한 것을 뺐다)은 놓는다
    const std::optional<std::string> now =
        st.selected.empty() ? std::nullopt : currentTextOf(*db, st.kind, st.selected);
    const std::string current = now.value_or(std::string());
    if (!st.selected.empty()) {
        if (st.pendingFrom) {
            if (current != *st.pendingFrom) {
                st.pendingFrom.reset();
                st.isNew = false;
                st.text = st.loaded = current;
                if (!now) {
                    st.selected.clear();
                }
            }
        } else if (!st.isNew) {
            if (!now) {
                st.selected.clear();
            } else if (st.text == st.loaded && current != st.loaded) {
                st.text = st.loaded = current;
                st.issues.clear();
            }
        }
    }

    // 왼쪽: 목록
    ImGui::BeginChild("##list", {200.f * dpi(), 0}, ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "거르기", st.filter.data(), st.filter.size());
    const std::string_view filter(st.filter.data());
    // 팩별 묶음 (v0.4 M4-T3) — 프로젝트 팩 · 설치한 패키지 · 그 밖. 팩이 하나면 머리 없이
    std::vector<Item> items;
    std::vector<std::string_view> ids;
    for (Item& item : itemsOf(*db, st.kind)) {
        if (containsIgnoreCase(item.id, filter)) {
            items.push_back(std::move(item));
        }
    }
    for (const Item& item : items) {
        ids.push_back(item.id);
    }
    const std::vector<PackGroup> groups = groupByPack(*db, ids);
    for (const PackGroup& g : groups) {
        if (groups.size() > 1) {
            ImGui::SeparatorText(g.label.c_str());
        }
        for (const usize i : g.items) {
            const Item& item = items[i];
            const std::string label = std::format("{}{}", item.overlay ? "● " : "   ", item.id);
            if (ImGui::Selectable(label.c_str(), st.selected == item.id && !st.isNew)) {
                st.selected = std::string(item.id);
                st.saveAsk = ContentPanelState::SaveAsk::None;
                st.isNew = false;
                st.pendingFrom.reset();
                st.text = st.loaded = currentTextOf(*db, st.kind, item.id).value_or(std::string());
                st.issues.clear();
                st.checked.clear();
            }
        }
    }
    if (editable) {
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(
            "##newid", st.kind == content::ContentKind::Script ? "새 id (팩.경로 — eco.ai.wander)" : "새 id (팩.이름)",
            st.newId.data(), st.newId.size());
        if (ImGui::Button("새 항목") && st.newId[0] != '\0') {
            st.selected = st.newId.data();
            const std::optional<std::string> existing = currentTextOf(*db, st.kind, st.selected);
            st.isNew = !existing.has_value();
            st.text = existing                                  ? *existing
                      : st.kind == content::ContentKind::Script ? scriptTemplate(st.selected)
                                                                : contentTemplate(st.kind, st.selected).dump(2);
            st.loaded = st.isNew ? std::string() : st.text;
            st.issues.clear();
            st.checked.clear();
            st.pendingFrom.reset();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // 오른쪽: 고르기 · JSON · 단추 · 검사 결과
    ImGui::BeginChild("##edit", {0, 0});
    if (st.selected.empty()) {
        ImGui::TextDisabled("왼쪽에서 항목을 고르십시오 (● = 이 월드가 바꾼 것)");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    const content::ContentOverlay& overlay = db->overlay();
    const bool inOverlay = overlay.find(st.kind, st.selected) != nullptr;
    ImGui::TextUnformatted(st.selected.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", st.isNew    ? "새 항목 (적용하면 오버레이에 더한다)"
                              : inOverlay ? "월드 오버레이"
                                          : "팩 원본 (적용하면 오버레이로)");
    if (st.kind == content::ContentKind::Script) {
        if (const auto f = scriptFileOf(*db, st.selected)) {
            // 팩 파일과 같은가 — 이 창의 마지막 저장 · 없으면 팩을 읽은 때의 원문 (v0.9 M6-T8 조각 3)
            const auto s = st.saved.find(st.selected);
            const std::string* disk = s != st.saved.end() ? &s->second.text : f->text;
            const char* fileState = disk == nullptr    ? "팩 파일 없음 — [저장] 하면 만든다"
                                    : *disk == st.text ? "팩 파일과 같음"
                                                       : "팩 파일과 다름";
            ImGui::TextDisabled(
                "%s · %s — [적용 (오버레이)] = 지금 월드 (서버가 다시 컴파일), [저장 (팩 파일)] = 다음 로드부터",
                f->display.c_str(), fileState);
        }
    }
    if (!st.isNew) {
        summary(*db, st.kind, st.selected);
    }
    if (!editable) {
        ImGui::TextDisabled("보기만 — 지금 역할로는 콘텐츠를 바꿀 수 없습니다");
    }
    const f32 issuesHeight = 90.f * dpi();
    const ImVec2 editSize{-1, std::max(120.f * dpi(), ImGui::GetContentRegionAvail().y - issuesHeight)};
    if (st.kind == content::ContentKind::Trigger) {
        // 트리거: 폼 (원본 JSON 을 바로 고친다 — 검사 · 적용은 아래 그대로) · JSON
        if (ImGui::BeginTabBar("##trigger_tabs")) {
            if (ImGui::BeginTabItem("폼")) {
                ImGui::BeginChild("##form", {editSize.x, editSize.y - ImGui::GetFrameHeightWithSpacing()},
                                  ImGuiChildFlags_Borders);
                ecs::Json j = ecs::Json::parse(st.text, nullptr, false);
                if (j.is_discarded() || !j.is_object()) {
                    ImGui::TextColored({1.f, 0.5f, 0.4f, 1.f}, "JSON 이 깨져 있습니다 — JSON 탭에서 고치십시오");
                } else if (drawTriggerForm(j, st.trigger, *db, !editable)) {
                    st.text = j.dump(2);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("JSON")) {
                inputJson("##json", st.text, {editSize.x, editSize.y - ImGui::GetFrameHeightWithSpacing()}, !editable);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else if (st.kind == content::ContentKind::Script) {
        // 스크립트 편집기 (v0.9 M6-T8) — 작업 공간 = 이 파일의 팩 스크립트 (오버레이 반영) + 편집 중인 글
        const std::string id = st.selected;
        // 디버거 (v0.9 ST-06-7): 이 파일의 중단점 · 멈춘 줄
        const auto path = content::scriptOverlayPath(id);
        st.script.breakpointGutter = st.debugger != nullptr && path.has_value();
        st.script.breakpointLines.clear();
        st.script.execLine.reset();
        if (st.script.breakpointGutter) {
            st.script.breakpointLines = st.debugger->breakpointLines(path->pack, path->rel);
            if (const auto at = st.debugger->pausedAt(); at && at->pack == path->pack && at->file == path->rel) {
                st.script.execLine = at->line;
            }
        }
        (void)drawScriptEditor("##script", st.text, editSize, !editable, st.script,
                               [db, id](std::string_view t) { return scriptWorkspace(*db, id, t); });
        if (st.script.toggledLine && st.script.breakpointGutter) {
            (void)st.debugger->toggleBreakpoint(path->pack, path->rel, *st.script.toggledLine);
            st.script.toggledLine.reset();
        }
    } else {
        inputJson("##json", st.text, editSize, !editable);
    }
    const bool dirty = st.isNew || st.text != st.loaded;
    if (ImGui::Button("검사")) {
        (void)checkContentEdit(host, st.kind, st.selected, st.text, st.issues);
        st.checked = st.text;
        if (st.issues.empty()) {
            st.issues.push_back({false, "문제 없음"});
        }
    }
    if (editable) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button(st.kind == content::ContentKind::Script ? "적용 (오버레이)" : "적용")) {
            st.checked = st.text;
            if (checkContentEdit(host, st.kind, st.selected, st.text, st.issues)) {
                if (editor.editContent(host, st.kind, st.selected, contentEditValue(st.kind, st.selected, st.text))) {
                    st.issues.push_back({false, "보냈습니다 — 서버가 받아들이면 모두에게 적용됩니다"});
                    st.pendingFrom = current;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("고친 것 버리기")) {
            st.text = st.loaded;
            st.issues.clear();
            if (st.isNew) {
                st.selected.clear();
                st.isNew = false;
            }
        }
        ImGui::EndDisabled();
        if (st.kind == content::ContentKind::Script && st.documents != nullptr) {
            // [저장 (팩 파일)] — 검사가 오류면 묻고, 바깥에서 파일이 바뀌었으면 묻는다 (v0.9 M6-T8 조각 3)
            const auto save = [&](bool force) {
                const ScriptSaveResult r = saveScriptEdit(*db, *st.documents, st.selected, st.text, st.saved, force);
                st.saveAsk = r.status == ScriptSaveResult::Status::DiskChanged ? ContentPanelState::SaveAsk::Overwrite
                                                                               : ContentPanelState::SaveAsk::None;
                st.issues.push_back({r.status != ScriptSaveResult::Status::Saved,
                                     r.status == ScriptSaveResult::Status::Saved
                                         ? r.message + " — 지금 월드에는 [적용 (오버레이)], 팩 파일은 다음 로드부터"
                                         : r.message});
            };
            ImGui::SameLine();
            if (ImGui::Button("저장 (팩 파일)")) {
                st.checked = st.text;
                if (checkContentEdit(host, st.kind, st.selected, st.text, st.issues)) {
                    save(false);
                } else {
                    st.saveAsk = ContentPanelState::SaveAsk::Errors;
                }
            }
            if (st.saveAsk == ContentPanelState::SaveAsk::Errors) {
                ImGui::SameLine();
                if (ImGui::Button("오류가 있어도 저장")) {
                    save(false);
                }
            } else if (st.saveAsk == ContentPanelState::SaveAsk::Overwrite) {
                ImGui::SameLine();
                if (ImGui::Button("덮어쓰기")) {
                    save(true);
                }
            }
        }
        if (inOverlay) {
            ImGui::SameLine();
            if (ImGui::Button("팩 값으로")) {
                if (editor.editContent(host, st.kind, st.selected, nullptr)) {
                    st.pendingFrom = current;
                }
                st.issues.clear();
            }
        }
    }
    ImGui::BeginChild("##issues", {0, 0}, ImGuiChildFlags_Borders);
    if (st.kind == content::ContentKind::Script) {
        // 실시간 진단 (입력 멈춤 300 ms 뒤 검사기 — 누르면 그 자리로)
        const auto& marks = st.script.assist.marks();
        const auto errors =
            static_cast<usize>(std::ranges::count_if(marks, [](const ScriptMark& m) { return m.error; }));
        if (st.script.assist.marksStale()) {
            ImGui::TextDisabled("실시간 진단 — 입력이 멈추면 다시 검사합니다");
        } else if (marks.empty()) {
            ImGui::TextDisabled("실시간 진단 — 문제 없음");
        } else {
            ImGui::TextDisabled("실시간 진단 — 오류 %zu · 경고 %zu (누르면 그 자리로)", errors, marks.size() - errors);
        }
        for (usize i = 0; i < marks.size(); ++i) {
            const ScriptMark& m = marks[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, m.error ? ImVec4{1.f, 0.5f, 0.4f, 1.f} : ImVec4{1.f, 0.8f, 0.4f, 1.f});
            const std::string first = m.text.substr(0, m.text.find('\n'));
            if (ImGui::Selectable(first.c_str())) {
                st.script.gotoOffset = m.begin;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && first.size() != m.text.size()) {
                ImGui::SetTooltip("%s", m.text.c_str());
            }
            ImGui::PopID();
        }
        if (!marks.empty() || !st.issues.empty()) {
            ImGui::Separator();
        }
    }
    for (const ContentPanelState::Issue& i : st.issues) {
        ImGui::PushTextWrapPos(0);
        if (i.error) {
            ImGui::TextColored({1.f, 0.5f, 0.4f, 1.f}, "%s", i.text.c_str());
        } else {
            ImGui::TextColored({0.6f, 0.9f, 0.6f, 1.f}, "%s", i.text.c_str());
        }
        ImGui::PopTextWrapPos();
    }
    if (!editor.lastMessage().empty() && editor.lastMessage().find("콘텐츠") != std::string::npos) {
        ImGui::TextColored({1.f, 0.6f, 0.4f, 1.f}, "%s", editor.lastMessage().c_str());
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();
}

} // namespace sbx::editor
