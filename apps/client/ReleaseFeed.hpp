#pragma once
// 새 버전 알림 (v0.5 M1-T3 — docs/20-CREATOR-ALPHA.md 7.6). 릴리스 피드를 읽어 이 클라이언트보다 새 버전이 있으면
// 알림만 — 받기 · 설치는 사용자 (자동 업데이트는 v0.9).
//
//   피드 형식  GitHub REST "List releases" 응답 그대로 — JSON 배열, 원소마다 tag_name · html_url · draft ·
//              prerelease 만 읽는다. draft 는 뺀다. tag_name 은 "v" 를 떼고 SemVer 여야 한다 (아니면 건너뛴다).
//   읽는 곳    --update-feed <파일> · client.json updateFeed (파일). 공개 Alpha 채널 (GitHub Releases) 을
//              네트워크로 읽는 것은 [계획] — 채널을 만드는 M1-T4 에서 (HTTPS 클라이언트를 함께 정한다).
//              피드를 읽지 못하면 조용히 (로그만)

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::client {

struct ReleaseInfo {
    SemVer version;
    std::string tag; // "v0.5.0"
    std::string url; // html_url (비면 없음)
    bool prerelease = false;
};

// draft · SemVer 가 아닌 태그는 뺀다. JSON 배열이 아니면 ParseError
[[nodiscard]] Expected<std::vector<ReleaseInfo>> parseReleaseFeed(std::string_view jsonText);

// current 보다 높은 것 중 가장 높은 것 (SemVer 우선순위 — 프리릴리스도 넣는다: Alpha 채널)
[[nodiscard]] std::optional<ReleaseInfo> newestRelease(const std::vector<ReleaseInfo>& releases, const SemVer& current);

// "새 버전 0.5.1 이 있다 (지금 0.5.0) — <url>. 받기 · 설치는 직접"
[[nodiscard]] std::string updateNotice(const ReleaseInfo& release, const SemVer& current);

// 피드 (파일) 를 읽어 새 버전이면 알림 문장. 없는 파일 · 읽기 실패 · 형식 오류 · URL ([계획]) 은 조용히 nullopt
// (로그만)
[[nodiscard]] std::optional<std::string> checkReleaseFeed(std::string_view feed, const SemVer& current);

} // namespace sbx::client
