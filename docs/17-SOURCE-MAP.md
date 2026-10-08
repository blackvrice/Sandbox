# 17. 소스 지도

> **참조 문서.** "이 코드가 어디 있지?"에 답합니다. 모듈·폴더·진입점·타깃을 추가/이동/삭제하면 같은 커밋에서 갱신합니다.
> 상태: Phase 9 (2026-10-08). **0장이 실제로 존재하는 파일**이고, 1장 이후는 전체 계획 구조입니다.

---

## 0. 현재 존재하는 것 (Phase 9)

| 경로                                                                                                        | 내용                                                                                                                                                                | 타깃                        |
|-------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------|
| `CMakeLists.txt`                                                                                            | 루트 빌드, 모듈 추가, 경계 규칙 선언                                                                                                                                | —                           |
| `CMakePresets.json`                                                                                         | 7개 configure 프리셋 + build/test 프리셋 ([15-BUILD](15-BUILD.md) 2장)                                                                                              | —                           |
| `cmake/SbxOptions.cmake`                                                                                    | `SBX_*` 옵션                                                                                                                                                        | —                           |
| `cmake/SbxCompilerSettings.cmake`                                                                           | `sbx_warnings`, `sbx_strict_conversions`, `sbx_simulation_flags`, 표준 라이브러리 기능 확인                                                                         | —                           |
| `cmake/SbxBoundaries.cmake`                                                                                 | `sbx_forbid_link()`, `sbx_check_link_boundaries()`                                                                                                                  | —                           |
| `cmake/SbxBoundarySelftest.cmake`                                                                           | 경계 검사 실패 경로 시험 (내부 옵션)                                                                                                                                | —                           |
| `cmake/SbxBuildInfo.cmake`, `cmake/BuildInfo.hpp.in`                                                        | `build/<preset>/generated/foundation/BuildInfo.hpp` 생성 (버전, 커밋, 컴파일러, 툴체인 키)                                                                          | —                           |
| `cmake/SbxShaders.cmake`                                                                                    | DXC 고정 버전 내려받기(`SBX_DXC`), `SBX_BUILD_SHADERS`, `sbx_add_shader()` (ADR-0019)                                                                               | —                           |
| `foundation/types/Types.hpp`                                                                                | 정수·실수 별칭                                                                                                                                                      | SandboxFoundation           |
| `foundation/types/Error.{hpp,cpp}`                                                                          | `ErrorCode`, `Error`, `Expected<T>`, `makeError`                                                                                                                    | SandboxFoundation           |
| `foundation/assert/Assert.{hpp,cpp}`                                                                        | `SBX_ASSERT`, `SBX_VERIFY`, `setAssertHandler`                                                                                                                      | SandboxFoundation           |
| `foundation/log/Log.{hpp,cpp}`                                                                              | `sbx::log::{trace,debug,info,warn,error}`, 레벨, 싱크                                                                                                               | SandboxFoundation           |
| `foundation/hash/Fnv1a.hpp`                                                                                 | `Fnv1a64`, `fnv1a64()` — **동결**                                                                                                                                   | SandboxFoundation           |
| `foundation/handle/Handle.hpp`                                                                              | `Handle<Tag>`                                                                                                                                                       | SandboxFoundation           |
| `foundation/container/SmallVector.hpp`                                                                      | 인라인 저장 벡터                                                                                                                                                    | SandboxFoundation           |
| `foundation/container/FixedString.hpp`                                                                      | 고정 용량 문자열, `ContentId`                                                                                                                                       | SandboxFoundation           |
| `foundation/math/Vec2.hpp`                                                                                  | `Vec2`(float), `Vec2i`(int32)                                                                                                                                       | SandboxFoundation           |
| `foundation/io/FileIo.{hpp,cpp}`                                                                            | `readFile`, `writeFileAtomic`, `createDirectories`, `replaceDirectory`                                                                                              | SandboxFoundation           |
| `foundation/io/Console.{hpp,cpp}`                                                                           | `console::useUtf8Output` · `utf8Arguments`(Windows 명령줄 → UTF-8, 10B) — 콘솔 출력 · 인자 인코딩                                                                   | SandboxFoundation           |
| `foundation/job/JobSystem.{hpp,cpp}`                                                                        | `JobSystem`(Worker 풀, 0 = 그 자리 실행), `JobGroup` — 경로 Job (5B)                                                                                                | SandboxFoundation           |
| `foundation/text/Utf8.hpp`                                                                                  | UTF-8 인코딩·디코딩 (`append`, `decodeNext`, `length`, `tail`) (6)                                                                                                  | SandboxFoundation           |
| `core/ecs/EntityId.hpp`, `EntityManager.{hpp,cpp}`                                                          | 엔티티 핸들·할당                                                                                                                                                    | SandboxCore                 |
| `core/ecs/Component.{hpp,cpp}`                                                                              | `SBX_COMPONENT`, `ComponentTraits`, Flags, `componentTypeId<T>()`                                                                                                   | SandboxCore                 |
| `core/ecs/Reflection.hpp`                                                                                   | `Hint`, `FieldMeta`, `Reflectable`, `visitConst`                                                                                                                    | SandboxCore                 |
| `core/ecs/ComponentPool.{hpp,cpp}`                                                                          | sparse set 풀, `validate()`                                                                                                                                         | SandboxCore                 |
| `core/ecs/View.hpp`                                                                                         | `Read/Write/Exclude`, `BasicView`                                                                                                                                   | SandboxCore                 |
| `core/ecs/Registry.{hpp,cpp}`                                                                               | Registry, `StructuralLockGuard`, 리소스                                                                                                                             | SandboxCore                 |
| `core/ecs/EntityCommandBuffer.{hpp,cpp}`                                                                    | ECB                                                                                                                                                                 | SandboxCore                 |
| `core/ecs/ComponentCatalog.{hpp,cpp}`                                                                       | 이름/stableId → 타입 소거 연산 (ADR-0011)                                                                                                                           | SandboxCore                 |
| `core/serialization/FieldCodec.hpp`                                                                         | 리플렉션 지원 필드 타입 목록                                                                                                                                        | SandboxCore                 |
| `core/serialization/JsonVisitor.hpp`                                                                        | `componentToJson`, `componentFromJson`                                                                                                                              | SandboxCore                 |
| `core/serialization/HashVisitor.hpp`                                                                        | `hashComponent` (H1 정수화)                                                                                                                                         | SandboxCore                 |
| `core/serialization/FieldAccess.hpp`                                                                        | 이름으로 필드 접근 (수치 읽기/쓰기, 필드 목록)                                                                                                                      | SandboxCore                 |
| `core/serialization/BinaryCodec.hpp`                                                                        | `componentToBinary`·`componentFromBinary`, `EntityRefCodec`, `ByteWriter`·`ByteReader` — 컴포넌트 값 와이어 바이트 (Phase 10A)                                      | SandboxCore                 |
| `core/components/core/{Transform,Velocity,Lifetime,Movement}.hpp`                                           | `core.transform`, `core.velocity`, `core.lifetime`, `core.movement`·`core.collider`(5B)                                                                             | SandboxCore                 |
| `core/components/core/Identity.hpp`                                                                         | `SaveId`, `NetEntityId`, `persist.persistence`, `net.identity`                                                                                                      | SandboxCore                 |
| `core/components/life/Age.hpp`, `core/components/debug/RandomWalk.hpp`                                      | `life.age`, `debug.random_walk`                                                                                                                                     | SandboxCore                 |
| `core/components/ai/Ai.hpp`                                                                                 | `ai.sensor`·`ai.behavior`·`ai.path` (5B, 틱 캐시 필드 포함)                                                                                                         | SandboxCore                 |
| `core/components/RegisterCoreComponents.{hpp,cpp}`                                                          | 엔진 컴포넌트 등록 (18종)                                                                                                                                           | SandboxCore                 |
| `core/simulation/SimConstants.{hpp,cpp}`                                                                    | `kTickRate`, `kFixedDt`, `Tick`, `isValidSnapshotRate`                                                                                                              | SandboxCore                 |
| `core/simulation/SimVersion.hpp`                                                                            | `kSimVersion` (+ 이력)                                                                                                                                              | SandboxCore                 |
| `core/simulation/SimulationClock.hpp`                                                                       | tick · paused · speed · Step · editSequence                                                                                                                         | SandboxCore                 |
| `core/simulation/SimulationWorld.{hpp,cpp}`                                                                 | 틱 파이프라인, 명령 적용기, 정체성 부여                                                                                                                             | SandboxCore                 |
| `core/simulation/System.hpp`, `SystemScheduler.{hpp,cpp}`                                                   | `Stage`, `ISystem`, `SystemContext`, `ISystemProfiler`, 실행기                                                                                                      | SandboxCore                 |
| `core/simulation/EventStream.hpp`                                                                           | 틱 이벤트                                                                                                                                                           | SandboxCore                 |
| `core/simulation/Intent.hpp`                                                                                | `Intent`, `IntentBuffer`(Stage 10→11), `SaveIndex`(saveId → EntityId) (5B)                                                                                          | SandboxCore                 |
| `core/systems/{RandomWalk,Movement,Lifecycle}System.{hpp,cpp}`, `DefaultSystems.cpp`                        | 기본 System 과 등록 (Movement 조향 5B)                                                                                                                              | SandboxCore                 |
| `core/systems/AiSystems.hpp`, `AiCommon.hpp`                                                                | 5B System 선언(PathCollect·Sensor·Behavior·PathRequest·Interaction·ResolveIntents·Collision), 공용 도우미                                                           | SandboxCore                 |
| `core/systems/{Sensor,Behavior,Collision}System.cpp`, `PathSystems.cpp`, `InteractionSystems.cpp`           | 5B System 구현                                                                                                                                                      | SandboxCore                 |
| `core/path/PathGrid.{hpp,cpp}`                                                                              | `PathGridSnapshot`(불변 이동 비용 배열), `tileLineClear` (5B)                                                                                                       | SandboxCore                 |
| `core/path/Pathfinder.{hpp,cpp}`                                                                            | 격자 A* (`findPath`) (5B)                                                                                                                                           | SandboxCore                 |
| `core/path/PathfindingService.{hpp,cpp}`                                                                    | 경로 Job 제출·수거 (Job 계약 T→T+1) (5B)                                                                                                                            | SandboxCore                 |
| `core/command/SimCommand.hpp`, `CommandQueue.{hpp,cpp}`                                                     | 명령 variant, `CommandResult`, 정렬 큐                                                                                                                              | SandboxCore                 |
| `core/command/CommandJson.{hpp,cpp}`                                                                        | 명령 페이로드 ↔ JSON (엔티티 참조 변환 함수를 받는다) — 리플레이 (5C)                                                                                               | SandboxCore                 |
| `core/random/{CounterRng,RandomService}.hpp`                                                                | 카운터 난수 — **동결**, 목적별 스트림                                                                                                                               | SandboxCore                 |
| `core/world/ChunkCoord.hpp`, `Terrain.hpp`                                                                  | 좌표 변환·상수, 지형 레이어·플래그                                                                                                                                  | SandboxCore                 |
| `core/world/WorldGrid.{hpp,cpp}`                                                                            | `GridBounds`, `Chunk`(지형·revision·해시 캐시), `WorldGrid`(paint, 경계)                                                                                            | SandboxCore                 |
| `core/world/SpatialIndex.{hpp,cpp}`                                                                         | 경계 안 격자 공간 색인 (카운팅 정렬, 청크 질의)                                                                                                                     | SandboxCore                 |
| `core/content/ContentDatabase.{hpp,cpp}`                                                                    | 콘텐츠 조회: 머티리얼·태그·Prefab·Rule·Behavior (불변)                                                                                                              | SandboxCore                 |
| `core/content/ContentLoader.{hpp,cpp}`                                                                      | 팩 로더 + 검증기 V1~V7 + contentHash                                                                                                                                | SandboxCore                 |
| `core/content/ContentModel.hpp`, `TagSet.hpp`                                                               | Prefab · Rule · BehaviorGraph 모델, TagSet · TagExpr                                                                                                                | SandboxCore                 |
| `core/persist/WorldSave.{hpp,cpp}`                                                                          | `saveWorld`, `loadWorld` (world.json · entities.jsonl · chunks/)                                                                                                    | SandboxCore                 |
| `core/persist/ChunkFile.{hpp,cpp}`, `Migration.{hpp,cpp}`                                                   | 청크 바이너리 포맷, `MigrationRegistry`                                                                                                                             | SandboxCore                 |
| `core/replay/WorldHash.{hpp,cpp}`                                                                           | WorldHash, 엔티티별 해시, 진단용 JSON                                                                                                                               | SandboxCore                 |
| `core/replay/Replay.{hpp,cpp}`                                                                              | 리플레이 기록(`ReplayRecorder`)·파일(JSON Lines)·재생(`playReplay`) — D3 (5C)                                                                                       | SandboxCore                 |
| `core/scenarios/Scenario.{hpp,cpp}`, `RandomWalkScenario.{hpp,cpp}`                                         | `IScenario`, `ScenarioRunner`, `random_walk_1k/10k`                                                                                                                 | SandboxCore                 |
| `core/scenarios/EcosystemScenario.{hpp,cpp}`                                                                | `ecosystem_small` · `ecosystem_survival` · `ecosystem_10k` (5C)                                                                                                     | SandboxCore                 |
| `core/scenarios/WorldSource.{hpp,cpp}`                                                                      | `openWorldSource` — 시나리오 이름 · 세이브 폴더 → 월드 (서버 · 클라이언트 --world, 10B)                                                                             | SandboxCore                 |
| `network/CMakeLists.txt`                                                                                    | SandboxNetwork — PUBLIC Core, PRIVATE sbx_enet (Phase 9, ADR-0024)                                                                                                  | SandboxNetwork              |
| `network/transport/Transport.{hpp,cpp}`                                                                     | `INetworkTransport`, `Endpoint`·`parseEndpoint`, `Channel`, `DisconnectReason`, `TransportEvent`·`TransportStats`                                                   | SandboxNetwork              |
| `network/transport/LoopbackTransport.{hpp,cpp}`, `SimulatedTransport.{hpp,cpp}`                             | 같은 프로세스 Transport (+ `LoopbackNetwork` 허브, 시험용 `severAll`), 지연·지터·손실·순서 흉내 (seed · 주입 시계)                                                  | SandboxNetwork              |
| `network/transport/EnetTransport.{hpp,cpp}`                                                                 | ENet UDP — 채널 3개, disconnect_later, 15 초 시간 초과. enet.h 는 이 .cpp 에만                                                                                      | SandboxNetwork              |
| `network/protocol/BitStream.{hpp,cpp}`                                                                      | `BitWriter`·`BitReader` — LSB 우선, LEB128 varint · zigzag, 상한 · UTF-8 검사, 오류 플래그                                                                          | SandboxNetwork              |
| `network/protocol/Messages.{hpp,cpp}`, `CommandCodec.{hpp,cpp}`                                             | 메시지 카탈로그 (`kProtocolVersion`, `Role`, `RejectReason`, encode/decode), 명령 페이로드 (`PayloadTag`)                                                           | SandboxNetwork              |
| `network/server/ServerHost.{hpp,cpp}`, `CommandValidator.{hpp,cpp}`                                         | 서버 (Net 절반 + Sim 절반, Inline · Threaded, 핸드셰이크 · 결과 · 통계), 순번 · 속도 제한 · 권한 표                                                                 | SandboxNetwork              |
| `network/server/LocalServerHost.{hpp,cpp}`                                                                  | 싱글플레이 서버 (10B, ADR-0026) — ServerHost + Loopback 둘, 역할 owner · 예산 없음, openWorldSource                                                                 | SandboxNetwork              |
| `network/client/ClientSession.{hpp,cpp}`                                                                    | 클라이언트 연결 — 핸드셰이크 · 명령 · 결과 · ServerStats · 복제 적용 · SnapshotAck (10A) · 선택 상세 (10B)                                                          | SandboxNetwork              |
| `network/client/ClientWorld.{hpp,cpp}`                                                                      | 복제 월드 (Phase 10A, ADR-0025) — Registry · WorldGrid · netId 표 · Opaque · transform 표본, Snapshot/TerrainChunk 적용 (시뮬레이션 없음)                           | SandboxNetwork              |
| `network/client/InterpolationClock.{hpp,cpp}`                                                               | 보간 시계 (10B, 08 9장) — 서버 틱 추정 · renderTick (지연 0.1 초 · 앞으로만 · 일시정지)                                                                             | SandboxNetwork              |
| `network/replication/ReplicationWriter.{hpp,cpp}`                                                           | 서버 복제 (10A) — 클라이언트별 기록 32 · 차분 · epoch · 지형 청크, 관심 영역 · 예산 우선순위 (11)                                                                   | SandboxNetwork              |
| `network/replication/Inspect.{hpp,cpp}`                                                                     | `buildInspect` — 선택한 개체의 서버 전용 상태 → InspectResult (10B)                                                                                                 | SandboxNetwork              |
| `editor/CMakeLists.txt`, `editor/EditorHost.hpp`                                                            | SandboxEditor (Core + Render + imgui, Network 금지). `ICommandSink` · `IEditorHost` — 에디터 ↔ 클라이언트 경계 (12A)                                                | SandboxEditor               |
| `editor/Editor.{hpp,cpp}`, `editor/ui/EditorPanel.{hpp,cpp}`                                                | 툴 다섯 (선택 · 이동 · 배치 · 지형 · 지우기) · EditPreview · 덧그림, "편집" 패널 (12A, ADR-0028)                                                                    | SandboxEditor               |
| `platform/CMakeLists.txt`                                                                                   | SandboxPlatform — OS 별 소스 선택 (WIN32: `windows/`, 그 밖: `stub/`)                                                                                               | SandboxPlatform             |
| `platform/common/Key.{hpp,cpp}`                                                                             | `Key`(물리 위치), `MouseButton`, `Modifiers`, 이름 표 (ADR-0017)                                                                                                    | SandboxPlatform             |
| `platform/common/PlatformEvent.hpp`                                                                         | `PlatformEvent` variant, `PlatformEventQueue`, `Extent2D`                                                                                                           | SandboxPlatform             |
| `platform/common/Window.hpp`                                                                                | `IWindow`, `WindowDesc`, `CursorShape`, `NativeWindowHandle`, `createWindow`                                                                                        | SandboxPlatform             |
| `platform/common/HeadlessWindow.{hpp,cpp}`                                                                  | OS 창 없는 IWindow (`inject`) — `--headless`·테스트                                                                                                                 | SandboxPlatform             |
| `platform/common/InputSystem.{hpp,cpp}`                                                                     | `InputState`, `InputSystem` (I1 `setCapture`/`downstream`, I3, I4)                                                                                                  | SandboxPlatform             |
| `platform/common/ActionMap.{hpp,cpp}`                                                                       | `settings/input.json` 파싱, `Binding`, `ActionMap`, `ActionState`, 충돌 검출                                                                                        | SandboxPlatform             |
| `platform/common/Gamepad.hpp`                                                                               | `IGamepadSource`, `GamepadState` — 인터페이스만 (I5)                                                                                                                | SandboxPlatform             |
| `platform/common/Audio.hpp`, `platform/audio/NullAudioBackend.{hpp,cpp}`                                    | `IAudioBackend`, 세대 핸들, Null 구현                                                                                                                               | SandboxPlatform             |
| `platform/common/Platform.hpp`, `FramePacer.cpp`                                                            | `attachConsole`, `windowBackendName`, `PreciseSleeper`, `FramePacer`                                                                                                | SandboxPlatform             |
| `platform/windows/Win32Window.cpp`, `Win32Platform.cpp`, `Win32Common.hpp`                                  | Win32 창·메시지 처리·DPI·Raw Input·IME·클립보드, 콘솔·고해상도 타이머 (WIN32 전용)                                                                                  | SandboxPlatform             |
| `platform/windows/Win32KeyMap.{hpp,cpp}`                                                                    | Win32 스캔 코드 → `Key` 표 (windows.h 없음 — 모든 OS 에서 컴파일·테스트)                                                                                            | SandboxPlatform             |
| `platform/stub/StubPlatform.cpp`                                                                            | 창이 아직 없는 OS: `createWindow` = Unsupported (Linux Phase 13, macOS Phase 14)                                                                                    | SandboxPlatform             |
| `render/CMakeLists.txt`                                                                                     | SandboxRender — OS 별 백엔드 선택 (WIN32: `dx12/`, 그 밖: `stub/`), stb 구현 OBJECT, 내장 셰이더(`sbx_add_shader`)                                                  | SandboxRender               |
| `render/rhi/RhiTypes.{hpp,cpp}`                                                                             | 핸들(버퍼·텍스처·샘플러·셰이더·파이프라인·바인드 그룹), 열거, Desc·Caps·Stats, 포맷 표, 파이프라인·바인딩 Desc, `layoutFromReflection`                              | SandboxRender               |
| `render/rhi/RenderDevice.hpp`                                                                               | `IRenderDevice`·`ICommandList`·`ICommandQueue`·`ISwapChain`, `createRenderDevice` (ADR-0006·0018)                                                                   | SandboxRender               |
| `render/rhi/HandlePool.hpp`, `DeferredDestruction.hpp`, `UploadRing.{hpp,cpp}`                              | 백엔드 독립 로직: 세대 핸들 풀, 펜스 지연 해제 큐, 업로드 링 구간 관리                                                                                              | SandboxRender               |
| `render/rhi/ShaderTypes.hpp`                                                                                | 셰이더 단계·바인딩 종류·`ShaderReflection`·`ShaderBytecode` (생성 헤더가 쓰는 값 타입, 7B)                                                                          | SandboxRender               |
| `render/rhi/RangeAllocator.hpp`, `PipelineValidation.{hpp,cpp}`                                             | 디스크립터 구간 할당기, 레이아웃·파이프라인·바인드 그룹 검사 (백엔드 독립, 7B)                                                                                      | SandboxRender               |
| `render/dx12/Dx12Common.{hpp,cpp}`                                                                          | `Com<T>`, HRESULT 문장, Format·상태 매핑 (d3d12.h 는 render/dx12 안에서만)                                                                                          | SandboxRender (WIN32)       |
| `render/dx12/Dx12Device.{hpp,cpp}`                                                                          | D3D12 디바이스·어댑터 선택·큐/펜스·리소스·RTV·업로드 링·프레임·Debug Layer·DRED                                                                                     | SandboxRender (WIN32)       |
| `render/dx12/Dx12CommandList.cpp`, `Dx12SwapChain.cpp`                                                      | 커맨드 리스트(배리어·Clear·복사·마커 + 7B 파이프라인·바인드 그룹·push·정점/인덱스·draw), 플립 모델 스왑체인                                                         | SandboxRender (WIN32)       |
| `render/dx12/Dx12Pipeline.cpp`                                                                              | 셰이더·샘플러·shader-visible 힙·바인드 그룹·루트 시그니처 캐시·PSO (7B, ADR-0019)                                                                                   | SandboxRender (WIN32)       |
| `render/stub/StubRenderDevice.cpp`                                                                          | 백엔드가 없는 OS: `createRenderDevice` = Unsupported (Linux Phase 13, macOS Phase 14)                                                                               | SandboxRender               |
| `render/asset/Image.{hpp,cpp}`, `StbImpl.cpp`                                                               | RGBA8 이미지, PNG 읽기·쓰기(stb), 비교·차이 그림                                                                                                                    | SandboxRender               |
| `render/asset/AssetManager.{hpp,cpp}`, `ShelfPacker.hpp`, `AssetId.hpp`                                     | 스프라이트 에셋: Worker 디코드 · 업로드 예산 · Texture2DArray 아틀라스 선반 패킹 · 자리 표시, 경로 정규화 id (8A, ADR-0020)                                         | SandboxRender               |
| `render/asset/MaterialLibrary.{hpp,cpp}`                                                                    | 머티리얼 이름 → 스프라이트 · 색 (`assets/<팩>/materials.json`), 대체 색 (8A)                                                                                        | SandboxRender               |
| `render/renderer/Camera2D.{hpp,cpp}`, `RenderWorld.hpp`                                                     | 2D 카메라(월드 y 위 · 줌 = px/칸), 프레임마다 채우는 그릴 거리 — 지형 · 스프라이트 · 오버레이 · 선 (8A · 8B)                                                        | SandboxRender               |
| `render/renderer/SpriteBatcher.{hpp,cpp}`, `Renderer.{hpp,cpp}`                                             | 컬링 · 정렬 키 · 묶음(순수 로직), RenderWorld → 패스 기록 (8A) + 패스 순서 · GPU 타임스탬프 (8B) + UI 콜백 · kMarkUi (8C)                                           | SandboxRender               |
| `render/renderer/TerrainPass.{hpp,cpp}`, `OverlayPasses.{hpp,cpp}`, `DebugDraw.{hpp,cpp}`                   | 지형(타일 번호 텍스처 + 팔레트, TerrainCache), GridPass · LinePass, 선 목록 (8B, ADR-0022)                                                                          | SandboxRender               |
| `render/imgui/ImGuiRenderer.{hpp,cpp}`                                                                      | ImGui 그리기 데이터 → RHI: 1.92 동적 텍스처 요청(만들기 · 갱신 · 해제), 업로드 링 정점/인덱스, scissor 별 drawIndexed (8C, ADR-0023)                                | SandboxRender               |
| `apps/client/FrameRenderer.hpp`, `ClientRenderer.cpp`                                                       | `IFrameRenderer`(Application ↔ 렌더러 경계), ClientRenderer — 월드면 Renderer, 메뉴면 Clear + 삼각형 (7B), AssetManager 소유 (8A), ImGuiRenderer · `info()` (8C)    | SandboxClient               |
| `apps/client/WorldSession.hpp`, `NetworkSession.{hpp,cpp}`                                                  | `IWorldSession`(ready · failure · setView), NetworkSession — 서버 접속 · 보간 · 명령 · 선택 (10B), 관심 · 다시 접속 (11)                                            | SandboxClient               |
| `apps/client/presentation/SpriteExtraction.{hpp,cpp}`                                                       | capture(ClientWorld + renderTick → 그릴 거리 · 지형 · 선택 상세, 10B) · emit, Presentation (8A · 8B)                                                                | SandboxClient               |
| `apps/client/presentation/SelectionOverlay.{hpp,cpp}`                                                       | 스냅숏에서 고르기(점 · 박스, netId) · 선택 외곽선 · 선택한 개체의 디버그 선 · 제목 줄 설명 (8B)                                                                     | SandboxClient               |
| `apps/client/ui/ImGuiLayer.{hpp,cpp}`, `DebugPanels.{hpp,cpp}`                                              | ImGui 플랫폼 쪽(PlatformEvent → ImGuiIO, 가로채기 I1, 커서 · 클립보드 · IME, 폰트), 기본 패널 "시뮬레이션" · "통계" → PanelActions (8C, ADR-0023)                   | SandboxClient               |
| `tests/render/main.cpp`, `RenderTestEnv.hpp`, `test_rhi_*.cpp`, `test_sprites.cpp`, `test_world_passes.cpp` | `sbx_render_tests` — 공유 디바이스, 기준 이미지 비교, 7A · 7B · 8A 케이스, 8B 지형 · 오버레이 · 타임스탬프                                                          | sbx_render_tests (WIN32)    |
| `tests/render/test_imgui.cpp`                                                                               | 8C ImGuiRenderer — 기준 이미지 imgui_basic, 텍스처 만들기 · 갱신 · 해제, 화면 밖 창                                                                                 | sbx_render_tests (WIN32)    |
| `tests/render/references/*.png`                                                                             | GPU 기준 이미지 (7A clear · upload_quadrants · region_copy, 7B 4장, 8A sprite · batch_1k, 8B terrain · overlay, 8C imgui_basic)                                     | —                           |
| `tools/wine/`                                                                                               | Wine(vkd3d)+lavapipe 실행 래퍼 `run.sh`(`WINE` 로 Wine 11 지정), 시험용 Vulkan 레이어 (ADR-0018), Windows dxc.exe 래퍼 `dxc.sh` (ADR-0019) — 빌드에 들어가지 않는다 | —                           |
| `shaders/*.hlsl`, `common/Common.hlsli`                                                                     | HLSL 원본 — basic_color · basic_texture (7B), sprite (8A), terrain · grid · lines (8B), imgui (8C), 공통 함수                                                       | SandboxRender (빌드 단계)   |
| `assets/<팩>/materials.json`, `assets/ecosystem/*.png`                                                      | 표현 에셋 (서버 · contentHash 와 무관 — 06 7.1): 머티리얼 표, 자리 표시 스프라이트 그림 (8A)                                                                        | — (SandboxClient 가 읽는다) |
| `tools/shader/sbx_shader_gen.py`                                                                            | SPIR-V 리플렉션 + DXIL 서명 검사 → `<Pascal>Shader.{hpp,cpp}` (내장 바이트코드·cbuffer 구조체) + `.reflect.json` (7B)                                               | 빌드 단계                   |
| `external/stb/`                                                                                             | stb_image v2.30 · stb_image_write v1.16                                                                                                                             | sbx_stb                     |
| `external/enet/`                                                                                            | ENet v1.3.18 (C 소스 · include/enet · LICENSE, 수정 없음)                                                                                                           | sbx_enet                    |
| `external/imgui/`                                                                                           | Dear ImGui v1.92.9b docking (imgui*.{h,cpp} · imstb_* · LICENSE, 수정 없음 — ADR-0023)                                                                              | sbx_imgui                   |
| `apps/client/main.cpp`                                                                                      | 클라이언트 진입점 (Windows: GUI 서브시스템 + main)                                                                                                                  | SandboxClient               |
| `apps/client/Application.{hpp,cpp}`                                                                         | 앱 상태기계(`AppState`, 전이 표), 프레임 루프, 제목 줄 입력 모니터, ImGui 프레임 · 패널 액션 (8C)                                                                   | SandboxClient               |
| `apps/client/ClientOptions.{hpp,cpp}`, `DefaultInput.{hpp,cpp}`                                             | 명령줄 파싱(`--world` · `--connect` · `--name` 10B, `--font` · `--no-ui` 8C), 기본 키 바인딩 JSON                                                                   | SandboxClient               |
| `apps/client/SandboxClient.manifest`                                                                        | Per-Monitor DPI v2 매니페스트 (MSVC)                                                                                                                                | SandboxClient               |
| `apps/server/main.cpp`                                                                                      | 서버 진입점 (`--scenario` 헤드리스 실행 · `--world` 네트워크 서버)                                                                                                  | SandboxServer               |
| `apps/server/ServerOptions.{hpp,cpp}`                                                                       | 명령줄 파싱                                                                                                                                                         | SandboxServer (+ Tests)     |
| `tools/sim_check/main.cpp`, `SimCheckOptions.{hpp,cpp}`                                                     | 결정론 하네스                                                                                                                                                       | sbx_sim_check (+ Tests)     |
| `tools/net_probe/main.cpp`, `NetProbeOptions.{hpp,cpp}`                                                     | 서버 접속 확인 도구 — 핸드셰이크 · 명령 · 결과 · 통계 (Phase 9)                                                                                                     | sbx_net_probe (+ Tests)     |
| `tests/golden/{random_walk_1k,world_save_load,eco_lifecycle,ecosystem_small}.json`                          | 골든 해시 (툴체인별, ADR-0012)                                                                                                                                      | 데이터                      |
| `tests/data/saves/v1_sample/`                                                                               | 커밋된 샘플 세이브 (고치지 않는다)                                                                                                                                  | SandboxTests (`persist`)    |
| `content/ecosystem/`                                                                                        | 콘텐츠 팩 \"eco\" (tags · terrain · prefabs · rules · behaviors)                                                                                                    | 데이터                      |
| `tests/main.cpp`, `tests/unit/**`                                                                           | doctest 단위 테스트 (스위트: foundation, core, ecs, persist, content, network, server, tools, platform, render, client, net — net = 복제 수렴 `net_convergence`)    | SandboxTests                |
| `tests/net/net_smoke.cmake`                                                                                 | CTest net_server_probe_smoke — SandboxServer + sbx_net_probe 두 프로세스, 실제 UDP (Phase 9)                                                                        | CTest `net`                 |
| `tests/unit/network/test_interest.cpp`                                                                      | 관심 영역 · 지연 해제 · 늘 보낼 것 · 관심 지형 · 예산 우선순위 · Subscribe (Phase 11)                                                                               | SandboxTests                |
| `tests/unit/editor/test_editor.cpp`, `tests/unit/client/test_editor_session.cpp`                            | 에디터 툴 (가짜 host), 로컬 서버까지 편집 · 앱 숫자 키 (Phase 12A)                                                                                                  | SandboxTests                |
| `tests/unit/ecs/TestComponents.hpp`                                                                         | 테스트 전용 컴포넌트 (`test.*`)                                                                                                                                     | SandboxTests                |
| `tests/property/**`                                                                                         | 속성 테스트 (스위트: property)                                                                                                                                      | SandboxTests                |
| `bench/main.cpp`, `bench/{Ecs,Sim}Bench.cpp`, `bench/BenchUtil.hpp`                                         | `sbx_bench`                                                                                                                                                         | sbx_bench                   |
| `bench/NetBench.cpp`                                                                                        | `net.snapshot` · `net.late_join` — 50k random_walk 복제 비용 · 바이트 · 새 접속 시간 (Phase 11)                                                                     | sbx_bench                   |
| `bench/baselines/*.json`                                                                                    | 머신별 벤치 기준선                                                                                                                                                  | —                           |
| `external/CMakeLists.txt`                                                                                   | `sbx_nlohmann_json`, `sbx_doctest`, `sbx_stb`, `sbx_imgui` 타깃                                                                                                     | —                           |
| `external/nlohmann_json/`                                                                                   | nlohmann/json v3.12.0                                                                                                                                               | —                           |
| `tests/arch/fixtures/**`                                                                                    | include 린터 자체 시험용 위반 파일 (**컴파일 안 함**)                                                                                                               | —                           |
| `tools/check_includes.py`                                                                                   | include 경계 린터                                                                                                                                                   | CTest `arch`                |
| `external/doctest/`                                                                                         | doctest v2.5.0                                                                                                                                                      | —                           |
| `.github/workflows/ci.yml`                                                                                  | CI (Windows MSVC, Linux clang/gcc/asan, macOS)                                                                                                                      | —                           |
| `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`                             | 포맷·린트·줄끝 규칙                                                                                                                                                 | —                           |

