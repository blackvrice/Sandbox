# Sandbox Simulation Maker

C++23 기반의 자체 **Sandbox Simulation Engine / Maker**입니다.

사용자가 World를 만들고 → Terrain을 칠하고 → Entity를 배치하고 → Component·Behavior·Rule을
정의하고 → Simulation을 실행·관찰·수정·저장하고 → 여러 사람이 같은 World에 접속해 함께 편집합니다.
Ecosystem, Colony, City, Traffic, Factory, Battle 같은 시뮬레이션을 **특정 장르 규칙에 묶이지 않고**
만들 수 있는 것이 목표입니다.

> **현재 상태: Phase 12 멀티플레이 에디터 완료 (1차 목표선). Phase 12D 세이브 · 리플레이 — "월드" 패널에서 저장(Ctrl+S) ·
> 불러오기 · 리플레이 기록 · 재생을 실행 중인 서버에 (모든 접속자의 월드가 함께 바뀌고, 재생은 해시를 비교하며 멈추고 · 한 call 씩 ·
> 빠르게), 완료 기준 시험(역할 × 명령 표 · 두 클라이언트 동시 편집 · 편집 명령 시나리오의 리플레이 왕복). Phase 12C 역할 · 콘텐츠 — "접속자" 패널에서 역할 바꾸기(전용 서버의 주인은 `--owner-key`), 역할이 막는 툴은 숨고,
> "콘텐츠" 창에서 규칙 · 행동 · Prefab 을 실행 중에 고친다(월드 오버레이 — 모두에게 · 세이브에 남는다). Phase 12B 인스펙터 · 되돌리기 — 선택한 개체의 컴포넌트 값을 보고 고친다(서버 전용 값 포함), 개체 목록, Ctrl+Z · Ctrl+Y. Phase 12A 에디터 툴 — SandboxClient 에서 숫자 키 1 ~ 5 로 선택 · 이동 · 배치 · 지형 칠하기 · 지우기 (명령은 서버로, 싱글플레이 · 원격 같은 길, "편집" 패널). Phase 11 관심 영역 완료 — 클라이언트는 화면 근처(+ 1 청크)만 받고(바이트 ∝ 보이는 개체), 예산은 새 개체 · 가까운 개체 · 오래 미룬 개체 순, 네트워크가 끊기면 세션 토큰으로 다시 접속. Phase 10 복제 완료 — SandboxClient 가 언제나 서버의 복제본을 그린다: 싱글플레이 `--world`(같은 프로세스의 서버) · 접속 `--connect`(SandboxServer), 보간 · 선택 상세 · 네트워크 패널 (10B, `--direct-sim` 삭제). 10A: 클라이언트별 차분 스냅숏(예산 · 손실 복구 · 다시 맞추기). Phase 9 네트워크 기초 (전용 서버 `SandboxServer --world`, ENet UDP · 핸드셰이크 · 명령 · 권한). Phase 8 렌더러 완료 (8C ImGui 패널, 8B 지형 · 격자 · 선택 · 디버그 선 · 패스별 GPU 시간, Phase 8A 스프라이트 렌더러 · `--direct-sim`, Phase 7 D3D12 RHI · 셰이더, Phase 6 Windows 플랫폼, Phase 5 까지 헤드리스 생태계 완료.** 빌드 시스템, Foundation(JobSystem 포함), ECS(Registry·View·ECB·리플렉션·JSON/해시),
> 30 TPS 틱 파이프라인·명령(SimCommand)·공간 색인·난수·WorldHash, 청크 월드·지형 칠하기·세이브/로드(마이그레이션·Opaque),
> 콘텐츠 팩 로더·검증기(Prefab·Tag·Rule·BehaviorGraph)·생명 주기(에너지·성장·번식·사망)·`content/ecosystem` 팩,
> 감지·FSM 행동·Rule 상호작용·A* 경로 Job·조향 이동·충돌, 리플레이 기록·재생, 세 종이 공존하는 생태계 시나리오,
> 결정론 하네스(`sbx_sim_check` — 재현·세이브 왕복·리플레이·골든·Worker 수·콘텐츠 검증·공존), 헤드리스 서버(`--scenario`)와 네트워크 서버(`--world` — ServerHost: Simulation 스레드 + Net IO 스레드, ENet · Loopback · Simulated Transport, 비트스트림 메시지, 핸드셰이크 · 콘텐츠 해시 · 역할별 권한 · 속도 제한, 복제 — ReplicationWriter · ClientWorld · 수렴 테스트),
> Platform 계층(Win32 창 — DPI v2·Raw Input·IME·클립보드, 물리 키 InputSystem·ActionMap, Null 오디오, HeadlessWindow),
> RHI + D3D12 백엔드(디바이스·큐/펜스·스왑체인·Clear·업로드 링·지연 해제·Debug Layer·셰이더·바인드 그룹·루트 시그니처·PSO·draw),
> HLSL 셰이더 빌드(DXC 고정 버전 · SPIR-V 리플렉션 · cbuffer 헤더 생성), `sbx_render_tests` 기준 이미지,
> 스프라이트 Renderer(아틀라스 · 인스턴싱 · Camera2D · 정렬 · 배치)와 AssetManager, 지형(타일 텍스처) · 격자 · 선택 · 디버그 선 패스와
> GPU 타임스탬프, SandboxClient 창(앱 상태기계, `--world` · `--connect` 로 서버 복제본 관찰 — 보간, 카메라 팬 · 줌 · 일시정지 · 속도는 서버 명령,
> 클릭 · 박스 선택), Dear ImGui(1.92 docking, RHI 위 렌더러 · 동적 폰트 텍스처 · 입력 가로채기)와 기본 패널, 테스트·벤치·경계 검사가 있습니다.
> Phase 13A 로 Vulkan 백엔드(volk · 타임라인 세마포어 · dynamic rendering)가 생겼습니다 — Linux 에서 lavapipe 로 D3D12 와 같은
> 기준 이미지를 통과하고, Windows 는 `-DSBX_ENABLE_VULKAN=ON` 으로 `--rhi vulkan` 을 고릅니다.
> Phase 13B 로 Linux X11 창(Xlib · XIM 한글 입력 · XInput2 · 클립보드)이 생겨 SandboxClient 가 Linux 에서 Vulkan 으로 그립니다.
> Phase 13C 로 Wayland 창(libdecor 틀 · xkbcommon · text-input-v3 한글 입력 · 포인터 잠금 · 정수 배율)이 생겼고, 실행 중에
> 창 시스템을 고릅니다 (`--platform auto|x11|wayland`). Linux CI 는 Xvfb · weston headless 로 창 시험까지 돕니다.
> Phase 14A 로 셰이더가 MSL 로도 만들어집니다 (벤더링한 SPIRV-Cross 호스트 도구 · Metal 번호 규칙 — macOS 가 없어도 생성 ·
> 번호를 시험). Phase 14B 로 macOS Cocoa 창(AppKit · 가상 키 코드 표 · 한글 입력기 · CAMetalLayer)을 썼습니다 — macOS 가
> 없는 환경이라 **아직 빌드해 보지 않았습니다** (CI 의 macOS 잡 · Mac 에서 확인). Phase 14C 로 Metal 백엔드(공유 이벤트
> 타임라인 · 내장 MSL 실행 중 컴파일 · 리플렉션 번호 표 · CAMetalLayer 스왑체인 · `--rhi metal`)를 써서 **Phase 14 는 구현
> 완료 · macOS 검증 대기**입니다 (CI 의 macOS 잡 · Mac 의 MANUAL-QA 14B · 14C 가 통과하면 완료). 다음은 Phase 15
> (최적화). 구현되지 않은 것은 문서마다 `[계획]`으로 표시합니다. 진행 상황은 [docs/16-ROADMAP.md](docs/16-ROADMAP.md).

---

## 핵심 기술

```text
Custom ECS (Sparse Set)        Fixed Tick Simulation (30 TPS)     Chunk World + Spatial Hash
Behavior(FSM) / Rule System    Job System · Async A*             Server Authoritative Networking
Delta Snapshot Replication     Interest Management                Multiplayer Editor
Replay / WorldHash             Custom RHI                         Win32 + DirectX 12
Linux + Vulkan                 macOS + Metal                      HLSL → DXIL / SPIR-V / MSL
Headless Dedicated Server
```

## 구조 한눈에

```text
SandboxClient ── Editor · Presentation · Renderer ── RHI ─┬─ D3D12  (Windows)
     │                                                     ├─ Vulkan (Linux)
     │ Command ↑   ↓ Snapshot                              └─ Metal  (macOS)
SandboxServer ── Authoritative SimulationWorld (SandboxCore) ── 창·GPU 없이 실행
```

싱글플레이도 같은 프로세스 안에서 Server를 띄우고 Loopback으로 연결합니다. 코드 경로는 하나입니다.

## 문서

**처음이라면 [docs/README.md](docs/README.md)부터 읽으십시오.** 모든 문서의 지도가 있습니다.
AI 에이전트는 [AGENTS.md](AGENTS.md)를 먼저 따릅니다.

| 알고 싶은 것               | 문서                                                               |
|----------------------------|--------------------------------------------------------------------|
| 무엇을 왜 만드나           | [00-OVERVIEW](docs/00-OVERVIEW.md)                                 |
| 전체 구조와 의존성         | [01-ARCHITECTURE](docs/01-ARCHITECTURE.md)                         |
| 다음에 할 일               | [16-ROADMAP](docs/16-ROADMAP.md)                                   |
| 빌드 방법                  | [15-BUILD](docs/15-BUILD.md)                                       |
| 원본 설계서 (Phase 0 입력) | [design/SANDBOX_ARCHITECTURE](docs/design/SANDBOX_ARCHITECTURE.md) |

## 빌드

Windows (x64 Native Tools Command Prompt for VS 또는 CLion Visual Studio 툴체인):

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
build\windows-msvc\bin\Debug\SandboxServer.exe --version
build\windows-msvc\bin\Debug\SandboxServer.exe --scenario random_walk_1k --ticks 300
build\windows-msvc\bin\Debug\sbx_sim_check.exe --repeat 2               # 결정론(D1) 확인
build\windows-msvc\bin\Debug\sbx_sim_check.exe --scenario world_save_load --save-at 250   # 세이브 왕복(D2) 확인
build\windows-msvc\bin\Debug\sbx_sim_check.exe --record-golden tests\golden\random_walk_1k.json    # 이 툴체인 골든 기록
build\windows-msvc\bin\Debug\sbx_sim_check.exe --record-golden tests\golden\world_save_load.json
build\windows-msvc\bin\Debug\SandboxClient.exe --console                 # D3D12 창 (Clear + 도는 삼각형). 제목 줄이 입력 모니터, Ctrl+Q 종료
build\windows-msvc\bin\Debug\SandboxClient.exe --console --world ecosystem_survival        # 싱글플레이 생태계 (WASD · 휠 · Space · 클릭 선택 · G 격자)
build\windows-msvc\bin\Debug\SandboxServer.exe --world ecosystem_survival --default-role admin   # 서버 (다른 창)
build\windows-msvc\bin\Debug\SandboxClient.exe --console --connect 127.0.0.1:7777 --name 철수  # 서버 접속
ctest --preset windows-msvc-debug -L render                                  # WARP 기준 이미지 테스트
```

Windows 첫 구성은 셰이더 컴파일러 DXC(NuGet, 약 53 MB)를 `.cache\dxc\` 로 한 번 내려받고, 셰이더 코드 생성에 Python 3 을
씁니다. 셰이더 없이 빌드하려면 `-D SBX_BUILD_SHADERS=OFF` ([15-BUILD](docs/15-BUILD.md) 9장).

Linux (Clang 19+ 또는 GCC 13+):

```bash
cmake --preset linux-clang -D CMAKE_CXX_COMPILER=clang++-19
cmake --build --preset linux-clang-debug && ctest --preset linux-clang-debug
```

실행 형태 (Phase 9 · 10 구현 — 옵션 전체는 [15-BUILD](docs/15-BUILD.md) 7장):

```text
SandboxServer --world ecosystem_survival          # 헤드리스 전용 서버 (Phase 9)
SandboxClient --world ecosystem_survival          # 싱글플레이, 같은 프로세스의 서버 (Phase 10B)
SandboxClient --connect 127.0.0.1:7777            # 원격 서버 접속 (Phase 10B)
```

상세는 [docs/15-BUILD.md](docs/15-BUILD.md).

## 기원

이 프로젝트는 [blackvrice/RTS](https://github.com/blackvrice/RTS)를 **기술 참고자료**로 삼아
처음부터 다시 설계했습니다. RTS 코드를 리팩터링하거나 복사하지 않습니다.
가져온 아이디어와 버린 설계는 [design/SANDBOX_ARCHITECTURE](docs/design/SANDBOX_ARCHITECTURE.md) 2~3장에 있습니다.
