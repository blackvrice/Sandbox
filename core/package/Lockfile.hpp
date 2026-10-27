#pragma once
// 잠금 파일 sandbox.lock 과 설치 폴더 확인 (v0.4 M3-T2, ADR-0071 5). docs/09-SERIALIZATION.md 12장.
//
//   <프로젝트>/sandbox.lock         해결 결과 (resolveDependencies) 를 팩 id 순으로 — 정규형 JSON (키 순서 고정)
//     {"lockVersion": 1, "packs": {"<팩>": {"version", "license", "source", "digest"?, "dependencies"}}}
//     source = {"content": true} | {"path": "<적은 그대로>"} | {"git": URL, "rev": 적은 rev, "commit": 푼 커밋}
//     digest = path · git 팩만 (content/ 의 팩은 프로젝트가 고치는 원본이라 잠그지 않는다)
//   <프로젝트>/.sandbox/packages/<팩 id>/   설치한 path · git 팩 (Install — 트랜잭션). 로더 · 열기 · .sbpkg 가 읽는다
//
// 확인 (verifyInstalled): 잠금의 path · git 팩마다 설치 폴더가 있고 pack.json id · digest 가 잠금과 같다, 설치 폴더에
// 잠금에 없는 폴더가 없다. 다르면 열지 않는다 (누가 고쳤다 — 다시 설치한다)

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/package/Resolve.hpp"

namespace sbx::package {

inline constexpr std::string_view kLockFile = "sandbox.lock";
inline constexpr std::string_view kInstallDir = ".sandbox/packages"; // 프로젝트 기준, '/' 구분
inline constexpr u32 kLockVersion = 1;

struct LockedPack {
    std::string id;
    std::string version;
    std::string license;
    ResolvedSource source;
    std::string digest;                    // path · git 팩만 (64 글자 소문자)
    std::vector<std::string> dependencies; // 팩 id 순

    [[nodiscard]] bool installed() const noexcept { return source.kind != content::PackSource::Root; }
    bool operator==(const LockedPack&) const = default;
};

struct LockFile {
    std::vector<LockedPack> packs; // 팩 id 순

    [[nodiscard]] const LockedPack* find(std::string_view id) const noexcept;
    bool operator==(const LockFile&) const = default;
};

// 해결 결과 → 잠금 (오류가 없는 결과여야 한다 — 부르는 쪽이 본다)
[[nodiscard]] LockFile lockFromResolution(const Resolution& resolution);

// 정규형 JSON (들여쓰기 2, 끝 줄바꿈). parseLockFile(writeLockFile(l)).lock == l
[[nodiscard]] std::string writeLockFile(const LockFile& lock);

struct LockParseResult {
    LockFile lock; // ok() 일 때만 믿을 수 있다
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};
// 모르는 키 · 타입 · 팩 id · digest · source 모양 · git url · rev 모양 · 경로 모양 — 오류는 모두 모은다
[[nodiscard]] LockParseResult parseLockFile(std::string_view text);

// 설치 폴더를 잠금과 견준다 — 오류 문장들 (비면 같다)
[[nodiscard]] std::vector<std::string> verifyInstalled(const std::filesystem::path& projectDir, const LockFile& lock);

} // namespace sbx::package
