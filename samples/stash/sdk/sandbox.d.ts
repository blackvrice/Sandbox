// SandboxScript SDK — 생성된 파일 (sbx sdk). 손으로 고치지 않는다 — script/api 의 표를 고치고 다시 만든다.
// Host API 1 · 지문 3b37e380f77eddfd — docs/19-SANDBOXSCRIPT.md
declare module "sandbox" {
  /** 32 비트 정수 — 산술은 감싼다 (2147483647 + 1 = -2147483648). TS 에서는 number 와 같다 */
  export type i32 = number;
  /** JS ToInt32 — NaN · ±Infinity → 0, 0 쪽으로 자르고 2^32 로 감싼다 */
  export function toI32(x: number): i32;
  /** 정수 나눗셈 (0 쪽으로 자름) — b 가 0 이면 트랩 */
  export function idiv(a: i32, b: i32): i32;
  /** 합집합 — 새 Set (a 의 넣은 차례 다음 b 에만 있는 것) */
  export function setUnion<T>(a: Set<T>, b: Set<T>): Set<T>;
  /** 교집합 — 새 Set (a 의 넣은 차례) */
  export function setIntersection<T>(a: Set<T>, b: Set<T>): Set<T>;
  /** 차집합 a - b — 새 Set (a 의 넣은 차례) */
  export function setDifference<T>(a: Set<T>, b: Set<T>): Set<T>;
  /** 스택 — 뒤에서 넣고 뺀다 */
  export class Stack<T> {
    constructor();
    push(x: T): void;
    pop(): T | undefined;
    peek(): T | undefined;
    clear(): void;
    readonly size: number;
  }
  /** 큐 (고리 버퍼) — 뒤에 넣고 앞에서 뺀다 */
  export class Queue<T> {
    constructor();
    enqueue(x: T): void;
    dequeue(): T | undefined;
    peek(): T | undefined;
    clear(): void;
    readonly size: number;
  }
  /** 덱 (고리 버퍼) — 양쪽에서 넣고 뺀다 */
  export class Deque<T> {
    constructor();
    pushFront(x: T): void;
    pushBack(x: T): void;
    popFront(): T | undefined;
    popBack(): T | undefined;
    peekFront(): T | undefined;
    peekBack(): T | undefined;
    clear(): void;
    readonly size: number;
  }
  /** 우선순위 큐 — 작은 우선순위 먼저, 같으면 넣은 차례. NaN 우선순위는 넣지 않는다 (false) */
  export class PriorityQueue<T> {
    constructor();
    push(x: T, priority: number): boolean;
    pop(): T | undefined;
    peek(): T | undefined;
    peekPriority(): number | undefined;
    clear(): void;
    readonly size: number;
  }
  /** 최소 힙 (number — -0 < +0, NaN 은 가장 크다) */
  export class MinHeap {
    constructor();
    insert(x: number): void;
    extractMin(): number | undefined;
    peek(): number | undefined;
    clear(): void;
    readonly size: number;
  }
  /** 최대 힙 (number — MinHeap 의 거꾸로) */
  export class MaxHeap {
    constructor();
    insert(x: number): void;
    extractMax(): number | undefined;
    peek(): number | undefined;
    clear(): void;
    readonly size: number;
  }
  /** 비트 집합 — 크기는 만들 때 정한다 (범위 밖 첨자는 트랩) */
  export class BitSet {
    constructor(size: i32);
    set(i: i32): void;
    clear(i: i32): void;
    test(i: i32): boolean;
    count(): i32;
    readonly size: number;
  }
  /** 그래프 — 노드 0 … n-1, 이웃은 넣은 차례 (v0.5 M8-T7 — 19 25.6). 월드 길찾기와 따로 */
  export class Graph {
    constructor(nodes: i32, directed: boolean);
    /** 간선 (무방향이면 양쪽). 음수 · NaN · 무한 가중치는 넣지 않는다 (false) */
    addEdge(a: i32, b: i32, weight?: number): boolean;
    neighbors(a: i32): i32[];
    /** 너비 우선 방문 차례 */
    bfs(start: i32): i32[];
    /** 깊이 우선 방문 차례 (재귀 앞 순서와 같다) */
    dfs(start: i32): i32[];
    /** 최단 거리 (닿지 않으면 Infinity) */
    dijkstra(start: i32): number[];
    /** 최단 경로 (같은 거리 = 작은 노드 번호), 없으면 undefined */
    pathTo(start: i32, goal: i32): i32[] | undefined;
    /** A* — 노드 자리 (xs · ys) 의 유클리드 휴리스틱 */
    astar(start: i32, goal: i32, xs: number[], ys: number[]): i32[] | undefined;
    /** 위상 정렬 (Kahn — 작은 번호 먼저), 순환이면 undefined */
    topoSort(): i32[] | undefined;
    /** 노드마다 연결 성분 번호 (방향은 무시) */
    components(): i32[];
    readonly size: number;
  }
  /** 수 배열을 오름차순으로 (안정 — -0 < +0, NaN 은 끝). 같은 배열을 돌려준다 */
  export function sortNumbers(xs: number[]): number[];
  /** 오름차순 배열에서 x 의 자리 (없으면 -1) */
  export function binarySearch(xs: number[], x: number): i32;
  export function binarySearch(xs: string[], x: string): i32;
  /** x 보다 작지 않은 첫 자리 (오름차순 배열) */
  export function lowerBound(xs: number[], x: number): i32;
  export function lowerBound(xs: string[], x: string): i32;
  /** x 보다 큰 첫 자리 (오름차순 배열) */
  export function upperBound(xs: number[], x: number): i32;
  export function upperBound(xs: string[], x: string): i32;
  /** 합 — 차례대로 더한다 */
  export function sum(xs: number[]): number;
  /** 평균 — 빈 배열은 NaN */
  export function average(xs: number[]): number;
  /** 가장 작은 값 (Math.min 과 같다 — NaN 이 있으면 NaN), 빈 배열은 undefined */
  export function minOf(xs: number[]): number | undefined;
  /** 가장 큰 값 (Math.max 와 같다), 빈 배열은 undefined */
  export function maxOf(xs: number[]): number | undefined;
  /** 조건이 참인 원소 수 */
  export function countIf<T>(xs: T[], f: (x: T, i: number) => boolean): i32;
  /** 누적 합 (새 배열) */
  export function prefixSum(xs: number[]): number[];
  /** 왼쪽으로 k 칸 돌린다 (음수면 오른쪽). 같은 배열을 돌려준다 */
  export function rotate<T>(xs: T[], k: i32): T[];
  /** 겹침을 뺀 새 배열 (첫 차례를 지킨다 — Set 과 같은 같음) */
  export function unique<T extends number | string | boolean>(xs: T[]): T[];
  /** 조건이 참인 원소를 앞으로 (안정). 참의 개수를 돌려준다 */
  export function partition<T>(xs: T[], f: (x: T, i: number) => boolean): i32;
  /** 씨앗으로 섞는다 (Fisher–Yates · SplitMix64 — 같은 씨앗 = 같은 차례). 씨앗은 ctx.randomInt 로 */
  export function shuffle<T>(xs: T[], seed: i32): T[];
  /** min ~ max 로 자른다 (셋 다 i32 면 i32) */
  export function clamp(x: number, min: number, max: number): number;
  /** a + (b - a) t — t 를 자르지 않는다 */
  export function lerp(a: number, b: number, t: number): number;
  /** (v - a) / (b - a) — a = b 면 0 */
  export function inverseLerp(a: number, b: number, v: number): number;
  /** 3t² - 2t³ (t = x 를 e0 ~ e1 에서 0 ~ 1 로 자른 값) */
  export function smoothstep(e0: number, e1: number, x: number): number;
  /** 6t⁵ - 15t⁴ + 10t³ */
  export function smootherstep(e0: number, e1: number, x: number): number;
  /** t 를 0 ~ length 로 되풀이 */
  export function repeat(t: number, length: number): number;
  /** t 를 0 ~ length 사이에서 왕복 */
  export function pingPong(t: number, length: number): number;
  /** 거의 같다 — |a - b| ≤ max(1e-12, 1e-6 · max(|a|, |b|)) */
  export function approximately(a: number, b: number): boolean;
  export function degToRad(deg: number): number;
  export function radToDeg(rad: number): number;
  /** 2 차원 벡터 함수 — 결과는 새 Vec2 */
  export namespace vec2 {
    function add(a: Vec2, b: Vec2): Vec2;
    function sub(a: Vec2, b: Vec2): Vec2;
    function mul(a: Vec2, s: number): Vec2;
    /** 0 으로 나누면 JS 와 같다 (±Infinity · NaN) */
    function div(a: Vec2, s: number): Vec2;
    function dot(a: Vec2, b: Vec2): number;
    /** 2 차원 외적 (z 성분) */
    function cross(a: Vec2, b: Vec2): number;
    function length(a: Vec2): number;
    function lengthSq(a: Vec2): number;
    function distance(a: Vec2, b: Vec2): number;
    function distanceSq(a: Vec2, b: Vec2): number;
    /** 0 벡터는 (0, 0) */
    function normalize(a: Vec2): Vec2;
    function lerp(a: Vec2, b: Vec2, t: number): Vec2;
    /** atan2(y, x) — 라디안 */
    function angle(a: Vec2): number;
    /** 라디안만큼 돌린다 (반시계) */
    function rotate(a: Vec2, rad: number): Vec2;
    /** v - 2 (v · n) n — n 은 단위 벡터 */
    function reflect(v: Vec2, n: Vec2): Vec2;
  }

