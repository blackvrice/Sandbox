// SandboxScript SDK — 생성된 파일 (sbx sdk). 손으로 고치지 않는다 — script/api 의 표를 고치고 다시 만든다.
// Host API 1 · 지문 d03bc50223a4ef73 — docs/19-SANDBOXSCRIPT.md
declare module "sandbox" {
  /** 32 비트 정수 — 산술은 감싼다 (2147483647 + 1 = -2147483648). TS 에서는 number 와 같다 */
  export type i32 = number;
  /** JS ToInt32 — NaN · ±Infinity → 0, 0 쪽으로 자르고 2^32 로 감싼다 */
  export function toI32(x: number): i32;
  /** 정수 나눗셈 (0 쪽으로 자름) — b 가 0 이면 트랩 */
  export function idiv(a: i32, b: i32): i32;

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
  /** 충돌체 (서버만 — 복제하지 않는다) — 엔진 컴포넌트 core.collider (읽기 server) */
  export interface Collider {
    /** 충돌 반지름 */
    readonly radius: number;
  }
  export const Collider: ComponentRef<Collider>;
  /** 이동 (속도 상한 · 목표 — 서버만, 복제하지 않는다) — 엔진 컴포넌트 core.movement (읽기 server) */
  export interface Movement {
    /** 월드 단위/초² */
    readonly accel: number;
    /** 도착으로 보는 거리 */
    readonly arriveRadius: number;
    /** 목표 (월드 좌표) */
    readonly goal: Vec2;
    /** 목표가 있나 */
    readonly hasGoal: boolean;
    /** 월드 단위/초 */
    readonly maxSpeed: number;
  }
  export const Movement: ComponentRef<Movement>;
  /** 위치 · 회전 — 엔진 컴포넌트 core.transform (읽기 server · client) */
  export interface Transform {
    /** 월드 좌표 */
    readonly position: Vec2;
    /** 라디안 */
    readonly rotation: number;
  }
  export const Transform: ComponentRef<Transform>;
  /** 속도 — 엔진 컴포넌트 core.velocity (읽기 server · client) */
  export interface Velocity {
    /** 월드 단위/초 */
    readonly value: Vec2;
  }
  export const Velocity: ComponentRef<Velocity>;
  /** 게임 세션 (상태 · 결과 — 바꾸는 길은 GameControl 명령) — 엔진 컴포넌트 game.session (읽기 server · client) */
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
  /** 에너지 — 엔진 컴포넌트 life.energy (읽기 server · client) */
  export interface Energy {
    /** 초당 줄어드는 양 */
    readonly drainPerSecond: number;
    /** 최대 */
    readonly max: number;
    /** 지금 (0 이하면 굶어 죽는다) */
    readonly value: number;
  }
  export const Energy: ComponentRef<Energy>;
  /** 체력 — 엔진 컴포넌트 life.health (읽기 server · client) */
  export interface Health {
    /** 최대 */
    readonly max: number;
    /** 지금 (0 이하면 죽는다) */
    readonly value: number;
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
