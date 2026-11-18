# ADR-0158. Prefab 변형 (inherits) · 인스턴스 오버라이드 = 값의 차이 · 전파는 EditContent.propagate

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0030](0030-roles-player-list-content-overlay.md) (콘텐츠 오버레이 · EditContent), [02-ECS](../02-ECS.md) 13장 (Prefab 인스턴스화),
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 2장, [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) (Prefab 인스턴스 제안),
  [04-DETERMINISM](../04-DETERMINISM.md), task-registry ST-06-5

## 맥락

ST-06-5 의 완료 기준은 "det 시험 (전파 순서 · 해시)". 지금 개체는 core.prefab{id} (출처) 만 갖는다 — Prefab 을 고쳐도 이미 있는 개체는
그대로이고 (새 개체만 새 값), 어느 필드를 개체에서 바꿨는지 (오버라이드) 알 길이 없다. Prefab 스키마에는 "inherits" 가 [계획] 으로
자리만 있다. 정할 것: (1) 변형의 병합 규칙, (2) 오버라이드를 상태로 저장할지, (3) 전파가 시뮬레이션 동작을 바꾸는가 (kSimVersion —
골든 해시는 툴체인마다 있어 올리면 모든 툴체인을 다시 기록해야 한다).

## 결정

```text
1. 변형        Prefab 의 "inherits": "<Prefab id>" — 로드 때 푼다 (팩 · 오버레이 모두 읽은 뒤). components 는 부모의 것에 컴포넌트마다
               키 단위로 덮어쓴다 (CreateEntity 의 병합과 같다), "<컴포넌트>": null 은 부모의 그 컴포넌트를 뺀다. tags 는 쓰면 바꾸고
               안 쓰면 부모 것, name · category 도 같다. 깊이 4 까지 (넘으면 · 순환 V2, 없는 부모 V4). 결과는 보통 Prefab — 인스턴스화 ·
               템플릿 · 해시는 지금 그대로 (core.prefab 은 변형 id)
2. 오버라이드  저장하지 않는다 — 값의 차이다. 개체의 컴포넌트 (정규 JSON — 기본값 + 값 → readJson → writeJson) 가 Prefab 의 것과
               다른 필드 = 오버라이드, Prefab 에 없는 컴포넌트 = 더함, 개체에 없는 Prefab 컴포넌트 = 뺌. core.transform 은 개체마다
               다른 것이라 보지 않는다. 세이브 · 해시 · 복제 · 형식 그대로
3. 전파        EditContent 에 propagate (기본 false — 지금 명령 · 리플레이 · 골든은 그대로, kSimVersion 그대로). Prefab 을 propagate 로
               바꾸면 같은 명령 안에서, 정의가 바뀐 Prefab (그것 · 그것을 inherits 하는 변형) 의 개체마다 saveId 차례로:
                 필드   개체 값 = 옛 Prefab 값 이고 새 값이 다르면 새 값 (patchJson — 원자적, 실패하면 그 컴포넌트만 그대로)
                 더함   새 Prefab 에 생긴 컴포넌트가 개체에 없으면 붙인다 (새 값)
                 뺌     Prefab 에서 없어진 컴포넌트는 개체 값이 옛 Prefab 값과 모두 같을 때만 뗀다
                 태그   개체 태그 = 옛 태그면 새 태그
                 opaque render.* · client.* 도 같은 규칙 (값 통째)
               core.transform · 엔진 관리 컴포넌트는 건드리지 않는다. 오버레이 값 null (팩 값으로 되돌리기) 도 같은 전파
4. 와이어      EditContent 에 propagate 비트 — 프로토콜 21. 명령 JSON (리플레이) 은 true 일 때만 "propagate": true (옛 리플레이 그대로)
5. Studio      Prefab 을 고치는 편집은 propagate (되돌리기도 propagate — 따라간 필드가 돌아온다). Inspector 는 오버라이드 필드를
               표시하고 "Prefab 값으로" (ChangeComponent · AddComponent · RemoveComponent — 지금 명령) 를 준다
```

## 결과

- 토끼 Prefab 의 maxSpeed 를 고치면 월드의 토끼들이 따라온다 — 개체에서 따로 바꾼 토끼는 그대로. 결과는 명령 안에서 saveId 차례라
  같은 입력 = 같은 해시 (det 시험), 세이브 → 로드 뒤에도 차이 계산이 같다 (상태가 없으므로).
- 실행 중 바뀌는 값 (에너지 · 체력) 은 Prefab 값과 달라지면 오버라이드로 보여 따라가지 않는다 — 의도 (지금 상태를 덮지 않는다).
  우연히 새 값과 같게 바꾼 필드는 구별되지 않는다 (차이 기반의 한계 — 명시 목록은 [계획]).
- 중첩 Prefab (Prefab 안의 Prefab) · 필드 안쪽 (배열 원소) 단위 오버라이드는 [계획] — 필드 단위.

## 고른 것 · 버린 것

- 오버라이드 목록을 컴포넌트로 저장 (Unity 의 수정 목록): 세이브 형식 · 해시 · 복제 · 모든 쓰기 경로 (규칙 · 스크립트 · 명령) 가
  목록을 고쳐야 한다. 차이 계산은 쓰기 경로를 모른다.
- EditContent 가 언제나 전파: 지금 명령의 결과가 바뀐다 → kSimVersion 5 · 모든 툴체인의 골든 다시 기록. 비트 하나로 같은 일을 고른다.
- 새 명령 (UpdatePrefab): 코덱 · 검증 · 권한 · JSON · 되돌리기를 하나 더 — EditContent 의 Prefab 경로 (검증 · 오버레이 · 크기 상한)
  를 그대로 쓰는 편이 작다.