  /** 2 차원 값 — vec2 필드의 값 (f32 둘로 저장) */
  export interface Vec2 {
    x: number;
    y: number;
  }
  /** 필드 정의 — field.* 가 만든다 (defineComponent 의 fields 안에서만) */
  export interface Field<T> {
    readonly __sandboxField: T;
  }
  /** 필드 범위 — 기본값 · 쓰기 · 세이브 · Inspector 가 이 안이다 */
  export interface FieldRange {
    min: number;
    max: number;
  }
  /** 필드 종류 7 — 인자는 모두 상수 */
  export namespace field {
    function i32(defaultValue?: i32, range?: FieldRange): Field<i32>;
    /** f32 로 저장 — 읽으면 number, 쓰면 f32 로 반올림 */
    function f32(defaultValue?: number, range?: FieldRange): Field<number>;
    function f64(defaultValue?: number, range?: FieldRange): Field<number>;
    function bool(defaultValue?: boolean): Field<boolean>;
    function vec2(x?: number, y?: number): Field<Vec2>;
    /** 개체 참조 (saveId 로 저장 — 세이브 · 로드 뒤에도 같은 개체), 기본 null */
    function entity(): Field<Entity | null>;
    /** enum 필드 — 인자 = 기본 멤버 (E.A) */
    function enumOf<E>(defaultValue: E): Field<E>;
  }
  /** defineComponent 의 정의 — 최상위 const 의 초기값으로만 */
  export interface ComponentDefinition<F> {
    /** "이름공간.이름" (엔진 이름공간 core · game · … 은 안 된다) */
    id: string;
    /** 1 ~ 65535 (기본 1) — 모양을 바꾸면 올린다 */
    version?: i32;
    fields: F;
    /** 세이브 · WorldHash (기본 true) */
    persistent?: boolean;
    /** 클라이언트로 복제 (기본 true, serverOnly 면 false) */
    replicated?: boolean;
    /** Inspector (기본 true) */
    editorVisible?: boolean;
    /** 서버에만 (기본 false) */
    serverOnly?: boolean;
  }
  /** 컴포넌트 참조 — ctx.read · ctx.query 의 인자 (v0.3 M3-T3) */
  export interface ComponentRef<T> {
    readonly __sandboxComponent: T;
  }
  /** 스크립트 컴포넌트 — 필드는 이름 순으로 저장된다 (선언 순서를 바꿔도 같은 컴포넌트) */
  export function defineComponent<F extends { [name: string]: Field<unknown> }>(
    definition: ComponentDefinition<F>,
  ): ComponentRef<{ [K in keyof F]: F[K] extends Field<infer T> ? T : never }>;
  /** 충돌체 (서버만 — 복제하지 않는다) — 엔진 컴포넌트 core.collider (읽기 server · 쓰기 none) */
  export interface Collider {
    /** 충돌 반지름 */
    readonly radius: number;
  }
  export const Collider: ComponentRef<Collider>;
  /** 이동 (속도 상한 · 목표 — 서버만, 복제하지 않는다) — 엔진 컴포넌트 core.movement (읽기 server · 쓰기 server) */
  export interface Movement {
    /** 월드 단위/초² */
    accel: number;
    /** 도착으로 보는 거리 */
    arriveRadius: number;
    /** 목표 (월드 좌표) */
    goal: Vec2;
    /** 목표가 있나 */
    hasGoal: boolean;
    /** 월드 단위/초 */
    maxSpeed: number;
  }
  export const Movement: ComponentRef<Movement>;
  /** 위치 · 회전 — 엔진 컴포넌트 core.transform (읽기 server · client · 쓰기 server) */
  export interface Transform {
    /** 월드 좌표 */
    position: Vec2;
    /** 라디안 */
    rotation: number;
  }
  export const Transform: ComponentRef<Transform>;
  /** 속도 — 엔진 컴포넌트 core.velocity (읽기 server · client · 쓰기 server) */
  export interface Velocity {
    /** 월드 단위/초 */
    value: Vec2;
  }
  export const Velocity: ComponentRef<Velocity>;
  /** 주인 플레이어 (v0.5 M2-T5) — 엔진 컴포넌트 game.owner (읽기 server · client · 쓰기 server) */
  export interface Owner {
    /** 주인 플레이어 (명령의 issuer — 0 = 없음) */
    player: i32;
  }
  export const Owner: ComponentRef<Owner>;
  /** 게임 세션 (상태 · 결과 — 바꾸는 길은 GameControl 명령) — 엔진 컴포넌트 game.session (읽기 server · client · 쓰기 none) */
  export interface GameSession {
    /** GameOutcome (0 진행 · 1 승 · 2 패 · 3 비김) */
    readonly result: i32;
    /** 끝낸 규칙의 번호 */
    readonly rule: i32;
    /** GameState (0 Lobby · 1 Running · 2 Paused · 3 Ended) */
    readonly state: i32;
    /** 지금 상태가 된 틱 */
    readonly stateTick: i32;
    /** 이긴 팀 (끝났을 때) */
    readonly team: i32;
  }
  export const GameSession: ComponentRef<GameSession>;
  /** 에너지 — 엔진 컴포넌트 life.energy (읽기 server · client · 쓰기 server) */
  export interface Energy {
    /** 초당 줄어드는 양 */
    drainPerSecond: number;
    /** 최대 */
    max: number;
    /** 지금 (0 이하면 굶어 죽는다) */
    value: number;
  }
  export const Energy: ComponentRef<Energy>;
  /** 체력 — 엔진 컴포넌트 life.health (읽기 server · client · 쓰기 server) */
  export interface Health {
    /** 최대 */
    max: number;
    /** 지금 (0 이하면 죽는다) */
    value: number;
  }
  export const Health: ComponentRef<Health>;
  /** 시스템 이벤트 종류 (onEvent 의 e.kind) */
  export const enum GameEventKind {
    Death = 1,
    Spawn = 2,
    AreaEnter = 3,
    Score = 4,
  }
  /** onEvent 의 이벤트 (단계 17d — 그 틱에 일어난 일) */
  export interface GameEvent {
    /** spawn · areaEnter 의 개체 (death · score 는 null — 죽은 개체는 없다) */
    readonly entity: Entity | null;
    /** 이벤트 종류 */
    readonly kind: GameEventKind;
    /** score: 틱 처음의 점수 */
    readonly previous: i32;
    /** score: 점수 칸 (0 = 공용, 팀 번호 + 1) */
    readonly team: i32;
    /** score: 지금 점수, death: 죽은 까닭 (1 굶음 · 2 늙음 · 3 죽임 · 4 수명) */
    readonly value: i32;
    /** 위치 x (death 는 틱 처음의 위치, 그 틱에 생겨 죽었으면 0) */
    readonly x: number;
    /** 위치 y */
    readonly y: number;
  }
  /** 태그 거르기 — 콘텐츠 태그 이름 (Rule · 트리거와 같은 식) */
  export interface TagFilter {
    all?: string[];
    any?: string[];
    none?: string[];
  }
  /** 구독 — 종류마다 하나 (값은 상수) */
  export interface SystemEvents {
    /** 그 틱에 죽은 개체 (태그는 틱 처음의 것) */
    death?: { tags?: TagFilter };
    /** 그 틱에 생겨 살아 있는 개체 */
    spawn?: { tags?: TagFilter };
    /** 틱 처음에 사각형 밖 (또는 없었다), 지금 안 */
    areaEnter?: { min: [number, number]; max: [number, number]; tags?: TagFilter };
    /** 점수 칸이 바뀌었다 (게임 세션이 있을 때) */
    score?: {};
  }
  /** defineSystem 의 정의 — export default defineSystem({ … }) 으로만 (파일 하나에 하나) */
  export interface SystemDefinition {
    /** "이름공간.이름" — 팩들 사이에 하나. 실행 순서 = (팩 순서, id) */
    id: string;
    /** "server" (기본 — 서버 시뮬레이션) 또는 "client" (클라이언트 화면 — onUpdate 만, ADR-0067) */
    domain?: "server" | "client";
    /** onTick 의 주기 — 틱 번호 % n == 0 인 틱 (1 ~ 65535, 기본 1) */
    intervalTicks?: i32;
    /** 한 틱에 받는 onEvent 수 (1 ~ 4096, 기본 256 — 넘으면 건너뛴다) */
    eventsPerTick?: i32;
    events?: SystemEvents;
    /** 단계 16b — Native System 뒤, 구조 변경 · 게임 규칙 · 트리거 앞. 쓰기는 호출이 끝나면 적용 */
    onTick?(ctx: ServerContext): void;
    /** 단계 17d — 게임이 Running 이 된 틱 (세션이 없으면 틱 1) */
    onStart?(ctx: ServerContext): void;
    /** 단계 17d — 게임이 Finished 가 된 틱 (세션이 없으면 없다) */
    onStop?(ctx: ServerContext): void;
    /** 단계 17d — events 의 구독마다, 그 틱에 일어난 차례로 */
    onEvent?(ctx: ServerContext, e: GameEvent): void;
    /** client 시스템만 — 클라이언트의 프레임마다 (복제 월드를 읽고 화면에 표시, 시뮬레이션과 무관) */
    onUpdate?(ctx: ClientContext): void;
  }
  /** 스크립트 시스템 (서버 · 클라이언트) */
  export function defineSystem(definition: SystemDefinition): SystemDefinition;

