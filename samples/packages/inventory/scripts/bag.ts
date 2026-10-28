// 보관함 — 다른 팩은 pack.json 에 이 팩을 적고 import { Bag, store, isFull } from "@inventory/bag" 으로 쓴다
import { defineComponent, field, i32 } from "sandbox"

export const Bag = defineComponent({
  id: "inventory.bag",
  fields: {
    items: field.i32(0, { min: 0, max: 1000 }),
    capacity: field.i32(10, { min: 1, max: 1000 }),
  },
})

// n 개를 넣은 뒤의 개수 — 자리가 모자라면 가득 찰 때까지만
export function store(items: i32, capacity: i32, n: i32): i32 {
  const room: i32 = capacity - items
  return items + (n < room ? n : room)
}

export function isFull(items: i32, capacity: i32): boolean {
  return items >= capacity
}
