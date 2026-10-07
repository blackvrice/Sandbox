# 11. 콘텐츠 스키마

> **규범 문서.** `content/<pack>/` 데이터의 모양과 검증 규칙을 정합니다.
> 데이터 모양을 바꾸면 이 문서를 같은 커밋에서 고칩니다.
> 상태: **Phase 5A·5B에서 구현됨** (2026-10-05) — 팩 로더·검증기(V1~V7)·Prefab·Tag·Rule·BehaviorGraph 모델·contentHash,
> Rule·Behavior 의 실행(5B). 0장이 구현과 문서의 차이입니다. PlayerAction(actions.json)은 Phase 12 `[계획]`.

---

## 0. 구현 상태 (Phase 5A · 5B)

```text
파일     core/content/ContentLoader.{hpp,cpp}   ContentLoader::load(root, packIds, catalog) → {db, issues}
         core/content/ContentModel.hpp          Prefab · Rule · BehaviorGraph · PackInfo (참조는 로드 때 해석)
         core/content/TagSet.hpp                TagSet(256 비트) · TagExpr{all, any, none}
         core/content/ContentDatabase.{hpp,cpp} 조회 API (findPrefab · findBehavior · findTag · rules() …)
팩       content/ecosystem/  (pack id "eco") — 7장 수치의 Grass/Rabbit/Wolf, 먹기 Rule 3개, Behavior 2개
검사     sbx_sim_check --validate-content eco  (CTest content_eco_validate: 오류·경고 0)

초안과 달라진 점 · 구현에서 정한 것
- 내장 "core" 팩: core.grass/rock/sand/water 머티리얼은 항상 들어 있다 (빈 월드·테스트의 기본 지형).
  팩 id "core" 는 예약 — 팩이 쓸 수 없다.
- 팩 폴더 이름은 자유, pack.json 의 id 로 찾는다 (예: content/ecosystem → "eco"). 모든 id 는 "<팩 id>." 로 시작 (V1).
  id 는 47 바이트 이하 (컴포넌트의 ContentId 필드에 담긴다).
- 6장 "default: true" 머티리얼 대신 월드가 WorldDesc.fillMaterial(id) 로 채움 머티리얼을 고른다 (V6 의 default 검사 없음).
- 머티리얼 flags 이름은 대문자 시작: "Blocked" "Water" "NoBuild" (5장 예시의 "water" 는 틀린 표기).
  terrain.json 은 머티리얼 배열 (Phase 4 의 {"terrainMaterials": [...]} 형식은 내장·테스트용 fromJson 에만 남는다).
- 태그 이름은 소문자·숫자·'_' (팩 접두사 없음, 여러 팩이 같은 태그를 선언해도 된다). 비트 = 전체 태그 이름 정렬 순.
- Prefab: "tags" 는 이름 배열 → 엔티티의 core.tags. 엔진 관리 컴포넌트(persist.persistence, net.identity, core.tags,
  core.prefab)는 components 에 쓸 수 없다. 모르는 컴포넌트는 render.* / client.* 만 허용(Opaque), 나머지는 V3 오류.
  "inherits" 는 null 이거나 없어야 한다 (상속 [계획]). enum 필드(예: life.reproduce.mode)는 지금 정수로 쓴다.
- Prefab 생성 시 core.prefab{id} 가 붙는다 (출처). 09 의 entities.jsonl "prefab" 필드 대신 이 컴포넌트가 저장된다.
- Rule: source/target 은 {"tags": TagExpr}. 조건 경로 "<source|target>.<컴포넌트>.<수치 필드>", op < <= > >= == !=.
  효과의 who 는 spawn(기본 source)·event(기본 target) 외에는 필수. spawn 의 count 1~16, event 이름은 "<pack>.<name>".
- BehaviorGraph: 조건 노드는 {"<노드>": 인자} 하나 또는 인자 없는 문자열("targetValid", "true").
  stateTime 은 {"stateTime": {"op": ">=", "seconds": 수}} — 초 단위 (틱으로 쓰지 않는다: tick rate 와 무관하게).
  when 은 필수 (항상이면 "true"). 행동 노드: wander{radius, interval}, seek{tags, strategy:"nearest"},
  flee{tags, distance}, interact{name}, idle, setBlackboard{slot 0~7, value}. 5B 가 실행한다.
- 검증 메시지 형식: "<팩 폴더>/<파일>:<JSON 포인터>: 오류|경고 V? — 설명". 모든 문제를 모아 보고한다.
  V4(고아 Rule)·V5(도달 불가 상태)는 경고, 나머지는 오류. 경고만 있으면 로드는 성공한다.
- contentHash = FNV(팩 (id, version) + 파일 (상대 경로, 바이트) 매니페스트 + 내장 머티리얼). 줄 끝(CRLF)은 LF 로
  정규화한 뒤 해시한다. presentation/ 은 읽지도 해시하지도 않는다. actions.json 은 경고 후 무시.

Phase 5B (실행 — 03 5·6·7장)
- 로드 마지막 단계(link): action 이름 표 = Rule 의 action ∪ interact 의 name (정렬, 번호 = 인덱스 + 1),
  action 별 Rule 색인 = (priority 내림, 정의 순). Behavior 의 감지 질의 = sensed · seek · flee 가 쓰는 서로 다른 TagExpr 를
  상태 정의 순 → 전이 순으로 모은 것 (그래프당 최대 8, 넘으면 V7 오류).
- 2장 예시의 "ai.sensor": {"radius", "mask"} 대신 {"radius"} 만 — 무엇을 감지할지는 그래프의 질의가 정한다.
  "ai.behavior": {"graph"} (state·enteredTick·target·blackboard 는 실행 상태). "ai.path": {} 를 붙이면 막힌 곳을 돌아가는
  경로를 쓴다 (없으면 목표로 곧장). "core.movement" · "core.collider" 는 2장 예시 그대로 (+ collider 의 layer·mask u32).
- 노드 의미: seek 는 질의의 가장 가까운 개체를 target 으로 (안 보이면 target 을 놓고 멈춤), targetValid = target 이 살아 있음,
  targetInRange(r) = 중심 거리 ≤ r. "*" → 지금 상태 자신 전이는 건너뛴다. stateTime 은 진입 틱부터 초.
- Rule 의 range 는 중심 거리다. 둘 다 core.collider 가 있으면 반지름 합보다 넉넉히 둔다 (분리가 반지름 합까지 민다) —
  eco 팩: 늑대 0.4 + 토끼 0.3 → targetInRange 0.9 · range 1.0.
- eco 팩: grass(행동 없음, 충돌 없음) · rabbit(herbivore, 감지 8) · wolf(carnivore, 감지 12) 에 core.movement · core.collider ·
  ai.sensor · ai.behavior · ai.path. 7장 수치는 5C 에서 조정했다.

Phase 5C
- life.reproduce 에 crowdRadius(0~32) · crowdMax(0~255, 0 = 끔): 반경 안에 같은 Prefab(offspring)이 crowdMax 이상이면 낳지 않고
  쿨다운을 다시 건다 (수용력). 기존 데이터는 그대로 읽힌다 (없는 키 = 끔).
```

