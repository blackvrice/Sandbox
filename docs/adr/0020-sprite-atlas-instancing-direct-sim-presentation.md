# ADR-0020. 스프라이트는 Texture2DArray 아틀라스 하나 + 인스턴스 정점 버퍼로 그리고, 머티리얼은 assets 의 표로 풀며, --direct-sim 은 클라이언트 쪽 진행만 조절한다

- 상태: **Accepted** · 날짜: 2026-10-07 — 결정 5(추출 시점) · 6(인라인 진행 · 4 틱 따라잡기)은
  [ADR-0021](0021-direct-sim-simulation-thread-snapshot.md) 이 대체 (2026-10-07). 결정 5 의 입력(SimulationWorld · saveId) ·
  6(--direct-sim)은 [ADR-0026](0026-network-session-local-server-inspect.md) 이 대체 (2026-10-08)
- 관련: [06-RENDERING](../06-RENDERING.md) 7 · 8 · 14장, [01-ARCHITECTURE](../01-ARCHITECTURE.md) 3장, [16-ROADMAP](../16-ROADMAP.md) Phase 8,
  [ADR-0019](0019-dxc-nuget-pin-own-spirv-reflector-root-signature-layout.md)

## 맥락

Phase 8 은 티켓이 6개(에셋 · 스프라이트 배치 · 지형/디버그 패스 · ImGui · --direct-sim · 지표)라 Phase 5 · 7 처럼 나눈다:
**8A** 텍스처 에셋 · Camera2D · SpriteBatcher · Renderer · --direct-sim 관찰 → **8B** TerrainPass · Grid · Selection · DebugDraw ·
타임스탬프 → **8C** ImGui. 8A 에서 정할 것이 여섯이었다.

```text
1. 인스턴스 데이터를 셰이더에 어떻게 주는가 (06 은 StructuredBuffer + SV_InstanceID 를 적었다)
2. 아틀라스: 텍스처를 언제 · 어디에 패킹하는가, 텍스처 배열을 다시 만드는가
3. 카메라 상수를 어디에 (프레임마다 바뀐다)
4. render.sprite 의 material 이름 → 실제 그림. 콘텐츠(서버 · contentHash)인가 에셋인가
5. ECS → RenderWorld 를 어디서 · 얼마나 자주 읽는가 (render.sprite 는 Opaque JSON)
6. --direct-sim 의 일시정지 · 속도를 월드 명령으로 할지
```

## 결정

```text
1. 인스턴스 = 정점 버퍼 슬롯 하나 (VertexStepMode::Instance, 48 바이트 SpriteInstanceGpu), 이번 프레임의 업로드 링 구간.
   사각형 꼭짓점은 SV_VertexID (삼각형 띠 4개) — 정점 · 인덱스 버퍼가 없다. 묶음마다 draw(4, n, 0, firstInstance).
2. 아틀라스 = Texture2DArray 하나, 크기 고정 (기본 1024² × 4 층, RGBA8Unorm, 16 MB). 다시 만들지 않는다.
   이미지는 디코드가 끝나는 대로 층에 선반(shelf) 패킹하고 그 영역만 업로드 링 → copyBufferToTexture 로 복사한다.
   둘레에 1 텍셀 테두리(가장자리 늘림). 디코드는 JobSystem Worker, 패킹 · 복사는 Render 스레드에서 요청 순서로.
   프레임 예산 8 MB (한 프레임에 최소 하나). 준비 전 = 흰색 자리 (색만), 실패 = 마젠타. 8A 는 해제 · 재패킹 없음.
3. 카메라 = push constant 16 바이트 (월드 → NDC 의 scale · offset). 아틀라스 바인드 그룹은 한 번 만들어 둔다.
4. 머티리얼은 에셋이다 (06 7.1): assets/<팩>/materials.json — { "materials": { "eco/rabbit": { "sprite": "...png",
   "color": [r,g,b,a] } } }. contentHash 에 들어가지 않고 서버는 읽지 않는다. 없는 이름은 흰 사각형 + 이름 해시 색
   (같은 이름은 늘 같은 색), 경고 한 번.
5. Extraction(SandboxClient/presentation)은 매 프레임 core.transform + persist.persistence 를 읽어 스프라이트 하나씩.
   render.sprite(Opaque)는 saveId 별로 한 번 풀어 캐시한다. 위치는 직전 틱과 지금 틱을 alpha 로 보간, depth = -y.
   월드 배경은 경계 크기의 사각형 하나 (머티리얼 terrain/<fillMaterial>) — 지형 청크는 8B.
6. --direct-sim 의 일시정지 · 한 틱 · 속도(×¼ ~ ×8)는 클라이언트가 ScenarioRunner.step 을 부르는 빈도만 바꾼다
   (월드에 SetPause 같은 명령을 넣지 않는다 — 결정론 경로 · 리플레이와 무관). 한 프레임 따라잡기 4 틱, 넘치면 버린다.
```

그 밖에 작게 정한 것:

