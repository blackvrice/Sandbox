# 09. 직렬화 — Save · Replay · 비트스트림 · 버전

> **규범 문서.** 디스크·와이어 포맷과 버전·마이그레이션 규칙을 정합니다.
> 상태: 3장 Save 와 6장 마이그레이션이 **Phase 4에서 구현됨** (2026-10-05, 3.5 참고). 4장 Replay(Phase 5), 5장 비트스트림(Phase 9)은 `[계획]`.

---

## 1. 원칙

```text
S1. 모든 포맷은 리플렉션 Visitor 하나로 읽고 쓴다. 컴포넌트마다 직렬화 코드를 손으로 쓰지 않는다.
S2. 디스크·와이어에는 stableId, saveId, NetEntityId 만. EntityId·ComponentTypeId·포인터 금지.
S3. 모든 파일은 버전을 갖고, 로더는 지원 범위 밖 버전을 명확한 오류로 거부한다 (조용히 기본값 금지).
S4. 사람이 읽는 포맷(JSON)은 저작·디버그용, 바이너리는 크기·속도가 필요한 곳만.
S5. 로드 순서·키 순서는 정렬되어 있다 (04 4.5).
```

## 2. 버전 체계

| 버전               | 위치          | 바뀌면                                             |
|--------------------|---------------|----------------------------------------------------|
| `WorldVersion`     | `world.json`  | 폴더 구조·파일 집합 변경. 로더 분기                |
| `SchemaVersion`    | `world.json`  | world.json·entities 스키마 변경. 마이그레이션 단계 |
| 컴포넌트 `version` | 컴포넌트 등록 | 필드 구조 변경. 컴포넌트 마이그레이션 체인         |
| `ReplayVersion`    | replay 헤더   | 리플레이 파일 구조                                 |
| `kProtocolVersion` | 핸드셰이크    | 와이어 메시지 ([08](08-NETWORK.md) 5장)            |
| `kSimVersion`      | 리플레이·골든 | 시뮬레이션 규칙 ([04](04-DETERMINISM.md) 6장)      |

### 이력

| 항목          | 버전 | 날짜                 | 변경 |
|---------------|------|----------------------|------|
| WorldVersion  | 1    | 2026-10-05 (Phase 4) | 최초 |
| SchemaVersion | 1    | 2026-10-05 (Phase 4) | 최초 |
| ReplayVersion | 1    | (Phase 5)            | 최초 |

---

## 3. 세이브 (World 패키지)

```text
worlds/<name>/
  world.json
  chunks/<cx>_<cy>.chunk      지형 (바이너리, zstd)
  entities.jsonl              엔티티 1줄 1개 (Persistent 컴포넌트만), saveId 오름차순
  overlay/rules/*.json        이 월드가 콘텐츠를 덮어쓴 것
  overlay/behaviors/*.json
  overlay/prefabs/*.json
  thumbnail.png               (클라이언트가 저장 시 생성, 선택)
```

### 3.1 world.json

```jsonc
{
  "worldVersion": 1,
  "schemaVersion": 1,
  "simVersion": 1,
  "name": "ecosystem01",
  "content": { "packs": ["ecosystem"], "contentHash": "0x…" },
  "seed": "0x1234abcd",
  "tick": 18000,
  "nextSaveId": 52311,
  "chunkSize": 32,
  "bounds": { "minChunk": [0, 0], "maxChunk": [15, 15] },
  "terrainMaterials": ["eco.dirt", "eco.grassland", "eco.water"],     // 저장 당시 인덱스 → id
  "tags": ["animal", "herbivore", "plant", "predator", "prey"],      // 저장 당시 TagSet 비트 → 이름
  "components": { "core.transform": 1, "life.energy": 1, "render.sprite": 1 },  // 저장 당시 버전
  "simulation": { "paused": false, "speed": 1.0 },
  "worldHash": "0x…"                                                  // 저장 시점 해시 (D2 검증용)
}
```

### 3.2 entities.jsonl

```jsonc
{"saveId":101,"prefab":"eco.rabbit","components":{"core.transform":{"position":[12.5,40.25],"rotation":0},"life.energy":{"value":61.5,"max":100,"drainPerSecond":1.5},"render.sprite":{"material":"eco/rabbit"}}}
```

```text
- prefab 은 출처 기록용. 로드 시 Prefab 을 다시 적용하지 않고 저장된 컴포넌트를 그대로 쓴다.
- EntityRef 필드는 saveId 로 기록 → 로드 2-pass (전부 생성 후 참조 연결).
- 모르는 stableId → Opaque 로 보존 (다시 저장하면 그대로 나간다).
- float 은 최단 왕복 표현(std::to_chars) — 로드 후 비트가 같아야 D2 가 성립한다.
```

