# 16. 로드맵

> **계획 문서.** "다음에 뭘 할지"를 정할 때 봅니다. 기준: 2026-10-05 (Phase 1 완료 반영).

---

## 1. 규칙

```text
1. 각 Phase 가 끝나면 빌드되고 모든 테스트가 통과한다. 빅뱅 없음.
2. 커밋은 컴파일 가능한 작은 단위. 커밋마다 CTest.
3. 완료 기준은 자동 검증 가능해야 한다 (수동 QA 는 보조).
4. 1차 목표선은 Phase 12 (Windows 멀티플레이 에디터). Linux/macOS 는 그 뒤.
5. Phase 순서를 바꾸려면 ADR.
```

## 2. 전체 순서

```text
Phase 0   분석·설계·문서                          ← 완료 (2026-10-05)
Phase 1   저장소 골격 · CI · 경계 검사              ← 완료 (2026-10-05, CI 원격 실행 확인 대기)
Phase 2   Core ECS
Phase 3   헤드리스 시뮬레이션 · 결정론 하네스
Phase 4   World (Chunk · Terrain · Spatial · Save/Load)
Phase 5   Ecosystem · Pathfinding Job · Replay       ← 헤드리스 콘텐츠 완성
Phase 6   Windows 플랫폼 (Win32 · Input · Audio)
Phase 7   DirectX 12 RHI · 셰이더 파이프라인
Phase 8   Renderer · Asset · ImGui                  ← 화면에 보인다
Phase 9   Network Foundation · Dedicated Server
Phase 10  Replication · LocalServerHost             ← 단일 코드 경로 완성
Phase 11  Interest Management
Phase 12  Multiplayer Editor                        ← 1차 목표선
Phase 13  Linux (X11 → Vulkan → Wayland)
Phase 14  macOS (Cocoa → Metal)
Phase 15  Optimization (측정 기반)
```

순서의 이유:

```text
- 시뮬레이션(2~5)을 렌더러(7~8)보다 먼저: 헤드리스로 정답이 확정된 뒤에 그려야
  "화면이 이상하다"가 렌더 버그인지 시뮬 버그인지 구분된다.
- 네트워크(9~10)를 에디터(12)보다 먼저: 에디터가 처음부터 명령을 서버로 보내는 구조로 태어나야 한다.
- Phase 8 의 --direct-sim(클라가 Core 를 직접 돌리는 임시 경로)은 Phase 10 완료 기준에서 삭제를 강제한다.
- Vulkan 을 Windows 에서 먼저 완성(13 사전 작업): Linux 포팅의 미지수를 "창 + 표면"으로 줄인다.
```

---

## 3. Phase별 산출물과 완료 기준

### Phase 0 — 분석·설계 ✅

```text
산출물  docs/design/SANDBOX_ARCHITECTURE.md, docs/00~17, adr/0001~0010, README, AGENTS.md, DEVELOPMENT_LOG.md
완료    문서 세트 작성 (2026-10-05)
```

### Phase 1 — 저장소 골격 ✅

| # | 티켓 | 상태 |
|---|---|---|
| 1.1 | `.gitignore`, `.gitattributes`(LF 기본, Windows 스크립트 CRLF), `.editorconfig`, `.clang-format`, `.clang-tidy` | ✅ 2026-10-05 |
| 1.1b | `git init` + 첫 커밋 + 원격 저장소 | ⏳ 사용자 작업 (이 세션은 사용자 PC 에서 git 을 실행할 수 없음) |
| 1.1c | `LICENSE` | ⏸ 열린 질문 Q1 결정 후 |
| 1.2 | 루트 `CMakeLists.txt`, `CMakePresets.json`(windows-msvc · windows-clangcl · linux-clang · linux-gcc · linux-clang-asan · linux-clang-tsan · macos), `cmake/Sbx*.cmake` | ✅ |
| 1.3 | `SandboxFoundation`: `Types`, `Error`/`Expected`, `SBX_ASSERT`/`SBX_VERIFY`, `log`, `Fnv1a64`(동결), `Handle<Tag>`, `BuildInfo`(생성) | ✅ — `SmallVector`는 첫 사용처인 Phase 2로 이동 |
| 1.4 | `SandboxCore`(시뮬레이션 상수), `SandboxServer`(`--help`/`--version`/`--log-level`), `SandboxTests`(doctest v2.5.0) | ✅ |
| 1.5 | `sbx_check_link_boundaries()`(전이 검사) + `tools/check_includes.py` + 각각의 자체 시험, CTest 라벨 `arch` | ✅ |
| 1.6 | `sbx_simulation_flags` (Core 가 PUBLIC 으로 전파) | ✅ |
| 1.7 | CI: `.github/workflows/ci.yml` (Windows MSVC · Linux clang/gcc/asan · macOS) | ✅ 작성 — 원격 저장소 생성 후 첫 실행으로 확인 |

