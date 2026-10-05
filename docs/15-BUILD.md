# 15. 빌드

> **참조 문서.** 툴체인, CMake 구조, 의존성, 실행 옵션, CI를 설명합니다. 근거: [ADR-0009](adr/0009-msvc-toolchain.md).
> 상태: 1~4장·7장 일부·8~9장은 **Phase 1에서 구현됨** (2026-10-05). `[계획]` 표시가 붙은 것만 아직 없습니다.

---

## 1. 툴체인

| 플랫폼 | 컴파일러 | 필수 | 비고 |
|---|---|---|---|
| Windows | MSVC (Visual Studio 2022 17.x 이상 또는 Build Tools, 최신 툴셋) 또는 clang-cl | Windows 10/11 SDK, CMake 3.28+, Ninja, Python 3(경계 테스트) | **MinGW 미지원** |
| Linux | **Clang 19+** 또는 GCC 13+ | CMake 3.28+, Ninja, Python 3 | Clang 18 + libstdc++ 13 조합은 `std::expected`가 비활성화되어 구성 단계에서 거부됩니다 |
| macOS | Apple Clang (Xcode 16+) | Xcode Command Line Tools, CMake, Ninja | Apple Silicon, macOS 13+ |

구성 단계가 `std::expected`·`std::format`·`std::source_location`을 실제로 컴파일해 보고, 안 되면 원인 한 줄로 중단합니다 (`cmake/SbxCompilerSettings.cmake`).

### 1.1 CLion 설정 (Windows)

```text
Settings → Build, Execution, Deployment → Toolchains → + → Visual Studio
  (RTS 에서 쓰던 MinGW 툴체인을 기본값으로 두지 않는다)
Settings → … → CMake → 프로필 목록에서 "Enable profile" 로 CMakePresets.json 의
  windows-msvc - Debug / RelWithDebInfo 를 켠다. 툴체인은 위의 Visual Studio.
```

명령줄은 **x64 Native Tools Command Prompt for VS**(또는 `vcvars64.bat`를 실행한 셸)에서 실행합니다.
Ninja 생성기는 `cl.exe`·Windows SDK 경로를 환경 변수에서 찾기 때문입니다.

## 2. CMake 프리셋

| configure 프리셋 | 대상 |
|---|---|
| `windows-msvc` | Windows · MSVC (Developer 환경 필요) |
| `windows-clangcl` | Windows · clang-cl |
| `linux-clang` | Linux · `clang++` (19 이상이어야 함 — 아니면 `-D CMAKE_CXX_COMPILER=clang++-19`) |
| `linux-gcc` | Linux · GCC (보조) |
| `linux-clang-asan` | Linux · Clang + AddressSanitizer + UBSan |
| `linux-clang-tsan` | Linux · Clang + ThreadSanitizer |
| `macos` | macOS · Apple Clang |

생성기는 **Ninja Multi-Config**, 빌드 폴더는 `build/<프리셋>/`. 빌드·테스트 프리셋은 `<configure>-debug`,
`<configure>-relwithdebinfo`, `<configure>-release`(빌드만)입니다.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

```bash
cmake --preset linux-clang -D CMAKE_CXX_COMPILER=clang++-19
cmake --build --preset linux-clang-debug && ctest --preset linux-clang-debug
```

실행 파일은 `build/<프리셋>/bin/<Config>/`에 모입니다 (예: `build/windows-msvc/bin/Debug/SandboxServer.exe`).

## 3. 옵션

