#pragma once
// 스크립트 편집기의 도움 — UI 를 모르는 부분 (v0.9 M6-T8 조각 2 — docs/27-SCRIPT-IDE.md 3 · 4장, ADR-0130). 위젯
// (editor/ui/ScriptEditor) 이 그리고, 이 서비스가 정한다:
//
//   작업 공간  편집 중인 파일의 팩 스크립트 (오버레이 소스가 있으면 그것, 편집 중인 파일은 편집기 글) + 의존 팩의
//              스크립트 ("@<팩>/…", 이어지는 의존까지) — 콘텐츠 로더의 컴파일과 같은 입력 (같은 진단이 나온다)
//   진단       검사기를 돌려 그 파일의 진단 → 밑줄 범위 (진단 자리부터 그 이름 끝 — 이름이 아니면 글자 하나, 줄 끝 ·
//              글 끝이면 앞 글자). 분석은 입력이 300 ms 멈춘 뒤 (편집 번호 — 늦은 결과는 버린다)
//   완성       '.' · Ctrl+Space 에 묻는다. 처음 물은 후보를 커서 앞 접두로 다시 거른다 (글자마다 검사기를 돌리지
//              않는다) — 접두가 처음보다 짧아지면 다시 묻고, 이름을 벗어나면 닫는다
//   hover      (편집 번호, 자리) 가 같으면 다시 묻지 않는다
//
// 위치는 모두 UTF-8 바이트 offset (텍스트 위치 계약 — NG-05).

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Types.hpp"
#include "script/lang/LanguageService.hpp"

namespace sbx::content {
class ContentDatabase;
}

namespace sbx::editor {

struct ScriptWorkspace {
    std::vector<script::SourceFile> files; // 팩 파일 ("scripts/a.ts") · 의존 팩 파일 ("@inv/scripts/b.ts")
    std::vector<script::PackImports> imports;
    usize file = 0; // 편집 중인 파일의 자리

    [[nodiscard]] script::LanguageQuery query(usize offset) const noexcept;
};

// 오버레이 스크립트 id ("t.regen" — scriptOverlayPath) 의 작업 공간. 그 팩을 읽지 않았으면 nullopt
[[nodiscard]] std::optional<ScriptWorkspace> scriptWorkspace(const content::ContentDatabase& db,
                                                             std::string_view overlayId, std::string_view text);

struct ScriptMark {
    usize begin = 0; // 밑줄 [begin, end) — 바이트
    usize end = 0;
    bool error = true;
    std::string code; // "S406"
    std::string text; // "3:5: 오류 S406 — …" + 고치는 법 줄
};

// 진단 자리 (바이트) → 밑줄 범위
[[nodiscard]] std::pair<usize, usize> markRange(std::string_view text, usize offset) noexcept;
// 작업 공간의 편집 중인 파일의 진단 (차례 = 검사기 차례)
[[nodiscard]] std::vector<ScriptMark> analyzeScript(const ScriptWorkspace& ws);

struct TextReplace {
    usize begin = 0; // [begin, end) 를 text 로
    usize end = 0;
    std::string text;
};

class ScriptAssist {
public:
    static constexpr f64 kAnalysisDelay = 0.3; // 입력 멈춤 (초 — 27 4장)

    // --- 분석 ---
    // 글을 고쳤다 (now = 초) — 다음 분석은 now + 300 ms 뒤. hover 기억을 지운다
    void edited(f64 now) noexcept;
    // 다른 글로 바뀌었다 (다른 파일을 열었다 · 버렸다) — 진단 · 완성 · hover 를 지우고 곧 분석
    void reset() noexcept;
    [[nodiscard]] bool analysisDue(f64 now) const noexcept;
    // 분석 결과 — version 이 지금 편집 번호가 아니면 버린다 (false)
    bool analyzed(u64 version, std::vector<ScriptMark> marks);
    [[nodiscard]] u64 version() const noexcept { return m_version; }
    [[nodiscard]] const std::vector<ScriptMark>& marks() const noexcept { return m_marks; }
    // 결과가 지금 글보다 옛것인가 (밑줄 자리가 어긋날 수 있다)
    [[nodiscard]] bool marksStale() const noexcept { return m_analyzedVersion != m_version; }
    [[nodiscard]] const ScriptMark* markAt(usize offset) const noexcept;

    // --- 완성 ---
    // 커서 자리에서 묻는다 (ws 의 편집 중인 파일 글 = 지금 글). 후보가 없으면 닫힌 채
    void openCompletion(const ScriptWorkspace& ws, usize cursor);
    // 글 · 커서가 바뀐 뒤 — 접두로 다시 거른다. 이름을 벗어나면 · 남은 후보가 없으면 닫는다. 접두가 처음 물은 접두보다
    // 짧아지면 true (다시 물어야 한다 — openCompletion)
    [[nodiscard]] bool refreshCompletion(std::string_view text, usize cursor);
    void closeCompletion() noexcept;
    void moveSelection(i32 delta) noexcept; // 끝에서 돌아간다
    // 고른 후보로 [접두 시작, 커서) 를 바꾼다 — 닫는다
    [[nodiscard]] std::optional<TextReplace> acceptCompletion(usize cursor);
    [[nodiscard]] bool completionOpen() const noexcept { return m_open; }
    [[nodiscard]] std::span<const script::CompletionItem> completionItems() const noexcept { return m_shown; }
    [[nodiscard]] usize selected() const noexcept { return m_selected; }
    [[nodiscard]] usize completionAnchor() const noexcept { return m_anchor; }

    // --- hover ---
    // (지금 편집 번호, offset) 로 물은 적이 있으면 그 답 (없음도 답이다), 아니면 nullptr
    [[nodiscard]] const std::optional<script::HoverInfo>* cachedHover(usize offset) const noexcept;
    void storeHover(usize offset, std::optional<script::HoverInfo> info);

private:
    void refilter(std::string_view prefix);

    u64 m_version = 0;
    u64 m_analyzedVersion = 0;
    std::optional<f64> m_lastEdit; // 분석을 기다리는 편집 (없으면 기다리지 않는다)
    bool m_analyzeNow = true;      // 처음 · reset 뒤 — 기다리지 않고
    std::vector<ScriptMark> m_marks;

    bool m_open = false;
    usize m_anchor = 0;
    std::string m_basePrefix;
    bool m_dropExact = false; // 이름 완성 — 다 쓴 이름은 보이지 않는다 (complete 와 같다)
    std::vector<script::CompletionItem> m_all;
    std::vector<script::CompletionItem> m_shown;
    usize m_selected = 0;

    std::optional<usize> m_hoverOffset;
    u64 m_hoverVersion = 0;
    std::optional<script::HoverInfo> m_hover;
};

} // namespace sbx::editor
