// 보석 동굴 — 클라이언트 (v0.12 통합 시험 NG-20). 방향 키 · 패드 십자 → 신호 dun.move, 보석을 주우면 주인공이 기뻐한다
// (애니메이터 상태 cheer — 끝나면 idle 로, presentation/animations.json)
import { defineSystem, GameEventKind } from "sandbox"

export default defineSystem({
  id: "dun.fx",
  domain: "client",
  events: { triggerEnter: { tags: { all: ["hero"] } } },
  onAction(ctx, a) {
    if (!a.pressed) {
      return
    }
    if (a.action === "right") { ctx.send("dun.move", 0) }
    else if (a.action === "up") { ctx.send("dun.move", 1) }
    else if (a.action === "left") { ctx.send("dun.move", 2) }
    else if (a.action === "down") { ctx.send("dun.move", 3) }
    else if (a.action === "stop") { ctx.send("dun.move", 4) }
  },
  onEvent(ctx, e) {
    const hero = e.entity // 좁히기는 지역 변수에서 (e.entity !== null 은 필드라 좁혀지지 않는다)
    if (e.kind === GameEventKind.TriggerEnter && hero !== null) {
      ctx.playAnimation(hero, "cheer")
    }
  },
})