### 3.3 청크 파일

```text
header { magic "SBXC", version u16, coord i32×2, revision u64, layerMask u8, compression u8 }
layers  material u16[1024], flags u8[1024], moveCost u8[1024], height i16[1024]   (layerMask 순서)
압축   zstd level 3 (초기 구현은 무압축 허용, compression=0)
```

### 3.4 저장·로드 절차

```text
저장  Simulation 스레드: 틱 경계(EndTick)에서 Persistent 컴포넌트·청크(변경분)·오버레이를 메모리 스냅샷으로 복사
      Worker: 직렬화·압축·임시 파일 쓰기 → 원자적 rename. 틱을 막지 않는다.
      증분: terrainRevision 이 마지막 저장 이후 바뀐 청크만 다시 쓴다.
로드  world.json 검증(버전, contentHash 비교 — 불일치 시 경고 후 계속 가능, 누락 Prefab 은 Opaque 유지)
      → 테이블 재매핑(materials, tags) → 청크 → 엔티티 1-pass 생성 → 2-pass 참조 연결
      → 마이그레이션(6장) → SpatialIndex 재구성 → worldHash 계산 (world.json 값과 같아야 함, D2)
```

### 3.5 구현 상태 (Phase 4)

```text
파일     core/persist/WorldSave.{hpp,cpp}  saveWorld(world, dir) / loadWorld(catalog, content, dir, options)
         core/persist/ChunkFile.{hpp,cpp}  청크 파일 인코딩·디코딩 (3.3 그대로, compression 0)
         core/persist/Migration.{hpp,cpp}  MigrationRegistry (6장)
         foundation/io/FileIo.{hpp,cpp}    readFile · writeFileAtomic · replaceDirectory
샘플     tests/data/saves/v1_sample  (worldVersion 1 · schemaVersion 1 · simVersion 2) — 고치지 않고, 포맷이 바뀌면 새 샘플을 추가한다

초안과 달라진 점 · 구현에서 정한 것
- 원자성: 파일마다 rename 하는 대신 <dir>.saving 에 전부 쓴 뒤 폴더째 바꿔 넣는다 (<dir> → <dir>.old → 삭제).
  지난 세이브의 청크 파일이 섞이지 않고, 중간 실패에 이전 세이브가 반쯤 덮이지 않는다. (ADR-0013)
- 저장은 호출 스레드에서 동기로 한다 (50k 엔티티 약 0.2 s, 14-PERFORMANCE 7.3). Worker 직렬화·증분 저장·zstd 는 [계획].
- 청크 파일은 terrainRevision > 0 인 청크만 쓴다. 나머지는 world.json 의 fillMaterial 로 다시 만든다.
- world.json 추가 필드: entityCount(줄 수 검증), fillMaterial, opaqueEntities, simulation.{pendingSteps, editSequence}.
  seed·worldHash 는 "0x…" 16진 문자열. contentHash 는 머티리얼 표의 지문 (팩 매니페스트 기반은 Phase 5, 7장).
  tags · overlay/ · thumbnail 은 Phase 5/8.
- entities.jsonl 한 줄 = {"saveId": n, "components": {"<이름>": <리플렉션 JSON>}}. persist.persistence 는 saveId 로 대신하고
  net.identity 는 저장하지 않는다 (세션 값 — 로드하면 saveId 순으로 1 부터 다시 매긴다). prefab 필드는 Phase 5.
  CRLF 줄 끝도 읽는다 (Windows git autocrlf).
- Opaque: 모르는 이름의 컴포넌트는 {version, value} 로 보관했다가 다시 저장한다 (버전 표에도 남긴다).
- 로드 검증 순서: 버전 → 경계·fillMaterial → 머티리얼 표(모르는 id 는 오류) → 청크(파일 이름·좌표·중복·인덱스) →
  엔티티(saveId 오름차순·nextSaveId 미만·버전 표·보호 컴포넌트 금지·리플렉션 검증) → 엔티티 수 → 해시.
- 해시 검증(D2): world.json 의 worldHash 와 다시 계산한 값을 비교하고 다르면 실패한다. 단 비교가 의미 없는 경우는 건너뛰고
  이유를 돌려준다: simVersion 이 다름 · 마이그레이션 적용 · 이번 로드에 Opaque 있음 · 저장한 프로세스에 Opaque 있었음.
- 컴포넌트 버전이 다른데 MigrationRegistry 가 없으면 실패, 저장 버전이 현재보다 높으면(새 빌드의 세이브) 실패.
- EntityRef 2-pass 재연결은 Phase 5B (EntityRef 필드를 가진 컴포넌트가 아직 없다).

Phase 5A 추가 (schemaVersion 은 1 그대로 — 모두 선택 필드, Phase 4 세이브도 읽는다)
- world.json "content": {"packs": [팩 id], "contentHash"} — 지금 콘텐츠와 다르면 LoadResult.warnings 에 남기고 계속한다.
- world.json "tags": [이름] = 저장 당시 비트 → 이름. 로드 때 core.tags 비트를 지금 콘텐츠의 비트로 재매핑한다.
  지금 콘텐츠에 없는 태그는 빠지고 경고. 표가 다르면 해시 비교를 건너뛴다.
- world.json "opaqueComponents": [이름] = 저장한 프로세스가 몰랐던 컴포넌트. 로더가 모르는 이름 집합과 **같을 때만**
  해시를 비교한다 (서버는 render.* 를 모르므로 서버가 저장·로드하면 비교한다. Phase 4 의 "Opaque 가 있으면 건너뜀" 규칙을 대체 — [ADR-0014](adr/0014-content-model-spawnqueue-name-tables.md)).
- Prefab 이 준 Opaque 컴포넌트(render.sprite 등)는 생성 때 version 1 로 붙고 세이브로 그대로 왕복한다.

Phase 5B 추가 (포맷 변화 없음 — 새 컴포넌트가 entities.jsonl 에 늘었을 뿐, schemaVersion 1 그대로)
- 엔티티 참조는 saveId(u64) 필드로 저장한다 (ai.behavior.target). EntityRef 2-pass 재연결은 필요 없어졌다 — EntityRef 필드는
  아직 없다 ([계획] 복제 Phase 10 과 함께). ADR-0015.
- 틱 캐시 필드(ai.sensor.sensed · ai.behavior.pendingAction · cacheIndex)는 저장하지 않는다 — 로드 후 첫 틱이 읽기 전에 다시 쓴다.
- 진행 중인 경로 Job 은 저장하지 않는다. ai.path 가 state = Submitted 와 start · goal · submittedTick 을 들고 저장되고,
  로드가 끝날 때(finishRestore) saveId 순으로 같은 입력을 다시 제출한다 → 다음 틱 Stage 5 결과가 같다 (D2).
```