```text
완료 기준  세 OS CI 에서 빌드 + unit/arch 테스트 통과. 의도적으로 Server 에 Platform 링크를 넣으면 구성이 실패한다.
검증 (2026-10-05, Linux 클라우드 환경)
  linux-clang(clang 19.1.1) · linux-gcc(gcc 13.3) · linux-clang-asan  ×  Debug · RelWithDebInfo
  → 각 8/8 테스트 통과 (doctest 29 케이스 / 68 단언), -Werror
  arch_link_boundary_selftest: 중간 타깃을 거친 전이적 금지 링크를 구성 단계에서 잡음
  arch_include_lint_selftest: 픽스처 8건 위반 정확히 검출
미검증  Windows MSVC · macOS 빌드 — 이 세션에서 실행할 수 없음. 사용자 PC 또는 CI 첫 실행에서 확인 필요.
```

### Phase 2 — Core ECS

| # | 티켓 |
|---|---|
| 2.1 | `EntityId`, `EntityManager` (LIFO 재사용, 퇴역 슬롯) |
| 2.2 | `ComponentPool<T>` sparse set + `validate()`, `foundation/container/SmallVector` (Phase 1에서 이동) |
| 2.3 | 컴포넌트 등록 `SBX_COMPONENT`, stableId(동결 테스트), Flags |
| 2.4 | 리플렉션 Visitor 개념 + `JsonWriter/Reader`, `HashVisitor` |
| 2.5 | `Registry` (create/destroy/emplace/remove/read/write, 구조 변경 가드, Resource) |
| 2.6 | `View<Read/Write/Exclude>` (가장 작은 풀 드라이버, changed[] 갱신) |
| 2.7 | `EntityCommandBuffer` (PendingEntity, 적용 순서, 상쇄) |
| 2.8 | 속성 테스트: Registry vs 참조 모델 10만 연산 |
| 2.9 | `sbx_bench ecs.iterate / ecs.churn` 최초 기록 |

```text
완료  02-ECS 15장 테스트 전부 통과. 10k view 순회 수치 기록.
```

### Phase 3 — 헤드리스 시뮬레이션

| # | 티켓 |
|---|---|
| 3.1 | `SimulationClock`, `SimulationWorld::tick()`, `SystemScheduler`(고정 Stage) |
| 3.2 | `SimCommand` variant + `CommandQueue` + 적용기(CreateEntity/DeleteEntity/MoveEntity/ChangeComponent) |
| 3.3 | `SpatialIndex` (정렬 배열 격자) + 속성 테스트 |
| 3.4 | `RandomService` (CounterRng) |
| 3.5 | `LifecycleSystem`(Lifetime, Age), `MovementSystem`(직선 적분), `EventStream` |
| 3.6 | `WorldHash` (리플렉션 기반) + `kSimVersion = 1` |
| 3.7 | `sbx_sim_check --repeat --hash-at --print-hash --threads` |
| 3.8 | 시나리오 `random_walk_1k` |

```text
완료  random_walk_1k 에서 D1, D5 통과. 1k 엔티티 평균 tick < 2 ms.
```

### Phase 4 — World

| # | 티켓 |
|---|---|
| 4.1 | `ChunkCoord`, `Chunk`, `WorldGrid` (고정 경계) |
| 4.2 | Terrain 레이어 + `TerrainMaterial`(ContentDatabase 최소판) + `PaintTerrain` 명령 |
| 4.3 | `queryRadius/AABB/Nearest/Chunk` 완성 |
| 4.4 | 세이브: world.json, entities.jsonl, chunk 파일, 테이블 재매핑, Opaque 보존 |
| 4.5 | 마이그레이션 프레임워크 + 샘플 세이브 테스트 |
| 4.6 | `sbx_sim_check --save-at`, 시나리오 `world_save_load` |

```text
완료  D2 통과. Spatial 속성 테스트 통과. 50k 저장 < 1 s.
```

### Phase 5 — Ecosystem

| # | 티켓 |
|---|---|
| 5.1 | ContentDatabase 완성: Prefab, Tag, Rule, BehaviorGraph 로더 + 검증기(V1~V7) + contentHash |
| 5.2 | `SensorSystem`, `BehaviorSystem`(FSM, 노드 어휘), `InteractionSystem` + `ResolveIntents`, Effect op |
| 5.3 | `life.*` 컴포넌트와 Lifecycle 완성 (Energy, Growth, Reproduce) |
| 5.4 | `PathfindingService` (A*, PathGridSnapshot, Job 계약 T→T+1), `CollisionSystem` |
| 5.5 | `content/ecosystem` 팩 (11-CONTENT-SCHEMA 7장 수치) |
| 5.6 | Replay 기록·재생 + `--replay-roundtrip` |
| 5.7 | 골든 `ecosystem_small`, 시나리오 `ecosystem_survival` |
| 5.8 | `sbx_bench sim.ecosystem 10k` |

