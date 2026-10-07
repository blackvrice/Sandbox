# Sandbox Simulation Maker

C++23 기반의 자체 **Sandbox Simulation Engine / Maker**입니다.

사용자가 World를 만들고 → Terrain을 칠하고 → Entity를 배치하고 → Component·Behavior·Rule을
정의하고 → Simulation을 실행·관찰·수정·저장하고 → 여러 사람이 같은 World에 접속해 함께 편집합니다.
Ecosystem, Colony, City, Traffic, Factory, Battle 같은 시뮬레이션을 **특정 장르 규칙에 묶이지 않고**
만들 수 있는 것이 목표입니다.

> **현재 상태: Phase 7A 구현 (D3D12 RHI 골격 — 디바이스 · 스왑체인 · Clear · 프레임 자원 · 기준 이미지 테스트). Phase 6 Windows 플랫폼, Phase 5 까지 헤드리스 생태계 완료.** 빌드 시스템, Foundation(JobSystem 포함), ECS(Registry·View·ECB·리플렉션·JSON/해시),
> 30 TPS 틱 파이프라인·명령(SimCommand)·공간 색인·난수·WorldHash, 청크 월드·지형 칠하기·세이브/로드(마이그레이션·Opaque),
> 콘텐츠 팩 로더·검증기(Prefab·Tag·Rule·BehaviorGraph)·생명 주기(에너지·성장·번식·사망)·`content/ecosystem` 팩,
> 감지·FSM 행동·Rule 상호작용·A* 경로 Job·조향 이동·충돌, 리플레이 기록·재생, 세 종이 공존하는 생태계 시나리오,
> 결정론 하네스(`sbx_sim_check` — 재현·세이브 왕복·리플레이·골든·Worker 수·콘텐츠 검증·공존), 헤드리스 서버(`--scenario`),
> Platform 계층(Win32 창 — DPI v2·Raw Input·IME·클립보드, 물리 키 InputSystem·ActionMap, Null 오디오, HeadlessWindow),
> RHI + D3D12 백엔드(디바이스·큐/펜스·스왑체인·Clear·업로드 링·지연 해제·Debug Layer), `sbx_render_tests` 기준 이미지,
> SandboxClient 창(앱 상태기계, 화면 Clear, 제목 줄 입력 모니터), 테스트·벤치·경계 검사가 있습니다.
> 셰이더·파이프라인(7B)·Renderer(8)·네트워크·에디터는 아직 없습니다. 구현되지 않은 것은 문서마다 `[계획]`으로 표시합니다. 진행 상황은 [docs/16-ROADMAP.md](docs/16-ROADMAP.md).

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
build\windows-msvc\bin\Debug\SandboxClient.exe --console                 # D3D12 창 (7A: Clear). 제목 줄이 입력 모니터, Ctrl+Q 종료
ctest --preset windows-msvc-debug -L render                                  # WARP 기준 이미지 테스트
```

Linux (Clang 19+ 또는 GCC 13+):

```bash
cmake --preset linux-clang -D CMAKE_CXX_COMPILER=clang++-19
cmake --build --preset linux-clang-debug && ctest --preset linux-clang-debug
```

계획된 실행 형태 `[계획]`:

```text
SandboxServer --world ecosystem01                 # 헤드리스 전용 서버 (Phase 9)
SandboxClient                                     # 싱글플레이, 내장 서버 (Phase 10)
SandboxClient --connect 127.0.0.1:7777            # 원격 서버 접속 (Phase 10)
```

상세는 [docs/15-BUILD.md](docs/15-BUILD.md).

## 기원

이 프로젝트는 [blackvrice/RTS](https://github.com/blackvrice/RTS)를 **기술 참고자료**로 삼아
처음부터 다시 설계했습니다. RTS 코드를 리팩터링하거나 복사하지 않습니다.
가져온 아이디어와 버린 설계는 [design/SANDBOX_ARCHITECTURE](docs/design/SANDBOX_ARCHITECTURE.md) 2~3장에 있습니다.
