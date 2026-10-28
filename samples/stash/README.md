# 보관함 (stash) — v0.4 패키지 데모

다른 제작자가 만든 Feature 패키지 [`samples/packages/inventory`](../packages/inventory) 를 설치해서 쓰는 게임 프로젝트
([docs/18-PLATFORM.md](../../docs/18-PLATFORM.md) 6.4 — v0.4 M6-T1, ADR-0071 ~ 0077).

```text
패키지 inventory 1.0.0 (MIT) — samples/packages/inventory
  scripts/bag.ts       컴포넌트 inventory.bag {items · capacity} + export function store · isFull
  scripts/collect.ts   서버 시스템 inventory.collect — 1 초마다 보관함마다 하나씩 (가득 차면 그대로), 10 초마다 로그
  prefabs/crate.json   inventory.crate (capacity 10)
게임 sandbox.stash — samples/stash
  sandbox.project.json dependencies {"inventory": {"version": "^1", "path": "../packages/inventory"}}
  sandbox.lock         잠금 — inventory 의 digest (설치는 이것 그대로)
  content/stash/       게임의 팩 (requires inventory)
    prefabs/cart.json  stash.cart — 패키지의 컴포넌트 inventory.bag 를 쓰는 Prefab (capacity 25)
    scripts/bonus.ts   서버 시스템 stash.bonus — import { Bag, store, isFull } from "@inventory/bag" (팩 사이 import)
  worlds/main          빈 시작 월드 (일시정지) — 패키지를 설치한 뒤 클라이언트에서 Ctrl+S 로 저장했다
  playthrough.json     자동 플레이 — 상자 넷 · 수레 둘, 10 초 뒤 상자 하나 더, 30 초 (900 틱)
```

## 만든 길 (sbx 명령만)

```text
sbx new --pack samples/packages/inventory --id inventory --license MIT   (틀을 고쳐 보관함으로)
sbx new samples/stash --id sandbox.stash --name "보관함"                 (빈 월드)
  content/stash 를 더하고 매니페스트 content.packs 에 stash
sbx add samples/stash inventory --path ../packages/inventory --version ^1
SandboxClient --world samples/stash → Ctrl+S                             (시작 월드를 지금 콘텐츠로 저장 — 검사 경고 0)
```

## 해 보기

설치 폴더 `.sandbox/` 는 저장소에 넣지 않는다 (잠금에서 다시 만든다). 처음 한 번:

```text
sbx install samples/stash                     잠금 그대로 설치 (캐시 → 없으면 ../packages/inventory 에서, digest 확인)
sbx check samples/stash                       의존 줄 "inventory ^1 → 1.0.0 · MIT · … (.sandbox/packages/inventory)"
sbx test samples/stash                        검사 + playthrough.json 을 끝까지 · expect
SandboxClient --world samples/stash           싱글플레이 — 편집 패널 배치 (3): "stash · 프로젝트" · "inventory 1.0.0 · MIT ·
                                              설치한 패키지" 묶음, 인스펙터의 inventory.bag, F1 의 스크립트 프로파일
sbx build samples/stash                       게임 패키지 — "팩 2 — MIT 2", sbx check <패키지> 가 팩마다 라이선스 · digest
```

## 자동 플레이

```text
SandboxServer --world samples/stash --script samples/stash/playthrough.json --port 0
SandboxClient --headless --world samples/stash --script samples/stash/playthrough.json
```

둘 다 같은 한 줄 (헤드리스 서버 = 로컬 플레이 · 게임 패키지도 — CTest `sample_stash` 가 빈 캐시 설치부터 본다):

```text
게임 결과: 게임 없음 · tick 900 · hash 0xaf95ab21bf58d0cb      (해시는 툴체인별 — playthrough.json 의 expect)
```

로그: `inventory.collect t300: 보관함 7 · 가득 4` → `stash.bonus t450: 아이템 합 100` (상자 다섯 × 10 + 수레 둘 × 25 — 모두 가득).