빌드 산출물: `build/<preset>/bin/<Config>/SandboxClient(.exe)`, `SandboxServer(.exe)`, `SandboxTests(.exe)`, `sbx_render_tests.exe`(Windows), `sbx_sim_check(.exe)`, `sbx_net_probe(.exe)`, `sbx_bench(.exe)`.

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
| `apps/client/`                                  | 클라이언트 진입점·앱 상태기계·Presentation·NetworkSession                                      | SandboxClient                  |
| `apps/server/`                                  | 서버 진입점                                                                                    | SandboxServer                  |
| `shaders/`                                      | HLSL 원본 (canonical)                                                                          | SandboxRender (빌드 단계)      |
| `content/`                                      | 콘텐츠 팩 (JSON)                                                                               | 런타임 데이터                  |
| `assets/`                                       | 텍스처·폰트·사운드                                                                             | 런타임 데이터                  |
| `tests/`                                        | 단위·속성·시나리오·결정론·네트워크·렌더 테스트, 골든                                           | SandboxTests, sbx_render_tests |
| `bench/`                                        | 벤치 시나리오·머신별 기준선                                                                    | sbx_bench                      |
| `tools/`                                        | `check_includes.py`·`sim_check`·`net_probe`(구현), `sbx_atlas`(계획)                           | 도구                           |
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
  serialization/ JsonVisitor, HashVisitor, BinaryVisitor, BitWriter/BitReader   ← BinaryCodec (Phase 10A). BitStream 은 network/protocol
  persist/      WorldSave(세이브·로드), ChunkFile, Migration              ← Phase 4 구현 (초안의 serialization/SaveGame 자리)
  replay/       ReplayWriter, ReplayReader, WorldHash
  random/       CounterRng, RandomService

