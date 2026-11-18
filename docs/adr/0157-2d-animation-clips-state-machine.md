# ADR-0157. 2D 애니메이션 = 표시 쪽 클립 · 상태 기계 (presentation/animations.json · render.animator), 시간은 렌더 틱

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0156](0156-tilemap-terrain-materials-sprite-cells.md) (시트의 칸 · SpriteDraw::sub), [ADR-0152](0152-client-onrender-draw-api.md)
  (client onRender), [06-RENDERING](../06-RENDERING.md) 8장, [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) (presentation/animations.json),
  [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) (client API), task-registry RT-06-14

## 맥락

RT-06-14 의 완료 기준은 "클립 재생 · 전이 · 결정론 표시". 개체 그림은 지금 머티리얼 하나 (render.sprite) — 걷기 · 맞기 같은 프레임 그림이
없다. NG-15 (ADR-0156) 가 시트의 칸 (Material::sub) 을 만들었으므로 프레임은 칸 고르기다. 정할 것은 (1) 애니메이션 상태가 시뮬레이션에
있는가, (2) 시간 기준, (3) 데이터 모양.

## 결정

```text
1. 표시 쪽만   애니메이션 상태는 클라이언트 추출 (SpriteExtraction) 에만 — 컴포넌트 · WorldHash · 세이브 · 복제 · kSimVersion 그대로.
               서버는 모른다 (render.* 컴포넌트처럼 Prefab 의 "render.animator" 는 클라이언트만 읽는다)
2. 데이터      팩 presentation/animations.json (클라 전용 — contentHash 밖, 팩 차례로 같은 이름은 뒤가 이긴다):
               clips      { material (시트), grid [열 수, 행 수], frames [[열, 행], …] (1 ~ 256), fps 1 ~ 60 (기본 8), loop (기본 true) }
               animators  { initial, states { 상태: 클립 } (1 ~ 64), transitions [{ from 상태|"*", to, when moving|still|end|always }],
                            flipWithVelocity }
               애니메이터는 다른 팩의 클립을 써도 된다 (모두 읽은 뒤 푼다 — 못 풀면 경고 · 그리지 않음). 틀린 항목은 그것만 빼고 경고
3. 시간        복제 월드의 렌더 틱 (30 TPS — 보간 틱 포함). 프레임 = floor((렌더 틱 − 상태 시작) × fps / 30), 반복이면 감고 아니면
               마지막에 멈춤 (end 조건). 같은 스냅숏 · 같은 렌더 틱 = 어느 클라이언트에서나 같은 프레임 (결정론 표시)
4. 전이        프레임마다 한 번, 정의 차례 첫째 맞는 것 (moving = 속도 ≥ 0.05 월드/초 — 복제된 Velocity). 바뀌면 그 틱부터 새로.
               flipWithVelocity — 좌우 속도 부호로 SpriteFlipX (멈추면 마지막 방향)
5. 스크립트    client 스크립트 ctx.playAnimation(e, state) — 전이 없이 그 상태를 처음부터 (프레임마다 256 개, 표시 효과 · Client 도메인).
               서버 스크립트에는 없다 (보이는 것만 바꾼다)
6. 그리기      그 개체의 render.sprite 크기 · 레이어 그대로, 머티리얼은 클립의 시트, sub 는 지금 칸
```

## 결과

- 걷기 · 서기 · 맞기 같은 상태 그림을 팩 데이터와 client 스크립트로 — 시뮬레이션 · 프로토콜 · 세이브 변경 없음.
- 서버 판정에 애니메이션 시간을 쓰는 게임 (공격 판정 프레임) 은 스크립트 타이머로 따로 — 표시와 판정은 다르다 (의도).
- 블렌딩 · 뼈대 (skeletal) · 이벤트 (프레임 콜백) · 편집기 미리보기는 [계획].

## 고른 것 · 버린 것

- 시뮬레이션 컴포넌트 (core.animation): 해시 · 세이브 · 복제 대역을 그림 때문에 쓴다 — 애니메이션은 보이는 것. 같은 렌더 틱으로
  클라이언트끼리 같은 프레임이면 충분하다.
- 벽시계 · 프레임 시간: 클라이언트마다 다르고 일시정지 · 리플레이와 어긋난다 — 렌더 틱.
- 클립을 materials.json 안에: 머티리얼은 칸 하나 (ADR-0156), 클립은 칸들 + 시간 — 파일을 나눠 읽기 쉽게.
