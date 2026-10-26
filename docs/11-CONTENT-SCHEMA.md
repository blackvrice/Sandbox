# 11. 콘텐츠 스키마

> **규범 문서.** `content/<pack>/` 데이터의 모양과 검증 규칙을 정합니다. (v0.3 M3-T2) 팩의 `scripts/` 는 11장.
> 데이터 모양을 바꾸면 이 문서를 같은 커밋에서 고칩니다.
> 상태: **Phase 5A·5B에서 구현됨** (2026-10-05) — 팩 로더·검증기(V1~V7)·Prefab·Tag·Rule·BehaviorGraph 모델·contentHash,
> Rule·Behavior 의 실행(5B). 0장이 구현과 문서의 차이입니다. PlayerAction(actions.json)은 v0.2 M3-T1 에서 구현 (6장,
> [ADR-0047](adr/0047-player-action-content-command-point-targets.md)).

---

## 0. 구현 상태 (Phase 5A · 5B)

```text
파일     core/content/ContentLoader.{hpp,cpp}   ContentLoader::load(root, packIds, catalog) → {db, issues}
         core/content/ContentModel.hpp          Prefab · Rule · BehaviorGraph · PackInfo (참조는 로드 때 해석)
         core/content/TagSet.hpp                TagSet(256 비트) · TagExpr{all, any, none}
         core/content/ContentDatabase.{hpp,cpp} 조회 API (findPrefab · findBehavior · findTag · rules() …)
팩       content/ecosystem/  (pack id "eco") — 7장 수치의 Grass/Rabbit/Wolf, 먹기 Rule 3개, Behavior 2개,
         (v0.2 M3-T3) 데모 모드 eco.protect · 행동 eco.feed · eco.trap · 게임 키 presentation/input.json (9장),
         (v0.2 M4-T2) 트리거 eco.rabbit_lost (토끼가 죽으면 점수 −1 — 10장)
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
  core.prefab, game.session, game.score)는 components 에 쓸 수 없다 (game.spawn_point 는 쓸 수 있다 — 9장). 모르는 컴포넌트는 render.* / client.* 만 허용(Opaque), 나머지는 V3 오류.
  "inherits" 는 null 이거나 없어야 한다 (상속 [계획]). enum 필드(예: life.reproduce.mode)는 지금 정수로 쓴다.
- Prefab 생성 시 core.prefab{id} 가 붙는다 (출처). 09 의 entities.jsonl "prefab" 필드 대신 이 컴포넌트가 저장된다.
- Rule: source/target 은 {"tags": TagExpr}. 조건 경로 "<source|target>.<컴포넌트>.<수치 필드>", op < <= > >= == !=.
  효과의 who 는 spawn(기본 source)·event(기본 target)·score.add(기본 target) 외에는 필수. spawn 의 count 1~16, event 이름은
  "<pack>.<name>". score.add (v0.2 M3-T2, ADR-0048) = {value: 정수 ±1,000,000, team?: 팀 id} — 9장.
- BehaviorGraph: 조건 노드는 {"<노드>": 인자} 하나 또는 인자 없는 문자열("targetValid", "true").
  stateTime 은 {"stateTime": {"op": ">=", "seconds": 수}} — 초 단위 (틱으로 쓰지 않는다: tick rate 와 무관하게).
  when 은 필수 (항상이면 "true"). 행동 노드: wander{radius, interval}, seek{tags, strategy:"nearest"},
  flee{tags, distance}, interact{name}, idle, setBlackboard{slot 0~7, value}. 5B 가 실행한다.
- 검증 메시지 형식: "<팩 폴더>/<파일>:<JSON 포인터>: 오류|경고 V? — 설명". 모든 문제를 모아 보고한다.
  V4(고아 Rule)·V5(도달 불가 상태)는 경고, 나머지는 오류. 경고만 있으면 로드는 성공한다.
- contentHash = FNV(팩 (id, version) + 파일 (상대 경로, 바이트) 매니페스트 + 내장 머티리얼). 줄 끝(CRLF)은 LF 로
  정규화한 뒤 해시한다. presentation/ 은 읽지도 해시하지도 않는다. actions.json 은 v0.2 M3-T1 부터 읽는다 (6장).

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

Phase 12C 월드 오버레이 (core/content/ContentOverlay, ADR-0030)
- 항목 = (종류 prefab · rule · behavior, id) → 이 문서의 형식 그대로인 JSON 객체 하나 ("id" = 항목 id, Rule 도 객체 하나).
  같은 id 의 팩 항목을 바꾸고, 없으면 더한다 (id 의 팩 접두사는 월드가 읽은 팩 — V1 · V2). 1024 항목 · 항목당 16 KB 까지.
- ContentLoader::applyOverlay(base, overlay) = 로더가 남긴 팩 원본(ContentSources — 파일 텍스트)에서 다시 만든다. 검증은
  V1~V7 그대로, 문제 위치는 "overlay/<prefabs|rules|behaviors>/<id>.json". 태그 · 머티리얼은 바꿀 수 없다 (tags.json ·
  terrain.json 은 팩에서만 — 오버레이가 모르는 태그를 쓰면 V7). 바꾼 Rule 은 원래 정의 순서, 더한 Rule 은 맨 뒤.
- 각 항목은 읽은 JSON 을 raw 로 갖고 overlay 표시가 있다 (에디터 "콘텐츠" 창). contentHash 는 팩 그대로.
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
  modes/*.json            GameMode 1파일 1개 (선택 — v0.2 M2, 9장)
  actions.json            PlayerAction 정의 (선택)
  triggers/*.json         Trigger 1파일 1개 (선택 — v0.2 M4-T1, 10장)
  scripts/**/*.ts         SandboxScript (선택 — v0.3 M3-T2, 11장 · 19-SANDBOXSCRIPT). 팩 하나 = 프로그램 하나
  presentation/           클라이언트 전용: sounds.json(이벤트→사운드), 아이콘 등  ← contentHash 제외
    input.json            게임 키 기본값 (v0.2 M3-T3, ADR-0049) — sandbox.input 형식 (07 5장), play.* 이름만.
                          "play.action.<PlayerAction id>": ["Q"] = 누르면 커서 자리에 그 행동. 로더는 원문만 PackInfo 에
```

