// 서버 시스템: 3 초 (90 틱) 마다 아직 차지 않은 보관함에 둘씩 더 넣는다 — 설치한 inventory 패키지의 컴포넌트 · 함수를 쓴다
// (팩 사이 import "@inventory/bag", v0.4 M4-T1)
import { defineSystem, i32 } from "sandbox"
import { Bag, store, isFull } from "@inventory/bag"

export default defineSystem({
  id: "stash.bonus",
  intervalTicks: 90,
  onTick(ctx) {
    let total: i32 = 0
    for (const e of ctx.query(Bag)) {
      const b = ctx.read(e, Bag)
      if (b !== null) {
        if (!isFull(b.items, b.capacity)) {
          b.items = store(b.items, b.capacity, 2)
          ctx.write(e, Bag, b)
        }
        total = total + b.items
      }
    }
    if (ctx.tick % 450 === 0) {
      ctx.log(`아이템 합 ${total}`)
    }
  },
})