---

## 4. 리플레이

```text
replay.sbxr
  header {
    magic "SBXR", replayVersion u16, simVersion u32, protocolVersion u32, buildId u64,
    contentHash u64, startWorld { path, worldHash }, startTick u64, hashInterval u16
  }
  records* { kind=Command,    tick varint, editSequence varint, issuer u16, sequence u32, SimCommand(바이너리) }
           { kind=Hash,       tick varint, worldHash u64 }
           { kind=Speed,      tick, speed }                      (재생 속도 복원용, 결과에 무관)
  footer  { endTick, finalHash, recordCount }
```

| 규칙             | 내용                                                                                                          |
|------------------|---------------------------------------------------------------------------------------------------------------|
| 기록 대상        | 서버가 **실제로 적용한** 명령만 (거절·중복·재전송 없음)                                                       |
| 시작점           | 반드시 세이브 (`startWorld`). 빈 월드라도 빈 세이브를 만든다                                                  |
| 일시정지 중 편집 | tick이 같으므로 `editSequence`로 순서 보존                                                                    |
| 재생             | 세이브 로드 → 명령을 tick/editSequence 순으로 주입 → Hash 레코드마다 비교                                     |
| 불일치           | simVersion 다르면 "규칙 차이", 같으면 "버그" — 틱→엔티티→컴포넌트→필드까지 진단 ([04](04-DETERMINISM.md) 5.3) |

**Phase 5C 구현** (`core/replay/Replay.*`, `core/command/CommandJson.*`, [ADR-0016](adr/0016-replay-call-index-json-lines.md))