```jsonc
// pack.json
{ "id": "ecosystem", "version": "0.1.0", "requires": [], "description": "Grass / Rabbit / Wolf" }
```

팩을 찾는 루트는 둘입니다: 저장소의 `content/` (`--content-root`, 시나리오 · 세이브) 와 게임 프로젝트의 `<프로젝트>/content/`
(v0.2 — 매니페스트 `content.packs` 가 고른다, [18-PLATFORM](18-PLATFORM.md) 5.2 · ADR-0040). 팩 형식 · 규칙은 같습니다.

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
같은 id 가 두 번 정의되면 오류 (덮어쓰기는 월드 오버레이만 허용 — 12C, 0장 끝)
단계 (v0.3 M3-T2): 0 스크립트 (팩 순서로 scripts/ 컴파일 → 스크립트 컴포넌트를 콘텐츠의 카탈로그에) → 1 태그 → 2 머티리얼 →
     3 파일별 (Prefab · Rule · Behavior · 모드 · 행동 · 트리거 — V3 가 스크립트 컴포넌트를 안다) → 4 오버레이 → 교차 검사 · link
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

**Phase 8B (표현):** 콘텐츠의 `render` 키는 아직 없다 — 지형 색은 표현 에셋 `assets/<팩>/materials.json` 의
`"terrain/<머티리얼 id>": {"color": [r, g, b]}` 로 정한다 (06 7.1 — contentHash 와 무관, 서버는 읽지 않는다). 없으면 이름 해시
색. 타일 그림(`tileset`) · 경계 섞기 · 애니메이션은 `[계획]` (ADR-0022).

## 6. Tag / Action

```jsonc
// tags.json — 팩이 쓰는 태그 선언 (선언되지 않은 태그 사용 = 검증 오류, 오타 방지)
["animal", "herbivore", "predator", "prey", "plant", "dead"]

// actions.json — PlayerAction 정의 (플레이어 역할이 쓸 수 있는 행동)
[ { "id": "eco.feed", "name": "먹이 주기", "targets": { "tags": { "all": ["animal"] } }, "range": 3, "maxTargets": 2,
    "conditions": [ { "field": "target.life.energy.value", "op": "<", "value": 90 } ],
    "effects": [ { "op": "field.add", "who": "target", "field": "life.energy.value", "value": 20 } ] } ]
```

PlayerAction (v0.2 M3-T1, [ADR-0047](adr/0047-player-action-content-command-point-targets.md)):

