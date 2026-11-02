# 표준 라이브러리 둘러보기 (stdlib_tour) — v0.5 M8-T9 예제

SandboxScript 표준 라이브러리 ([docs/19-SANDBOXSCRIPT.md](../../docs/19-SANDBOXSCRIPT.md) 25장, ADR-0085) 를 쓰는 서버 시스템
셋. 안내는 [docs/tutorials/stdlib.md](../../docs/tutorials/stdlib.md).

```text
content/tour/
  scripts/items.ts      컴포넌트 tour.item {kind: enum Kind, count} · kindName
  scripts/inventory.ts  예제 A 인벤토리 — 2 초마다 종류별 개수 (Map<Kind, i32>), 큰 더미 종류 (Set<Kind>), 정렬 · sortNumbers ·
                        unique · sum. Map 의 반복은 넣은 차례 → 같은 월드 = 같은 줄
  scripts/soldier.ts    컴포넌트 tour.soldier {team, hp}
  scripts/rts.ts        예제 B RTS 유닛 AI — 1 초마다 병사마다 ctx.kNearest 로 가까운 16 명, 적을 PriorityQueue (거리 + 체력/10)
                        에 넣어 가장 작은 것으로 ctx.requestPath (ai.path — 엔진이 다음 틱 제출 · 그다음 틱 결과, 틱 예산)
  scripts/board.ts      예제 C 보드게임 AI — 시작할 때 6 × 6 판 (벽 8) 의 Graph: BFS 로 닿는 칸, pathTo 로 최소 수 · 길,
                        기술 트리 Graph 의 topoSort
  prefabs/              tour.potion · tour.arrows · tour.gem (물건), tour.red · tour.blue (병사 — ai.path · core.movement)
worlds/main             빈 시작 월드 (sbx new — 일시정지, 엔티티 0)
playthrough.json        자동 플레이 — 물건 다섯 · 빨강 셋 · 파랑 둘, 300 틱 (Windows MSVC 해시를 적었다)
```

## 만든 길

```text
sbx new samples/stdlib_tour --id sandbox.stdlib_tour --name "표준 라이브러리 둘러보기"
  content/tour 를 더하고 매니페스트 content.packs 에 tour
  시작 월드는 엔티티 0 이라 world.json 의 content (packs · contentHash) 와 components 표만 지금 콘텐츠로 맞췄다
  (클라이언트에서 Ctrl+S 로 다시 저장한 것과 같은 결과 — sbx check 경고 0)
sbx check samples/stdlib_tour   → 오류 0, 경고 0 · 시스템 셋
sbx test samples/stdlib_tour    → 통과 1 (CTest sample_stdlib_tour — 줄마다 확인)
```

스크립트는 SDK (sdk/sandbox.d.ts) 와 함께 `tsc --strict` 도 받는다 (tsconfig.json — 2026-11-02 TypeScript 5.6 로 확인).

## 출력 (MSVC Debug · 2026-11-02 측정)

```text
tour.board t1: 판 6x6: 닿는 칸 28 · 최소 수 10 · 길 0-1-7-13-19-20-21-27-28-29-35 | 기술 차례 0>1>2>4>3
tour.inventory t60: 가방 t60: 물약 6 · 보석 1 · 화살 24 | 큰 더미 종류 1 · 더미 크기 1,3,12 · 합 31
tour.rts t30: RTS t30: 경로 요청 5 · 적 없음 0
… (t300 까지) → playthrough 통과 · hash 0x5c11cde6edb4b5dd
```