```text
- 정렬 키 64 비트 [pass 4 | layer 8 | pipeline 12 | material(아틀라스) 20 | depth 20] (06 8.2). depth 는 float 를 순서가
  지켜지는 u32 로 바꾼 상위 20 비트. 같은 키는 제출 순서 — 안정 LSD 기수 정렬(바이트 8단계, 모두 같은 바이트는 건너뜀).
  첫 구현의 비교 정렬은 50k 에서 4.85 ms 로 렌더 CPU 예산(4 ms)을 혼자 넘었다 → 2.19 ms (14-PERFORMANCE 7.6).
- 컬링: 회전을 생각한 외접원과 카메라 사각형. 아틀라스가 하나라 층 · 레이어가 달라도 대개 Draw 1개 (P2).
- 블렌드 = 직선 알파(Alpha), 컬링 없음 (뒤집기는 CPU 가 uv 를 바꾼다), 샘플러 선형 + Clamp.
- 카메라 줌 = pixelsPerUnit (0.25 ~ 512), 휠은 커서 아래 점 고정, 끌기는 잡은 점이 커서를 따라온다.
- Application: 월드 세션(IWorldSession)이 있으면 MainMenu → Connecting → InWorld 로 바로 (메뉴 UI 는 8C).
  헤드리스는 프레임마다 1/60 초로 진행 (CTest client_direct_sim_headless 가 틱 수를 정확히 본다).
- 기준 이미지가 WARP · lavapipe 에서 같도록 batch_1k 는 픽셀 경계에 맞춘 무회전 사각형 + 단색 · 완만한 텍스처만 쓴다
  (축소 샘플링이 날카로운 경계에 걸리면 구현마다 다르다). 회전 · 뒤집기 · 확대 샘플링은 sprite 기준 이미지가 본다.
- 자리 표시 스프라이트 그림(풀 · 토끼 · 늑대 32²)은 이 저장소에서 새로 그린 단순 도형이다.
```

## 근거

```text
- 인스턴스 정점 버퍼는 D3D12 · Vulkan · Metal 모두 1급이고 바인드 그룹 없이 오프셋만 바꾸면 된다. StructuredBuffer 는
  프레임마다 다른 오프셋을 가리키는 디스크립터(또는 루트 SRV)가 필요하다 — 8A 의 규모(수만)에서 이득이 없다.
- 고정 크기 배열 + 영역 복사는 텍스처를 다시 만들지 않으므로 바인드 그룹 · 파이프라인이 바뀌지 않고, 업로드가 예산 안에서
  자연스럽게 나뉜다. 스프라이트 수십 ~ 수백 장(8A 의 콘텐츠)은 1024² 한 층에도 넉넉하다.
- push constant 는 프레임 상수 버퍼(프레임 슬롯마다 하나 + 바인드 그룹)보다 단순하고, 루트 상수라 D3D12 에서 가장 싸다.
- 머티리얼을 콘텐츠에 넣으면 그림을 바꿀 때마다 contentHash 가 바뀌어 서버 · 세이브 호환이 깨진다 (06 7.1 의 구분).
- 매 프레임 전체 추출은 10k 개체에서도 단순하고 정확하다 (Wine + lavapipe 에서 12,877 스프라이트 · Draw 1 · 54 fps —
  소프트웨어 렌더링 수치). 변화 추적은 Phase 10 의 ClientWorld 에서 필요해지면.
- 진행 빈도만 바꾸면 --direct-sim 을 지울 때(Phase 10.5) 월드 쪽에 남는 것이 없다.
```

## 결과

- 얻는 것: `SandboxClient --direct-sim ecosystem_survival` 로 생태계를 실제 창에서 본다. 스프라이트 경로가 기준 이미지
  (sprite · batch_1k)와 단위 테스트(카메라 · 정렬 · 배치 · 패킹 · 추출 · 앱 흐름)로 묶였다.
- 포기하는 것: 아틀라스 해제 · 재패킹 · 밉맵 · 핫 리로드 (8A 는 쌓기만), 지형 그림(8B), 서버 경유(Phase 10).
- 위험: 아틀라스가 차면 이후 스프라이트는 마젠타 — 경고가 남는다. 페이지 수 · 크기는 AssetManagerDesc 로 바꾼다.
  선형 필터의 정밀도 차이는 기준 이미지 허용 오차(3 · 1 %)로 흡수한다.

## 대안

| 대안                                      | 기각 사유                                                                         |
|-------------------------------------------|-----------------------------------------------------------------------------------|
| StructuredBuffer + SV_InstanceID          | 프레임마다 바뀌는 오프셋을 디스크립터로 — 바인드 그룹을 프레임마다 다시 써야 한다 |
| 스프라이트마다 Texture2D                  | 텍스처가 바뀔 때마다 Draw (P1 · P2 위반)                                          |
| 빌드 단계 아틀라스 도구 (sbx_atlas)       | 지금은 그림이 몇 장뿐 — 로드 시 패킹으로 충분. 그림이 늘면 [계획]                 |
| 카메라를 프레임 상수 버퍼로               | 프레임 슬롯마다 버퍼 · 바인드 그룹 — 16 바이트에 과하다                           |
| 머티리얼을 content/ 의 Prefab 에          | contentHash · 서버 의존 (06 7.1)                                                  |
| --direct-sim 일시정지를 SetPause 명령으로 | 월드 상태 · 리플레이에 클라이언트 표시 조작이 섞인다                              |

## 재검토 조건

스프라이트 그림이 아틀라스 한 개를 넘을 때(아틀라스 여러 개 · 해제), 50k 가시 스프라이트에서 CPU 렌더 > 4 ms (06 8.5 목표),
Phase 8B 지형 패스(같은 정렬 키 공간), Phase 10 ClientWorld(추출을 변화 추적으로), Phase 13 Vulkan(같은 인스턴스 배치).
