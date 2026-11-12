#pragma once
// "콘텐츠" 패널 (Phase 12C, docs/10-EDITOR.md 2 · 8장 — Rules · Behaviors 패널의 첫 모양, ADR-0030).
//
//   종류(규칙 · 행동 · Prefab · 트리거) → 목록 (거르기, 오버레이로 바꾼 것 · 더한 것은 "●") → 고른 항목의 JSON (팩
//   파일과 같은 형식) 을 고친다. "검사" = 이 컴퓨터에서 콘텐츠 로더로 (V1~V7 — 서버도 같은 검사를 한다), "적용" =
//   검사가 통과하면 Editor::editContent (EditContent 명령 — 되돌리기 한 칸), "팩 값으로" = 오버레이에서 뺀다, "새 항목"
//   = 틀에서 시작. 행동은 상태 · 전이를, 규칙은 action · 효과를 요약해 보여 준다. 트리거 (v0.2 M4-T3) 는 "폼" 탭
//   (TriggerForm — 노드 · 연결 양식) 과 "JSON" 탭, 요약에 실행 표현 (펼친 명령열). 행동 · 규칙의 폼 · 노드 그래프
//   편집은 [계획]. 역할이 콘텐츠를 바꿀 수 없으면 보기만. 스크립트 (v0.3 M5-T2, ADR-0066) 는 팩의 scripts/*.ts 를 소스
//   그대로 고친다 — 검사 결과의 오류마다 그 줄을 붙여 보이고, 적용 = 오버레이 Script (서버가 팩을 다시 컴파일).
//   스크립트 편집 칸은 ScriptEditor (v0.9 M6-T8 — 입력 멈춤 300 ms 뒤 진단 밑줄 · 자동 완성 · hover), 아래 목록의
//   실시간 진단을 누르면 그 자리로 커서를 옮긴다.
//
// ImGui::NewFrame 과 Render 사이에서. 월드는 Editor 가 명령으로만 바꾼다.

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentOverlay.hpp"
#include "editor/Editor.hpp"
#include "editor/ui/ScriptEditor.hpp"
#include "editor/ui/TriggerForm.hpp"
#include "script/lang/LanguageIndex.hpp"

