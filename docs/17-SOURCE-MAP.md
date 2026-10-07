# 17. 소스 지도

> **참조 문서.** "이 코드가 어디 있지?"에 답합니다. 모듈·폴더·진입점·타깃을 추가/이동/삭제하면 같은 커밋에서 갱신합니다.
> 상태: Phase 7A (2026-10-06). **0장이 실제로 존재하는 파일**이고, 1장 이후는 전체 계획 구조입니다.

---

## 0. 현재 존재하는 것 (Phase 7A)

| 경로                                                                                              | 내용                                                                                                                                                                | 타깃                      |
|---------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------|
| `CMakeLists.txt`                                                                                  | 루트 빌드, 모듈 추가, 경계 규칙 선언                                                                                                                                | —                         |
| `CMakePresets.json`                                                                               | 7개 configure 프리셋 + build/test 프리셋 ([15-BUILD](15-BUILD.md) 2장)                                                                                              | —                         |
| `cmake/SbxOptions.cmake`                                                                          | `SBX_*` 옵션                                                                                                                                                        | —                         |
| `cmake/SbxCompilerSettings.cmake`                                                                 | `sbx_warnings`, `sbx_strict_conversions`, `sbx_simulation_flags`, 표준 라이브러리 기능 확인                                                                         | —                         |
| `cmake/SbxBoundaries.cmake`                                                                       | `sbx_forbid_link()`, `sbx_check_link_boundaries()`                                                                                                                  | —                         |
| `cmake/SbxBoundarySelftest.cmake`                                                                 | 경계 검사 실패 경로 시험 (내부 옵션)                                                                                                                                | —                         |
| `cmake/SbxBuildInfo.cmake`, `cmake/BuildInfo.hpp.in`                                              | `build/<preset>/generated/foundation/BuildInfo.hpp` 생성 (버전, 커밋, 컴파일러, 툴체인 키)                                                                          | —                         |
| `cmake/SbxShaders.cmake`                                                                          | DXC 고정 버전 내려받기(`SBX_DXC`), `SBX_BUILD_SHADERS`, `sbx_add_shader()` (ADR-0019)                                                                               | —                         |
| `foundation/types/Types.hpp`                                                                      | 정수·실수 별칭                                                                                                                                                      | SandboxFoundation         |
| `foundation/types/Error.{hpp,cpp}`                                                                | `ErrorCode`, `Error`, `Expected<T>`, `makeError`                                                                                                                    | SandboxFoundation         |
| `foundation/assert/Assert.{hpp,cpp}`                                                              | `SBX_ASSERT`, `SBX_VERIFY`, `setAssertHandler`                                                                                                                      | SandboxFoundation         |
| `foundation/log/Log.{hpp,cpp}`                                                                    | `sbx::log::{trace,debug,info,warn,error}`, 레벨, 싱크                                                                                                               | SandboxFoundation         |
| `foundation/hash/Fnv1a.hpp`                                                                       | `Fnv1a64`, `fnv1a64()` — **동결**                                                                                                                                   | SandboxFoundation         |
| `foundation/handle/Handle.hpp`                                                                    | `Handle<Tag>`                                                                                                                                                       | SandboxFoundation         |
| `foundation/container/SmallVector.hpp`                                                            | 인라인 저장 벡터                                                                                                                                                    | SandboxFoundation         |
| `foundation/container/FixedString.hpp`                                                            | 고정 용량 문자열, `ContentId`                                                                                                                                       | SandboxFoundation         |
| `foundation/math/Vec2.hpp`                                                                        | `Vec2`(float), `Vec2i`(int32)                                                                                                                                       | SandboxFoundation         |
| `foundation/io/FileIo.{hpp,cpp}`                                                                  | `readFile`, `writeFileAtomic`, `createDirectories`, `replaceDirectory`                                                                                              | SandboxFoundation         |
| `foundation/io/Console.{hpp,cpp}`                                                                 | `console::useUtf8Output` — Windows 콘솔 출력을 UTF-8 로 (도구·서버 시작 때)                                                                                         | SandboxFoundation         |
| `foundation/job/JobSystem.{hpp,cpp}`                                                              | `JobSystem`(Worker 풀, 0 = 그 자리 실행), `JobGroup` — 경로 Job (5B)                                                                                                | SandboxFoundation         |
| `foundation/text/Utf8.hpp`                                                                        | UTF-8 인코딩·디코딩 (`append`, `decodeNext`, `length`, `tail`) (6)                                                                                                  | SandboxFoundation         |
| `core/ecs/EntityId.hpp`, `EntityManager.{hpp,cpp}`                                                | 엔티티 핸들·할당                                                                                                                                                    | SandboxCore               |
| `core/ecs/Component.{hpp,cpp}`                                                                    | `SBX_COMPONENT`, `ComponentTraits`, Flags, `componentTypeId<T>()`                                                                                                   | SandboxCore               |
| `core/ecs/Reflection.hpp`                                                                         | `Hint`, `FieldMeta`, `Reflectable`, `visitConst`                                                                                                                    | SandboxCore               |
| `core/ecs/ComponentPool.{hpp,cpp}`                                                                | sparse set 풀, `validate()`                                                                                                                                         | SandboxCore               |
| `core/ecs/View.hpp`                                                                               | `Read/Write/Exclude`, `BasicView`                                                                                                                                   | SandboxCore               |
| `core/ecs/Registry.{hpp,cpp}`                                                                     | Registry, `StructuralLockGuard`, 리소스                                                                                                                             | SandboxCore               |
| `core/ecs/EntityCommandBuffer.{hpp,cpp}`                                                          | ECB                                                                                                                                                                 | SandboxCore               |
| `core/ecs/ComponentCatalog.{hpp,cpp}`                                                             | 이름/stableId → 타입 소거 연산 (ADR-0011)                                                                                                                           | SandboxCore               |
| `core/serialization/FieldCodec.hpp`                                                               | 리플렉션 지원 필드 타입 목록                                                                                                                                        | SandboxCore               |
| `core/serialization/JsonVisitor.hpp`                                                              | `componentToJson`, `componentFromJson`                                                                                                                              | SandboxCore               |
| `core/serialization/HashVisitor.hpp`                                                              | `hashComponent` (H1 정수화)                                                                                                                                         | SandboxCore               |
| `core/serialization/FieldAccess.hpp`                                                              | 이름으로 필드 접근 (수치 읽기/쓰기, 필드 목록)                                                                                                                      | SandboxCore               |
| `core/components/core/{Transform,Velocity,Lifetime,Movement}.hpp`                                 | `core.transform`, `core.velocity`, `core.lifetime`, `core.movement`·`core.collider`(5B)                                                                             | SandboxCore               |
| `core/components/core/Identity.hpp`                                                               | `SaveId`, `NetEntityId`, `persist.persistence`, `net.identity`                                                                                                      | SandboxCore               |
| `core/components/life/Age.hpp`, `core/components/debug/RandomWalk.hpp`                            | `life.age`, `debug.random_walk`                                                                                                                                     | SandboxCore               |
| `core/components/ai/Ai.hpp`                                                                       | `ai.sensor`·`ai.behavior`·`ai.path` (5B, 틱 캐시 필드 포함)                                                                                                         | SandboxCore               |
| `core/components/RegisterCoreComponents.{hpp,cpp}`                                                | 엔진 컴포넌트 등록 (18종)                                                                                                                                           | SandboxCore               |
| `core/simulation/SimConstants.{hpp,cpp}`                                                          | `kTickRate`, `kFixedDt`, `Tick`, `isValidSnapshotRate`                                                                                                              | SandboxCore               |
| `core/simulation/SimVersion.hpp`                                                                  | `kSimVersion` (+ 이력)                                                                                                                                              | SandboxCore               |
| `core/simulation/SimulationClock.hpp`                                                             | tick · paused · speed · Step · editSequence                                                                                                                         | SandboxCore               |
| `core/simulation/SimulationWorld.{hpp,cpp}`                                                       | 틱 파이프라인, 명령 적용기, 정체성 부여                                                                                                                             | SandboxCore               |
| `core/simulation/System.hpp`, `SystemScheduler.{hpp,cpp}`                                         | `Stage`, `ISystem`, `SystemContext`, `ISystemProfiler`, 실행기                                                                                                      | SandboxCore               |
| `core/simulation/EventStream.hpp`                                                                 | 틱 이벤트                                                                                                                                                           | SandboxCore               |
| `core/simulation/Intent.hpp`                                                                      | `Intent`, `IntentBuffer`(Stage 10→11), `SaveIndex`(saveId → EntityId) (5B)                                                                                          | SandboxCore               |
| `core/systems/{RandomWalk,Movement,Lifecycle}System.{hpp,cpp}`, `DefaultSystems.cpp`              | 기본 System 과 등록 (Movement 조향 5B)                                                                                                                              | SandboxCore               |
| `core/systems/AiSystems.hpp`, `AiCommon.hpp`                                                      | 5B System 선언(PathCollect·Sensor·Behavior·PathRequest·Interaction·ResolveIntents·Collision), 공용 도우미                                                           | SandboxCore               |
| `core/systems/{Sensor,Behavior,Collision}System.cpp`, `PathSystems.cpp`, `InteractionSystems.cpp` | 5B System 구현                                                                                                                                                      | SandboxCore               |
| `core/path/PathGrid.{hpp,cpp}`                                                                    | `PathGridSnapshot`(불변 이동 비용 배열), `tileLineClear` (5B)                                                                                                       | SandboxCore               |
| `core/path/Pathfinder.{hpp,cpp}`                                                                  | 격자 A* (`findPath`) (5B)                                                                                                                                           | SandboxCore               |
| `core/path/PathfindingService.{hpp,cpp}`                                                          | 경로 Job 제출·수거 (Job 계약 T→T+1) (5B)                                                                                                                            | SandboxCore               |
| `core/command/SimCommand.hpp`, `CommandQueue.{hpp,cpp}`                                           | 명령 variant, `CommandResult`, 정렬 큐                                                                                                                              | SandboxCore               |
| `core/command/CommandJson.{hpp,cpp}`                                                              | 명령 페이로드 ↔ JSON (엔티티 참조 변환 함수를 받는다) — 리플레이 (5C)                                                                                               | SandboxCore               |
| `core/random/{CounterRng,RandomService}.hpp`                                                      | 카운터 난수 — **동결**, 목적별 스트림                                                                                                                               | SandboxCore               |
| `core/world/ChunkCoord.hpp`, `Terrain.hpp`                                                        | 좌표 변환·상수, 지형 레이어·플래그                                                                                                                                  | SandboxCore               |
| `core/world/WorldGrid.{hpp,cpp}`                                                                  | `GridBounds`, `Chunk`(지형·revision·해시 캐시), `WorldGrid`(paint, 경계)                                                                                            | SandboxCore               |
| `core/world/SpatialIndex.{hpp,cpp}`                                                               | 경계 안 격자 공간 색인 (카운팅 정렬, 청크 질의)                                                                                                                     | SandboxCore               |
| `core/content/ContentDatabase.{hpp,cpp}`                                                          | 콘텐츠 조회: 머티리얼·태그·Prefab·Rule·Behavior (불변)                                                                                                              | SandboxCore               |
| `core/content/ContentLoader.{hpp,cpp}`                                                            | 팩 로더 + 검증기 V1~V7 + contentHash                                                                                                                                | SandboxCore               |
| `core/content/ContentModel.hpp`, `TagSet.hpp`                                                     | Prefab · Rule · BehaviorGraph 모델, TagSet · TagExpr                                                                                                                | SandboxCore               |
| `core/persist/WorldSave.{hpp,cpp}`                                                                | `saveWorld`, `loadWorld` (world.json · entities.jsonl · chunks/)                                                                                                    | SandboxCore               |
| `core/persist/ChunkFile.{hpp,cpp}`, `Migration.{hpp,cpp}`                                         | 청크 바이너리 포맷, `MigrationRegistry`                                                                                                                             | SandboxCore               |
| `core/replay/WorldHash.{hpp,cpp}`                                                                 | WorldHash, 엔티티별 해시, 진단용 JSON                                                                                                                               | SandboxCore               |
| `core/replay/Replay.{hpp,cpp}`                                                                    | 리플레이 기록(`ReplayRecorder`)·파일(JSON Lines)·재생(`playReplay`) — D3 (5C)                                                                                       | SandboxCore               |
| `core/scenarios/Scenario.{hpp,cpp}`, `RandomWalkScenario.{hpp,cpp}`                               | `IScenario`, `ScenarioRunner`, `random_walk_1k/10k`                                                                                                                 | SandboxCore               |
| `core/scenarios/EcosystemScenario.{hpp,cpp}`                                                      | `ecosystem_small` · `ecosystem_survival` · `ecosystem_10k` (5C)                                                                                                     | SandboxCore               |
| `platform/CMakeLists.txt`                                                                         | SandboxPlatform — OS 별 소스 선택 (WIN32: `windows/`, 그 밖: `stub/`)                                                                                               | SandboxPlatform           |
| `platform/common/Key.{hpp,cpp}`                                                                   | `Key`(물리 위치), `MouseButton`, `Modifiers`, 이름 표 (ADR-0017)                                                                                                    | SandboxPlatform           |
| `platform/common/PlatformEvent.hpp`                                                               | `PlatformEvent` variant, `PlatformEventQueue`, `Extent2D`                                                                                                           | SandboxPlatform           |
| `platform/common/Window.hpp`                                                                      | `IWindow`, `WindowDesc`, `CursorShape`, `NativeWindowHandle`, `createWindow`                                                                                        | SandboxPlatform           |
| `platform/common/HeadlessWindow.{hpp,cpp}`                                                        | OS 창 없는 IWindow (`inject`) — `--headless`·테스트                                                                                                                 | SandboxPlatform           |
| `platform/common/InputSystem.{hpp,cpp}`                                                           | `InputState`, `InputSystem` (I1 `setCapture`/`downstream`, I3, I4)                                                                                                  | SandboxPlatform           |
| `platform/common/ActionMap.{hpp,cpp}`                                                             | `settings/input.json` 파싱, `Binding`, `ActionMap`, `ActionState`, 충돌 검출                                                                                        | SandboxPlatform           |
| `platform/common/Gamepad.hpp`                                                                     | `IGamepadSource`, `GamepadState` — 인터페이스만 (I5)                                                                                                                | SandboxPlatform           |
| `platform/common/Audio.hpp`, `platform/audio/NullAudioBackend.{hpp,cpp}`                          | `IAudioBackend`, 세대 핸들, Null 구현                                                                                                                               | SandboxPlatform           |
| `platform/common/Platform.hpp`, `FramePacer.cpp`                                                  | `attachConsole`, `windowBackendName`, `PreciseSleeper`, `FramePacer`                                                                                                | SandboxPlatform           |
| `platform/windows/Win32Window.cpp`, `Win32Platform.cpp`, `Win32Common.hpp`                        | Win32 창·메시지 처리·DPI·Raw Input·IME·클립보드, 콘솔·고해상도 타이머 (WIN32 전용)                                                                                  | SandboxPlatform           |
| `platform/windows/Win32KeyMap.{hpp,cpp}`                                                          | Win32 스캔 코드 → `Key` 표 (windows.h 없음 — 모든 OS 에서 컴파일·테스트)                                                                                            | SandboxPlatform           |
| `platform/stub/StubPlatform.cpp`                                                                  | 창이 아직 없는 OS: `createWindow` = Unsupported (Linux Phase 13, macOS Phase 14)                                                                                    | SandboxPlatform           |
| `render/CMakeLists.txt`                                                                           | SandboxRender — OS 별 백엔드 선택 (WIN32: `dx12/`, 그 밖: `stub/`), stb 구현 OBJECT, 내장 셰이더(`sbx_add_shader`)                                                  | SandboxRender             |
| `render/rhi/RhiTypes.{hpp,cpp}`                                                                   | 핸들(버퍼·텍스처·샘플러·셰이더·파이프라인·바인드 그룹), 열거, Desc·Caps·Stats, 포맷 표, 파이프라인·바인딩 Desc, `layoutFromReflection`                              | SandboxRender             |
| `render/rhi/RenderDevice.hpp`                                                                     | `IRenderDevice`·`ICommandList`·`ICommandQueue`·`ISwapChain`, `createRenderDevice` (ADR-0006·0018)                                                                   | SandboxRender             |
| `render/rhi/HandlePool.hpp`, `DeferredDestruction.hpp`, `UploadRing.{hpp,cpp}`                    | 백엔드 독립 로직: 세대 핸들 풀, 펜스 지연 해제 큐, 업로드 링 구간 관리                                                                                              | SandboxRender             |
| `render/rhi/ShaderTypes.hpp`                                                                      | 셰이더 단계·바인딩 종류·`ShaderReflection`·`ShaderBytecode` (생성 헤더가 쓰는 값 타입, 7B)                                                                          | SandboxRender             |
| `render/rhi/RangeAllocator.hpp`, `PipelineValidation.{hpp,cpp}`                                   | 디스크립터 구간 할당기, 레이아웃·파이프라인·바인드 그룹 검사 (백엔드 독립, 7B)                                                                                      | SandboxRender             |
| `render/dx12/Dx12Common.{hpp,cpp}`                                                                | `Com<T>`, HRESULT 문장, Format·상태 매핑 (d3d12.h 는 render/dx12 안에서만)                                                                                          | SandboxRender (WIN32)     |
| `render/dx12/Dx12Device.{hpp,cpp}`                                                                | D3D12 디바이스·어댑터 선택·큐/펜스·리소스·RTV·업로드 링·프레임·Debug Layer·DRED                                                                                     | SandboxRender (WIN32)     |
| `render/dx12/Dx12CommandList.cpp`, `Dx12SwapChain.cpp`                                            | 커맨드 리스트(배리어·Clear·복사·마커 + 7B 파이프라인·바인드 그룹·push·정점/인덱스·draw), 플립 모델 스왑체인                                                         | SandboxRender (WIN32)     |
| `render/dx12/Dx12Pipeline.cpp`                                                                    | 셰이더·샘플러·shader-visible 힙·바인드 그룹·루트 시그니처 캐시·PSO (7B, ADR-0019)                                                                                   | SandboxRender (WIN32)     |
| `render/stub/StubRenderDevice.cpp`                                                                | 백엔드가 없는 OS: `createRenderDevice` = Unsupported (Linux Phase 13, macOS Phase 14)                                                                               | SandboxRender             |
| `render/asset/Image.{hpp,cpp}`, `StbImpl.cpp`                                                     | RGBA8 이미지, PNG 읽기·쓰기(stb), 비교·차이 그림                                                                                                                    | SandboxRender             |
| `apps/client/FrameRenderer.hpp`, `ClearRenderer.cpp`                                              | `IFrameRenderer`(Application ↔ 렌더러 경계), Clear 렌더러 (7A) + 도는 삼각형 데모 (7B, `SBX_HAS_SHADERS`)                                                           | SandboxClient             |
| `tests/render/main.cpp`, `RenderTestEnv.hpp`, `test_rhi_basic.cpp`, `test_rhi_draw.cpp`           | `sbx_render_tests` — 공유 디바이스, 읽기·기준 이미지 비교, 7A 케이스, 7B 그리기 케이스(셰이더가 내장된 빌드)                                                        | sbx_render_tests (WIN32)  |
| `tests/render/references/*.png`                                                                   | GPU 기준 이미지 (7A clear · upload_quadrants · region_copy, 7B triangle · coord_convention · culling_ccw · texture_linear)                                          | —                         |
| `tools/wine/`                                                                                     | Wine(vkd3d)+lavapipe 실행 래퍼 `run.sh`(`WINE` 로 Wine 11 지정), 시험용 Vulkan 레이어 (ADR-0018), Windows dxc.exe 래퍼 `dxc.sh` (ADR-0019) — 빌드에 들어가지 않는다 | —                         |
| `shaders/basic_color.hlsl`, `basic_texture.hlsl`, `common/Common.hlsli`                           | HLSL 원본 — 정점 색 + tint + push 변환, 텍스처 사각형, 공통 함수 (7B)                                                                                               | SandboxRender (빌드 단계) |
| `tools/shader/sbx_shader_gen.py`                                                                  | SPIR-V 리플렉션 + DXIL 서명 검사 → `<Pascal>Shader.{hpp,cpp}` (내장 바이트코드·cbuffer 구조체) + `.reflect.json` (7B)                                               | 빌드 단계                 |
| `external/stb/`                                                                                   | stb_image v2.30 · stb_image_write v1.16                                                                                                                             | sbx_stb                   |
| `apps/client/main.cpp`                                                                            | 클라이언트 진입점 (Windows: GUI 서브시스템 + main)                                                                                                                  | SandboxClient             |
| `apps/client/Application.{hpp,cpp}`                                                               | 앱 상태기계(`AppState`, 전이 표), 프레임 루프, 제목 줄 입력 모니터                                                                                                  | SandboxClient             |
| `apps/client/ClientOptions.{hpp,cpp}`, `DefaultInput.{hpp,cpp}`                                   | 명령줄 파싱, 기본 키 바인딩 JSON                                                                                                                                    | SandboxClient             |
| `apps/client/SandboxClient.manifest`                                                              | Per-Monitor DPI v2 매니페스트 (MSVC)                                                                                                                                | SandboxClient             |
| `apps/server/main.cpp`                                                                            | 서버 진입점 (`--scenario` 헤드리스 실행)                                                                                                                            | SandboxServer             |
| `apps/server/ServerOptions.{hpp,cpp}`                                                             | 명령줄 파싱                                                                                                                                                         | SandboxServer (+ Tests)   |
| `tools/sim_check/main.cpp`, `SimCheckOptions.{hpp,cpp}`                                           | 결정론 하네스                                                                                                                                                       | sbx_sim_check (+ Tests)   |
| `tests/golden/{random_walk_1k,world_save_load,eco_lifecycle,ecosystem_small}.json`                | 골든 해시 (툴체인별, ADR-0012)                                                                                                                                      | 데이터                    |
| `tests/data/saves/v1_sample/`                                                                     | 커밋된 샘플 세이브 (고치지 않는다)                                                                                                                                  | SandboxTests (`persist`)  |
| `content/ecosystem/`                                                                              | 콘텐츠 팩 \"eco\" (tags · terrain · prefabs · rules · behaviors)                                                                                                    | 데이터                    |
| `tests/main.cpp`, `tests/unit/**`                                                                 | doctest 단위 테스트 (스위트: foundation, core, ecs, persist, content, server, tools, platform, client)                                                              | SandboxTests              |
| `tests/unit/ecs/TestComponents.hpp`                                                               | 테스트 전용 컴포넌트 (`test.*`)                                                                                                                                     | SandboxTests              |
| `tests/property/**`                                                                               | 속성 테스트 (스위트: property)                                                                                                                                      | SandboxTests              |
| `bench/main.cpp`, `bench/{Ecs,Sim}Bench.cpp`, `bench/BenchUtil.hpp`                               | `sbx_bench`                                                                                                                                                         | sbx_bench                 |
| `bench/baselines/*.json`                                                                          | 머신별 벤치 기준선                                                                                                                                                  | —                         |
| `external/CMakeLists.txt`                                                                         | `sbx_nlohmann_json`, `sbx_doctest` 타깃                                                                                                                             | —                         |
| `external/nlohmann_json/`                                                                         | nlohmann/json v3.12.0                                                                                                                                               | —                         |
| `tests/arch/fixtures/**`                                                                          | include 린터 자체 시험용 위반 파일 (**컴파일 안 함**)                                                                                                               | —                         |
| `tools/check_includes.py`                                                                         | include 경계 린터                                                                                                                                                   | CTest `arch`              |
| `external/doctest/`                                                                               | doctest v2.5.0                                                                                                                                                      | —                         |
| `.github/workflows/ci.yml`                                                                        | CI (Windows MSVC, Linux clang/gcc/asan, macOS)                                                                                                                      | —                         |
| `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`                   | 포맷·린트·줄끝 규칙                                                                                                                                                 | —                         |

