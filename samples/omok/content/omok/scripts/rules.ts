// 서버 규칙 — 신호 omok.place (칸 0 ~ 224) 로 지금 차례의 돌을 둔다 (빈 칸 · 끝나지 않은 판만). 둔 돌에서 네 방향 (가로 · 세로 ·
// 두 대각선) 으로 같은 색이 다섯 이상 이어지면 그 색의 승리 — 승리 표시 개체 omok.winner 를 만들고 게임 모드 omok.match 가 센다.
// 판이 가득 차면 무승부 (omok.draw). 판은 복제되는 돌 개체에서 다시 읽는다 (월드 상태 필드는 배열이 없다)
import { defineSystem, GameEventKind, ServerContext, i32, idiv } from "sandbox"
import { Game, Result, Stone, cellX, cellY, kSize } from "./board"

// 칸마다 돌 색 (0 = 빈 칸)
function readBoard(ctx: ServerContext): i32[] {
  const board: i32[] = []
  for (let i: i32 = 0; i < kSize * kSize; i = i + 1) {
    board.push(0)
  }
  for (const e of ctx.query(Stone)) {
    const s = ctx.read(e, Stone)
    if (s !== null) {
      board[s.cell] = s.color
    }
  }
  return board
}

// (dc, dr) 방향으로 같은 색이 몇 개 이어지나 (자기 칸 빼고)
function run(board: i32[], cell: i32, color: i32, dc: i32, dr: i32): i32 {
  let c: i32 = (cell % kSize) + dc
  let r: i32 = idiv(cell, kSize) + dr
  let n: i32 = 0
  while (c >= 0 && c < kSize && r >= 0 && r < kSize && board[r * kSize + c] === color) {
    n = n + 1
    c = c + dc
    r = r + dr
  }
  return n
}

function fiveFrom(board: i32[], cell: i32, color: i32): boolean {
  const dirs: i32[] = [1, 0, 0, 1, 1, 1, 1, -1] // 가로 · 세로 · ↘ · ↗
  for (let k: i32 = 0; k < 8; k = k + 2) {
    const dc = dirs[k]
    const dr = dirs[k + 1]
    if (1 + run(board, cell, color, dc, dr) + run(board, cell, color, -dc, -dr) >= 5) {
      return true
    }
  }
  return false
}

function place(ctx: ServerContext, cell: i32, color: i32, order: i32): void {
  const e = ctx.spawn(color === 1 ? "omok.black" : "omok.white", cellX(cell), cellY(cell))
  if (e !== null) {
    ctx.add(e, Stone, { cell: cell, color: color, order: order })
  }
}

export default defineSystem({
  id: "omok.rules",
  events: { signal: {} },
  onEvent(ctx, e) {
    if (e.kind !== GameEventKind.Signal || e.signal !== "omok.place") {
      return
    }
    const g = ctx.state(Game)
    if (g.over) {
      return
    }
    const board = readBoard(ctx)
    if (board[e.value] !== 0) {
      ctx.log(`칸 ${e.value} 에는 이미 돌이 있다`)
      return
    }
    board[e.value] = g.turn
    g.moves = g.moves + 1
    place(ctx, e.value, g.turn, g.moves)
    if (fiveFrom(board, e.value, g.turn)) {
      g.over = true
      g.winner = g.turn
      const marker = ctx.spawn("omok.winner", 0, 0)
      if (marker !== null) {
        ctx.add(marker, Result, { winner: g.turn, moves: g.moves })
      }
      ctx.log(`${g.turn === 1 ? "흑" : "백"} 승리 — ${g.moves} 수`)
    } else if (g.moves === kSize * kSize) {
      g.over = true
      const marker = ctx.spawn("omok.draw", 0, 0)
      if (marker !== null) {
        ctx.add(marker, Result, { winner: 0, moves: g.moves })
      }
    } else {
      g.turn = 3 - g.turn
    }
    ctx.setState(Game, g)
  },
})
