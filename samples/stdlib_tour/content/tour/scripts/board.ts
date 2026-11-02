// 예제 C — 보드게임 AI (Graph · BFS · Dijkstra · 위상 정렬, docs/19-SANDBOXSCRIPT.md 25.6)
// 시작할 때 6 × 6 판 (벽 8 칸) 에서 말이 왼쪽 위에서 오른쪽 아래까지 가는 최소 수와 길, 그리고 기술 트리의 배우는 차례를
// 셈한다. 스크립트 안의 그래프 (월드 길찾기와 따로) — 같은 판 = 같은 답 (동점은 작은 노드 번호).
import { defineSystem, Graph, i32 } from "sandbox"

const W: i32 = 6

function walls(): i32[] {
  return [2, 8, 11, 14, 16, 22, 26, 32]
}

function board(): Graph {
  const g = new Graph(W * W, false)
  const wall = walls()
  for (let y: i32 = 0; y < W; y++) {
    for (let x: i32 = 0; x < W; x++) {
      const id = y * W + x
      if (wall.includes(id)) {
        continue
      }
      if (x + 1 < W && !wall.includes(id + 1)) {
        g.addEdge(id, id + 1)
      }
      if (y + 1 < W && !wall.includes(id + W)) {
        g.addEdge(id, id + W)
      }
    }
  }
  return g
}

export default defineSystem({
  id: "tour.board",
  onStart(ctx) {
    const g = board()
    const goal = W * W - 1
    const reach = g.bfs(0).length
    const path = g.pathTo(0, goal)
    const moves = path !== undefined ? path.length - 1 : -1
    // 기술 트리: 0 농사 → 1 목축 → 3 기병, 0 → 2 채광 → 4 철기 → 3
    const tech = new Graph(5, true)
    tech.addEdge(0, 1)
    tech.addEdge(0, 2)
    tech.addEdge(2, 4)
    tech.addEdge(1, 3)
    tech.addEdge(4, 3)
    const order = tech.topoSort()
    ctx.log(
      `판 6x6: 닿는 칸 ${reach} · 최소 수 ${moves} · 길 ${path !== undefined ? path.join("-") : "없음"} | 기술 차례 ${order !== undefined ? order.join(">") : "순환"}`,
    )
  },
})