빌드 산출물: `build/<preset>/bin/<Config>/SandboxClient(.exe)`, `SandboxServer(.exe)`, `SandboxTests(.exe)`, `sbx_render_tests.exe`(Windows), `sbx_sim_check(.exe)`, `sbx_bench(.exe)`.

## 1. 최상위

| 경로                                            | 내용                                                                                           | 타깃                           |
|-------------------------------------------------|------------------------------------------------------------------------------------------------|--------------------------------|
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | 빌드 정의, 프리셋, 함수(`sbx_add_shader`, `sbx_check_link_boundaries`, `sbx_simulation_flags`) | —                              |
| `external/`                                     | vendored 서드파티 (LICENSE 보존)                                                               | —                              |
| `foundation/`                                   | 기본 타입·수학·핸들·컨테이너·로그·해시·시간·IO·Job·메트릭                                      | SandboxFoundation              |
| `core/`                                         | ECS·시뮬레이션·월드·콘텐츠·명령·직렬화·리플레이·난수                                           | SandboxCore                    |
| `network/`                                      | Transport·Protocol·Session·Snapshot·Replication·Interest·ServerHost                            | SandboxNetwork                 |
| `platform/`                                     | 창·입력·오디오                                                                                 | SandboxPlatform                |
| `render/`                                       | RHI·백엔드·Renderer·Shader·Asset·ImGui 렌더러                                                  | SandboxRender                  |
| `editor/`                                       | 패널·툴·선택·Inspector·편집 명령                                                               | SandboxEditor                  |
| `apps/client/`                                  | 클라이언트 진입점·앱 상태기계·Presentation·LocalServerHost                                     | SandboxClient                  |
| `apps/server/`                                  | 서버 진입점                                                                                    | SandboxServer                  |
| `shaders/`                                      | HLSL 원본 (canonical)                                                                          | SandboxRender (빌드 단계)      |
| `content/`                                      | 콘텐츠 팩 (JSON)                                                                               | 런타임 데이터                  |
| `assets/`                                       | 텍스처·폰트·사운드                                                                             | 런타임 데이터                  |
| `tests/`                                        | 단위·속성·시나리오·결정론·네트워크·렌더 테스트, 골든                                           | SandboxTests, sbx_render_tests |
| `bench/`                                        | 벤치 시나리오·머신별 기준선                                                                    | sbx_bench                      |
| `tools/`                                        | `check_includes.py`(구현), `sbx_sim_check`·`sbx_atlas`(계획)                                   | 도구                           |
| `docs/`                                         | 문서                                                                                           | —                              |

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
  job/          JobSystem (Phase 5 단순 큐 → Phase 15 work-stealing)
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
  behavior/     BehaviorGraph, 노드 레지스트리 (conditions/, actions/)   ← 5B: 모델은 content/ContentModel, 실행은 systems/BehaviorSystem
  rules/        Rule, RuleIndex, effect op 레지스트리 (ops/)            ← 5B: 색인은 ContentDatabase::rulesForAction, 실행은 systems/InteractionSystems
  pathfinding/  PathfindingService, PathGridSnapshot, GridAStar      ← Phase 5B 구현은 core/path/ (PathGrid · Pathfinder · PathfindingService)
  content/      ContentDatabase, Prefab, TagTable, TerrainMaterial, 로더, Validator, ContentHash, Overlay
  command/      SimCommand, CommandQueue, CommandApplier
  serialization/ JsonVisitor, HashVisitor, BinaryVisitor, BitWriter/BitReader
  persist/      WorldSave(세이브·로드), ChunkFile, Migration              ← Phase 4 구현 (초안의 serialization/SaveGame 자리)
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
  common/       IWindow, WindowDesc, NativeWindowHandle, PlatformEvent, Key, InputSystem, ActionMap, IAudioBackend,
                HeadlessWindow, Gamepad, Platform(FramePacer · attachConsole)
  windows/      Win32Window, Win32Platform, Win32KeyMap(순수 표 — 모든 OS 에서 테스트)   (나머지는 WIN32 전용)
  stub/         StubPlatform                      (창이 아직 없는 OS — Phase 13·14 에서 linux/ · macos/ 로 대체)
  linux/        X11Window, WaylandWindow          (Linux 전용)
  macos/        CocoaWindow.mm                    (APPLE 전용)
  audio/        NullAudioBackend, MiniaudioBackend

