#pragma once
// 새 버전 알림 (v0.5 M1-T3 · T4 — docs/20-CREATOR-ALPHA.md 7.6, ADR-0084). 릴리스 피드를 읽어 이 클라이언트보다 새
// 버전이 있으면 알림만 — 받기 · 설치는 사용자 (자동 업데이트는 v0.9).
//
//   피드 형식  GitHub REST "List releases" 응답 그대로 — JSON 배열, 원소마다 tag_name · html_url · draft ·
//              prerelease 만 읽는다. draft 는 뺀다. tag_name 은 "v" 를 떼고 SemVer 여야 한다 (아니면 건너뛴다).
//   읽는 곳    --update-feed · client.json updateFeed — 파일 경로, 또는 https:// URL (M1-T4): 시스템 curl 을
//              셸 없이 띄운다 (platform/process — https 만 · 리디렉트도 https · 5 초 · 1 MiB). 다른 스킴 (http:// …)
//              은 거절. 기본 피드 URL 은 없다 — 공개 Alpha 채널 (GitHub Releases) 이 생기는 M7-T4 에서 정한다.
//   실패       없는 파일 · 오프라인 · 시간 초과 · HTTP 오류 · 형식 오류는 조용히 — detail 한 줄 (로그) 만
//
// checkReleaseFeed 는 로그를 쓰지 않는다 — 클라이언트가 백그라운드 스레드에서 부르고 (URL 은 몇 초 걸린다) 결과를
// 프레임에서 받는다 (Application — 알림 창 · 로그).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::client {

inline constexpr usize kMaxFeedBytes = usize{1} << 20; // 1 MiB
inline constexpr int kFeedTimeoutSeconds = 5;
inline constexpr usize kMaxFeedUrlBytes = 2048;

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

// "://" 가 있으면 URL (아니면 파일 경로)
[[nodiscard]] bool isUrlFeed(std::string_view feed);

// https URL 을 읽는 curl 인자 (셸 없음). https 가 아니거나 · 너무 길거나 · 공백 · 제어 문자가 있으면 InvalidArgument
[[nodiscard]] Expected<std::vector<std::string>> curlArguments(std::string_view url, std::string_view userAgent);

struct FeedCheck {
    std::optional<std::string> notice; // 새 버전이면 알림 문장
    std::string detail;                // 로그 한 줄 (새 버전 없음 · 조용한 실패의 이유 · 알림과 같은 문장)
};

[[nodiscard]] FeedCheck checkReleaseFeed(std::string_view feed, const SemVer& current);

} // namespace sbx::client
