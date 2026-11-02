# SandboxScript 표준 라이브러리 안내 (v0.5)

> 제작자용 안내다. 규범 (지원 매트릭스 · 정확한 뜻 · 연료) 은 [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 25장, 결정은
> [ADR-0085](../adr/0085-script-standard-library-intrinsics-collections-callbacks.md). 예제 프로젝트는
> [samples/stdlib_tour](../../samples/stdlib_tour/README.md).

SandboxScript 는 TypeScript 의 부분집합이다. 표준 라이브러리는 JS 엔진 없이 VM 안의 C++ (내장) 로 돈다 — 그래서 빠르고, 같은
입력이면 언제나 같은 결과 (서버 시뮬레이션 · 세이브 · 리플레이) 이며, 연료 · 메모리 예산을 우회하지 않는다.

## 1. 어디에 무엇이 있나

```text
TS 전역 (import 없이)   Array 메서드 · Map · Set
"sandbox" 에서 import    Stack · Queue · Deque · PriorityQueue · MinHeap · MaxHeap · BitSet · Graph,
                         sortNumbers · binarySearch · lowerBound · upperBound · sum · average · minOf · maxOf · countIf ·
                         prefixSum · rotate · unique · partition · shuffle, clamp · lerp · inverseLerp · smoothstep ·
                         smootherstep · repeat · pingPong · approximately · degToRad · radToDeg, vec2.*, setUnion ·
                         setIntersection · setDifference
ctx (서버)               ctx.near · nearWith · nearest · kNearest · inBox, ctx.requestPath · pathState · pathPoints · cancelPath
```

## 2. 배열

```ts
const xs: i32[] = [5, 1, 4]
const last = xs.pop()            // i32 | undefined — 빈 배열이면 undefined
if (last !== undefined) { ctx.log(`${last}`) }
xs.sort((a, b) => a - b)         // 비교자 — 안정 정렬. 수 배열의 sort() 는 거절 (JS 는 문자열로 비교한다)
const big = xs.filter(x => x > 2)
const total = xs.reduce((acc, x) => acc + x, 0)   // 초기값은 꼭 (빈 배열의 TypeError 를 없앤다)
```

- 콜백은 **최상위 함수 이름** 이나 **캡처 없는 화살표** 다. 화살표 몸은 자기 매개변수 · 최상위 const · 함수 · enum 만 쓴다 —
  바깥 함수의 지역 변수를 쓰면 S304 (클로저는 [계획]). 필요한 값은 원소에 넣거나 최상위 const 로.
- 콜백 매개변수의 타입은 원소와 같아야 한다 (i32 배열에 `(x: number) => …` 는 S404 — TS 보다 엄격).
- `pop` · `shift` · `find` 의 결과는 `T | undefined` — `x !== undefined` 로 좁힌다. `T | null` 과 섞지 않는다.

## 3. Map · Set

```ts
const counts = new Map<string, i32>()
const c = counts.get("potion")
counts.set("potion", (c !== undefined ? c : 0) + 1)
for (const k of counts.keys()) { /* 넣은 차례 */ }
const kinds = new Set<string>(["a", "b", "a"])   // a, b
const both = setIntersection(kinds, new Set<string>(["b", "c"]))
const list = Array.from(counts.keys())
```

- 키는 i32 · number · string · boolean · enum. 반복은 **넣은 차례** (해시 순서가 아니다 — 결정적).
- 반복 중에 넣으면 그 항목도 돌고, 지우면 건너뛰고, clear 하면 끝난다 (JS 와 같다).
- `keys()` · `values()` 는 for-of 와 `Array.from` 에서만. Map 자체의 for-of (`[k, v]`) · `entries` · `forEach` 는 [계획].

## 4. "sandbox" 컬렉션

```ts
import { Queue, PriorityQueue, BitSet } from "sandbox"
const open = new Queue<i32>()
open.enqueue(0)
const next = open.dequeue()          // i32 | undefined
const pq = new PriorityQueue<Entity>()
pq.push(enemy, distance)            // 작은 우선순위 먼저, 같으면 넣은 차례. NaN 우선순위는 false
const seen = new BitSet(1024)
seen.set(3)
```

## 5. 알고리즘 · 수학 · vec2

```ts
import { sortNumbers, binarySearch, shuffle, clamp, smoothstep, vec2 } from "sandbox"
sortNumbers(xs)                       // 오름차순, -0 < +0, NaN 은 끝
const i = binarySearch(xs, 7)         // 없으면 -1
shuffle(deck, ctx.randomInt(0, 2147483647))   // 씨앗이 같으면 같은 차례 (Math.random 은 없다)
const hp = clamp(rawHp, 0, 100)       // 셋 다 i32 면 i32
const dir = vec2.normalize(vec2.sub(target, me))   // 0 벡터는 (0, 0)
```

## 6. 그래프 (보드게임 · 퍼즐)

```ts
import { Graph } from "sandbox"
const g = new Graph(36, false)        // 노드 0 … 35, 무방향
g.addEdge(0, 1)                       // 가중치 기본 1 (음수 · NaN · 무한은 false)
const order = g.bfs(0)
const path = g.pathTo(0, 35)          // i32[] | undefined — 같은 거리면 작은 노드 번호
const plan = g.topoSort()             // 순환이면 undefined
```

스크립트 안의 그래프다 (호출 안에서 만들고 버린다). 월드의 길찾기는 7 절의 경로 요청을 쓴다.

## 7. 엔진 질의 · 경로 요청 (서버)

```ts
const target = ctx.nearest(x, y, 20)          // Entity | null — 거리가 같으면 saveId 작은 것
for (const e of ctx.kNearest(x, y, 40, 16)) { }
for (const e of ctx.inBox(-10, -10, 10, 10)) { }
if (ctx.requestPath(unit, gx, gy)) { }          // 개체의 ai.path 에 — 호출 끝에 반영
const state = ctx.pathState(unit)               // 0 없음 · 1 요청 · 2 제출 · 3 따라감 · 4 길 없음 · -1 ai.path 없음
```

- 위치는 틱 처음 (단계 4) 의 공간 색인이다. 이 틱에 옮기거나 만든 개체는 다음 틱부터 보인다.
- 경로는 엔진이 계산한다: 요청 → 다음 틱 단계 8 제출 (틱마다 예산) → 그다음 틱 결과. 기다리는 동안 다시 요청하지 않으려면
  pathState 가 1 · 2 인지 본다. 경로는 ai.path 가 있는 개체에만 (Prefab 에 붙인다).

## 8. 예산 · 결정론

- 모든 내장은 원소 · 비교 · 방문마다 연료를 낸다. 콜백은 자기 명령의 연료를 그대로 — 내장으로 연료를 피할 수 없다.
- 컬렉션 · 새 배열은 그 호출의 아레나에 산다 (다음 호출이 버린다). 틱을 넘기는 상태는 컴포넌트에 둔다.
- 반복 차례 · 같은 값의 차례 · 해시 · 정렬 알고리즘이 고정이라 같은 월드 = 같은 결과 (세이브 → 로드 · 리플레이 · Worker 수와
  무관).

## 9. 얼마나 빠른가 (2026-11-02, i7-14700K · MSVC 19.44 RelWithDebInfo — [14](../14-PERFORMANCE.md) 7.15)

```text
원소 10,000   sortNumbers 0.75 ms · sort(cmp) 6.2 ms · 같은 병합 정렬을 스크립트로 9.0 ms · Map set+has 1.8 ms ·
              PriorityQueue push+pop 3.7 ms · Graph.bfs (격자) 0.17 ms · Graph.dijkstra (격자) 1.2 ms
```

비교자 정렬은 비교마다 스크립트 함수를 부른다 — 수만 정렬하면 sortNumbers 가 8 배쯤 빠르다.
