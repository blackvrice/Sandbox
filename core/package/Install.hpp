#pragma once
// 설치 트랜잭션 (v0.4 M3-T2, ADR-0071 6). docs/09-SERIALIZATION.md 12장.
//
//   1. 해결 결과의 path · git 팩을 캐시에서 <프로젝트>/.sandbox/packages.staging/<팩 id>/ 로 복사 (훑기의 파일만 —
//      캐시 표시 파일은 빼고) → 다시 훑어 digest 가 해결 결과와 같은가
//   2. 콘텐츠 검사 — 매니페스트가 불러올 팩 (loadedPacks) 을 content/ + staging 에서 읽는다 (V* · 스크립트 검증)
//   3. 바꿔 끼우기 — packages → packages.old, staging → packages, sandbox.lock 을 원자적으로 쓰기, packages.old 지우기
//   어느 단계든 실패하면 프로젝트는 그대로 (staging 을 지우고, 바꾼 이름을 되돌린다). 설치 훅 · 실행 단계는 없다.
//   중간에 프로세스가 죽으면 .sandbox/packages.* 가 남을 수 있다 — 다음 설치가 지우고, 열기는 packages 와 잠금만 본다
//   (packages 와 잠금이 어긋나면 verifyInstalled 가 거절한다)

#include <filesystem>
#include <string>
#include <vector>

#include "core/ecs/ComponentCatalog.hpp"
#include "core/package/Lockfile.hpp"
#include "core/project/GameProject.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::package {

struct InstallResult {
    LockFile lock;
    usize installed = 0; // .sandbox/packages 에 넣은 팩 수
};

// resolution 은 resolveDependencies(projectDir, manifest.dependencies, …) 의 오류 없는 결과. 실패하면 ValidationFailed
// (처음 몇 문제를 메시지에) — 프로젝트 폴더는 바뀌지 않는다
[[nodiscard]] Expected<InstallResult> installPackages(const std::filesystem::path& projectDir,
                                                      const project::GameManifest& manifest,
                                                      const Resolution& resolution,
                                                      const ecs::ComponentCatalog& catalog);

} // namespace sbx::package
