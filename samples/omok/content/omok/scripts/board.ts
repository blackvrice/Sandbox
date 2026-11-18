// 오목의 판 — 돌 (복제 — 클라이언트 HUD · 그리기가 읽는다) · 판 상태 (월드 상태 — 서버만, 세이브 · 해시).
// 칸 번호 0 ~ 224 = 왼쪽 위부터 줄마다 (열 = 칸 % 15, 행 = 칸 / 15). 칸의 월드 자리 = (열 − 7, 7 − 행) — 가운데 칸 = 원점
import { defineComponent, defineState, field, i32, idiv } from "sandbox"

export const kSize: i32 = 15

export const Stone = defineComponent({
  id: "omok.stone",
  fields: {
    cell: field.i32(0, { min: 0, max: 224 }),
    color: field.i32(1, { min: 1, max: 2 }), // 1 = 흑 · 2 = 백
    order: field.i32(0, { min: 0, max: 225 }), // 몇 번째 수 (마지막 수 표시)
  },
})

// 판의 결과 (승리 표시 개체에 — 복제, HUD 가 읽는다)
export const Result = defineComponent({
  id: "omok.result",
  fields: {
    winner: field.i32(0, { min: 0, max: 2 }), // 0 = 무승부
    moves: field.i32(0, { min: 0, max: 225 }),
  },
})

export const Game = defineState({
  id: "omok.game",
  fields: {
    turn: field.i32(1, { min: 1, max: 2 }), // 둘 차례
    moves: field.i32(0, { min: 0, max: 225 }),
    over: field.bool(false),
    winner: field.i32(0, { min: 0, max: 2 }), // 0 = 아직 · 무승부
  },
})

export function cellX(cell: i32): number {
  return (cell % kSize) - 7
}

export function cellY(cell: i32): number {
  return 7 - idiv(cell, kSize)
}