  /** 개체 핸들 — 같음 비교만, 틱을 넘겨 두려면 EntityRef 필드에 */
  export interface Entity {
    readonly __sandboxOpaque: "Entity";
  }

  /** 서버 콜백의 ctx — 호출 안에서만 유효 */
  export interface ServerContext {
    readonly __sandboxOpaque: "ServerContext";
    /** 지금 시뮬레이션 틱 (30 TPS) — ctx.tick */
    readonly tick: i32;
    /** 서버 로그에 한 줄 (스크립트 이름과 틱이 붙는다) — ctx.log */
    log(message: string): void;
    /** 컴포넌트 값의 복사 (없으면 null) — 이 호출에서 쓴 값이 보인다 — ctx.read */
    read<T>(entity: Entity, component: ComponentRef<T>): T | null;
    /** 개체에 컴포넌트가 있나 — ctx.has */
    has<T>(entity: Entity, component: ComponentRef<T>): boolean;
    /** 컴포넌트를 가진 개체 (saveId 오름차순 — 개체마다 연료 1) — ctx.query */
    query<T>(component: ComponentRef<T>): Entity[];
    /** 컴포넌트 값을 쓴다 (호출이 끝나면 적용, 트랩이면 버림 — 범위 밖 · 없는 컴포넌트는 트랩) — ctx.write */
    write<T>(entity: Entity, component: ComponentRef<T>, value: T): void;
    /** Prefab 으로 새 개체 (이 틱 끝에 생긴다 — 이 호출에서는 핸들만: 읽기 null · add · destroy 가 된다). 없는 Prefab · 경계 밖 · 상한이면 null — ctx.spawn */
    spawn(prefab: string, x: number, y: number): Entity | null;
    /** 개체를 지운다 (이 틱 끝에) — ctx.destroy */
    destroy(entity: Entity): void;
    /** 스크립트 컴포넌트를 붙인다 (이 틱 끝에 — 이미 있으면 버린다, 엔진 컴포넌트는 [계획]) — ctx.add */
    add<T>(entity: Entity, component: ComponentRef<T>, value: T): void;
    /** 스크립트 컴포넌트를 뗀다 (이 틱 끝에) — ctx.remove */
    remove<T>(entity: Entity, component: ComponentRef<T>): void;
    /** 시뮬레이션 시간 (초 = tick / 30) — ctx.time */
    readonly time: number;
    /** 0 이상 1 미만 — 시스템마다 흐름 (같은 월드 · 같은 틱 = 같은 수열, 시스템 차례와 무관) — ctx.random */
    random(): number;
    /** lo 이상 hi 이하의 정수 (lo > hi 면 HostError) — 흐름은 ctx.random 과 같다 — ctx.randomInt */
    randomInt(lo: i32, hi: i32): i32;
    /** (x, y) 에서 반경 안의 개체 — 틱 처음 (단계 4) 의 위치, saveId 오름차순, 개체마다 연료 1, 1,024 개 · 반경 128 까지 — ctx.near */
    near(x: number, y: number, radius: number): Entity[];
    /** ctx.near 중 컴포넌트를 가진 개체 — ctx.nearWith */
    nearWith<T>(x: number, y: number, radius: number, component: ComponentRef<T>): Entity[];
    /** 반경 안에서 가장 가까운 개체 (거리가 같으면 saveId 작은 것) — ctx.near 와 같은 색인 · 반경 상한, 없으면 null — ctx.nearest */
    nearest(x: number, y: number, radius: number): Entity | null;
    /** 반경 안의 가까운 개체 k 개 (가까운 차례, 같은 거리 = saveId 순) — k 는 0 ~ 1,024 — ctx.kNearest */
    kNearest(x: number, y: number, radius: number, k: i32): Entity[];
    /** 사각형 (경계 포함) 안의 개체 — saveId 순, 한 변 256 까지 · 1,024 개까지 — ctx.inBox */
    inBox(minX: number, minY: number, maxX: number, maxY: number): Entity[];
    /** 개체의 경로를 요청한다 (ai.path — 호출 끝에 반영, 다음 틱 단계 8 에 제출 · 그다음 틱 단계 5 에 결과, 틱 예산을 넘으면 기다린다). ai.path 가 없으면 false — ctx.requestPath */
    requestPath(entity: Entity, x: number, y: number): boolean;
    /** 경로 상태 — 0 없음 · 1 요청됨 · 2 제출됨 · 3 따라가는 중 · 4 길 없음, ai.path 가 없으면 -1 — ctx.pathState */
    pathState(entity: Entity): i32;
    /** 경유점 [x0, y0, x1, y1, …] (새 배열 — 경로가 없으면 빈 배열) — ctx.pathPoints */
    pathPoints(entity: Entity): number[];
    /** 경로를 지운다 (상태 0 — 호출 끝에 반영) — ctx.cancelPath */
    cancelPath(entity: Entity): void;
  }

