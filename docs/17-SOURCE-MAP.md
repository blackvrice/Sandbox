# 17. 소스 지도

> **참조 문서.** "이 코드가 어디 있지?"에 답합니다. 모듈·폴더·진입점·타깃을 추가/이동/삭제하면 같은 커밋에서 갱신합니다.
> 상태: Phase 1 완료 (2026-10-05). **0장이 실제로 존재하는 파일**이고, 1장 이후는 전체 계획 구조입니다.

---

## 0. 현재 존재하는 것 (Phase 1)

| 경로 | 내용 | 타깃 |
|---|---|---|
| `CMakeLists.txt` | 루트 빌드, 모듈 추가, 경계 규칙 선언 | — |
| `CMakePresets.json` | 7개 configure 프리셋 + build/test 프리셋 ([15-BUILD](15-BUILD.md) 2장) | — |
| `cmake/SbxOptions.cmake` | `SBX_*` 옵션 | — |
| `cmake/SbxCompilerSettings.cmake` | `sbx_warnings`, `sbx_strict_conversions`, `sbx_simulation_flags`, 표준 라이브러리 기능 확인 | — |
| `cmake/SbxBoundaries.cmake` | `sbx_forbid_link()`, `sbx_check_link_boundaries()` | — |
| `cmake/SbxBoundarySelftest.cmake` | 경계 검사 실패 경로 시험 (내부 옵션) | — |
| `cmake/SbxBuildInfo.cmake`, `cmake/BuildInfo.hpp.in` | `build/<preset>/generated/foundation/BuildInfo.hpp` 생성 (버전, 커밋, 컴파일러) | — |
| `foundation/types/Types.hpp` | 정수·실수 별칭 | SandboxFoundation |
| `foundation/types/Error.{hpp,cpp}` | `ErrorCode`, `Error`, `Expected<T>`, `makeError` | SandboxFoundation |
| `foundation/assert/Assert.{hpp,cpp}` | `SBX_ASSERT`, `SBX_VERIFY`, `setAssertHandler` | SandboxFoundation |
| `foundation/log/Log.{hpp,cpp}` | `sbx::log::{trace,debug,info,warn,error}`, 레벨, 싱크 | SandboxFoundation |
| `foundation/hash/Fnv1a.hpp` | `Fnv1a64`, `fnv1a64()` — **동결** | SandboxFoundation |
| `foundation/handle/Handle.hpp` | `Handle<Tag>` | SandboxFoundation |
| `core/simulation/SimConstants.{hpp,cpp}` | `kTickRate`, `kFixedDt`, `Tick`, `isValidSnapshotRate` | SandboxCore |
| `apps/server/main.cpp` | 서버 진입점 | SandboxServer |
| `apps/server/ServerOptions.{hpp,cpp}` | 명령줄 파싱 (`--help`, `--version`, `--log-level`) | SandboxServer (+ Tests) |
| `tests/main.cpp`, `tests/unit/**` | doctest 단위 테스트 (스위트: foundation, core, server) | SandboxTests |
| `tests/arch/fixtures/**` | include 린터 자체 시험용 위반 파일 (**컴파일 안 함**) | — |
| `tools/check_includes.py` | include 경계 린터 | CTest `arch` |
| `external/doctest/` | doctest v2.5.0 | — |
| `.github/workflows/ci.yml` | CI (Windows MSVC, Linux clang/gcc/asan, macOS) | — |
| `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore` | 포맷·린트·줄끝 규칙 | — |

빌드 산출물: `build/<preset>/bin/<Config>/SandboxServer(.exe)`, `SandboxTests(.exe)`.

## 1. 최상위

