#pragma once
// 패키지 (= 콘텐츠 팩) 의 digest · 로컬 폴더 소스 · 사용자 캐시 (v0.4 M2-T1, ADR-0071 3). docs/09-SERIALIZATION.md
// 10장.
//
//   digest      팩 폴더의 파일마다 "<sha256> <size> <path>\n" 를 path 순으로 이은 SHA-256 (.sbpkg 와 같은 규칙 —
//   packageDigest).
//               path 는 팩 폴더 기준 '/' 구분 — 머신 · 시각 · 폴더 이름과 무관하다
//   훑기        일반 파일 · 폴더만 (심볼릭 링크 · 장치는 거절), 경로 규칙 (project::checkRelativePath — '..' · 역슬래시
//   · 예약
//               이름 금지), 파일 kMaxPackFiles 개 · 하나 kMaxPackFileBytes · 합 kMaxPackBytes 까지. 버전 관리 폴더
//               (.git · .svn · .hg) 와 캐시 표시 파일은 빼고 센다
//   캐시        <cache>/packages/<digest 앞 16>/ — 임시 폴더에 복사하고 다시 훑어 digest 가 같을 때만 이름을 바꿔
//   넣는다 (원자적).
//               있던 것은 다시 훑어 같으면 그대로, 다르면 (누가 고쳤다) 다시 넣는다. 오프라인은 digest 로만 찾는다
//   사용자 캐시 SBX_CACHE_DIR, Windows %LOCALAPPDATA%\sandbox\cache, macOS ~/Library/Caches/sandbox,
//               그 밖 $XDG_CACHE_HOME/sandbox · ~/.cache/sandbox (모두 없으면 임시 폴더)

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/package/GamePackage.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::package {

inline constexpr usize kMaxPackFiles = 4096;
inline constexpr u64 kMaxPackFileBytes = u64{16} << 20; // 16 MiB
inline constexpr u64 kMaxPackBytes = u64{256} << 20;    // 256 MiB

// 팩 폴더의 지금 모습
struct PackSnapshot {
    std::string id;                  // pack.json 의 id
    std::string version;             // pack.json 의 version (없으면 빈 문자열)
    std::vector<PackageEntry> files; // path 순 (offset 은 0)
    u64 bytes = 0;
    std::string digest; // 64 글자 소문자
};

// 팩 폴더를 훑는다 (위 규칙). pack.json 이 없거나 id 가 없으면 ValidationFailed
[[nodiscard]] Expected<PackSnapshot> snapshotPack(const std::filesystem::path& dir);

[[nodiscard]] std::filesystem::path defaultUserCache();

struct CachedPack {
    std::filesystem::path dir; // 캐시 안의 팩 폴더
    PackSnapshot snapshot;
    bool reused = false; // 이미 있던 것을 그대로 썼다
};

// 로컬 폴더 소스를 캐시에 넣는다. expectId 가 비지 않았으면 pack.json 의 id 와 같아야 한다 (의존 키와 다른 팩 거절)
[[nodiscard]] Expected<CachedPack> cacheLocalPack(const std::filesystem::path& sourceDir, std::string_view expectId,
                                                  const std::filesystem::path& cacheRoot);
// 오프라인 — digest 로만. 없으면 NotFound, 있는데 내용이 digest 와 다르면 ValidationFailed (지우지 않는다)
[[nodiscard]] Expected<CachedPack> findCachedPack(std::string_view digest, const std::filesystem::path& cacheRoot);

// 의존의 path (pack.json · 매니페스트에 적은 상대 경로 — '/' 구분) → 폴더. base = 그것을 적은 팩 · 프로젝트 폴더
[[nodiscard]] std::filesystem::path resolveLocalSource(const std::filesystem::path& base, std::string_view relPath);

} // namespace sbx::package
