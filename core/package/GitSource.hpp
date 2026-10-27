#pragma once
// Git 소스 (v0.4 M2-T2, ADR-0071 3 · ADR-0072). docs/09-SERIALIZATION.md 10장.
//
// 시스템 git 실행 파일로 저장소를 받아 rev (태그 · 커밋) 를 꺼내고, 그 폴더를 로컬 소스처럼 캐시에 넣는다 (PackSource —
// .git 은 digest 에 들지 않는다). 저장소 뿌리가 팩이다 (pack.json — 하위 폴더는 [계획]). 푼 커밋 (40 글자) 을 돌려준다
// — 잠금 파일 (M3) 이 고정한다.
//
// core 는 프로세스를 띄우지 않는다 (OS 헤더 금지 — AGENTS 4장): 부르는 쪽 (sbx · 시험) 이 ProcessRunner 를 넣는다.
// 실행기는 셸을 거치지 않고 argv 를 그대로 넘겨야 한다 (URL · rev 가 셸 문법으로 읽히지 않게). url · rev 는 '-' 로
// 시작할 수 없고 (git 옵션으로 읽히지 않게), git 명령에는 "--" 로 옵션 끝을 표시한다.

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "core/package/PackSource.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::package {

struct ProcessResult {
    int exitCode = 0;
    std::string output; // stdout + stderr
};
// argv[0] = 실행 파일 이름 ("git" — PATH 에서 찾는다). 띄울 수 없으면 (없는 실행 파일) NotFound
using ProcessRunner =
    std::function<Expected<ProcessResult>(std::span<const std::string> argv, const std::filesystem::path& cwd)>;

// url · rev 의 모양 — 맞으면 nullopt, 아니면 이유
[[nodiscard]] std::optional<std::string> checkGitUrl(std::string_view url);
[[nodiscard]] std::optional<std::string> checkGitRev(std::string_view rev);

struct GitFetched {
    CachedPack pack;
    std::string commit; // rev 를 푼 커밋 (40 글자 16 진수)
};

// 저장소를 받아 rev 를 꺼내 캐시에 넣는다. expectId 가 비지 않았으면 pack.json id 와 같아야 한다.
// git 이 없으면 NotFound ("git 이 필요하다"), 받기 · 꺼내기 실패는 IoError (git 의 말을 붙인다)
[[nodiscard]] Expected<GitFetched> fetchGitPack(std::string_view url, std::string_view rev, std::string_view expectId,
                                                const std::filesystem::path& cacheRoot, const ProcessRunner& run);

} // namespace sbx::package
