// 병사 — RTS 유닛 AI 예제의 컴포넌트
import { defineComponent, field } from "sandbox"

export const Soldier = defineComponent({
  id: "tour.soldier",
  fields: {
    team: field.i32(0, { min: 0, max: 1 }),
    hp: field.i32(10, { min: 0, max: 100 }),
  },
})