---

## 1. 콘텐츠 팩

```text
content/<pack>/
  pack.json
  tags.json
  terrain.json            TerrainMaterial 목록
  prefabs/*.json          Prefab 1파일 1개 (또는 배열)
  rules/*.json            Rule 배열
  behaviors/*.json        BehaviorGraph 1파일 1개
  actions.json            PlayerAction 정의 (선택)
  presentation/           클라이언트 전용: sounds.json(이벤트→사운드), 아이콘 등  ← contentHash 제외
```

```jsonc
// pack.json
{ "id": "ecosystem", "version": "0.1.0", "requires": [], "description": "Grass / Rabbit / Wolf" }
```

### 1.1 id 규칙

```text
형식      <pack>.<name>   소문자·숫자·'_'·'.' (정규식 ^[a-z0-9_]+(\.[a-z0-9_]+)+$)
네임스페이스 pack id 가 접두사. 엔진 예약: core ai life society combat net persist render editor client debug
          (debug: 진단·부하 시험 전용 컴포넌트, 예 debug.random_walk — Phase 3 추가. 콘텐츠 팩이 쓰지 않는다)
참조      다른 팩 참조는 requires 에 선언된 팩만
안정성    id 는 세이브·리플레이·네트워크에 남는다. 이름 변경 = 새 id + 마이그레이션
```

### 1.2 로드 순서

```text
팩: requires 위상 정렬, 동순위는 id 정렬. 파일: 경로 문자열 정렬. (04 4.5)
같은 id 가 두 번 정의되면 오류 (덮어쓰기는 월드 오버레이만 허용)
```

---

## 2. Prefab