| 옵션 | 기본 | 의미 | 상태 |
|---|---|---|---|
| `SBX_BUILD_SERVER` | ON | SandboxServer | 구현 |
| `SBX_BUILD_TESTS` | ON | SandboxTests + CTest 등록 | 구현 |
| `SBX_WARNINGS_AS_ERRORS` | OFF (CI ON) | 경고 = 에러 | 구현 |
| `SBX_BOUNDARY_SELFTEST` | OFF | 내부: 경계 검사 실패 경로 시험 (테스트만 켬) | 구현 |
| `SBX_BUILD_CLIENT` | ON | Client·Render·Editor·Platform | `[계획]` Phase 6 |
| `SBX_BUILD_BENCH` | OFF | sbx_bench (+ EnTT 기준선) | `[계획]` Phase 2 |
| `SBX_ENABLE_VULKAN_ON_WINDOWS` | OFF | Windows 에서 Vulkan 백엔드도 빌드 | `[계획]` Phase 13 |
| `SBX_ENABLE_TRACY` | OFF | Tracy 계측 | `[계획]` |
| `SBX_SHADER_HOT_RELOAD` | ON(Debug) | 런타임 DXC 호출 허용 | `[계획]` Phase 7 |

옵션은 기능이 실제로 생기는 Phase 에 추가합니다. 쓰이지 않는 옵션을 미리 만들지 않습니다.

## 4. 타깃 구조

현재 (Phase 1):

```text
sbx_warnings              INTERFACE  경고 수준. MSVC: /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
sbx_strict_conversions    INTERFACE  -Wconversion -Wsign-conversion (Foundation·Core 만)
sbx_simulation_flags      INTERFACE  /fp:precise · -ffp-contract=off -fno-fast-math  (04-DETERMINISM 4.6)
SandboxFoundation         STATIC     include 루트 = 저장소 루트 + build/<preset>/generated
SandboxCore               STATIC     PUBLIC Foundation, PUBLIC sbx_simulation_flags
SandboxServer             EXE        Core
SandboxTests              EXE        Foundation Core (+ apps/server/ServerOptions.cpp 직접 컴파일), doctest
```

`sbx_simulation_flags`는 Core 가 **PUBLIC**으로 전파합니다. ECS·시스템 코드 상당수가 헤더 템플릿이라 Core 를 쓰는
모든 번역 단위(Network, Server, Tests, Client)에 같은 규칙이 걸려야 하기 때문입니다. Render 는 Core 를 링크하지 않으므로 자유롭습니다.

전역 설정: C++23, 확장 끔, `CMAKE_CXX_SCAN_FOR_MODULES OFF`(모듈 미사용 — 켜 두면 clang-scan-deps 가 없는 환경에서 전부 실패),
`compile_commands.json` 생성.

계획 (Phase 6 이후):

```cmake
add_library(SandboxNetwork  STATIC …)   # PUBLIC Core, PRIVATE enet          Phase 9
add_library(SandboxPlatform STATIC …)   # PUBLIC Foundation                  Phase 6
add_library(SandboxRender   STATIC …)   # PUBLIC Platform                    Phase 7
add_library(SandboxEditor   STATIC …)   # PUBLIC Core Render, PRIVATE imgui  Phase 8/12
add_executable(SandboxClient …)         # Editor Network Render Platform Core
if(WIN32) platform/windows + render/dx12   elseif(APPLE) OBJCXX + platform/macos + render/metal   elseif(UNIX) platform/linux + render/vulkan
```

경계 규칙은 아직 없는 타깃까지 루트 `CMakeLists.txt`에 미리 선언되어 있어, 타깃이 생기는 순간부터 검사됩니다
([01-ARCHITECTURE](01-ARCHITECTURE.md) 2.2).

## 5. 외부 의존성 (버전 고정)

| 라이브러리 | 용도 | 대상 | 방식 |
|---|---|---|---|
| nlohmann/json | 콘텐츠·세이브 | Core | `external/` vendored `[계획]` Phase 2 |
| doctest | 테스트 | Tests | vendored — **v2.5.0** (`external/doctest/`) |
| ENet | Transport | Network | vendored |
| zstd | 청크·스냅샷 압축 | Core/Network | FetchContent (해시 고정) |
| Dear ImGui (docking) | Editor UI | Editor/Render | vendored, 백엔드 파일 미사용 |
| stb_image, stb_truetype, stb_rect_pack | 디코드·폰트·아틀라스 | Render | vendored |
| miniaudio | 오디오 | Platform | vendored |
| D3D12 Memory Allocator | D3D12 메모리 | Render/dx12 | vendored |
| volk, Vulkan-Headers, VMA | Vulkan | Render/vulkan | vendored |
| DXC, SPIRV-Cross | 셰이더 빌드 도구 | 빌드 타임 | 릴리스 바이너리 다운로드(SHA256 고정) / FetchContent |
| WinPixEventRuntime | PIX 마커 | Render/dx12 | NuGet 패키지 버전 고정 |
| Tracy | 프로파일러 | 옵션 | FetchContent |
| EnTT | 벤치 기준선만 | sbx_bench | FetchContent, `SBX_BUILD_BENCH` |

