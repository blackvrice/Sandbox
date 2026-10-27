// 마나 — 토끼마다 (스크립트 컴포넌트: 세이브 · 복제 · Inspector · 해시가 내장 컴포넌트와 같다)
import { defineComponent, field } from "sandbox"

export const Mana = defineComponent({
  id: "eco.mana",
  fields: {
    current: field.f32(20, { min: 0, max: 100 }),
    max: field.f32(100, { min: 1, max: 100 }),
    regen: field.f32(2, { min: 0, max: 20 }),
  },
})