```jsonc
{
  "id": "eco.rabbit",
  "name": "Rabbit",                         // 에디터 표시
  "category": "Animals",                    // Palette 그룹
  "tags": ["animal", "herbivore", "prey"],
  "components": {
    "core.transform":  {},
    "core.velocity":   {},
    "core.movement":   { "maxSpeed": 2.5, "accel": 8.0, "arriveRadius": 0.2 },
    "core.collider":   { "radius": 0.3 },
    "life.health":     { "value": 20, "max": 20 },
    "life.energy":     { "value": 60, "max": 100, "drainPerSecond": 1.5 },
    "life.age":        { "maxAgeTicks": 54000 },
    "life.reproduce":  { "minEnergy": 70, "energyCost": 40, "cooldown": 12.0, "offspring": "eco.rabbit" },
    "ai.sensor":       { "radius": 8, "mask": ["plant", "predator"] },
    "ai.behavior":     { "graph": "eco.herbivore" },
    "render.sprite":   { "material": "eco/rabbit", "size": [0.8, 0.8], "layer": 10 }
    // material 은 표현 에셋 이름 — assets/<팩>/materials.json 이 스프라이트 · 색으로 푼다 (06 7.1 · 7.4, Phase 8A).
    // 서버 · contentHash 와 무관 (render.* 는 Opaque). size 는 월드 단위, layer 0~255 (큰 값이 위)
  },
  "inherits": null                          // 후속: 단일 상속 (병합 규칙 미정)
}
```

| 규칙 | 내용                                                                                           |
|------|------------------------------------------------------------------------------------------------|
| P1   | `components`의 키는 등록된 stableId 이름. 모르는 이름은 Opaque로 보존 (서버에서 `render.*` 등) |
| P2   | 생략한 필드는 컴포넌트 기본값                                                                  |
| P3   | 값은 FieldMeta 범위로 검증. 범위 밖이면 오류                                                   |
| P4   | `core.transform`은 생성 시 위치로 덮어쓴다                                                     |

---

## 3. Rule

```jsonc
[
  {
    "id": "eco.wolf_eats_rabbit",
    "action": "eat",
    "source": { "tags": { "all": ["predator"] } },
    "target": { "tags": { "all": ["prey"], "none": ["dead"] } },
    "range": 0.8,
    "priority": 0,
    "exclusive": true,
    "conditions": [ { "field": "source.life.energy.value", "op": "<", "value": 90 } ],
    "effects": [
      { "op": "field.add", "who": "source", "field": "life.energy.value", "value": 30 },
      { "op": "destroy",   "who": "target" },
      { "op": "event",     "name": "eco.eaten", "who": "target" }
    ]
  }
]
```

| 필드       | 의미                                                                          |
|------------|-------------------------------------------------------------------------------|
| action     | Behavior의 `interact(action)`과 매칭되는 문자열                               |
| tags       | `all`(모두 보유), `any`(하나 이상), `none`(하나도 없음)                       |
| range      | 월드 단위 거리                                                                |
| exclusive  | 같은 target에 대해 이번 틱 하나만 승리 (`destroy(target)`이 있으면 자동 true) |
| conditions | 리플렉션 경로 비교. op: `< <= > >= == !=`                                     |
| effects    | [03-SIMULATION](03-SIMULATION.md) 6.3의 op만                                  |

`field` 경로 형식: `<who>.<stableId>.<field>` (조건) / `<stableId>.<field>` (effect, who 별도).

---

## 4. BehaviorGraph

```jsonc
{
  "id": "eco.herbivore",
  "initial": "wander",
  "states": [
    { "id": "wander", "onTick": [ { "action": "wander", "radius": 6, "interval": 3.0 } ] },
    { "id": "seek_food", "onTick": [ { "action": "seek", "tags": { "all": ["plant"] } } ] },
    { "id": "eat", "onTick": [ { "action": "interact", "name": "eat" } ] },
    { "id": "flee", "onTick": [ { "action": "flee", "tags": { "all": ["predator"] }, "distance": 10 } ] }
  ],
  "transitions": [
    { "from": "*",         "to": "flee",      "priority": 100, "when": { "sensed": { "all": ["predator"] } } },
    { "from": "flee",      "to": "wander",    "priority": 50,  "when": { "not": { "sensed": { "all": ["predator"] } } } },
    { "from": "wander",    "to": "seek_food", "priority": 10,  "when": { "energyBelow": 50 } },
    { "from": "seek_food", "to": "eat",       "priority": 10,  "when": { "targetInRange": 0.6 } },
    { "from": "eat",       "to": "wander",    "priority": 10,  "when": { "or": [ { "energyAbove": 90 }, { "not": "targetValid" } ] } }
  ]
}
```