```text
완료  시드 3개 18,000틱 세 종 공존. D1~D5 통과. 10k 평균 tick < 10 ms, p99 < 25 ms.
```

### Phase 6 — Windows 플랫폼

```text
6.1 IWindow / NativeWindowHandle / PlatformEvent / Key        6.2 Win32Window (DPI v2, Raw Input, IME)
6.3 InputSystem + ActionMap                                    6.4 IAudioBackend + Null 구현
6.5 SandboxClient 빈 창 + 앱 상태기계 골격
완료  입력 단위 테스트 + qa/MANUAL-QA 의 Phase 6 항목
```

### Phase 7 — DirectX 12 RHI

```text
7.1 RHI 인터페이스·Desc·Caps·Handle              7.2 Device/Adapter/Queue/Fence/SwapChain (Clear)
7.3 FrameContext, 업로드 링, 파괴 대기열          7.4 셰이더 빌드(sbx_add_shader, DXC, 리플렉션 JSON, 헤더 생성)
7.5 Pipeline/RootSignature/BindGroup (Triangle)   7.6 Texture + 업로드 (Texture)
7.7 sbx_render_tests + WARP CI
완료  clear/triangle/texture 기준 이미지 통과, Debug Layer·GBV 경고 0
```

### Phase 8 — Renderer

```text
8.1 AssetManager (Texture, Material, Font), Worker 디코드 → Upload Queue
8.2 Camera2D, SpriteBatcher(인스턴싱, 정렬 키, Texture2DArray 아틀라스)
8.3 TerrainPass(청크 메시 캐시), DebugDraw, GridPass, SelectionPass
8.4 ImGuiRenderer(RHI) + InputState→ImGuiIO, 기본 패널(Simulation, Stats)
8.5 SandboxClient --direct-sim 으로 Ecosystem 관찰 (임시)
8.6 Graphics 오버레이 지표, 타임스탬프 쿼리
완료  10k 스프라이트 60 FPS, Draw ≤ 16, sprite/batch_1k/coord_convention/imgui_basic 기준 이미지
```

### Phase 9 — Network Foundation

```text
9.1 INetworkTransport + Loopback + Simulated            9.2 EnetTransport (vendored)
9.3 BitWriter/Reader + 메시지 카탈로그(08 5장)          9.4 핸드셰이크·버전·contentHash·Reject
9.5 ServerHost (Simulation 스레드 + Net IO 스레드), CommandValidator(형식·속도 제한)
9.6 SandboxServer --world --ticks N --exit
완료  Loopback 핸드셰이크·명령·거절 테스트, 헤드리스 서버 CI 실행
```

### Phase 10 — Replication

```text
10.1 NetEntityId, NetIdentity 부여, NetEntityMap        10.2 Spawn/Update/Despawn, ack, baseline 32개
10.3 양자화, EntityRef 변환, tombstone                  10.4 SnapshotBuffer + 보간
10.5 LocalServerHost — 싱글플레이 경로 전환, --direct-sim 삭제
10.6 ServerStats 메시지, Network 오버레이
완료  net_convergence (100ms, 5% 손실) 통과, --direct-sim 코드 0줄
```

### Phase 11 — Interest

```text
11.1 Subscribe, relevantChunks/Entities, alwaysRelevant   11.2 히스테리시스, 우선순위·바이트 예산
11.3 Bulk TerrainChunk 캐시(revision), Late Join 흐름     11.4 Reconnect 토큰
완료  50k 월드에서 클라당 바이트 ∝ 가시 엔티티 (sbx_bench net.snapshot), Late Join < 2 s
```

### Phase 12 — Multiplayer Editor (1차 목표선)

```text
12.1 EditorContext, ICommandSink, Selection             12.2 Hierarchy/Inspector(리플렉션)/Palette
12.3 툴: Select/Move/Place/TerrainBrush/Erase + EditPreview
12.4 Rules/Behaviors 패널, 월드 오버레이, ContentOverlay 전송
12.5 권한(역할 표), Players 패널                        12.6 Undo/Redo
12.7 세이브/로드 UI, 리플레이 재생 UI
완료  edit_session 시나리오 D3, 2클라 동시 편집 통합 테스트, 권한 매트릭스 테스트
```

### Phase 13 — Linux

```text
13.0 (Windows) SBX_ENABLE_VULKAN_ON_WINDOWS 로 Vulkan 백엔드 완성 — 기준 이미지 DX12 와 일치
13.1 X11Window + 입력           13.2 Linux 빌드·실행        13.3 WaylandWindow (libdecor)
완료  Core·Renderer 상위 diff 0줄, Linux CI lavapipe 기준 이미지 통과
```

### Phase 14 — macOS

