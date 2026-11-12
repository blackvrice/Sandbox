#pragma once
// "스크립트 디버거" 창 (v0.9 ST-06-7 조각 3 — docs/27-SCRIPT-IDE.md 6장, ADR-0134). 기록 (리플레이) 을 골라 디버그
// 재생 — editor/services/ScriptDebugService 위의 UI:
//
//   기록 · 열기 · 시작 (틱에서 멈추기) · 계속 · 한 줄 (Over) · 들어가기 (Into) · 나가기 (Out) · 그만 · 처음부터
//   멈춘 자리 (팩 · 파일:줄 · 틱 · call) · [편집기에서 보기] (콘텐츠 창의 스크립트 편집기를 그 줄로 — 부르는 쪽이)
//   호출 스택 (고르면 그 프레임의 지역 · 감시), 지역 (이름 · 타입 · 값), 감시 식, 중단점 (편집기의 줄 번호 칸으로도
//   놓는다 · logpoint 글), logpoint 가 남긴 글, 끝나면 재생 결과 (해시 일치 · 끝 해시)
//
// ImGui::NewFrame 과 Render 사이에서. 재생은 디버그 스레드 — 창은 세션의 스냅숏만 읽는다.

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "editor/services/ScriptDebugService.hpp"

namespace sbx::editor {

struct ScriptDebugPanelState {
    std::vector<services::RecordingEntry> recordings;
    bool scanned = false;
    usize selected = 0;
    std::array<char, 16> stopTick{};
    std::array<char, 128> watchInput{};
    std::array<char, 32> bpPack{};
    std::array<char, 128> bpFile{};
    std::array<char, 12> bpLine{};
    std::array<char, 128> bpLog{};
    usize frame = 0;   // 고른 프레임 (0 = 안쪽)
    u64 frameStop = 0; // 그 프레임을 고른 멈춤 번호 (새로 멈추면 안쪽으로)
    usize frameWatchKey = static_cast<usize>(-1);
    std::vector<script::vm::WatchResult> frameWatches; // 안쪽이 아닌 프레임의 감시 (디버그 스레드에 물은 것)
    std::string message;                               // 열기 · 처음부터 오류
    bool wasOpen = false;
};

struct ScriptDebugPanelResult {
    std::optional<services::DebugLocation> showInEditor; // [편집기에서 보기] — 콘텐츠 창의 스크립트를 그 줄로
};

// open 이 false 면 그리지 않는다. savesRoot = 기록을 찾을 폴더, contentRoot = 팩을 읽을 콘텐츠 폴더
ScriptDebugPanelResult drawScriptDebugPanel(services::ScriptDebugService& service, ScriptDebugPanelState& state,
                                            const std::filesystem::path& savesRoot,
                                            const std::filesystem::path& contentRoot, bool& open);

} // namespace sbx::editor