노드 어휘: [03-SIMULATION](03-SIMULATION.md) 5.2. 조건 JSON은 노드 이름을 키로, 인자를 값으로 씁니다.

---

## 5. TerrainMaterial

```jsonc
[
  { "id": "eco.grassland", "moveCost": 10, "flags": [],          "render": { "tileset": "terrain/grass", "variant": "auto" } },
  { "id": "eco.dirt",      "moveCost": 12, "flags": [],          "render": { "tileset": "terrain/dirt" } },
  { "id": "eco.water",     "moveCost": 0,  "flags": ["water"],   "render": { "tileset": "terrain/water", "animated": true } }
]
```

`moveCost` 0 = 통행 불가. 1~255. `default: true`인 머티리얼이 하나 있어야 합니다(새 월드·지우개용).

**Phase 4 구현 (최소판, `core/content/ContentDatabase`)**

```text
입력     {"terrainMaterials": [{"id": "core.grass", "moveCost": 10, "flags": ["Blocked" | "Water" | "NoBuild"]}, …]}
검사     id 규칙(1.1) · 중복 · moveCost 0~255 정수 · 모르는 플래그 · 모르는 키(render 는 Phase 8 에서 추가) · 비어 있지 않음
인덱스   id 정렬 순서. 세이브에는 id 표를 함께 적고 로드 때 재매핑한다 (09 3.5)
기본     default 플래그 대신 WorldDesc.fillMaterial(id) — 새 월드의 채움 머티리얼. 세이브 world.json 에도 남는다
내장     core.grass(10) · core.rock(0, Blocked·NoBuild) · core.sand(14) · core.water(0, Water·NoBuild) — 테스트·시나리오·빈 월드용
플래그   이름은 대문자 시작 (Blocked/Water/NoBuild). 위 초안 예시의 소문자 "water" 는 Phase 5 팩 로더에서 같이 정리한다
```

## 6. Tag / Action

```jsonc
// tags.json — 팩이 쓰는 태그 선언 (선언되지 않은 태그 사용 = 검증 오류, 오타 방지)
["animal", "herbivore", "predator", "prey", "plant", "dead"]

// actions.json — PlayerAction 정의 (플레이어 역할이 쓸 수 있는 행동)
[ { "id": "eco.feed", "targets": { "tags": { "all": ["animal"] } }, "range": 3,
    "effects": [ { "op": "field.add", "who": "target", "field": "life.energy.value", "value": 20 } ] } ]
```

TagSet 비트 인덱스는 로드 시 태그 이름 정렬 순서로 매기며, 팩 전체 태그는 최대 256개입니다.

---

## 7. Ecosystem 수치 (Phase 5C 에서 조정 — `content/ecosystem` 이 기준)

| 항목                                       | Grass                                                                                  | Rabbit                                                                | Wolf                                                              |
|--------------------------------------------|----------------------------------------------------------------------------------------|-----------------------------------------------------------------------|-------------------------------------------------------------------|
| 초기 개체 비율                             | 70%                                                                                    | 25%                                                                   | 5%                                                                |
| maxSpeed (타일/초)                         | —                                                                                      | 2.5                                                                   | 3.2                                                               |
| energy max / 시작                          | —                                                                                      | 100 / 60                                                              | 150 / 100                                                         |
| drainPerSecond                             | —                                                                                      | 1.0 (초안 1.5)                                                        | 1.0 (초안 2.0)                                                    |
| 먹었을 때 energy                           | —                                                                                      | +15 (풀, 초안 +12)                                                    | +40 (토끼, 초안 +30)                                              |
| sensor radius                              | —                                                                                      | 8                                                                     | 12                                                                |
| reproduce minEnergy / cost / cooldown(초)  | 성장 완료 시 / — / 8 (초안 20), 확률 0.5                                               | 75 / 35 / 20                                                          | 120 / 60 / 60                                                     |
| 밀도 제한 crowdRadius / crowdMax (5C 신규) | 3 / 8                                                                                  | 6 / 4                                                                 | 20 / 2                                                            |
| maxAge (초)                                | 600                                                                                    | 1800                                                                  | 2400                                                              |
| Grass growth                               | stage 0→2, rate 0.15/초 (초안 0.05), 먹히면 growth −1 stage, stage 0에서 먹히면 사라짐 |                                                                       |                                                                   |
| Grass spread                               | stage 2에서 인접 빈 셀로 번식 (RandomService `Spawn`)                                  |                                                                       |                                                                   |
| 행동                                       | —                                                                                      | 배고프고(energy < 60) **풀이 보일 때만** 찾는다. 대상을 잃으면 배회로 | 사냥은 energy < 80 이고 먹이가 보일 때. 120 넘게 먹으면 15초 쉰다 |

