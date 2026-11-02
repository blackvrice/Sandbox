// 가방의 물건 — 인벤토리 예제의 컴포넌트 (스크립트 컴포넌트: 세이브 · 복제 · 해시가 내장과 같다)
import { defineComponent, field } from "sandbox"

export enum Kind {
  Potion,
  Arrow,
  Gem,
}

export const Item = defineComponent({
  id: "tour.item",
  fields: {
    kind: field.enumOf(Kind.Potion),
    count: field.i32(1, { min: 1, max: 99 }),
  },
})

export function kindName(k: Kind): string {
  switch (k) {
    case Kind.Potion:
      return "물약"
    case Kind.Arrow:
      return "화살"
    default:
      return "보석"
  }
}