```text
14.0 Slang 재평가 스파이크 (ADR-0007 재검토 여부)
14.1 CocoaWindow.mm            14.2 Metal 백엔드 (.mm)      14.3 SPIRV-Cross MSL 경로
완료  Core·Renderer 상위 diff 0줄, macOS CI 기준 이미지 통과
```

### Phase 15 — Optimization

```text
14-PERFORMANCE 6장의 착수 조건을 만족한 항목만. 각 커밋에 전후 벤치 + 해시 동일성.
완료  50k: 평균 tick < 10 ms, p99 < 25 ms, 50k 가시 스프라이트 Draw ≤ 64
```

---

## 4. 지금 착수할 티켓

```text
0. [1.1b] 사용자: D:\Game\Sandbox 에서 git init → 첫 커밋 → GitHub 원격 생성 → push (CI 첫 실행)
0. [1.7]  사용자: Windows 에서 cmake --preset windows-msvc 로 빌드·테스트 1회 (MSVC 경로 확인)
1. [2.1] EntityId, EntityManager (LIFO 재사용, 퇴역 슬롯)
2. [2.2] ComponentPool<T> sparse set + validate()   (SmallVector 포함)
3. [2.3] SBX_COMPONENT 등록, stableId 동결 테스트, Flags
4. [2.4] 리플렉션 Visitor + JsonWriter/Reader, HashVisitor   (nlohmann/json vendoring)
5. [2.5~2.7] Registry · View · ECB
6. [2.8~2.9] 속성 테스트, 첫 벤치
```

## 5. 위험

| # | 위험 | 가능성 | 영향 | 완화 |
|---|---|---|---|---|
| R1 | 범위: 1인 개발, 15 Phase, 3 플랫폼 | 높음 | 치명 | Phase 12를 1차 목표선으로 고정. Phase마다 동작하는 상태 |
| R2 | 툴체인 전환 (MinGW → MSVC) | 중 | 높음 | Phase 1에서 즉시. RTS의 MinGW 간헐 컴파일 실패도 해소 |
| R3 | RHI 과잉/과소 추상화 | 중 | 높음 | Vulkan을 Windows에서 일찍 붙여 인터페이스 검증. 그 전까지 RHI 변경 허용 |
| R4 | GPU 동기화·수명 버그 | 높음 | 중 | Debug Layer/GBV/Validation 상시, 파괴 대기열 단일화, DRED, 기준 이미지 |
| R5 | 50k 성능 미달 | 중 | 높음 | Phase 2부터 벤치, EnTT 기준선, 착수 조건 표 |
| R6 | ENet 암호화 부재 | 확정 | 중 | LAN·신뢰 환경 한정 명시. 공개 서버 전 GNS 또는 DTLS (ADR 필요) |
| R7 | 네트워크 대역폭 (50k + 다수 클라) | 중 | 높음 | Interest, 양자화, 우선순위·예산, 측정 후 압축 |
| R8 | 복제 엔티티 참조 꼬임 | 중 | 중 | 재사용 금지, tombstone, 대기 목록, 수렴 테스트 |
| R9 | 결정론 회귀가 숨음 | 중 | 중 | Persistent → Hashed 기본, 리플렉션 자동 해시, 골든 simVersion 불일치 시 실패 |
| R10 | 셰이더 툴체인 (MSL 변환 실패, 버전 차이) | 중 | 중 | HLSL 부분집합 규약, 버전 고정, Phase 14 전 macOS 컴파일 CI |
| R11 | macOS 하드웨어·CI 접근 | 중 | 중 | Apple Silicon CI 러너, 실기 확인은 마일스톤만 |
| R12 | Wayland 복잡도 | 높음 | 낮음 | X11 먼저, libdecor, 대안 SDL3 백엔드 (ADR-0005 대안) |
| R13 | 원격 편집 체감 지연 | 중 | 중 | EditPreview, 로컬 서버는 지연 0 |
| R14 | 콘텐츠 표현력 부족 | 높음 | 중 | 요구 3건 누적 시 스크립팅 검토 (03 6.4) |
| R15 | ImGui 자체 렌더러 유지비 | 낮음 | 낮음 | 폴백: 공식 dx12 백엔드 래핑 (ADR-0008) |

## 6. 범위 밖 (의도적)

```text
3D 렌더링 · 스크립팅 언어 · 클라이언트 게임플레이 예측 · 공개 인터넷 서버 운영(암호화·계정)
· 워크샵/월드 공유 플랫폼 · 무한 월드 스트리밍 · 모바일/웹
→ 설계는 막지 않는다. 필요해지면 ADR 로 범위에 넣는다.
```

## 7. 열린 질문

```text
Q1 저장소 이름·라이선스      Q2 3D 시점      Q3 계정·인증      Q4 월드 공유 방식      Q5 스트리밍 시점
```