```text
조정 과정 (2026-10-06, ecosystem_survival · 시드 1·2 를 반복해 돌림, 수치는 측정값)
1. 초안 수치: 풀을 다 먹은 토끼가 굶고, 늑대가 토끼를 다 잡은 뒤 굶는다 (3,000틱 안에 토끼·늑대 붕괴).
   "풀이 안 보이면 멈춘다" 는 행동 탓에 배고픈 토끼가 10초씩 서 있었다 → 배회 중 풀이 보일 때만 찾기로 바꿈.
2. 풀이 지도를 다 덮을 때까지 지수적으로 늘어 틱 비용이 폭증 → life.reproduce 에 밀도 제한(crowdRadius · crowdMax) 추가.
   같은 장치를 토끼(포화)·늑대(영역)에도 써서 포식자 과잉 → 붕괴 순환을 끊었다.
3. 풀 crowdMax 6 은 먹이가 모자라 토끼가 줄어든다 (시드 3 붕괴), 8 에서 세 시드 모두 공존.
결과  256×256, 1,500 개체 시작, 18,000틱: 시드 1·2·3 모두 공존 — 끝에서 풀 7,200~7,900 · 토끼 980~1,010 · 늑대 250~260.
      (CTest balance_ecosystem_survival_{1,2,3}, 최적화 빌드에서만)
```

합격 기준: 18,000틱 동안 고정 시드 3개 모두에서 세 종의 개체수가 0이 되지 않는다 ([00-OVERVIEW](00-OVERVIEW.md) 5장). — **충족 (Phase 5C)**

-------------------------------------------|----------------------------------------------------------------------------|--------------|---------------|
| 초기 개체 비율                            | 70%                                                                        | 25%          | 5%            |
| maxSpeed (타일/초)                        | —                                                                          | 2.5          | 3.2           |
| energy max / 시작                         | —                                                                          | 100 / 60     | 150 / 100     |
| drainPerSecond                            | —                                                                          | 1.5          | 2.0           |
| 먹었을 때 energy                          | —                                                                          | +12 (풀)     | +30 (토끼)    |
| sensor radius                             | —                                                                          | 8            | 12            |
| reproduce minEnergy / cost / cooldown(초) | 성장 완료 시 / — / 20                                                      | 70 / 40 / 12 | 110 / 60 / 30 |
| maxAge (초)                               | 600                                                                        | 1800         | 2400          |
| Grass growth                              | stage 0→2, rate 0.05/초, 먹히면 growth −1 stage, stage 0에서 먹히면 사라짐 |              |               |
| Grass spread                              | stage 2에서 인접 빈 셀로 번식 (확률 RandomService `Spread`)                |              |               |

합격 기준: 18,000틱 동안 고정 시드 3개 모두에서 세 종의 개체수가 0이 되지 않는다 ([00-OVERVIEW](00-OVERVIEW.md) 5장).

---

## 8. 검증기

`sbx_sim_check --validate-content content/<pack>` 와 서버 로드 시 같은 검증기를 씁니다 (CTest 라벨 `content`).

```text
V1  id 형식·중복·네임스페이스
V2  참조 무결성: Prefab→Behavior, Rule/Behavior→Tag, Reproduce.offspring→Prefab, requires
V3  컴포넌트 이름 존재(서버 기준: 정의 없으면 Opaque 허용 목록 render.* client.* 만), 필드 존재, 범위
V4  Rule: action 을 interact 하는 Behavior 가 하나 이상 (고아 Rule 경고)
V5  Behavior: 도달 불가 상태, initial 존재, 전이 대상 존재
V6  Terrain: default 머티리얼 1개, moveCost 범위
V7  태그 선언 여부 (오타 방지)
실패 메시지 형식:  <파일>:<JSON 포인터>: <규칙> — <설명>
```