render/
  rhi/          IRenderDevice, ICommandList, ICommandQueue, ISwapChain, RhiTypes(Desc·Caps·Handles·Formats), HandlePool,
                DeferredDestruction, UploadRing, ShaderTypes · RangeAllocator · PipelineValidation (7B)
  dx12/         Dx12Device, Dx12CommandList, Dx12SwapChain, Dx12Common (7A) · Dx12Pipeline (7B)  (WIN32)
  stub/         StubRenderDevice  (백엔드가 아직 없는 OS)
  vulkan/       VkDevice…  (Linux, Windows 옵션)
  metal/        MetalDevice.hpp + *.mm  (APPLE)
  renderer/     Renderer, RenderWorld, RenderQueue, Passes/, SpriteBatcher, Camera2D, DebugDraw, ImGuiRenderer
  shader/       ShaderLibrary  [계획] (7B 는 생성 헤더가 바이트코드·리플렉션을 직접 내놓는다 — ADR-0019)
  asset/        Image(7A: PNG·비교) · AssetManager, TextureAsset, MaterialAsset, FontAsset, UploadQueue (8)
  generated/    (빌드 산출 <빌드>/generated/render/generated/: <Pascal>Shader.{hpp,cpp} · *.reflect.json) — 저장소에 없음

editor/
  EditorContext, ICommandSink
  panels/ tools/ selection/ commands/(UndoStack) inspector/(ImGuiInspector)

