// 틱택토의 판 — 돌 (복제 — 클라이언트 HUD 가 읽는다) · 판 상태 (월드 상태 — 서버만, 세이브 · 해시).
// 칸 번호 0 ~ 8 = 왼쪽 위부터 줄마다. 비트 c = 칸 c
import { defineComponent, defineState, field } from "sandbox"

export const Stone = defineComponent({
  id: "ttt.stone",
  fields: {
    cell: field.i32(0, { min: 0, max: 8 }),
    mark: field.i32(1, { min: 1, max: 2 }), // 1 = X (사람) · 2 = O (스크립트)
  },
})

export const Board = defineState({
  id: "ttt.board",
  fields: {
    x: field.i32(0, { min: 0, max: 511 }),
    o: field.i32(0, { min: 0, max: 511 }),
    over: field.bool(false),
  },
})
