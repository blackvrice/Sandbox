# 05. 월드 — Chunk · Terrain · Spatial

> **규범 문서.** 월드 분할, 지형, 공간 질의를 정합니다. 상태: 전부 `[계획]` — Phase 3(Spatial), Phase 4(Chunk/Terrain).

---

## 1. 좌표계와 단위

| 항목 | 값 |
|---|---|
| 월드 단위 | 1.0 = 1 타일 = 1 m (가정 단위), `float` |
| 축 | +X 동쪽, +Y 북쪽(화면 위). 3D 확장 시 +Z 위 |
| 타일 좌표 | `Vec2i{int32}` = `floor(world)` |
| 청크 크기 | **32 × 32 타일** — `ChunkCoord = floorDiv(tile, 32)` |
| 월드 크기 | Phase 4: 고정 경계, 최대 64 × 64 청크 (2048² 타일). 후속: 스트리밍 |
| 정밀도 | float 23비트 가수 → ±2048에서 1/4096 m 정밀도. 스트리밍 무한 월드에서는 청크 상대 좌표 도입 검토 |

**청크 32인 이유:** 32² = 1024 타일 × 타일당 4~6바이트 ≈ 한 레이어 4 KB. 네트워크 전송·저장·HPA* 클러스터 크기로
표준적이고, Spatial 셀(8 타일)의 정수배입니다.

---

## 2. Chunk

```cpp
struct ChunkCoord { std::int32_t x, y; auto operator<=>(const ChunkCoord&) const = default; };

struct Chunk {
    ChunkCoord     coord;
    TerrainLayers  terrain;          // 3장
    Revision       terrainRevision;  // 지형 편집마다 ++ → 복제·저장·경로 스냅샷·렌더 메시 무효화
    Revision       entityRevision;   // 소속 엔티티 집합 변경마다 ++
    std::uint32_t  entityCount;      // SpatialIndex 가 유지
    ChunkState     state;            // Unloaded / Loading / Active / Dormant
};
```

`WorldGrid`가 청크를 소유합니다. 고정 경계 월드에서는 `vector<Chunk>`(행 우선), 스트리밍에서는
`ChunkCoord → Chunk` 맵 + **좌표 정렬 순회**(04 4.2).

### 2.1 Chunk를 공유하는 기능

| 용도 | 쓰는 정보 | 문서 |
|---|---|---|
| Terrain 편집·렌더 | terrain, terrainRevision | 3장, 06 |
| Spatial Query 1차 가지치기 | 청크 → 셀 버킷 | 4장 |
| Network Interest | 구독 청크 집합 | 08 8장 |
| Save / Load | 청크별 파일, 증분 저장 | 09 3장 |
| Pathfinding | 경로 스냅샷 블록, HPA* 클러스터 | 03 7장 |
| Visibility | 카메라 AABB ∩ 청크 | 06 9장 |
| Streaming `[후속]` | state | — |

같은 분할을 쓰므로 "청크 하나 칠함 = 청크 하나 전송 = 청크 하나 저장 = 경로 블록 하나 갱신"입니다.

---

## 3. Terrain

### 3.1 레이어 (SoA, 청크당)

| 레이어 | 타입 | 의미 |
|---|---|---|
| material | u16 | `TerrainMaterial` 인덱스 (콘텐츠 테이블) |
| flags | u8 | Blocked, Water, NoBuild, … (머티리얼 기본값 + 셀별 덮어쓰기) |
| moveCost | u8 | 0 = 통행 불가, 1~255. 기본은 머티리얼에서 |
| height | i16 | 후속 3D·시야용. 초기 0 |

### 3.2 TerrainMaterial (콘텐츠)

```jsonc
{ "id": "eco.grassland", "moveCost": 10, "flags": [], "render": { "tileset": "terrain/grass", "variant": "auto" } }
```

머티리얼 인덱스는 **콘텐츠 로드 시 id 정렬 순서**로 매깁니다 (04 4.5). 세이브에는 인덱스가 아니라
`materials` 테이블(id 목록)을 함께 저장하고 로드 시 재매핑합니다.

### 3.3 편집

```text
PaintTerrain{materialId, brush{shape, radius}, center | cells[]}
  → 서버 검증 (권한, 경계, 셀 수 상한 4096/명령)
  → 셀 갱신 → 영향받은 청크 terrainRevision++
  → 다음 스냅샷에서 Bulk 채널로 변경 청크 (또는 셀 델타) 전송
  → PathGridSnapshot 해당 블록 갱신 (다음 Stage 8 이전)
```

---

## 4. Spatial Index

### 4.1 구조

```text
균일 격자 해시. 셀 크기 = 8 타일 (청크당 4 × 4 셀).
cellOf(position) → CellKey (int32 x, y)
버킷: 셀마다 컨테이너를 두지 않고, 전체를 하나의 정렬 배열로 두고 셀별 구간만 기록한다.

매 틱 재구성 (Stage 4):
  1. Transform 을 가진 엔티티 목록에서 (cellKey, saveId) 쌍 생성
  2. cellKey 로 카운팅 정렬 → 셀별 연속 구간 (start, count)
  3. 셀 내부는 saveId 오름차순 (결정적 질의 결과 순서)
```

**매 틱 재구성을 기본으로 하는 이유:** 50k 엔티티 카운팅 정렬은 O(N)이고 수백 µs 수준(추정, Phase 3에서 측정)입니다.
증분 갱신은 이동이 적을 때 유리하지만 구현·결정론 검증 비용이 큽니다. 측정 결과 1 ms를 넘으면 증분(움직인 엔티티만)으로 바꿉니다.

### 4.2 질의 API

```cpp
class SpatialIndex {
public:
    void queryRadius(Vec2 center, float r, TagMask mask, SmallVector<EntityId>& out) const;
    void queryAABB(Rect, TagMask mask, SmallVector<EntityId>& out) const;
    std::optional<EntityId> queryNearest(Vec2 p, float maxR, TagMask mask, EntityId exclude = {}) const;
    std::span<const EntityId> queryChunk(ChunkCoord) const;     // Interest, 렌더 컬링
    std::uint32_t countInChunk(ChunkCoord) const;
};
```

| 규칙 | 내용 |
|---|---|
| S1 | 결과 순서는 (셀 행 우선, 셀 내 saveId) — 결정적 |
| S2 | `queryNearest` 동점은 saveId 작은 쪽 (02 E3) |
| S3 | 질의는 틱 시작 시점(Stage 4) 위치 기준. 같은 틱의 이동은 다음 틱에 반영 |
| S4 | 전체 엔티티 선형 탐색 금지. 질의 비용은 O(검사 셀 수 + 후보 수) |
| S5 | TagMask 필터는 셀 순회 중에 적용 (후보 복사 전) |

### 4.3 충돌·분리

`CollisionSystem`(Stage 16)은 SpatialIndex로 이웃을 찾아 원-원 분리를 하고, 지형 `moveCost == 0` 셀과의 겹침을 밀어냅니다.
정밀 물리는 목표가 아닙니다.

---

## 5. 필수 테스트

```text
- 좌표 변환: 음수 좌표의 floorDiv (−1 → 청크 −1), 청크 경계
- Spatial 속성 테스트: 무작위 배치 + 무작위 질의 → brute-force 결과와 집합·순서 일치
- 셀 경계·반경이 셀보다 큰 질의
- Terrain 편집 → revision 증가 → 경로 스냅샷 반영
- 머티리얼 재매핑: 콘텐츠 순서를 바꾼 뒤 세이브 로드 결과 동일
```