network/
  transport/    INetworkTransport, EnetTransport, LoopbackTransport, SimulatedTransport   ← Phase 9 구현
  protocol/     BitStream, Messages(kProtocolVersion · Role), CommandCodec                   ← Phase 9 구현
  client/       ClientSession, ClientWorld, InterpolationClock                               ← Phase 9 · 10A · 10B 구현
  session/      [계획] 초안의 ServerSession · Handshake · Roles 는 ServerHost · Messages 안에 (Phase 9)
  snapshot/     Snapshot, SnapshotBuffer        ← Snapshot 은 protocol/Messages (10A), SnapshotBuffer [계획 10B]
  replication/  ReplicationWriter, ReplicationReader, NetEntityMap   ← Writer 구현 (10A), Reader · NetEntityMap = client/ClientWorld
  interest/     InterestManager
  server/       ServerHost, CommandValidator (Phase 9), LocalServerHost (10B) · PersistenceService, ReplayRecorder [계획]

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
  renderer/     (8A) Camera2D, RenderWorld, SpriteBatcher, Renderer
  imgui/        (8C) ImGuiRenderer — 1.92 동적 텍스처, RHI 위 (ADR-0023)
  shader/       ShaderLibrary  [계획] (7B 는 생성 헤더가 바이트코드·리플렉션을 직접 내놓는다 — ADR-0019)
  asset/        Image(7A: PNG·비교) · AssetManager · ShelfPacker · AssetId · MaterialLibrary (8A) · FontAsset [계획 — 8C 는 ImGuiLayer 가 폰트 파일을 직접]
  generated/    (빌드 산출 <빌드>/generated/render/generated/: <Pascal>Shader.{hpp,cpp} · *.reflect.json) — 저장소에 없음

