#pragma once
// 세이브·로드 (World 패키지). docs/09-SERIALIZATION.md 3장.
//
//   <dir>/world.json          버전·시드·틱·경계·테이블(머티리얼 id, 컴포넌트 버전)·시계·worldHash
//   <dir>/entities.jsonl      한 줄에 엔티티 하나, saveId 오름차순, Persistent 컴포넌트 (이름 → 리플렉션 JSON)
//   <dir>/chunks/<x>_<y>.chunk  손댄 청크만 (terrainRevision > 0). 나머지는 fillMaterial 로 다시 만든다
//
// 저장은 틱 경계에서 부른다 (tick() 밖). 같은 부모 폴더의 임시 폴더에 전부 쓴 뒤 폴더째 바꿔 넣는다 —
// 중간에 실패해도 이전 세이브가 반쯤 덮이지 않고, 지난 세이브의 남은 청크 파일도 섞이지 않는다.
//
// 로드는 새 SimulationWorld 를 만든다: world.json 검사 → 경계·기본 머티리얼 → 청크(머티리얼 재매핑) →
// 엔티티 (saveId 순으로 생성, 컴포넌트 마이그레이션, 모르는 컴포넌트는 Opaque 로 보관) → 시계 복원 →
// worldHash 비교 (같은 simVersion, 마이그레이션·Opaque 없음일 때 — D2).
//
// 콘텐츠 (Phase 5A): world.json 에 팩 목록·contentHash·태그 표. 다르면 경고, core.tags 비트는 태그 표로 재매핑.
// [Phase 5B~] EntityRef 필드는 saveId 로 기록·2-pass 재연결, 오버레이(rules/behaviors/prefabs).
// [계획]   증분 저장 (바뀐 청크만), zstd 압축, Worker 스레드 직렬화 (지금은 호출 스레드에서 동기).

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/persist/Migration.hpp"
#include "core/simulation/SimulationWorld.hpp"

namespace sbx::persist {

inline constexpr u32 kWorldVersion = 1;  // 폴더 구조·파일 집합
inline constexpr u32 kSchemaVersion = 1; // world.json · entities.jsonl 스키마

struct SaveOptions {
    std::string name = "world"; // world.json 의 name (표시용)
};

struct SaveStats {
    usize entities = 0;
    usize chunkFiles = 0;
    usize opaqueEntities = 0;
    u64 worldHash = 0;
};

// world 를 dir 에 저장한다 (dir 은 만들어지거나 통째로 바뀐다).
[[nodiscard]] Expected<SaveStats> saveWorld(const sim::SimulationWorld& world, const std::filesystem::path& dir,
                                            const SaveOptions& options = {});

struct LoadOptions {
    const MigrationRegistry* migrations = nullptr; // nullptr = 마이그레이션 없음 (버전이 다르면 실패)
    bool verifyHash = true;                        // 조건이 맞으면 world.json 의 worldHash 와 비교
    bool registerDefaultSystems = true;
};

struct LoadResult {
    std::unique_ptr<sim::SimulationWorld> world;
    bool hashVerified = false;     // 비교를 했고 일치했다
    std::string hashSkippedReason; // 비교하지 않은 이유 (했으면 빈 문자열)
    usize migratedComponents = 0;
    usize opaqueComponents = 0;
    u32 savedSimVersion = 0;
    std::vector<std::string> warnings; // 콘텐츠가 다름, 없어진 태그 등 — 로드는 성공했다
};

[[nodiscard]] Expected<LoadResult> loadWorld(const ecs::ComponentCatalog& catalog,
                                             const content::ContentDatabase& content, const std::filesystem::path& dir,
                                             const LoadOptions& options = {});

} // namespace sbx::persist