  /** 클라이언트 콜백 (onUpdate) 의 ctx — 호출 안에서만 유효, 읽기 · 화면 표시만 */
  export interface ClientContext {
    readonly __sandboxOpaque: "ClientContext";
    /** 복제 월드의 서버 틱 (마지막으로 받은 스냅숏) — client.tick */
    readonly tick: i32;
    /** 이 접속에서 몇 번째 onUpdate 인가 (1 부터) — client.frame */
    readonly frame: i32;
    /** 앞 onUpdate 뒤 지난 실제 시간 (초 — 표시용, 시뮬레이션과 무관) — client.dt */
    readonly dt: number;
    /** 복제 월드의 컴포넌트 값 복사 (없으면 null) — 개체 참조 필드는 null [계획] — client.read */
    read<T>(entity: Entity, component: ComponentRef<T>): T | null;
    /** 개체에 컴포넌트가 있나 (복제 월드) — client.has */
    has<T>(entity: Entity, component: ComponentRef<T>): boolean;
    /** 컴포넌트를 가진 복제 개체 (netId 오름차순 — 개체마다 연료 1) — client.query */
    query<T>(component: ComponentRef<T>): Entity[];
    /** 이번 프레임 개체 위에 글자 (한 줄, 256 바이트까지) — client.label */
    label(entity: Entity, text: string): void;
    /** 이번 프레임 화면 왼쪽 위 "스크립트" 칸에 한 줄 (256 바이트까지) — client.print */
    print(text: string): void;
  }
}