editor/
  EditorHost.hpp (ICommandSink · IEditorHost), Editor (툴 다섯 · EditPreview), ui/EditorPanel   ← Phase 12A 구현 (ADR-0028)
  panels/ tools/ selection/ commands/(UndoStack) inspector/(ImGuiInspector)   [계획 12B ~ 12D — 지금은 Editor 한 클래스]

apps/client/
  main.cpp, Application, ClientOptions, DefaultInput, SandboxClient.manifest, NetworkSession (10B — LocalServerHost 는 network/server)
  presentation/ InterpolationSystem, ExtractionSystem, AudioExtraction, components/(render.*, client.*, editor.*)
  ui/           (8C) ImGuiLayer (PlatformEvent → ImGuiIO · I1), DebugPanels (시뮬레이션 · 통계 · 네트워크 10B)
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
| `sbx_net_probe`    | `tools/net_probe/main.cpp` | 서버 접속 · 명령 확인 (Phase 9)          |
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
| 복제 (무엇을 보내나)        | `network/replication/ReplicationWriter.cpp` → `network/client/ClientWorld.cpp` + [08](08-NETWORK.md) 6장        |
| 스프라이트가 화면에 가는 길 | `apps/client/presentation/SpriteExtraction.cpp` → `render/renderer/SpriteBatcher.cpp` · `Renderer.cpp`          |
| 화면 패널 (ImGui)           | `apps/client/ui/DebugPanels.cpp` → `Application.cpp` (`applyPanelActions`) · `render/imgui/ImGuiRenderer.cpp`   |
| D3D12 디바이스 생성         | `render/dx12/D3D12Device.cpp`                                                                                   |
| 창 메시지 처리              | `platform/windows/Win32Window.cpp`                                                                              |
| 콘텐츠 검증 규칙            | `core/content/Validator.cpp` + [11](11-CONTENT-SCHEMA.md) 8장                                                   |