| 경로 | 내용 | 타깃 |
|---|---|---|
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | 빌드 정의, 프리셋, 함수(`sbx_add_shader`, `sbx_check_link_boundaries`, `sbx_simulation_flags`) | — |
| `external/` | vendored 서드파티 (LICENSE 보존) | — |
| `foundation/` | 기본 타입·수학·핸들·컨테이너·로그·해시·시간·IO·Job·메트릭 | SandboxFoundation |
| `core/` | ECS·시뮬레이션·월드·콘텐츠·명령·직렬화·리플레이·난수 | SandboxCore |
| `network/` | Transport·Protocol·Session·Snapshot·Replication·Interest·ServerHost | SandboxNetwork |
| `platform/` | 창·입력·오디오 | SandboxPlatform |
| `render/` | RHI·백엔드·Renderer·Shader·Asset·ImGui 렌더러 | SandboxRender |
| `editor/` | 패널·툴·선택·Inspector·편집 명령 | SandboxEditor |
| `apps/client/` | 클라이언트 진입점·앱 상태기계·Presentation·LocalServerHost | SandboxClient |
| `apps/server/` | 서버 진입점 | SandboxServer |
| `shaders/` | HLSL 원본 (canonical) | SandboxRender (빌드 단계) |
| `content/` | 콘텐츠 팩 (JSON) | 런타임 데이터 |
| `assets/` | 텍스처·폰트·사운드 | 런타임 데이터 |
| `tests/` | 단위·속성·시나리오·결정론·네트워크·렌더 테스트, 골든 | SandboxTests, sbx_render_tests |
| `bench/` | 벤치 시나리오·머신별 기준선 | sbx_bench |
| `tools/` | `check_includes.py`(구현), `sbx_sim_check`·`sbx_atlas`(계획) | 도구 |
| `docs/` | 문서 | — |

## 2. 모듈별 상세

```text
foundation/
  types/        Types.hpp (정수 별칭), Error.hpp (Error, Expected<T>)          ← 구현
  math/         Vec2, Vec2i, Vec4, Rect, Mat3, 각도
  handle/       Handle<Tag> ← 구현, HandlePool
  container/    SmallVector, RingBuffer, SpscQueue
  log/          Log.hpp (레벨, 카테고리, 싱크)                                 ← 구현
  hash/         Fnv1a64 ← 구현, hashCombine
  time/         SteadyClock 래퍼 (시뮬레이션 사용 금지)
  io/           파일 읽기/쓰기, 원자적 저장
  job/          JobSystem (Phase 3 단순 큐 → Phase 15 work-stealing)
  metrics/      카운터·히스토그램 링 버퍼
  assert/       SBX_ASSERT, SBX_VERIFY                                        ← 구현

core/
  ecs/          EntityId, EntityManager, ComponentPool, Registry, View, EntityCommandBuffer,
                ComponentRegistry(SBX_COMPONENT), Reflection(Visitor 개념, Hint, FieldMeta), Opaque
  components/   core/ life/ ai/ society/ combat/ net/ persist/   (02-ECS 14장)
  simulation/   SimulationWorld, SimulationClock, SystemScheduler, ISystem, IntentBuffer, EventStream
  systems/      Sensor, Behavior, PathRequest, Movement, Interaction, ResolveIntents, Combat, Resource,
                Production, Lifecycle, Collision, CollectPathResults
  world/        ChunkCoord, Chunk, WorldGrid, TerrainLayers, SpatialIndex
  behavior/     BehaviorGraph, 노드 레지스트리 (conditions/, actions/)
  rules/        Rule, RuleIndex, effect op 레지스트리 (ops/)
  pathfinding/  PathfindingService, PathGridSnapshot, GridAStar
  content/      ContentDatabase, Prefab, TagTable, TerrainMaterial, 로더, Validator, ContentHash, Overlay
  command/      SimCommand, CommandQueue, CommandApplier
  serialization/ JsonVisitor, BinaryVisitor, BitWriter/BitReader, SaveGame, Migration
  replay/       ReplayWriter, ReplayReader, WorldHash
  random/       CounterRng, RandomService

network/
  transport/    INetworkTransport, EnetTransport, LoopbackTransport, SimulatedTransport
  protocol/     Messages, ProtocolVersion, MessageCodec
  session/      ClientSession, ServerSession, Handshake, Roles
  snapshot/     Snapshot, SnapshotBuffer
  replication/  ReplicationWriter, ReplicationReader, NetEntityMap
  interest/     InterestManager
  server/       ServerHost, CommandValidator, PersistenceService, ReplayRecorder

platform/
  common/       IWindow, WindowDesc, NativeWindowHandle, PlatformEvent, Key, InputSystem, ActionMap, IAudioBackend
  windows/      Win32Window, Win32Input           (WIN32 전용)
  linux/        X11Window, WaylandWindow          (Linux 전용)
  macos/        CocoaWindow.mm                    (APPLE 전용)
  audio/        NullAudioBackend, MiniaudioBackend

render/
  rhi/          IRenderDevice, ICommandList, ICommandQueue, ISwapChain, Desc, Caps, Handles, Formats
  dx12/         D3D12Device, D3D12CommandList, D3D12SwapChain, D3D12Descriptor, D3D12Pipeline …  (WIN32)
  vulkan/       VkDevice…  (Linux, Windows 옵션)
  metal/        MetalDevice.hpp + *.mm  (APPLE)
  renderer/     Renderer, RenderWorld, RenderQueue, Passes/, SpriteBatcher, Camera2D, DebugDraw, ImGuiRenderer
  shader/       ShaderLibrary, ReflectionLoader
  asset/        AssetManager, TextureAsset, MaterialAsset, FontAsset, UploadQueue
  generated/    (빌드 산출: 셰이더 상수 버퍼 헤더) — 저장소에 없음

editor/
  EditorContext, ICommandSink
  panels/ tools/ selection/ commands/(UndoStack) inspector/(ImGuiInspector)

apps/client/
  main.cpp, Application, LocalServerHost
  presentation/ InterpolationSystem, ExtractionSystem, AudioExtraction, components/(render.*, client.*, editor.*)
apps/server/
  main.cpp (인자 파싱 → ServerHost)
```

