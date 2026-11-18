# ADR-0156. 2D 타일맵 = 지형 머티리얼 + 그림 — 시트의 칸 (cell · grid), 보이는 칸만 타일 스프라이트

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0022](0022-terrain-tile-texture-overlay-passes-gpu-timestamps.md) (TerrainPass), [05-WORLD](../05-WORLD.md) 3 · 4장 (지형 칸 · 칠하기),
  [06-RENDERING](../06-RENDERING.md) 7 · 8장, [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) (terrain.json · materials.json),
  [26-ASSET-PIPELINE](../26-ASSET-PIPELINE.md), task-registry NG-15 (GM-39)

## 맥락

NG-15 의 완료 기준은 "칠하기 · 저장 왕복 · 해시", 출시 기준은 "Tilemap 생성 · 편집 · 저장 · 빌드". 월드에는 이미 칸 지형이 있다 — 칸마다
머티리얼 번호 (WorldGrid), 칠하기 명령 PaintTerrain (편집기의 지형 브러시 · 되돌리기), 세이브 · WorldHash · 복제 (청크 revision), 통행
비용 (moveCost 0 = 막힘 — 경로 · 충돌). 빠진 것은 그림이다 (GM-39 — "지형 칠하기는 있고 스프라이트 타일 · 타일셋이 없다"): TerrainPass 는
머티리얼마다 색 하나로 칸을 칠한다.

## 결정

```text
1. 타일      타일 하나 = 지형 머티리얼 하나 (팩 terrain.json — id · moveCost · flags). 칠하기 · 세이브 · 해시 · 복제 · 막힘은 지금 것
             그대로 — 새 시뮬레이션 상태 · 형식 · 명령이 없다 (kSimVersion · 세이브 형식 · 프로토콜 그대로)
2. 그림      presentation/materials.json 의 "terrain/<id>" 에 sprite 가 있으면 그 칸을 그림으로 (없으면 지금처럼 색). 타일셋 = 시트
             한 장 + 칸: "cell": [열, 행] · "grid": [열 수, 행 수] (0 부터, 위에서 아래로) — Material::sub (그림 안의 비율 사각형)
3. 그리기    SpriteDraw::sub — 스프라이트 묶기가 아틀라스 자리 안에서 비율로 (뒤집기 그대로). 클라이언트 추출이 보이는 칸 (카메라 ∩
             월드) 중 그림 있는 머티리얼의 칸마다 스프라이트 하나 (월드 1 단위, 레이어 0 · 깊이 가장 아래 — 개체 밑, TerrainPass 색
             위). 보이는 칸이 32,768 을 넘으면 (멀리 줄임) 그림 없이 색만
4. 개체      같은 sub 를 개체의 render.sprite 머티리얼도 쓴다 — 시트 한 장의 프레임 (애니메이션 RT-06-14 가 이어 쓴다)
5. 편집      Studio 의 지형 툴 (브러시 · 머티리얼 고르기) 이 그대로 타일 칠하기다
```

## 결과

- 타일맵 게임 (던전 · 플랫폼 바닥) 을 팩 데이터만으로 — 벽 타일은 moveCost 0 이라 길 찾기 · 충돌이 이미 막는다.
- 층 (바닥 위 장식 타일 층) · 자동 타일 (이웃에 따라 칸 고르기) · 타일 회전은 [계획]. 칸마다 그림이라 아주 큰 화면은 색으로 떨어진다.

## 고른 것 · 버린 것

- 지형과 따로 된 타일 층 (칸마다 두 번째 번호): 세이브 · 해시 · 복제 · 명령 · 편집기 · 경로가 모두 두 겹이 된다 — v0.12 는 지형이 곧
  타일. 장식 층이 필요해지면 그때 같은 칸 저장을 늘린다.
- TerrainPass 셰이더에서 타일 그림 (아틀라스 표를 GPU 로): 셰이더 · 백엔드 셋을 고쳐야 한다 — 보이는 칸만 스프라이트 묶기로.