```text
- 명령 PlayerAction{action, position} — 플레이어가 월드의 한 점을 고른다. 대상 = core.transform 이 있는 개체 중 그 점에서
  range 안 · targets 태그 식 · conditions 에 맞는 것을 (거리², saveId) 순으로 maxTargets 개. 없으면 명령 거절.
- 키: id (<pack>.<name>) · name (표시, 1~128 바이트, 기본 id) · targets {tags} (기본 = 무엇이든) · range (0~32, 기본 1) ·
  maxTargets (1~64, 기본 1) · conditions (Rule 과 같은 모양, target. 만) · effects (Rule 과 같은 op, 비어 있으면 안 된다).
- source 가 없다: effects · conditions 의 who 는 target 뿐 (spawn · event 의 기본도 target — 대상 둘레 ±0.5 에 spawn).
- 효과는 대상마다 정의 순서로 명령 단계에서 바로. 앞 효과가 지운 대상의 남은 효과는 건너뛴다.
- 역할 player 부터, 게임 세션이 있으면 Running 에서만. 팩 전체 64 개까지. 월드 오버레이로 고치기는 [계획].
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
V8  GameMode: 필수 키 · 범위 (플레이어 1~64, 팀 ≤ 8 · id 중복 · 팀 태그 중복, 초 범위, 끝 규칙 ≤ 32 · when (time · count ·
    score) · op · result · team 참조)
V9  PlayerAction: who · 조건 경로는 target 뿐, range 0~32, maxTargets 1~64, 팩 전체 64 개까지 (v0.2 M3-T1)
V10 Trigger: 그래프 모양 (이벤트 하나 · 나무 · 순환 · 닿음 · 깊이 16 · 노드 64 · 대상) · limits · 개수 256, 재진입 고리 경고 (v0.2 M4-T1)
S1xx~S5xx 스크립트 (scripts/*.ts — v0.3 M3-T2): 컴파일러 진단 그대로, 포인터 자리에 "줄:열" (19 8장). 스크립트 컴포넌트 id 가
    엔진 컴포넌트 · 앞 팩의 스크립트 컴포넌트와 같으면 V1, 시스템 id 가 앞 팩의 시스템과 같으면 V1 (v0.3 M4-T1), 시스템 구독의
    태그가 tags.json 에 없으면 V7 (v0.3 M4-T2)
실패 메시지 형식:  <파일>:<JSON 포인터>: <규칙> — <설명>
```

---

## 9. GameMode (v0.2 M2 — [ADR-0045](adr/0045-game-mode-content-session-in-simulation-player-actions.md))

게임 하나의 진행 규칙: 참가자 · 팀 · 시작 카운트다운 · 끝 조건 · 재시작. 프로젝트 매니페스트(schemaVersion 2)의
`gameMode` 가 하나를 고른다 ([18-PLATFORM](18-PLATFORM.md) 5.2). 진행 상태(Lobby → Starting → Running → Finished)는
시뮬레이션의 `game.session` 이 든다 — 전이는 단계 17b GameRules ([03](03-SIMULATION.md) 2장, M2-T2).

```jsonc
// modes/survival.json — 파일 하나에 모드 하나
{
  "id": "eco.survival",                        // <팩>.<이름> (V1)
  "name": "토끼 지키기",                        // 필수, ≤ 256 바이트
  "description": "3 분 동안 토끼를 지켜라",     // 선택
  "players": { "min": 1, "max": 4 },           // 1 ~ 64, 기본 1 ~ 16
  "teams": [ { "id": "red", "name": "빨강", "tag": "team_red" } ], // 선택, ≤ 8. id 는 모드 안에서만, tag 선택 (M3-T2)
  "start": { "countdownSeconds": 3 },          // Starting 길이 0 ~ 60 초 (기본 0)
  "end": [                                     // 위에서부터 처음 맞는 것 (Running 중 틱마다). 비면 끝나지 않는다
    { "when": { "time":  { "op": ">=", "seconds": 180 } }, "result": "win" },
    { "when": { "count": { "tags": { "all": ["prey"] }, "op": "<=", "value": 0 } }, "result": "lose" },
    { "when": { "time": { "op": ">=", "seconds": 60 },
                "count": { "tags": { "all": ["predator"] }, "op": "==", "value": 0 } }, "result": "win", "team": "red" },
    { "when": { "score": { "team": "red", "op": ">=", "value": 10 } }, "result": "win", "team": "red" }
  ],
  "restart": { "mode": "manual", "delaySeconds": 5 }   // manual(명령) | auto(지연 뒤 서버가)
}
```

