// 예제 B — RTS 유닛 AI (ctx.kNearest · PriorityQueue · vec2 · 경로 요청, docs/19-SANDBOXSCRIPT.md 25.4 · 25.5 · 25.7)
// 1 초마다 병사마다 가까운 적 16 명 중 (거리 + 체력/10) 이 가장 작은 적을 골라 그쪽으로 경로를 요청한다.
// 경로는 엔진이 계산한다 (ai.path — 다음 틱 제출, 그다음 틱 결과, 틱 예산). 같은 점수는 먼저 넣은 적 (kNearest 의 차례).
import { defineSystem, Entity, i32, PriorityQueue, Transform, vec2 } from "sandbox"
import { Soldier } from "./soldier"

export default defineSystem({
  id: "tour.rts",
  intervalTicks: 30,
  onTick(ctx) {
    let requested: i32 = 0
    let idle: i32 = 0
    for (const e of ctx.query(Soldier)) {
      const me = ctx.read(e, Soldier)
      const at = ctx.read(e, Transform)
      if (me === null || at === null) {
        continue
      }
      const targets = new PriorityQueue<Entity>()
      for (const o of ctx.kNearest(at.position.x, at.position.y, 40, 16)) {
        const other = ctx.read(o, Soldier)
        const ot = ctx.read(o, Transform)
        if (other === null || ot === null || other.team === me.team) {
          continue
        }
        targets.push(o, vec2.distance(at.position, ot.position) + other.hp / 10)
      }
      const target = targets.pop()
      if (target === undefined) {
        idle++
        continue
      }
      const tt = ctx.read(target, Transform)
      const state = ctx.pathState(e)
      // 요청 · 제출 중이면 기다린다 (1 · 2)
      if (tt !== null && state !== 1 && state !== 2 && ctx.requestPath(e, tt.position.x, tt.position.y)) {
        requested++
      }
    }
    ctx.log(`RTS t${ctx.tick}: 경로 요청 ${requested} · 적 없음 ${idle}`)
  },
})
