// 클라이언트 HUD — 복제된 돌을 읽어 판 버튼의 글자 (UI 변수 ttt.c0 ~ ttt.c8) 를 채운다
import { defineSystem, i32 } from "sandbox"
import { Stone } from "./board"

export default defineSystem({
  id: "ttt.hud",
  domain: "client",
  onUpdate(ctx) {
    const marks: string[] = ["", "", "", "", "", "", "", "", ""]
    for (const e of ctx.query(Stone)) {
      const s = ctx.read(e, Stone)
      if (s !== null) {
        marks[s.cell] = s.mark === 1 ? "X" : "O"
      }
    }
    for (let c: i32 = 0; c < 9; c = c + 1) {
      ctx.setUiText(`ttt.c${c}`, marks[c])
    }
  },
})
