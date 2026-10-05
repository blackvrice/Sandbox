# Sandbox Simulation Maker

C++23 기반의 자체 **Sandbox Simulation Engine / Maker**입니다.

사용자가 World를 만들고 → Terrain을 칠하고 → Entity를 배치하고 → Component·Behavior·Rule을
정의하고 → Simulation을 실행·관찰·수정·저장하고 → 여러 사람이 같은 World에 접속해 함께 편집합니다.
Ecosystem, Colony, City, Traffic, Factory, Battle 같은 시뮬레이션을 **특정 장르 규칙에 묶이지 않고**
만들 수 있는 것이 목표입니다.

> **현재 상태: Phase 1 완료 (저장소 골격).** 빌드 시스템, Foundation 최소판, 헤드리스 서버 골격, 테스트·경계 검사가 있습니다.
> 시뮬레이션·렌더러·네트워크는 아직 없습니다. 구현되지 않은 것은 문서마다 `[계획]`으로 표시합니다. 진행 상황은 [docs/16-ROADMAP.md](docs/16-ROADMAP.md).

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

| 알고 싶은 것 | 문서 |
|---|---|
| 무엇을 왜 만드나 | [00-OVERVIEW](docs/00-OVERVIEW.md) |
| 전체 구조와 의존성 | [01-ARCHITECTURE](docs/01-ARCHITECTURE.md) |
| 다음에 할 일 | [16-ROADMAP](docs/16-ROADMAP.md) |
| 빌드 방법 | [15-BUILD](docs/15-BUILD.md) |
| 원본 설계서 (Phase 0 입력) | [design/SANDBOX_ARCHITECTURE](docs/design/SANDBOX_ARCHITECTURE.md) |

## 빌드

Windows (x64 Native Tools Command Prompt for VS 또는 CLion Visual Studio 툴체인):

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
build\windows-msvc\bin\Debug\SandboxServer.exe --version
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
