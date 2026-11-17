// 서버 규칙 — 신호 ttt.place (칸) 로 X 를 두고, 스크립트가 O 를 둔다 (이길 수 있으면 이기고 · 막고 · 가운데 · 모서리 · 변).
// 줄이 되면 승패 표시 개체 (ttt.victory · ttt.defeat · ttt.draw) 를 만들고, 게임 모드 ttt.match 의 끝 조건이 그것을 센다
import { defineSystem, GameEventKind, ServerContext, i32, idiv } from "sandbox"
import { Board, Stone } from "./board"

function lines(): i32[] {
  return [7, 56, 448, 73, 146, 292, 273, 84] // 가로 셋 · 세로 셋 · 대각선 둘
}

function wins(mask: i32): boolean {
  for (const l of lines()) {
    if ((mask & l) === l) {
      return true
    }
  }
  return false
}

// mine 에 한 칸만 더하면 줄이 되는 빈 칸 (줄 차례 · 칸 차례), 없으면 -1
function finishing(mine: i32, taken: i32): i32 {
  for (const l of lines()) {
    for (let c: i32 = 0; c < 9; c = c + 1) {
      const bit: i32 = 1 << c
      if ((l & bit) !== 0 && (taken & bit) === 0 && ((mine | bit) & l) === l) {
        return c
      }
    }
  }
  return -1
}

function aiMove(x: i32, o: i32): i32 {
  const taken: i32 = x | o
  const win = finishing(o, taken)
  if (win >= 0) {
    return win
  }
  const block = finishing(x, taken)
  if (block >= 0) {
    return block
  }
  const order: i32[] = [4, 0, 2, 6, 8, 1, 3, 5, 7] // 가운데 · 모서리 · 변
  for (const c of order) {
    if ((taken & (1 << c)) === 0) {
      return c
    }
  }
  return -1
}

// 돌 하나 — 칸의 월드 자리 (가운데 칸 = 원점, 칸 사이 2)
function place(ctx: ServerContext, cell: i32, mark: i32): void {
  const e = ctx.spawn(mark === 1 ? "ttt.x" : "ttt.o", ((cell % 3) - 1) * 2, (1 - idiv(cell, 3)) * 2)
  if (e !== null) {
    ctx.add(e, Stone, { cell: cell, mark: mark })
  }
}

function finish(ctx: ServerContext, prefab: string): void {
  const marker = ctx.spawn(prefab, 0, 0)
  if (marker === null) {
    ctx.log(`${prefab} 을 만들지 못했다`)
  }
}

export default defineSystem({
  id: "ttt.rules",
  events: { signal: {} },
  onEvent(ctx, e) {
    if (e.kind !== GameEventKind.Signal || e.signal !== "ttt.place") {
      return
    }
    const b = ctx.state(Board)
    const bit: i32 = 1 << e.value
    if (b.over || ((b.x | b.o) & bit) !== 0) {
      ctx.log(`칸 ${e.value} 에는 둘 수 없다`)
      return
    }
    b.x = b.x | bit
    place(ctx, e.value, 1)
    if (wins(b.x)) {
      b.over = true
      finish(ctx, "ttt.victory")
    } else if ((b.x | b.o) === 511) {
      b.over = true
      finish(ctx, "ttt.draw")
    } else {
      const c = aiMove(b.x, b.o)
      b.o = b.o | (1 << c)
      place(ctx, c, 2)
      if (wins(b.o)) {
        b.over = true
        finish(ctx, "ttt.defeat")
      } else if ((b.x | b.o) === 511) {
        b.over = true
        finish(ctx, "ttt.draw")
      }
    }
    ctx.setState(Board, b)
  },
})
