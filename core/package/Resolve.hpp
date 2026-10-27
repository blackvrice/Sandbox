#pragma once
// 의존 그래프 해결 (v0.4 M3-T1, ADR-0071 4 — D-05). docs/09-SERIALIZATION.md 11장.
//
// 출발 = 프로젝트 매니페스트의 dependencies. 의존마다 소스를 받아 (Root = <프로젝트>/content 의 팩 · Path = 적은 쪽
// 폴더 기준 · Git = GitSource) 그 pack.json 의 dependencies · requires 를 따라간다 (너비 우선).
//
// 한 버전: 팩 id 마다 하나. 같은 id 를 digest 가 다른 소스로 두 번 가리키면 충돌 — 프로젝트가 그 id 를 직접 적었으면
// 그것이 이긴다 (고정 — 다른 쪽 소스는 받지 않는다), 아니면 오류 (두 요구자 · 두 소스).
// 범위: 소스가 곧 버전 — 고르지 않고 확인한다. 그 팩을 가리키는 모든 범위를 version 이 만족해야 한다 (요구자 · 범위).
// 순환 · Git 팩의 path 의존 · 소스 오류 · pack.json 모양 오류도 모두 모아 돌려준다 (처음 하나에서 멈추지 않는다).
// 결과는 팩 id 순 — 잠금 파일 (M3-T2) 이 이것을 쓴다. Git 소스는 받는다 (네트워크) — 오프라인 · 잠금 파일 재현은 M3-T2.

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/content/PackManifest.hpp"
#include "core/package/GitSource.hpp"

namespace sbx::package {

struct ResolvedSource {
    content::PackSource kind = content::PackSource::Root;
    std::string path;   // Path — 적은 그대로 (상대)
    std::string git;    // Git — URL
    std::string rev;    // Git — 적은 rev
    std::string commit; // Git — 푼 커밋

    bool operator==(const ResolvedSource&) const = default;
};

struct ResolvedPack {
    std::string id;
    std::string version;
    std::string license;
    std::string digest;
    ResolvedSource source;
    std::filesystem::path dir;             // 읽을 폴더 (캐시 · <프로젝트>/content/<팩>)
    std::vector<std::string> dependencies; // 이 팩이 요구한 팩 id (id 순)
    std::vector<std::string> requiredBy;   // "project" 또는 팩 id (id 순 — 처음 넣은 차례가 아니다)
};

struct Resolution {
    std::vector<ResolvedPack> packs; // 팩 id 순
    std::vector<std::string> errors; // 사람이 읽는 문장, 하나에 하나

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
    [[nodiscard]] const ResolvedPack* find(std::string_view id) const noexcept;
};

struct ResolveOptions {
    std::filesystem::path cacheRoot; // Path · Git 소스를 넣을 캐시
    ProcessRunner run;               // Git 소스의 실행기 (없으면 Git 소스는 오류)
};

// projectDir = 매니페스트가 있는 폴더 (Root 소스는 <projectDir>/content, Path 는 projectDir 기준)
[[nodiscard]] Resolution resolveDependencies(const std::filesystem::path& projectDir,
                                             std::span<const content::PackDependency> roots,
                                             const ResolveOptions& options);

// 팩 폴더의 pack.json 에서 의존 목록 (dependencies + 범위 없는 requires) — 모양 오류는 errors 에 (그 의존은 뺀다)
[[nodiscard]] std::vector<content::PackDependency> readPackDependencies(const std::filesystem::path& packDir,
                                                                        std::vector<std::string>& errors);

} // namespace sbx::package
