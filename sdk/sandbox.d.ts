// SandboxScript SDK — 생성된 파일 (sbx sdk). 손으로 고치지 않는다 — script/api 의 표를 고치고 다시 만든다.
// Host API 1 · 지문 0388e85856c3e9f3 — docs/19-SANDBOXSCRIPT.md
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
}
