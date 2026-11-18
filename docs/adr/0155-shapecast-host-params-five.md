# ADR-0155. ShapeCast — 원 쓸기는 raycast 의 Minkowski 합, Host 표의 매개변수 상한을 5 로

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0085](0085-script-standard-library-intrinsics-collections-callbacks.md) (Host 함수 · 표), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 5장,
  [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 25장 (질의 표), task-registry RT-06-6

## 맥락

카드 RT-06-6 은 ctx.shapeCast(x0, y0, x1, y1, r) → Entity | null (길이 ≤ 256, 차례 (t, saveId), raycast 시험과 같은 표) 이다. Host 표의
매개변수는 주인 (ctx) 을 빼고 4 개까지 (kMaxHostParams) 라 다섯째 (반지름) 가 들어가지 않는다. ADR-0152 는 같은 상한 때문에 그리기 색을
호출 안의 상태로 두었다.

## 결정

```text
1. 상한      kMaxHostParams 4 → 5. .sbc import 의 인자 수 상한도 (+1) 같이 늘어난다 — 옛 파일은 4 이하라 그대로 읽힌다
             (형식 번호 그대로). 지문은 표가 바뀌어 새로 (0xf8098f2fe1551dbf)
2. 뜻        반지름 R 의 원을 쓸기 = 각 충돌 원의 반지름을 R 만큼 키운 raycast (Minkowski 합). 같은 식 · 같은 차례 (닿는 t, 같으면
             saveId 작은 것) · 시작에 겹치면 t = 0 · 길이 256, R = 0 이면 raycast 와 같다. 반지름 0 ~ 32 (밖이면 HostError).
             후보는 선분의 상자 + (최대 콜라이더 반지름 + R) — 단계 4 색인
3. 권한      EcsRead (질의 — 어느 서버 콜백에서나), 연료 16 + 후보마다 1
```

## 결과

- 굵은 탄 · 몸통 쓸기 · 미리 보는 이동 판정이 한 호출이다. layer · mask 로 거르기는 [계획] (raycast 와 같이).
- ADR-0152 의 그리기 색은 상태 그대로 둔다 (그리기 API 를 다시 바꾸지 않는다).

## 고른 것 · 버린 것

- 반지름을 ctx 의 상태로 (drawColor 처럼): 질의가 앞 호출에 기대게 된다 — 상한을 올렸다.
- Vec2 ABI 타입 (점 둘을 묶기): 새 ABI 타입은 VM · 검증기 · 코드 생성 · d.ts 전부가 바뀐다 — 상한 하나가 작다.