현재 vendored 목록과 버전: [external/README.md](../external/README.md).

```text
규칙: 의존성 추가 = ADR 또는 이 표 갱신 + 라이선스 확인(external/<lib>/LICENSE 보존).
      버전 업그레이드는 단독 커밋.
```

## 6. 셰이더 빌드

```cmake
sbx_add_shader(TARGET SandboxRender SOURCE shaders/sprite.hlsl STAGES vs:VSMain ps:PSMain
               OUTPUT_HEADER render/generated/SpriteShader.hpp)
```

플랫폼별 산출물과 명령은 [06-RENDERING](06-RENDERING.md) 6장. 산출물은 `build/<preset>/shaders/`에 생기고 실행 파일 옆으로 복사됩니다.

## 7. 실행

현재 구현 (Phase 1):

```text
SandboxServer --help | --version | --log-level <trace|debug|info|warn|error|off>
종료 코드: 0 정상 · 1 아직 구현되지 않은 동작 · 2 잘못된 인자
```

계획:

```text
SandboxServer --world <name|path> [--content <pack>] [--port 7777] [--tick-rate 30]
              [--max-clients 16] [--default-role editor] [--autosave 300] [--record-replay]
              [--ticks N --exit] [--metrics-csv path] [--log-level info]

SandboxClient                         싱글플레이 (LocalServerHost)
              [--world <name>] [--connect host:port] [--name <displayName>]
              [--rhi dx12|vulkan|metal] [--rhi-debug] [--rhi-gbv] [--rhi-warp] [--rhi-capture N]
              [--frames-in-flight 2|3] [--vsync on|off] [--platform x11|wayland]
              [--console]                    (Windows: 콘솔 창 표시)
```

## 8. CI

[13-TESTING](13-TESTING.md) 8장의 매트릭스. 워크플로: `.github/workflows/ci.yml` — Phase 1 범위(Windows MSVC, Linux clang-19/gcc-13/asan, macOS)로 작성됨.
원격 저장소에 push 하면 첫 실행됩니다 (아직 실행된 적 없음).

```text
- Core·Server·Tests 는 Phase 1 부터 Windows/Linux/macOS 세 OS 에서 빌드.
- 아티팩트: 실패 시 sbx_sim_check 진단 로그, 렌더 diff 이미지.
- 캐시: FetchContent 다운로드, DXC 바이너리.
```

## 9. 알려진 함정

```text
- MinGW 로 구성된 기존 CLion 프로필(RTS)을 재사용하지 말 것. 새 프로필을 Visual Studio 툴체인으로.
- Windows 경로 길이: 저장소를 짧은 경로(D:\Game\Sandbox)에 둔다. FetchContent 깊은 경로 주의.
- sbx_simulation_flags 는 Core 가 PUBLIC 으로 전파한다(4장). Render 는 Core 를 모르므로 /fp:fast 허용.
- MSVC 는 /utf-8 이 필수다. 소스에 한국어 주석이 있어 빠지면 C4819 와 잘못된 컴파일이 난다 (sbx_warnings 가 넣는다).
- Ubuntu 24.04 의 기본 clang++ 는 18 이다. linux-clang 프리셋에는 -D CMAKE_CXX_COMPILER=clang++-19 를 붙인다.
- 열거형 → 문자열 함수를 toString 이라 부르지 않는다 (doctest 가 ADL 로 잡아 컴파일 오류). 12-CODING-STANDARDS 2장.
- 셰이더 산출물을 저장소에 커밋하지 않는다.
```
