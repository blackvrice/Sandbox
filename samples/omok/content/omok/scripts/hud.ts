// 클라이언트 — 칸 클릭 (게임 키 play.place = 마우스 왼쪽) → 신호 omok.place, HUD 글자 (차례 · 결과), 판의 줄 · 화점 · 마지막 수 표시
// (그리기 — 화면에만). 차례 · 결과는 복제된 돌에서 센다 (월드 상태는 서버만)
import { defineSystem, i32, toI32 } from "sandbox"
import { Result, Stone, cellX, cellY, kSize } from "./board"

export default defineSystem({
  id: "omok.hud",
  domain: "client",
  onAction(ctx, a) {
    if (a.action !== "place" || !a.pressed) {
      return
    }
    const col: i32 = toI32(Math.round(a.x)) + 7
    const row: i32 = 7 - toI32(Math.round(a.y))
    if (col >= 0 && col < kSize && row >= 0 && row < kSize) {
      ctx.send("omok.place", row * kSize + col)
    }
  },
  onUpdate(ctx) {
    let blacks: i32 = 0
    let whites: i32 = 0
    for (const e of ctx.query(Stone)) {
      const s = ctx.read(e, Stone)
      if (s !== null) {
        if (s.color === 1) { blacks = blacks + 1 } else { whites = whites + 1 }
      }
    }
    let result: string = ""
    for (const e of ctx.query(Result)) {
      const r = ctx.read(e, Result)
      if (r !== null) {
        result = r.winner === 0 ? `무승부 — ${r.moves} 수` : `${r.winner === 1 ? "흑" : "백"} 승리! — ${r.moves} 수`
      }
    }
    ctx.setUiText("omok.turn", result !== "" ? result : blacks > whites ? "백 차례" : "흑 차례")
    ctx.setUiText("omok.count", `흑 ${blacks} · 백 ${whites}`)
  },
  onRender(ctx) {
    ctx.drawColor(0x5A3C1EFF)
    for (let i: i32 = 0; i < kSize; i = i + 1) {
      ctx.drawLine(-7, i - 7, 7, i - 7)
      ctx.drawLine(i - 7, -7, i - 7, 7)
    }
    const stars: i32[] = [-4, 0, 4]
    for (const x of stars) {
      for (const y of stars) {
        ctx.drawCircle(x, y, 0.08)
      }
    }
    // 마지막 수
    let last: i32 = -1
    let lastOrder: i32 = 0
    for (const e of ctx.query(Stone)) {
      const s = ctx.read(e, Stone)
      if (s !== null && s.order > lastOrder) {
        lastOrder = s.order
        last = s.cell
      }
    }
    if (last >= 0) {
      ctx.drawColor(0xE03C3CFF)
      ctx.drawCircle(cellX(last), cellY(last), 0.2)
    }
  },
})