```text
when      time  = Running 에 들어간 뒤 지난 시간 {op, seconds 0 ~ 86400}
          count = 태그 식(6장)에 맞는 살아 있는 개체 수 {tags, op, value 0 ~ 1,000,000}
          score = game.score 의 한 칸 {team?, op, value 정수 ±10억} — team 이 없으면 공용 칸 (v0.2 M3-T2)
          여럿 쓰면 AND. 하나는 있어야 한다. op = < <= > >= == !=
result    win · lose · draw. team 이 있으면 그 팀의 승리 · 패배 (draw 에는 team 없음), 없으면 모두
시간      초는 로드할 때 틱으로 (× 30, 반올림 — 0.5 초 = 15 틱). 시뮬레이션은 틱만 본다 (04)
팀 태그  teams[].tag (선택, 선언된 태그 V7, 두 팀이 같은 태그 V8): core.tags 에 그 태그가 있는 개체가 그 팀 (여럿이면 앞의
          팀). 효과 score.add 가 team 을 안 쓰면 who 의 팀 칸에, 팀이 없는 개체면 공용 칸에 더한다
점수      game.score (세션 엔티티 · 엔진 관리): 칸 0 = 공용, 1..8 = teams 순서. 시작 (GameControl Start) · resetSession 이 0 으로.
          score.add 는 세션이 Running 일 때만 센다 (Rule · PlayerAction 모두). team 이 세션 모드에 없으면 효과 없음 (어느
          모드에도 없으면 V2 경고). 값은 ±1,000,000,000 으로 자른다 (ADR-0048)
스폰 지점 컴포넌트 game.spawn_point {prefab, count 1~64, radius 0~16} — 에디터로 월드에 놓는다 (core.transform 필요). 시작할 때
          지점을 saveId 순으로 prefab 을 count 개, 위치 ±radius 안에 만든다 (03 4장). 다시 나오기 · 주기적 생성은 [계획]
[계획]    월드 오버레이로 고치기(EditContent), 가장 높은 팀 승리 · 점수 이벤트 (M4 Visual Trigger), 접속자별 점수
```

데모 모드 (v0.2 M3-T3, [ADR-0049](adr/0049-game-keys-pack-presentation-camera-start-demo-mode.md)) — `content/ecosystem`:

```text
modes/protect.json      eco.protect "토끼 지키기": 카운트다운 3 초, 토끼(prey) 0 → lose · 늑대(predator) 0 → win · 2 분 → win,
                        재시작 manual
actions.json            eco.feed "먹이 주기" (prey · 반경 3 · 3 마리 · energy < 90 → +25) · eco.trap "덫 놓기" (predator ·
                        반경 1.5 · 1 마리 → score.add +1 · destroy)
presentation/input.json play.action.eco.feed = Q · play.action.eco.trap = E
만들기                  sbx new <폴더> --id <gameId> --from ecosystem_small --mode eco.protect (15 7장, CTest project_demo_protect)
```

---

## 10. Trigger (v0.2 M4-T1 — [ADR-0050](adr/0050-visual-trigger-schema-tree-graph-flat-program.md))

월드 수준 규칙: 이벤트 하나 → 조건 → 효과. 원본은 그래프 (노드 · 연결 · 편집 배치), 로더가 실행 표현 (이벤트 + 조건 · 효과 표 +
펼친 명령열) 을 만든다. 실행은 단계 17c (v0.2 M4-T2, [ADR-0051](adr/0051-trigger-execution-stage-17c-same-tick-cascade-budget.md),
03 2장) — **게임 세션이 Running 일 때만** (세션 없는 샌드박스 월드에서는 돌지 않는다).

```jsonc
// triggers/gate.json — 파일 하나에 트리거 하나
{
  "id": "eco.gate",                 // <팩>.<이름> (V1)
  "name": "우리 문",                 // 선택, 1~128 바이트 (기본 id)
  "limits": { "perTick": 4 },       // 선택, 한 틱에 몇 번까지 (1 ~ 64, 기본 1) — M4-T2 가 지킨다
  "nodes": [
    { "id": "enter", "type": "event.area_enter", "min": [-2, -2], "max": [2, 2], "tags": { "all": ["prey"] },
      "pos": [40, 80] },                                    // pos · note = 편집 정보 (실행 표현에 없다)
    { "id": "hungry", "type": "cond.field", "field": "life.energy.value", "op": "<", "value": 50 },
    { "id": "feed",   "type": "effect.field.add", "field": "life.energy.value", "value": 20 },
    { "id": "point",  "type": "effect.score.add", "value": 1 }
  ],
  "links": [
    { "from": "enter", "to": "hungry" },
    { "from": "hungry", "to": "feed" },                    // port 기본 "then"
    { "from": "hungry", "to": "point", "port": "else" }    // else 는 조건 노드에서만
  ]
}
```

