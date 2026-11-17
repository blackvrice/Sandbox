// 구슬 모으기 — 클라이언트 (v0.11 통합 시험 NG-19). 방향 키 → 신호 orb.push, 동전 · 벽 이벤트 → 소리 (팩 sounds.json),
// onRender 로 구슬 둘레의 원 (그리기 명령 — 화면에만)
import { defineSystem, GameEventKind, Transform, Velocity } from "sandbox"

export default defineSystem({
  id: "orb.fx",
  domain: "client",
  events: { triggerEnter: { tags: { all: ["ball"] } }, collisionEnter: { tags: { all: ["ball"] } } },
  onAction(ctx, a) {
    if (!a.pressed) {
      return
    }
    if (a.action === "right") { ctx.send("orb.push", 0) }
    else if (a.action === "up") { ctx.send("orb.push", 1) }
    else if (a.action === "left") { ctx.send("orb.push", 2) }
    else if (a.action === "down") { ctx.send("orb.push", 3) }
    else if (a.action === "stop") { ctx.send("orb.push", 4) }
  },
  onEvent(ctx, e) {
    ctx.playSound(e.kind === GameEventKind.TriggerEnter ? "coin" : "bump")
  },
  onRender(ctx) {
    ctx.drawColor(0xF0C83CFF)
    for (const e of ctx.query(Velocity)) {
      const t = ctx.read(e, Transform)
      if (t !== null) {
        ctx.drawCircle(t.position.x, t.position.y, 0.6)
      }
    }
  },
})
