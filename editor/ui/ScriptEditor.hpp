#pragma once
// 스크립트 편집기 위젯 (v0.9 M6-T8 조각 2 — docs/27-SCRIPT-IDE.md 3 · 4장, ADR-0130). "콘텐츠" 창의 스크립트 종류가
// 쓴다. ImGui 여러 줄 입력 위에 — 정하는 일은 editor/services/ScriptAssist (UI 를 모른다):
//
//   줄 번호 · 진단 — 왼쪽 줄 번호, 오류 · 경고 줄에 점, 진단 자리에 물결 밑줄 (오류 빨강 · 경고 노랑). 진단은 입력이
//     300 ms 멈춘 뒤 검사기로 (UI 스레드 — Release 5,000 줄 21 ms, Worker 는 [계획])
//   자동 완성 — '.' 를 치면 · Ctrl+Space. 위아래 = 고르기, Enter · Tab = 넣기, Esc = 닫기, 계속 치면 접두로 거른다
//   hover — 마우스를 0.35 초 멈추면 그 자리의 진단 · 서명 · 설명 (언어 서비스 hover)
//   이동 — gotoOffset: 그 바이트로 커서를 옮기고 보이게 스크롤 (진단 목록에서)
//
// 키: ImGui 여러 줄 입력의 Esc 는 활성화 뒤 고친 것을 모두 되돌린다 — 이 편집기에서는 Esc 를 가로채 팝업만 닫는다
// (되돌리기는 Ctrl+Z). 팝업이 열려 있을 때 Enter · Tab 은 글자 거르기 콜백이 버리고 넣기로, 위아래는 커서를 되돌린다.
// 스크롤: 입력 칸을 글 크기로 키우고 바깥 자식 창이 스크롤한다 (줄 번호 · 밑줄을 같은 좌표로 그린다 — 입력 칸 바탕은
// 투명, 밑줄은 그 아래에 먼저 그린다).
//
// ImGui::NewFrame 과 Render 사이에서.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "editor/services/ScriptAssist.hpp"

namespace sbx::editor {

struct ScriptEditorState {
    ScriptAssist assist;
    std::optional<usize> gotoOffset; // 커서를 여기로 (바이트) — 위젯을 활성화하고 적용되면 비운다
    bool focusRequest = false;       // 다음 프레임에 입력 칸에 키보드 포커스
    // 디버거 (v0.9 ST-06-7 조각 3): breakpointGutter 면 줄 번호 칸을 누른 줄이 toggledLine 으로 나온다 (놓고 거두기는
    // 부르는 쪽이). breakpointLines = 빨간 점, execLine = 멈춘 줄 (칠하고 처음 보일 때 스크롤). 줄은 1 부터
    bool breakpointGutter = false;
    std::vector<u32> breakpointLines;
    std::optional<u32> execLine;
    std::optional<u32> toggledLine; // 이번 프레임에 누른 줄 (부르는 쪽이 읽고 비운다)

    // --- 위젯 안 (부르는 쪽은 읽기만) ---
    std::string shadow; // 마지막으로 본 글 — 바깥에서 바꾸면 (다른 파일 · 버리기 · 서버 값) reset
    bool seen = false;
    u32 generation = 0;  // 바깥에서 글을 바꿀 때마다 — 입력 칸의 ImGui id (활성 입력 칸은 제 버퍼로 글을 덮는다)
    bool active = false; // 이번 프레임 입력 칸이 활성
    usize cursor = 0;    // 바이트 (마지막 콜백)
    usize selStart = 0;
    usize selEnd = 0;
    i32 nav = 0;                             // 이번 프레임 팝업 고르기 (위 -1 · 아래 +1)
    usize followed = static_cast<usize>(-1); // 스크롤을 맞춘 커서
    bool layoutDirty = true;
    std::vector<usize> lineStarts{0}; // 줄마다 시작 바이트 ('\n' 다음) — 글이 바뀔 때마다 (자리 → 줄은 이분 탐색)
    f32 contentWidth = 0.f;
    std::optional<u32> shownExecLine; // 스크롤을 맞춘 멈춘 줄
    ImVec2 hoverMouse{-1.f, -1.f};
    f64 hoverSince = 0.0;
};

// 작업 공간 (팩 파일 + 편집 중인 글) — 없으면 진단 · 완성 · hover 없이 편집만
using ScriptWorkspaceFn = std::function<std::optional<ScriptWorkspace>(std::string_view text)>;

// 글을 고쳤으면 true
bool drawScriptEditor(const char* id, std::string& text, ImVec2 size, bool readOnly, ScriptEditorState& st,
                      const ScriptWorkspaceFn& workspace);

// 글 배치 — ImGui 입력 칸과 같은 모양 ('\n' 만 줄을 나눈다, 폭은 지금 글꼴 · 크기). 글 원점 기준
[[nodiscard]] ImVec2 scriptTextPos(std::string_view text, usize offset);
// 그 점 아래 글자의 바이트 (줄 밖 · 줄 끝 너머면 nullopt)
[[nodiscard]] std::optional<usize> scriptTextOffsetAt(std::string_view text, ImVec2 pos);

} // namespace sbx::editor
