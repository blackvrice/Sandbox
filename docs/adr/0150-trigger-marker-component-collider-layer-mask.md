# ADR-0150. 트리거는 표시 컴포넌트 core.trigger — 콜라이더에 필드를 더하지 않는다, layer · mask 는 양쪽이 맞아야 겹친다

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0086](0086-runtime-architecture-v2.md) (Runtime v2), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 5장, [05-WORLD](../05-WORLD.md) 4.3,
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md), [04-DETERMINISM](../04-DETERMINISM.md) (골든), task-registry RT-06-4

## 맥락

22 5장은 "16 Collision 원 분리 + layer · mask 거르기, core.collider.trigger [계획]" 을 적었다. 확인해 보니 layer · mask 거르기는 Phase
5B 의 CollisionSystem 에 이미 있다 (두 쪽 다 (내 layer & 상대 mask) ≠ 0). 남은 것은 트리거 — 겹쳐도 밀지 않고 겹침만 알리는 콜라이더
(RT-06-5 의 triggerEnter · Exit). core.collider 는 Persistent → Hashed 라, 필드 하나를 더하면 그 값이 false 여도 모든 콜라이더
개체의 해시 바이트가 바뀌어 골든 · 리플레이 해시가 깨진다 (kSimVersion 5 가 필요).

## 결정

```text
1. 표시      core.trigger (u8 reserved = 0 — 빈 구조체 대신) — Persistent · EditorVisible. 붙인 개체만 세이브 · 해시가 바뀐다
             (트리거 없는 월드의 해시 · 골든 · kSimVersion 4 그대로)
2. 뜻        core.collider 와 함께 둔다. 한쪽이라도 트리거면 겹쳐도 밀어내지 않는다 (밀지도 밀리지도). layer · mask 는 콜라이더의
             것 그대로 — 거르기를 지난 겹침만 트리거 겹침이다. 콜라이더 없는 core.trigger 는 아무 일도 하지 않는다
3. layer · mask 그대로 — 두 쪽이 다 맞아야 겹친다 (한쪽만 맞으면 겹치지 않는다). 기본 layer 1 · mask 전부
4. 스크립트  엔진 컴포넌트 표 (EngineComponents) 에는 아직 넣지 않는다 (Host 지문 그대로). 트리거 겹침은 RT-06-5 의 이벤트로 본다
```

## 결과

- 트리거 영역 (함정 · 목표 · 줍는 것) 을 콘텐츠로 만든다 — "core.trigger": {} 하나.
- 카탈로그가 26 개가 된다 (엔진 컴포넌트 등록 — 세이브의 모르는 컴포넌트가 아니다).

## 고른 것 · 버린 것

- core.collider 에 trigger: bool: 모든 콜라이더 개체의 해시가 바뀐다 (골든 · 리플레이) — 버림.
- NotHashed 필드: 해시에서 빠지는 시뮬레이션 상태는 갈라짐을 숨긴다 — 버림.
- 트리거를 layer 비트 규칙 (예: 31 번 비트) 으로: 뜻이 숫자에 숨는다 — 버림.
