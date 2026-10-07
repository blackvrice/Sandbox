#pragma once
// WorldHash — 시뮬레이션 상태 전체의 64비트 지문. docs/04-DETERMINISM.md 5장.
//
// 입력 순서 (★ 바꾸면 kSimVersion++ 과 골든 갱신)
//   1. 머리:   "SBXWH" 태그, kSimVersion, tick, worldSeed, paused
//   2. 엔티티 수 (persist.persistence 를 가진 엔티티)
//   3. 엔티티를 saveId 오름차순으로:
//        saveId
//        Hashed 컴포넌트를 stableId 오름차순으로: stableId, version, reflect(HashVisitor)
//        끝 표식
//   4. 지형 (Phase 4, simVersion 2): 경계, 청크(좌표 오름차순)마다 Chunk::terrainHash
//        = (좌표, terrainRevision, 머티리얼 id 의 stableId, flags, moveCost, height). revision 으로 캐시된다.
//   [Phase 5] 월드 오버레이 콘텐츠 해시
//   제외: Opaque 컴포넌트(이 프로세스가 모르는 것), net.identity, EventStream, 시계의 speed·editSequence
//
// 엔티티 순서가 EntityId.index 가 아니라 saveId 인 이유: 로드하면 index 가 바뀌어도 saveId 는 그대로다 (D2).
// 구조 오류(카탈로그에 없는 Hashed 풀, 정체성 없는 엔티티)는 해시를 만들지 않고 Error 를 돌려준다 — 조용히
// 빼면 그 상태의 divergence 가 숨는다 (H3).

#include <string>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/ecs/Registry.hpp"
#include "core/world/WorldGrid.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::replay {

struct WorldHashHeader {
    sim::Tick tick = 0;
    u64 worldSeed = 0;
    bool paused = false;
};

[[nodiscard]] Expected<u64> computeWorldHash(const ecs::Registry& registry, const ecs::ComponentCatalog& catalog,
                                             const WorldHashHeader& header, const world::WorldGrid& grid,
                                             const content::ContentDatabase& content);

// 진단용: 엔티티별 해시 (saveId 오름차순). 두 실행의 divergence 를 엔티티 단위로 좁힐 때 쓴다.
struct EntityHash {
    SaveId saveId = kInvalidSaveId;
    u64 hash = 0;
};
[[nodiscard]] Expected<std::vector<EntityHash>> computeEntityHashes(const ecs::Registry& registry,
                                                                    const ecs::ComponentCatalog& catalog);

// 진단용: 한 엔티티의 Hashed 컴포넌트를 JSON 으로 ({"core.transform": {...}, ...}). 필드 단위 비교에 쓴다.
[[nodiscard]] ecs::Json describeEntity(const ecs::Registry& registry, const ecs::ComponentCatalog& catalog,
                                       SaveId saveId);

// "0x0123456789abcdef"
[[nodiscard]] std::string formatHash(u64 hash);
[[nodiscard]] Expected<u64> parseHash(std::string_view text);

} // namespace sbx::replay
