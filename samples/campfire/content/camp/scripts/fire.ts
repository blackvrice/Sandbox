// 모닥불 표시 — 스크립트가 만든 모닥불에 붙인다 (복제 — client HUD 가 이것으로 모닥불을 찾는다) · 파도 수 (월드 상태)
import { defineComponent, defineState, field } from "sandbox"

export const Fire = defineComponent({
  id: "camp.marker",
  fields: { lit: field.bool(true) },
})

export const Waves = defineState({
  id: "camp.waves",
  fields: { count: field.i32(0, { min: 0, max: 10000 }) },
})
