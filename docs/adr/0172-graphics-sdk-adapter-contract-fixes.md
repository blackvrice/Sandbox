# ADR-0172. Graphics SDK 어댑터의 RHI 계약 — 못 하는 것은 이유와 함께 거절, 흉내는 상태를 되돌린다

- 상태: **Accepted** · 날짜: 2026-11-23
- 관련: [ADR-0168](0168-graphics-sdk-rhi-backend.md), [06-RENDERING](../06-RENDERING.md) 3장 (RHI 계약),
  [SDK_REQUIREMENTS](../graphics-sdk-refactor/SDK_REQUIREMENTS.md)

## 맥락

Graphics SDK 가 RHI 의 일부를 갖지 않아 어댑터 (render/graphics_sdk) 가 흉내 · 대체를 했는데, 2026-11-23 분석에서 RHI 계약을
조용히 바꾸는 곳 넷을 찾았다.

```text
1. 블렌드      PremultipliedAlpha · Additive 를 Alpha 로 그렸다 (SDK 는 alphaBlend 켬/끔뿐). 예전 D3D12 백엔드는 셋 다 했다
2. 삼각형 띠   TriangleStrip 을 인덱스 그리기로 바꾸며 공용 인덱스 버퍼를 묶고 되돌리지 않았다 — 뒤의 drawIndexed 가 엉뚱한
               인덱스로 그린다. 바꾼 인덱스는 u16 이라 정점 65,536 개를 넘으면 넘쳤다
3. 컬링        CullMode 를 CPU 로 흉내 — 비인덱스 · 인스턴스 1 · CPU 에서 읽히는 정점 버퍼만, 나머지 (drawIndexed · GPU 전용 정점 ·
               인스턴스 · 띠) 는 컬링 없이 조용히 그렸다. 감기 판정에 한 셰이더의 push constant 배치 (8 ~ 15 바이트 = scale) 를
               가정했다
4. 텍스처 모양 Texture2DArray 인지를 debugName == "sprite atlas" 로 정했다 (이름을 바꾸면 그리기가 바뀐다). 배열 뷰 칸
               (arrayView) 은 만들어지지 않는 죽은 코드였다
```

## 결정

1. **못 하는 것은 거절한다.** PremultipliedAlpha · Additive 파이프라인은 생성이 실패하고 이유를 남긴다 (SDK-REQ-007). 지금 쓰는
   곳은 없다. RHI enum 은 계약으로 남긴다.
2. **흉내는 상태를 되돌린다.** 띠 변환 뒤 사용자가 묶은 인덱스 버퍼를 다시 묶는다. 변환 인덱스는 정점이 65,535 를 넘으면 u32.
3. **컬링 흉내는 범위를 좁히고 밖은 알린다.** 흉내 = 비인덱스 TriangleList · 인스턴스 1 · CPU 에서 읽히는 정점 버퍼, 감기는 정점
   버퍼의 POSITION (RG32Float) 그대로 (정점 셰이더를 보지 않는다 — 셰이더가 뒤집는 파이프라인은 컬링에 기대지 않는다; Sandbox
   렌더러의 파이프라인은 모두 CullMode::None). push constant 배치 가정은 지운다. 흉내 밖의 그리기는 컬링 없이 그리되 파이프라인
   마다 한 번 오류로 알린다 (SDK-REQ-004 — SDK 가 cull 상태를 가지면 흉내를 지운다).
4. **텍스처 모양은 RHI 가 말한다.** `TextureDesc::viewDim` (기본 Tex2D) — 셰이더가 Texture2DArray 로 읽는 텍스처 (아틀라스) 는
   layers 가 1 이어도 Tex2DArray 를 적는다. 어댑터는 layers > 1 또는 viewDim == Tex2DArray 면 배열로 만든다. 죽은 arrayView 를 지운다.

## 결과

- 어댑터가 다른 그림을 조용히 내지 않는다 — 할 수 없으면 생성 실패 또는 오류 수로 보인다 (렌더 시험의 Debug Layer 오류 검사).
- 각 항목은 단독 커밋 (render 시험 + 회귀 시험). SDK 가 기능을 갖추면 (SDK-REQ-004 · 007) 흉내 · 거절을 지운다.
