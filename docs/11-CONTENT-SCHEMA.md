# 11. 콘텐츠 스키마

> **규범 문서.** `content/<pack>/` 데이터의 모양과 검증 규칙을 정합니다.
> 데이터 모양을 바꾸면 이 문서를 같은 커밋에서 고칩니다. 상태: 전부 `[계획]` — Phase 5.

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
네임스페이스 pack id 가 접두사. 엔진 예약: core ai life society combat net persist render editor client
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
  },
  "inherits": null                          // 후속: 단일 상속 (병합 규칙 미정)
}
```

| 규칙 | 내용 |
|---|---|
| P1 | `components`의 키는 등록된 stableId 이름. 모르는 이름은 Opaque로 보존 (서버에서 `render.*` 등) |
| P2 | 생략한 필드는 컴포넌트 기본값 |
| P3 | 값은 FieldMeta 범위로 검증. 범위 밖이면 오류 |
| P4 | `core.transform`은 생성 시 위치로 덮어쓴다 |

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

| 필드 | 의미 |
|---|---|
| action | Behavior의 `interact(action)`과 매칭되는 문자열 |
| tags | `all`(모두 보유), `any`(하나 이상), `none`(하나도 없음) |
| range | 월드 단위 거리 |
| exclusive | 같은 target에 대해 이번 틱 하나만 승리 (`destroy(target)`이 있으면 자동 true) |
| conditions | 리플렉션 경로 비교. op: `< <= > >= == !=` |
| effects | [03-SIMULATION](03-SIMULATION.md) 6.3의 op만 |

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

## 7. Ecosystem 초기 수치 (밸런스 출발점 — **추정**, Phase 5에서 조정)

| 항목 | Grass | Rabbit | Wolf |
|---|---|---|---|
| 초기 개체 비율 | 70% | 25% | 5% |
| maxSpeed (타일/초) | — | 2.5 | 3.2 |
| energy max / 시작 | — | 100 / 60 | 150 / 100 |
| drainPerSecond | — | 1.5 | 2.0 |
| 먹었을 때 energy | — | +12 (풀) | +30 (토끼) |
| sensor radius | — | 8 | 12 |
| reproduce minEnergy / cost / cooldown(초) | 성장 완료 시 / — / 20 | 70 / 40 / 12 | 110 / 60 / 30 |
| maxAge (초) | 600 | 1800 | 2400 |
| Grass growth | stage 0→2, rate 0.05/초, 먹히면 growth −1 stage, stage 0에서 먹히면 사라짐 | | |
| Grass spread | stage 2에서 인접 빈 셀로 번식 (확률 RandomService `Spread`) | | |

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
