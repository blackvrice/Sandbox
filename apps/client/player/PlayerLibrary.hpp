#pragma once
// Player 모드의 최근 게임 · 오류 문장 (v0.8 NG-03 — docs/25-STUDIO-ARCHITECTURE.md 12장, ADR-0098 1 · ADR-0121). Studio
// 와 따로 — 사용자 설정 폴더의 player.json {"schemaVersion": 1, "recentGames": [{path, gameId, name, version,
// playedAt}]} (게임을 받는 사람의 것 — studio.json 과 섞지 않는다). 깨진 파일은 player.json.broken 으로 옮기고 빈 목록
// + 경고 (M1-T3 규칙), 파일 이름이 비면 (안전 모드) 저장하지 않는다. 10 개까지, 같은 경로는 하나, 새로 연 것이 맨 앞.
// 읽을 때 없어진 파일은 missing (지우지 않는다 — 외장 디스크).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"

namespace sbx::client {

struct RecentGame {
    std::filesystem::path path; // .sbpkg
    std::string gameId;
    std::string name;
    std::string version;
    std::string playedAt; // UTC "2026-11-10T09:12:03Z"
    bool missing = false;
};

class PlayerLibrary {
public:
    static constexpr usize kMaxRecent = 10;

    explicit PlayerLibrary(std::filesystem::path file);
    [[nodiscard]] const std::vector<RecentGame>& recent() const noexcept { return m_recent; }
    void remember(RecentGame game); // playedAt 은 지금 (비었을 때)
    void forget(const std::filesystem::path& path);
    [[nodiscard]] std::vector<std::string> takeWarnings();

private:
    void load();
    void save();

    std::filesystem::path m_file;
    std::vector<RecentGame> m_recent;
    std::vector<std::string> m_warnings;
};

// 게임을 열지 못한 이유를 사람의 말로 (ADR-0098 1 — 형식 · 범위 · 손상 · 엔진 낮음 · 없음). 제목 한 줄 + 할 일 한 줄
struct PlayError {
    std::string title;  // "이 게임 파일은 손상됐다"
    std::string advice; // "받은 곳에서 다시 받는다"
    std::string detail; // 원래 오류 (보고 묶음 · 자세히)
};
[[nodiscard]] PlayError describePlayError(const Error& error);

// 패키지의 정체성 (이름 · gameId · 판 — 패키지 index 만 읽는다). 읽지 못하면 경로만
[[nodiscard]] RecentGame recentGameOf(const std::filesystem::path& package);

} // namespace sbx::client