## 3. 진입점

| 실행 파일 | 소스 | 용도 |
|---|---|---|
| `SandboxClient` | `apps/client/main.cpp` | 에디터·관찰·플레이, 싱글플레이 내장 서버 |
| `SandboxServer` | `apps/server/main.cpp` | 헤드리스 전용 서버 |
| `SandboxTests` | `tests/main.cpp` (doctest) | 단위·속성·시나리오·네트워크 |
| `sbx_sim_check` | `tools/sim_check/main.cpp` | 결정론 하네스, 콘텐츠 검증 |
| `sbx_bench` | `bench/main.cpp` | 벤치마크 |
| `sbx_render_tests` | `tests/render/main.cpp` | 기준 이미지 |

## 4. "이 기능은 어디에?"

| 질문 | 위치 |
|---|---|
| 틱 순서 | `core/simulation/SystemScheduler.cpp` + [03](03-SIMULATION.md) 2장 |
| 명령 적용 | `core/command/CommandApplier.cpp` |
| 명령 권한 검사 | `network/server/CommandValidator.cpp` |
| 해시 | `core/replay/WorldHash.cpp` |
| 세이브 포맷 | `core/serialization/SaveGame.cpp` + [09](09-SERIALIZATION.md) |
| 와이어 메시지 | `network/protocol/Messages.hpp` + [08](08-NETWORK.md) 5장 |
| 스프라이트가 화면에 가는 길 | `apps/client/presentation/ExtractionSystem.cpp` → `render/renderer/SpriteBatcher.cpp` |
| D3D12 디바이스 생성 | `render/dx12/D3D12Device.cpp` |
| 창 메시지 처리 | `platform/windows/Win32Window.cpp` |
| 콘텐츠 검증 규칙 | `core/content/Validator.cpp` + [11](11-CONTENT-SCHEMA.md) 8장 |