apps/client/
  main.cpp, Application, ClientOptions, DefaultInput, SandboxClient.manifest, LocalServerHost([계획] Phase 10)
  presentation/ InterpolationSystem, ExtractionSystem, AudioExtraction, components/(render.*, client.*, editor.*)
apps/server/
  main.cpp (인자 파싱 → ServerHost)
```

## 3. 진입점

| 실행 파일          | 소스                       | 용도                                     |
|--------------------|----------------------------|------------------------------------------|
| `SandboxClient`    | `apps/client/main.cpp`     | 에디터·관찰·플레이, 싱글플레이 내장 서버 |
| `SandboxServer`    | `apps/server/main.cpp`     | 헤드리스 전용 서버                       |
| `SandboxTests`     | `tests/main.cpp` (doctest) | 단위·속성·시나리오·네트워크              |
| `sbx_sim_check`    | `tools/sim_check/main.cpp` | 결정론 하네스, 콘텐츠 검증               |
| `sbx_bench`        | `bench/main.cpp`           | 벤치마크                                 |
| `sbx_render_tests` | `tests/render/main.cpp`    | 기준 이미지                              |

## 4. "이 기능은 어디에?"

| 질문                        | 위치                                                                                                            |
|-----------------------------|-----------------------------------------------------------------------------------------------------------------|
| 틱 순서                     | `core/simulation/SimulationWorld.cpp` (`tick`) + `core/systems/DefaultSystems.cpp` + [03](03-SIMULATION.md) 2장 |
| 명령 적용                   | `core/simulation/SimulationWorld.cpp` (`applyPayload`) — 커지면 `core/command/CommandApplier.cpp` 로 분리       |
| 명령 권한 검사              | `network/server/CommandValidator.cpp`                                                                           |
| 해시                        | `core/replay/WorldHash.cpp`                                                                                     |
| 세이브 포맷                 | `core/persist/WorldSave.cpp` + [09](09-SERIALIZATION.md) 3장                                                    |
| 와이어 메시지               | `network/protocol/Messages.hpp` + [08](08-NETWORK.md) 5장                                                       |
| 스프라이트가 화면에 가는 길 | `apps/client/presentation/ExtractionSystem.cpp` → `render/renderer/SpriteBatcher.cpp`                           |
| D3D12 디바이스 생성         | `render/dx12/D3D12Device.cpp`                                                                                   |
| 창 메시지 처리              | `platform/windows/Win32Window.cpp`                                                                              |
| 콘텐츠 검증 규칙            | `core/content/Validator.cpp` + [11](11-CONTENT-SCHEMA.md) 8장                                                   |
