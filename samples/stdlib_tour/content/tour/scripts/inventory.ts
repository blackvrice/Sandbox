// 예제 A — 인벤토리 (Map · Set · Array · 알고리즘, docs/19-SANDBOXSCRIPT.md 25.2 · 25.3 · 25.5)
// 2 초마다 월드의 물건을 종류별로 센다. Map 의 반복은 넣은 차례 (결정적), 정렬 · 겹침 빼기는 표준 라이브러리로.
import { defineSystem, i32, sortNumbers, unique, sum } from "sandbox"
import { Item, Kind, kindName } from "./items"

export default defineSystem({
  id: "tour.inventory",
  intervalTicks: 60,
  onTick(ctx) {
    const counts = new Map<Kind, i32>()
    const big = new Set<Kind>() // 한 더미가 5 개 이상인 종류
    const stacks: i32[] = []
    for (const e of ctx.query(Item)) {
      const it = ctx.read(e, Item)
      if (it === null) {
        continue
      }
      const c = counts.get(it.kind)
      counts.set(it.kind, (c !== undefined ? c : 0) + it.count)
      if (it.count >= 5) {
        big.add(it.kind)
      }
      stacks.push(it.count)
    }
    const lines: string[] = []
    for (const k of counts.keys()) {
      const c = counts.get(k)
      lines.push(`${kindName(k)} ${c !== undefined ? c : 0}`)
    }
    lines.sort()
    sortNumbers(stacks)
    ctx.log(
      `가방 t${ctx.tick}: ${lines.join(" · ")} | 큰 더미 종류 ${big.size} · 더미 크기 ${unique(stacks).join(",")} · 합 ${sum(stacks)}`,
    )
  },
})
