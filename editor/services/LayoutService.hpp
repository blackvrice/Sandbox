#pragma once
// Studio 레이아웃 (v0.8 M6-T6 — docs/25-STUDIO-ARCHITECTURE.md 4장, ADR-0114). 창 배치 (ImGui 의 .ini 텍스트 — 위치 ·
// 크기 · 접힘 · 도킹) 를 사용자 설정 폴더 studio/layouts/<이름>.ini 에. ImGui 를 모른다 — 텍스트를 받아 두고 돌려줄 뿐,
// 적용 · 꺼내기는 앱 (ImGuiLayer) 의 몫.
//
//   이름       [a-z0-9_-] 1 ~ 32 자 (파일 이름 그대로 — 대소문자 · 경로 문자 없음). "default" = 기본
//              (파일이 없으면 창들의 처음 자리 — 코드의 FirstUseEver)
//   지금 것    studio.json "layout" (StudioSettings — 없거나 틀린 이름이면 default). 다른 이름을 고르거나 그
//              이름으로 저장하면 바뀐다
//   깨진 파일  1 MiB 넘음 · UTF-8 아님 · NUL · ini 줄 ([종류][이름] · 키=값 · ;주석) 이 아닌 줄
//              → "<이름>.ini.broken" 으로 옮기고 기본 배치 + 경고 (M1-T3 의 규칙)
//   폴더가 비면 (안전 모드) 아무 파일도 읽거나 쓰지 않는다 — 이 실행만 기본 배치

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"

namespace sbx::editor::services {

class LayoutService {
public:
    static constexpr std::string_view kDefault = "default";
    static constexpr usize kMaxBytes = usize{1} << 20;
    static constexpr usize kMaxName = 32;

    // folder = <설정 폴더>/studio/layouts, settingsFile = studio.json (비면 지금 레이아웃을 기억하지 않는다), active =
    // studio.json 에서 읽은 이름 (틀리면 default + 경고)
    LayoutService(std::filesystem::path folder, std::filesystem::path settingsFile, std::string active);

    [[nodiscard]] static bool isName(std::string_view name) noexcept;
    // ImGui .ini 모양인가 (위의 "깨진 파일" 규칙). 빈 텍스트는 성하다 (기본 배치)
    [[nodiscard]] static bool isLayoutText(std::string_view text) noexcept;

    [[nodiscard]] bool enabled() const noexcept { return !m_folder.empty(); }
    [[nodiscard]] const std::string& active() const noexcept { return m_active; }
    [[nodiscard]] std::filesystem::path fileFor(std::string_view name) const;
    // default + 저장된 이름들 (이름 순)
    [[nodiscard]] std::vector<std::string> names() const;

    // 지금 레이아웃의 텍스트 (파일이 없으면 "" = 기본 배치, 깨졌으면 옮기고 "" + 경고)
    [[nodiscard]] std::string loadActive();
    // 다른 레이아웃으로 — 그 텍스트 (없는 파일 = "" — 기본 배치에서 시작해 그 이름으로 저장된다). 지금 것을 기억한다
    Expected<std::string> select(std::string_view name);
    // 지금 레이아웃에 쓴다 (ImGui 가 바뀌었다고 할 때 · 끝낼 때). 같은 텍스트면 쓰지 않는다
    Expected<void> save(std::string_view text);
    // 다른 이름으로 저장하고 그것을 지금 것으로
    Expected<void> saveAs(std::string_view name, std::string_view text);

    // 깨진 파일 · 기억 실패 같은 경고 — 꺼내면 비운다
    [[nodiscard]] std::vector<std::string> takeWarnings();

private:
    Expected<std::string> readLayout(std::string_view name);
    void remember();

    std::filesystem::path m_folder;
    std::filesystem::path m_settingsFile;
    std::string m_active;
    std::string m_lastSaved; // 마지막으로 쓰거나 읽은 지금 레이아웃 텍스트 — 같으면 다시 쓰지 않는다
    std::vector<std::string> m_warnings;
};

} // namespace sbx::editor::services
