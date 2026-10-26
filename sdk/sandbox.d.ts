// SandboxScript SDK — 생성된 파일 (sbx sdk). 손으로 고치지 않는다 — script/api 의 표를 고치고 다시 만든다.
// Host API 1 · 지문 8fe70a7792b22639 — docs/19-SANDBOXSCRIPT.md
declare module "sandbox" {
  /** 32 비트 정수 — 산술은 감싼다 (2147483647 + 1 = -2147483648). TS 에서는 number 와 같다 */
  export type i32 = number;
  /** JS ToInt32 — NaN · ±Infinity → 0, 0 쪽으로 자르고 2^32 로 감싼다 */
  export function toI32(x: number): i32;
  /** 정수 나눗셈 (0 쪽으로 자름) — b 가 0 이면 트랩 */
  export function idiv(a: i32, b: i32): i32;

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
  }
}