노드 종류 (표 = `core/trigger/TriggerSchema` — 에디터 폼도 같은 표, 모든 노드 권한 ServerGameplay):

| type                | 키 (* 필수)                                       | 뜻                                                          |
|---------------------|---------------------------------------------------|-------------------------------------------------------------|
| `event.game_start`  | —                                                 | 세션이 Running 이 될 때                                     |
| `event.tick`        | —                                                 | Running 동안 매 틱                                          |
| `event.timer`       | seconds* (1 틱 ~ 86400) · repeat                  | Running 경과 seconds 에 (repeat 면 그 간격마다)             |
| `event.area_enter`  | min* · max* ([x, y]) · tags                       | 태그에 맞는 개체가 사각형 밖 → 안 — **대상** = 그 개체      |
| `event.score`       | team · op* · value* (정수)                        | 점수 칸이 조건을 거짓 → 참으로 넘을 때 (team 없으면 공용)   |
| `event.death`       | tags                                              | 태그에 맞는 개체가 죽음 — **대상** = 죽은 개체              |
| `cond.count`        | tags* · op* · value* (0 ~ 1,000,000)              | 태그 식에 맞는 개체 수                                      |
| `cond.score`        | team · op* · value*                               | 점수 칸                                                     |
| `cond.time`         | op* · seconds*                                    | Running 경과                                                |
| `cond.field`        | field* · op* · value*                             | 대상의 수치 필드 (대상 필요)                                |
| `cond.tags`         | tags*                                             | 대상의 태그 식 (대상 필요)                                  |
| `cond.chance`       | p* (0 ~ 1)                                        | 확률 (결정적 난수 — M4-T2)                                  |
| `effect.field.add` · `.set` | field* · value*                           | 대상 필드 (FieldMeta 범위로 자름, 대상 필요)                |
| `effect.destroy`    | —                                                 | 대상 없애기 (대상 필요)                                     |
| `effect.spawn`      | prefab* · count (1 ~ 16) · at ([x, y])            | at 이 없으면 대상 둘레 ±0.5 (그때는 대상 필요)              |
| `effect.tag.add` · `.remove` | tag*                                     | 대상 태그 (대상 필요)                                       |
| `effect.event`      | name* (`<pack>.<name>`)                           | EventStream.Custom                                          |
| `effect.score.add`  | value* (±1,000,000) · team                        | 점수 (team → 대상의 팀 태그 → 공용, 9장)                    |
| `effect.game.end`   | result* (win · lose · draw) · team                | 게임 끝 (draw 에는 team 없음)                               |

```text
모양 (V10)  이벤트 노드 하나, 이벤트로 들어오는 연결 없음, 노드마다 들어오는 연결 하나까지 (나무 — 같은 노드를 두 길에서 쓰려면
            복사), 순환 없음, 모든 노드가 이벤트에서 닿음, else 는 조건에서만, 노드 ≤ 64, 깊이(이벤트에서 노드 수) ≤ 16,
            대상이 필요한 노드는 대상을 주는 이벤트(area_enter · death) 아래, 노드 id 는 트리거 안에서 하나 (소문자 · 숫자 · '_')
            경고: 닿는 효과가 없다
값          표에 없는 키 · 필수 키 · 타입 · 범위 V3, 태그 V7, Prefab V2, 팀 id 가 어느 GameMode 에도 없음 V2 경고
DB          트리거 ≤ 256 (V10), id 중복 V1. 정의 순서 = 팩 로드 순 → 파일 경로 순 (같은 틱 실행 순서 — M4-T2)
재진입      효과가 내는 이벤트 (score.add → event.score, destroy → event.death, spawn → event.area_enter) 를 듣는 트리거로 잇는
            고리 (자기 자신 포함) 는 경고 V10 — 한 틱에 perTick 번까지만 돈다
실행 표현   조건 = Cond(거짓이면 else 로) · then · (else 가 있으면) Jump(끝으로) · else, 효과 = Effect · 다음 노드. 자식은 links 순서
실행 (M4-T2) 이벤트: 앞 상태 = 틱 처음 (그 틱의 명령 전) 스냅숏. area_enter 는 명령으로 옮기거나 만든 개체도, death 의 대상
            위치 · 태그는 틱 처음 값 (트리거의 destroy 면 지우기 직전 값). 효과가 낸 이벤트 (destroy · spawn · score.add) 는 같은
            틱에 이어서. 예산: 트리거마다 perTick, 틱 전체 1024 실행 · 65536 줄 — 넘으면 건너뛴다. game.end 면 그 틱의 남은 실행 없음
대상 규칙   죽은 대상: field · destroy · tag 는 아무것도 안 함, spawn 은 죽은 자리, score.add 는 공용 칸 (팀은 team 으로)
오버레이    (M4-T3) EditContent{trigger} 로 월드에서 바꾸거나 더한다 — 바꾼 것은 팩의 실행 순서, 더한 것은 맨 뒤 (10 8장 "폼")
[계획]      노드 그래프 캔버스, 합류 · 변수 · 반복 · 대기 노드, 영역 나감 · 머묾, 세션 없는 월드의 트리거
```

