#pragma once
// 민감정보 제거 (v0.5 M1-T1 — 충돌 · 오류 보고 묶음, docs/20-CREATOR-ALPHA.md 7.1).
//
//   비밀       secrets 의 문자열 (소유자 열쇠 · 토큰) → "<SECRET>"   — 정확히 같은 바이트
//   경로       homes 의 폴더 (사용자 폴더 — '\' · '/' 어느 쪽이든, 대소문자 무시) → "<HOME>"
//   사용자     userName (3 바이트 이상) 이 낱말로 나오면 (앞뒤가 영문 · 숫자 · '_' 가 아니다, 대소문자 무시) → "<USER>"
//   주소       IPv4 a.b.c.d → "<IP>" (127.0.0.1 · 0.0.0.0 는 그대로 — 자기 기계), 전자우편 x@y.z → "<EMAIL>"
//
// 순서: 비밀 → 경로 → 전자우편 → 주소 → 사용자. 같은 입력 = 같은 출력 (시험이 고정 문자열로 본다).

#include <string>
#include <string_view>
#include <vector>

namespace sbx::diag {

struct ScrubRules {
    std::vector<std::string> secrets;
    std::vector<std::string> homes;
    std::string userName;
};

// 이 프로세스의 기본 규칙: 사용자 폴더 (USERPROFILE · HOME), 사용자 이름 (USERNAME · USER). 비밀은 부르는 쪽이 더한다
[[nodiscard]] ScrubRules defaultScrubRules();

[[nodiscard]] std::string scrub(std::string_view text, const ScrubRules& rules);

} // namespace sbx::diag
