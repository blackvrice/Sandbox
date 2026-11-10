#pragma once
// Studio 자동 저장 · 크래시 복구 (v0.8 M6-T7 — docs/25-STUDIO-ARCHITECTURE.md 7장, docs/23-ROADMAP.md 1.1, ADR-0113).
// UI · 네트워크를 모른다 — 세이브는 앱이 세션 (같은 프로세스의 로컬 서버) 에 시키고, 이 서비스는 언제 · 어디에 · 무엇을
// 남겼나를 맡는다.
//
// 복구본 = 사용자 설정 폴더의 recovery/<프로젝트 경로 해시>/ — world/ (세이브 형식 그대로 — 09, tick · worldHash 는 그
// world.json 이 정본), documents/<n>.txt (저장하지 않은 문서의 글), recovery.json {project, savedAt, clean, documents:
// [{path, file}]}. 프로젝트 폴더에는 쓰지 않는다 (25 S4 · 24 D3). 프로젝트에 들어가면 (begin) 먼저 clean = false 를
// 적는다 — 첫 자동 저장 전에 죽어도 안다. 주기 (기본 180 초) 와 플레이 시작 전 (requestNow) 에 저장한다. 깨끗이 끝나면
// clean = true. 다음 시작 (begin) 에 clean 이 false 인 복구본이 있으면 그것이 "복구 제안" (pending) — 사용자가 고를
// 때까지 새 자동 저장은 멈춘다 (복구본을 덮지 않는다). 복구 = 복구본 world/ 를 <프로젝트>/worlds/recovered-<n> 으로
// 복사 (있는 이름은 건너뛴다 — 덮어쓰지 않는다) → 앱이 그 세이브를 연다. 버리기 = 복구본 폴더를 지운다.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "foundation/types/Error.hpp"

namespace sbx::editor::services {

class DocumentService;

struct RecoveredDocument {
    std::filesystem::path path; // 원래 문서 경로
    std::filesystem::path file; // 복구본의 글 파일
};

struct RecoveryInfo {
    std::filesystem::path folder;
    std::filesystem::path project;
    std::string savedAt; // "2026-11-10T09:12:03Z" (UTC — 화면 표시용)
    u64 tick = 0;
    u64 worldHash = 0;
    bool clean = false;
    bool hasWorld = false;
    std::vector<RecoveredDocument> documents;
};

class AutosaveService {
public:
    static constexpr f64 kDefaultInterval = 180.0;               // 초
    static constexpr usize kMaxDocumentBytes = 4u * 1024 * 1024; // 저장하지 않은 문서 글의 합 상한

    // root = <설정 폴더>/recovery (비면 꺼짐). intervalSeconds ≤ 0 = 주기 저장 끔 (requestNow 만)
    explicit AutosaveService(std::filesystem::path root, f64 intervalSeconds = kDefaultInterval);

    [[nodiscard]] static std::filesystem::path folderFor(const std::filesystem::path& root,
                                                         const std::filesystem::path& project);
    [[nodiscard]] bool enabled() const noexcept { return !m_root.empty(); }

    // 프로젝트에 들어갔다 (now = 단조 초). 앞 실행이 깨끗이 끝나지 않은 복구본이 있으면 pending
    void begin(const std::filesystem::path& project, f64 now);
    [[nodiscard]] const std::optional<RecoveryInfo>& pending() const noexcept { return m_pending; }
    // 복구 제안을 받아들인다: 복구본 world/ → <프로젝트>/worlds/recovered-<n>. 그 세이브 이름 (앱이 연다)
    Expected<std::string> restore();
    // 복구가 끝났다 (열었다) 또는 버린다 — 복구본 폴더를 지우고 자동 저장을 다시 시작한다
    void discard(f64 now);

    // 지금 세이브할 때인가 (프로젝트 · 복구 제안 없음 · 앞 세이브를 기다리지 않음 · 주기가 됐거나 requestNow)
    [[nodiscard]] bool due(f64 now) const noexcept;
    void requestNow() noexcept { m_now = true; }
    [[nodiscard]] std::filesystem::path worldDir() const { return m_folder / "world"; }
    void markRequested(f64 now) noexcept;
    // 세션이 세이브를 끝냈다 — 실패면 이유를 적고 다음 주기에 다시
    void onWorldSaved(bool ok, u64 tick, u64 worldHash, f64 now);
    // 저장하지 않은 문서의 글을 복구본에 (상한 넘는 것은 건너뛴다). 남긴 수
    usize saveDocuments(const DocumentService& docs);
    // 깨끗이 끝남 — 다음 시작에 복구를 제안하지 않는다
    void finishClean();

    [[nodiscard]] std::optional<RecoveryInfo> read() const; // 지금 프로젝트 복구본의 recovery.json
    // 들어가지 않고 본다 (v0.8 UI-2 — 홈의 "복구 가능" 표시): 그 프로젝트에 깨끗이 끝나지 않은 복구본 (월드 · 문서) 이
    // 있으면. 아무것도 바꾸지 않는다
    [[nodiscard]] std::optional<RecoveryInfo> peek(const std::filesystem::path& project) const;
    [[nodiscard]] u64 saves() const noexcept { return m_saves; }
    [[nodiscard]] std::vector<std::string> takeWarnings();

private:
    void writeInfo(bool clean);
    [[nodiscard]] static std::optional<RecoveryInfo> readFolder(const std::filesystem::path& folder);

    std::filesystem::path m_root;
    f64 m_interval;
    std::filesystem::path m_project;
    std::filesystem::path m_folder;
    std::optional<RecoveryInfo> m_pending;
    f64 m_last = 0;
    bool m_now = false;
    bool m_waiting = false;
    u64 m_saves = 0;
    bool m_hasWorld = false;
    std::vector<RecoveredDocument> m_documents;
    std::vector<std::string> m_warnings;
};

} // namespace sbx::editor::services