## 11. 스크립트 (v0.3 M3-T2 — [ADR-0059](adr/0059-script-components-schema-dynamic-pool-name-order-catalog.md) 6, [19](19-SANDBOXSCRIPT.md) 15장)

```text
파일       <팩>/scripts/**/*.ts — 경로 = 모듈 이름 ("scripts/mana.ts"), import 는 같은 팩 안만 (19 1장). 그 밖의 확장자는
           "알 수 없는 파일" 경고. contentHash 에 들어간다 (팩의 다른 파일과 같다 — 스크립트를 바꾸면 contentHash 가 바뀐다)
컴파일     로드 0 단계: 팩마다 검사 + 코드 생성. 진단은 콘텐츠 문제 "<팩 폴더>/scripts/a.ts:줄:열: 오류 S404 — …" (규칙 = 진단 코드).
           오류가 있으면 로드 실패 (sbx check · 서버 · 클라이언트가 같은 목록을 본다). 팩마다 만든 바이트코드 (.sbc 모듈) 는
           ContentDatabase::scriptPrograms 에 (v0.3 M4-T1 — 오버레이 DB 와 나눈다)
컴포넌트   defineComponent (19 15장) 가 정의한 것을 콘텐츠의 카탈로그 = 엔진 카탈로그 + 스크립트 컴포넌트 에 넣는다 (팩 순서).
           이 카탈로그로 Prefab 의 components · Rule 조건 · 효과의 필드 경로를 검사한다 (V3 — 모르는 필드 · 범위 · 타입은 내장과
           같은 메시지). 다른 함수에 오류가 있어도 오류 없는 정의는 넣는다 (Prefab 에 이어지는 V3 가 나지 않게). id 가 엔진 ·
           앞 팩과 겹치면 V1 (덮어쓰기 없음)
월드       ContentDatabase::worldCatalog(엔진) = 그 카탈로그 (스크립트 컴포넌트가 없으면 엔진 카탈로그 그대로). SimulationWorld ·
           loadWorld · ClientWorld 가 이것을 쓴다 → Prefab 생성 · 명령 (Add · Change · Remove — Inspector) · WorldHash · 세이브 ·
           복제가 내장과 같은 경로. 오버레이 (12C) 는 스크립트를 다시 컴파일하지 않고 같은 카탈로그를 나눈다
없는 정의  스크립트 컴포넌트가 든 세이브를 그 스크립트가 없는 콘텐츠로 열면 Opaque 로 보관한다 (09 · 9.5 — 해시 비교는 건너뛴다)
시스템     (v0.3 M4-T1, ADR-0062) export default defineSystem({ id, intervalTicks, onTick }) — 파일 하나에 하나 (19 17장). 시뮬레이션
           단계 16b 가 (팩 순서, 시스템 id) 차례로 부른다 (03 2장). 시스템 id 가 앞 팩의 시스템과 같으면 V1 (자리는 뒤 팩의 정의).
           (M4-T2, ADR-0063) onStart · onStop · onEvent 는 단계 17d, 구독 events 의 tags 는 콘텐츠 태그 이름 — 로드 1 단계 뒤 태그
           비트로 풀어 (ScriptPackProgram::events) 없는 태그는 V7 ("시스템 'id' 의 events.death.tags.all — 선언되지 않은 태그")
```