```text
파일     replay.sbxr = JSON Lines (머리 1행 · 레코드 · end 1행). 바이너리 명령은 와이어 포맷(Phase 9)과 함께 — 그때 replayVersion 2
머리     magic "SBXR", replayVersion 1, simVersion, contentHash, packs[], startWorld(리플레이 파일 기준 상대 경로),
         startWorldHash, startTick, hashInterval. (protocolVersion · buildId 는 [계획] 네트워크와 함께)
cmd      {k:"cmd", call, tick, editSeq, c:{op…}} — call = 기록 시작 뒤 몇 번째 tick() 호출인가. 일시정지 편집 단계는 틱 번호가
         같아도 call 이 다르므로 위치가 보존된다 (문서 초안의 editSequence 역할 — editSeq 는 진단용으로 함께 남긴다)
hash     {k:"hash", call, tick, hash} — hashInterval 틱마다
end      {k:"end", calls, endTick, finalHash}. end 가 없으면 잘린 파일로 보고 거절한다
명령     수락된 명령만, 적용 순서대로. 엔티티 참조(NetEntityId)는 **saveId** 로 기록한다 — 로드한 월드는 netId 를 다시 매기므로.
         바꾸는 시점은 적용 전 (DeleteEntity 뒤에는 대상이 없다). 재생 때 그 월드의 netId 로 되돌린다.
재생     시작 세이브 로드(해시 확인) → call 마다 그 call 의 명령을 헤더 (executeTick 0, issuer 0, 기록 순번) 으로 넣고 tick() 한 번 →
         Hash 레코드마다 비교. 기록 때 수락된 명령이 거절되면 경고. 첫 불일치에서 멈추고 simVersion 으로 규칙 차이/버그를 가른다
도구     sbx_sim_check --replay-roundtrip [--replay-dir D]   기록 → 재생 (CTest det_replay_*)
         sbx_sim_check --replay D/replay.sbxr                기록된 리플레이 검사만
```

---

## 5. 비트스트림 (와이어·바이너리)

```text
BitWriter / BitReader
  정수        varint (LEB128) — 부호 있는 값은 zigzag
  고정 폭     writeBits(value, n)
  bool        1비트
  float       기본 32비트 IEEE. FieldMeta.quantize 가 있으면 (min, max, bits) 양자화
  문자열      varint 길이 + UTF-8 (상한 필수)
  배열        varint 개수 + 원소 (상한 필수)
  위치        청크 상대 좌표 × 1/256 타일, 16비트 + 청크 좌표 (스냅샷당 청크 그룹핑)
  회전        8비트
  EntityRef   NetEntityId varint
읽기 오류  경계 초과·상한 초과 → reader.error() = true, 이후 모든 읽기는 0. 호출자는 메시지 단위로 검사 후 폐기.
엔디안      리틀 엔디안 고정
```

양자화는 **표현용 손실**입니다. 서버 시뮬레이션 값은 float 그대로이고 복제본만 거칩니다.

---

## 6. 마이그레이션

```cpp
// 컴포넌트 필드 구조가 바뀌면 version 을 올리고 JSON 단계 변환을 등록한다
SBX_COMPONENT(sbx::comp::Energy, "life.energy", /*version*/ 2, …);
registerMigration("life.energy", /*from*/ 1, /*to*/ 2, [](nlohmann::json& j) {
    j["drainPerSecond"] = j.value("drainPerTick", 0.05f) * 30.0f;   // 단위 변경
    j.erase("drainPerTick");
});
```

```text
- 마이그레이션은 JSON 단계에서만 한다 (세이브는 JSON, 바이너리 청크는 WorldVersion 분기로).
- from → to 체인을 순서대로 적용. 빠진 단계가 있으면 로드 실패 (명확한 오류).
- 마이그레이션 함수는 영구 보존. 삭제하지 않는다.
- 테스트: tests/data/saves/<버전>_sample/ 에 버전별 샘플 세이브를 커밋하고, 최신으로 로드되는지 검사.
  (구현: tests/unit/persist/test_sample_saves.cpp. 마이그레이션 체인·누락 단계는 test_world_save.cpp)
- 리플레이는 마이그레이션하지 않는다. simVersion 이 다르면 재생은 시도하되 결과는 "규칙 차이"로 분류.
```

---

## 7. 콘텐츠 매니페스트와 contentHash

```text
매니페스트 = content/<pack>/ 아래 모든 .json 을 경로 정렬 → (상대경로, 바이트 FNV-1a64) 목록
contentHash = 매니페스트를 다시 해시
월드 오버레이는 contentHash 에 포함하지 않는다 (서버가 ContentOverlay 로 내려준다).
```

## 8. 필수 테스트

```text
- Visitor 왕복: JSON / Binary / Bit 각각, 모든 필드 타입
- float 왕복 비트 동일 (to_chars / from_chars)
- 세이브 → 로드 → worldHash 동일 (D2)
- Opaque 컴포넌트 보존 왕복
- 재매핑: 콘텐츠 머티리얼·태그 순서를 바꾼 뒤 로드
- 마이그레이션 체인, 누락 단계 오류
- 리플레이 기록 → 재생 divergence 0 (D3), 손상 파일 거부
- BitReader 퍼징 (무작위 바이트 → 크래시 없음)
```
