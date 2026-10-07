# 결정 기록 (ADR)

"왜 이렇게 했지?"에 답하는 기록입니다.

## 규칙

```text
1. 결정을 내리거나 뒤집으면 새 ADR 을 추가한다. 기존 ADR 은 수정하지 않는다 (오타 제외).
2. 뒤집힌 ADR 은 상태만 "Superseded by ADR-NNNN" 으로 바꾼다.
3. 형식: 맥락 → 결정 → 근거 → 결과(얻는 것 / 포기하는 것 / 위험) → 대안 → 재검토 조건
4. 파일명: NNNN-kebab-case-title.md
```

## 목록

| #                                                                       | 제목                                                                                                                                                     | 상태                                      | 날짜       |
|-------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------|------------|
| [0001](0001-new-repository-rewrite.md)                                  | RTS를 리팩터링하지 않고 새 저장소에서 재작성한다                                                                                                         | Accepted                                  | 2026-10-05 |
| [0002](0002-custom-sparse-set-ecs.md)                                   | 자체 Sparse Set ECS를 쓴다 (RTS ADR-0003 반전)                                                                                                           | Accepted                                  | 2026-10-05 |
| [0003](0003-server-authoritative.md)                                    | Server Authoritative + Delta Snapshot, 싱글플레이도 Loopback                                                                                             | Accepted                                  | 2026-10-05 |
| [0004](0004-determinism-scope.md)                                       | 결정론은 같은 바이너리 안에서만, 시뮬레이션은 float                                                                                                      | Accepted                                  | 2026-10-05 |
| [0005](0005-dependency-boundaries.md)                                   | 타깃 의존 경계, Render는 Core를 모른다, 네이티브 플랫폼 계층                                                                                             | Accepted                                  | 2026-10-05 |
| [0006](0006-thin-rhi.md)                                                | 얇은 RHI + Handle + Capability                                                                                                                           | Accepted                                  | 2026-10-05 |
| [0007](0007-hlsl-shader-pipeline.md)                                    | HLSL 단일 소스 + DXC + SPIRV-Cross, 오프라인 컴파일                                                                                                      | Accepted                                  | 2026-10-05 |
| [0008](0008-imgui-on-rhi.md)                                            | ImGui는 RHI 위 자체 렌더러로 통합                                                                                                                        | Accepted                                  | 2026-10-05 |
| [0009](0009-msvc-toolchain.md)                                          | Windows 툴체인을 MSVC로                                                                                                                                  | Accepted                                  | 2026-10-05 |
| [0010](0010-enet-transport.md)                                          | Transport 1차 구현은 ENet                                                                                                                                | Accepted                                  | 2026-10-05 |
| [0011](0011-explicit-component-registration.md)                         | 컴포넌트는 카탈로그에 명시적으로 등록한다                                                                                                                | Accepted                                  | 2026-10-05 |
| [0012](0012-golden-hash-per-toolchain.md)                               | 골든 해시는 툴체인별로 기록한다                                                                                                                          | Accepted                                  | 2026-10-05 |
| [0013](0013-save-folder-swap-and-load-hash.md)                          | 세이브는 폴더째 바꿔 넣고, 로드는 해시로 스스로 검증한다                                                                                                 | Accepted (2번 Opaque 조건은 0014 가 대체) | 2026-10-05 |
| [0014](0014-content-model-spawnqueue-name-tables.md)                    | 콘텐츠는 이름으로 묶고, Prefab 생성은 SpawnQueue 로, 로드 해시는 Opaque 이름 집합으로 판단한다                                                           | Accepted                                  | 2026-10-05 |
| [0015](0015-ai-tick-caches-saveid-refs-path-jobs.md)                    | 행동 상태는 saveId 로 참조하고, 틱 캐시는 저장하지 않으며, 경로 Job 은 다음 틱에 제출 순서로 받는다                                                      | Accepted                                  | 2026-10-05 |
| [0016](0016-replay-call-index-json-lines.md)                            | 리플레이는 tick() 호출 번호로 명령을 놓고 saveId 로 대상을 적으며, 읽기 전용 System 패스는 Worker 와 나눈다                                              | Accepted                                  | 2026-10-06 |
| [0017](0017-physical-keys-event-queue-headless-window.md)               | 키는 물리 위치로, 창은 이벤트 큐를 채우고, 창이 없는 실행은 HeadlessWindow 로 한다                                                                       | Accepted                                  | 2026-10-06 |
| [0018](0018-rhi-frame-protocol-committed-resources-wine-testing.md)     | RHI 는 beginFrame/endFrame 으로 프레임 슬롯을 돌리고, D3D12 첫 구현은 committed 리소스로 하며, 클라우드에서는 Wine + vkd3d 로 시험한다                   | Accepted                                  | 2026-10-06 |
| [0019](0019-dxc-nuget-pin-own-spirv-reflector-root-signature-layout.md) | DXC 는 NuGet 패키지로 고정해 내려받고, 리플렉션은 자체 SPIR-V 파서로 하며, D3D12 루트 시그니처는 BindGroup 슬롯마다 표 두 개로 만든다                    | Accepted                                  | 2026-10-07 |
| [0020](0020-sprite-atlas-instancing-direct-sim-presentation.md)         | 스프라이트는 Texture2DArray 아틀라스 하나 + 인스턴스 정점 버퍼로 그리고, 머티리얼은 assets 의 표로 풀며, --direct-sim 은 클라이언트 쪽 진행만 조절한다   | Accepted (결정 5 · 6 → 0021)              | 2026-10-07 |
| [0021](0021-direct-sim-simulation-thread-snapshot.md)                   | --direct-sim 은 창에서 Simulation 스레드로 돌리고, 렌더 쪽에는 틱마다 만든 불변 스냅숏만 넘긴다                                                          | Accepted                                  | 2026-10-07 |
| [0022](0022-terrain-tile-texture-overlay-passes-gpu-timestamps.md)      | 지형은 타일 머티리얼 번호 텍스처 + 팔레트로 한 번에 그리고, 격자 · 선택 · 디버그는 화면 픽셀 오버레이로, 패스별 GPU 시간은 프레임 슬롯 타임스탬프로 잰다 | Accepted                                  | 2026-10-07 |
