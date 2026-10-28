#pragma once
// 게임 패키지 (.sbpkg) — 검증된 실행 배포 단위 (v0.2 M5-T2, ADR-0054 — 결정 D-02, docs/09-SERIALIZATION.md 9장).
//
// 파일 하나 (작은 끝 little-endian):
//   magic "SBXPKG\0\n" (8) · format u32 (= 1) · indexBytes u64 · index (JSON, 키 정렬) · 파일 바이트를 index 순서로
//   이어서
// index = {format, gameId, name, version, engine, entryWorld, gameMode, digest, files: [{path, size, sha256, offset}],
//          packs: [{id, version, license, digest, path}]}   ← packs 는 형식 2 (v0.4 M5-T2 — 라이선스 요약 SBOM)
//   packs  읽은 팩마다 (팩 id 순): pack.json 의 version · license, 그 팩 폴더 (path — "content/<폴더>" ·
//          ".sandbox/packages/<팩>") 의 파일로 만든 팩 digest (같은 규칙 — 팩 폴더 기준 경로, PackSource 와 같다). 읽는
//          쪽이 files 에서 다시 계산하고 (readPackageIndex), 파일을 보는 검증 · 풀기는 pack.json 의 id · version ·
//          license 와도 견준다 — index 만 고쳐 라이선스를 속일 수 없다. 형식 1 (packs 없음) 도 읽는다
//   path   프로젝트 기준 상대 경로 ('/' 구분 — project::checkRelativePath 규칙), 바이트 순 정렬 · 겹침 없음
//   offset 파일 데이터 시작에서 (앞 파일들의 크기 합 — 빈틈 없음)
// digest = SHA-256 ( 파일마다 "<sha256> <size> <path>\n" 를 path 순으로 ) — 파일 내용 · 경로만의 지문이라 같은
// 프로젝트를
//          두 번 빌드하면 같다 (시각 · 압축 · 순서가 들어가지 않는다). 압축 · 서명 · 증분 다운로드는 [계획].
//
// 담는 것: sandbox.project.json, 프로젝트 콘텐츠가 읽은 팩 (requires 포함 — 폴더째, presentation/ 도), 시작 월드 폴더.
//          다른 worlds/ (세이브) · build/ 는 담지 않는다.
// 여는 길: 헤더 · index 모양 → 파일마다 SHA-256 → digest. 하나라도 다르면 거절 (고친 패키지는 열지 않는다). 그 뒤 캐시
//          폴더 <cache>/<digest 앞 16 글자>/ 에 풀어 project::openProject 로 연다 (같은 digest 면 다시 확인하고 그대로
//          쓴다).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/ecs/ComponentCatalog.hpp"
#include "foundation/types/Error.hpp"
#include "foundation/version/SemVer.hpp"

namespace sbx::package {

inline constexpr std::string_view kExtension = ".sbpkg";
inline constexpr u32 kFormatVersion = 2;    // 쓰는 판 (2 = packs — v0.4 M5-T2)
inline constexpr u32 kMinFormatVersion = 1; // 읽는 가장 옛 판
inline constexpr usize kMaxSbomPacks = 256;
inline constexpr usize kMaxFiles = 65'536;
inline constexpr u64 kMaxPackageBytes = u64{2} << 30; // 2 GiB (파일을 한 번에 읽는다 — 큰 패키지의 흐름 읽기는 [계획])
inline constexpr u64 kMaxIndexBytes = u64{16} << 20;

struct PackageEntry {
    std::string path;
    u64 size = 0;
    std::string sha256; // 64 글자 소문자
    u64 offset = 0;
};

struct PackageIndex {
    u32 format = kFormatVersion;
    std::string gameId;
    std::string name;
    std::string version; // SemVer 문자열
    std::string engine;  // 엔진 범위 문자열 (매니페스트 그대로)
    std::string entryWorld;
    std::string gameMode;            // 없으면 빈 문자열
    std::string digest;              // 64 글자 소문자
    std::vector<PackageEntry> files; // path 순
    // 형식 2 — 라이선스 요약 (SBOM). 팩 id 순. 형식 1 패키지는 비었다
    struct Pack {
        std::string id;
        std::string version; // pack.json 에 없으면 빈 문자열
        std::string license; // pack.json 에 없으면 빈 문자열
        std::string digest;  // 팩 digest (64 글자 소문자 — 설치한 팩이면 잠금의 것과 같다)
        std::string path;    // 패키지 안의 팩 폴더
        bool operator==(const Pack&) const = default;
    };
    std::vector<Pack> packs;
};

// 파일 목록의 지문 (위 규칙) — 64 글자 소문자
[[nodiscard]] std::string packageDigest(const std::vector<PackageEntry>& files);

// 프로젝트 → 패키지. 먼저 project::openProject 와 같은 검사를 하고 (오류면 ValidationFailed) 파일을 모은다.
// out 이 비면 <프로젝트>/build/<gameId>-<version>.sbpkg. 원자적으로 쓴다 (임시 파일 → 이름 바꾸기)
struct BuildResult {
    std::filesystem::path file;
    PackageIndex index;
    u64 bytes = 0;
};
[[nodiscard]] Expected<BuildResult> buildPackage(const std::filesystem::path& projectDir,
                                                 const std::filesystem::path& out, const SemVer& engine,
                                                 const ecs::ComponentCatalog& catalog);

// 헤더 · index 만 (모양 · digest 다시 계산 — 파일 내용은 보지 않는다)
[[nodiscard]] Expected<PackageIndex> readPackageIndex(const std::filesystem::path& file);
// 모든 파일의 SHA-256 까지
[[nodiscard]] Expected<PackageIndex> verifyPackage(const std::filesystem::path& file);

// 검증하고 cacheRoot/<digest 앞 16>/ 에 푼다 (이미 있으면 그 파일들을 다시 확인하고 쓴다 — 다르면 다시 푼다)
struct ExtractedPackage {
    std::filesystem::path dir;
    PackageIndex index;
    bool reused = false;
};
[[nodiscard]] Expected<ExtractedPackage> extractPackage(const std::filesystem::path& file,
                                                        const std::filesystem::path& cacheRoot);

[[nodiscard]] std::filesystem::path defaultPackageCache(); // <임시 폴더>/sbx-packages
// 패키지를 돌리는 서버 · 싱글플레이의 세이브 폴더 기본값: <패키지 폴더>/<패키지 이름>-saves (캐시가 아니다)
[[nodiscard]] std::filesystem::path packageSavesRoot(const std::filesystem::path& file);
// 확장자가 .sbpkg 인 일반 파일
[[nodiscard]] bool isPackageFile(const std::filesystem::path& path);

} // namespace sbx::package