namespace sbx::editor {

namespace services {
class DocumentService;
class ScriptDebugService;
struct DebugLocation;
} // namespace services

// 이 창이 팩 파일에 쓴 스크립트 (v0.9 M6-T8 조각 3) — 다음 저장의 base · "팩 파일과 같음" 표시
struct SavedScript {
    std::string written; // 디스크에 쓴 바이트 (줄 끝 되돌린 것)
    std::string text;    // 그때의 편집기 글
};

// 참조 한 자리 (v0.9 ST-06-8 — Shift+F12 목록)
struct ScriptReference {
    std::string id;   // 스크립트 오버레이 id ("t.regen")
    std::string file; // "scripts/regen.ts"
    usize offset = 0; // 바이트
    u32 line = 0;
    std::string preview; // 그 줄 (앞뒤 빈칸 없이)
};

struct ContentPanelState {
    content::ContentKind kind = content::ContentKind::Rule;
    std::string selected;          // 고른 항목 id
    std::string text;              // 고치는 JSON
    std::string loaded;            // 마지막으로 불러온 JSON (고치지 않았으면 서버 값이 바뀔 때 다시 불러온다)
    bool isNew = false;            // "새 항목" — 아직 콘텐츠에 없다
    std::array<char, 64> filter{}; // 목록 거르기
    std::array<char, 64> newId{};  // 새 항목 id
    struct Issue {
        bool error = true;
        std::string text;
    };
    std::vector<Issue> issues; // 마지막 검사
    std::string checked;       // 검사한 JSON
    // 적용 · 팩 값으로를 보낸 뒤: 그때의 값 (서버 값이 이것과 달라지면 — 받아들여졌다 — 새 값으로 다시 불러온다)
    std::optional<std::string> pendingFrom;
    bool wasOpen = false;     // 열리는 프레임에 창을 앞으로
    TriggerFormState trigger; // 트리거 폼 (v0.2 M4-T3)
    ScriptEditorState script; // 스크립트 편집기 (v0.9 M6-T8 — 줄 번호 · 밑줄 · 완성 · hover)
    // [저장 (팩 파일)] (v0.9 M6-T8 조각 3) — Application 이 문서 서비스를 넣는다 (없으면 저장 단추 없음)
    services::DocumentService* documents = nullptr;
    std::map<std::string, SavedScript, std::less<>> saved; // 스크립트 id → 이 창의 마지막 저장
    // 스크립트 디버거 (v0.9 ST-06-7) — Application 이 넣는다: 줄 번호 칸을 누르면 중단점, 멈춘 줄을 칠한다
    services::ScriptDebugService* debugger = nullptr;
    enum class SaveAsk : u8 { None, Errors, Overwrite };
    SaveAsk saveAsk = SaveAsk::None; // 저장을 멈추고 묻는 중 (오류가 있다 · 바깥에서 파일이 바뀌었다)
    // 색인 (v0.9 ST-06-8): 스크립트마다 적용 · 저장 전의 글 (다른 파일로 옮겨도 남는다 · 이름 바꾸기가 다른 파일에 쓴다
    // — 진단 · 색인의 작업 공간도 이 글), 참조 목록, 이름 바꾸기 · 빠른 수정 창
    ScriptDrafts drafts;
    std::vector<ScriptReference> references;
    std::string referencesOf; // 참조 목록의 이름
    bool renameOpen = false;
    usize renameOffset = 0;
    std::array<char, 64> renameInput{};
    bool quickFixOpen = false;
    std::vector<script::QuickFix> quickFixes;
};

// 고친 JSON 을 이 컴퓨터에서 검사한다 (오버레이에 넣어 콘텐츠 로더로). JSON 이 아니면 그 오류 하나.
// 이 항목과 관계된 문제만 (다른 파일의 경고는 뺀다). 오류가 없으면 true
[[nodiscard]] bool checkContentEdit(const IEditorHost& host, content::ContentKind kind, const std::string& id,
                                    const std::string& text, std::vector<ContentPanelState::Issue>& out);
// "새 항목" 틀 (팩 파일 형식)
[[nodiscard]] ecs::Json contentTemplate(content::ContentKind kind, const std::string& id);
// 고친 텍스트 → 오버레이 값: JSON 종류는 파싱 (실패하면 discarded), 스크립트 (v0.3 M5-T2) 는 {"id", "source": 텍스트}
[[nodiscard]] ecs::Json contentEditValue(content::ContentKind kind, const std::string& id, const std::string& text);
// [저장 (팩 파일)] (v0.9 M6-T8 조각 3, ADR-0131) — 스크립트 오버레이 id 의 팩 파일 (읽은 팩 폴더 / scripts/…) 에 편집기
// 글을 쓴다. base = 이 창의 마지막 저장 (saved) · 없으면 팩을 읽은 때의 원문. 설치한 패키지 (.sandbox/packages/) 의
// 파일 · 폴더를 모르는 팩은 쓰지 않는다. 저장되면 saved 를 고친다. 검사 (오류) 는 부르는 쪽이 먼저 한다
[[nodiscard]] ScriptSaveResult saveScriptEdit(const content::ContentDatabase& db, services::DocumentService& docs,
                                              const std::string& id, const std::string& text,
                                              std::map<std::string, SavedScript, std::less<>>& saved, bool force);
// 스크립트 열기 (v0.9 ST-06-8) — 지금 스크립트의 고친 글은 drafts 에 남기고 (팩 값과 같으면 지운다) id 를 고른다 (draft
// 가 있으면 그 글). offset 이 있으면 그 바이트로 커서. 콘텐츠에 없는 id 면 false
bool openScript(ContentPanelState& state, const content::ContentDatabase& db, std::string_view id,
                std::optional<usize> offset = std::nullopt);
// 색인 요청 (v0.9 ST-06-8, ADR-0136) — 지금 스크립트 (state.selected · text) 의 offset 에서. 작업 공간은 drafts 를 쓴다
// F12: 정의가 이 파일이면 커서, 다른 파일이면 그 스크립트를 연다. 없으면 false
bool goToDefinition(ContentPanelState& state, const content::ContentDatabase& db, usize offset);
// Shift+F12: 참조 목록 (state.references) — 수
usize findScriptReferences(ContentPanelState& state, const content::ContentDatabase& db, usize offset);
// F2: 이 파일은 편집기에 (pendingEdits — 되돌리기에 남는다), 다른 파일은 drafts 에. 실패면 이유 (빈 글 = 성공)
std::string renameInScripts(ContentPanelState& state, const content::ContentDatabase& db, usize offset,
                            std::string_view newName);
// Ctrl+.: 그 자리의 빠른 수정 · 하나 넣기 (편집기에 — 빠른 수정은 이 파일만 고친다)
[[nodiscard]] std::vector<script::QuickFix> scriptQuickFixes(const ContentPanelState& state,
                                                             const content::ContentDatabase& db, usize offset);
void applyQuickFix(ContentPanelState& state, const script::QuickFix& fix);
// 디버거의 [편집기에서 보기] (v0.9 ST-06-7) — 그 팩 · 파일의 스크립트를 고르고 그 줄로 커서를 옮긴다 (이미 고친 채 열려
// 있으면 글은 그대로). 오버레이 id 로 쓸 수 없는 경로 · 콘텐츠에 없는 파일이면 false
bool showScriptLine(ContentPanelState& state, const content::ContentDatabase& db, const services::DebugLocation& at);
// 새 스크립트 파일의 틀 (시스템 하나 — id 는 파일의 오버레이 id)
[[nodiscard]] std::string scriptTemplate(const std::string& id);
// 진단의 "줄:열" 이 가리키는 소스 줄 — "\n  12 | <줄>" (오류 줄 보이기, 없는 줄이면 빈 문자열)
[[nodiscard]] std::string scriptExcerpt(const std::string& source, std::string_view pointer);

// open 이 false 면 그리지 않는다 ("편집" 패널의 "콘텐츠 창" 단추가 켠다 — 창의 X 가 끈다)
void drawContentPanel(Editor& editor, ContentPanelState& state, IEditorHost& host, bool& open);

} // namespace sbx::editor
