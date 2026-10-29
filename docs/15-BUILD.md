# 15. 빌드

> **참조 문서.** 툴체인, CMake 구조, 의존성, 실행 옵션, CI를 설명합니다. 근거: [ADR-0009](adr/0009-msvc-toolchain.md).
> 상태 (2026-10-26): 1~9장 모두 현재 빌드의 설명입니다 (Phase 1 에서 시작, 7장 실행 옵션은 SandboxServer · SandboxClient ·
> sbx_sim_check · sbx (v0.2 프로젝트 · 패키지, v0.3 SDK) · sbx_net_probe · sbx_bench 까지). `[계획]` 표시가 붙은 것만 아직 없습니다.

---

## 1. 툴체인

| 플랫폼  | 컴파일러                                                                      | 필수                                                         | 비고                                                                                   |
|---------|-------------------------------------------------------------------------------|--------------------------------------------------------------|----------------------------------------------------------------------------------------|
| Windows | MSVC (Visual Studio 2022 17.x 이상 또는 Build Tools, 최신 툴셋) 또는 clang-cl | Windows 10/11 SDK, CMake 3.28+, Ninja, Python 3(경계 테스트) | **MinGW 미지원**                                                                       |
| Linux   | **Clang 19+** 또는 GCC 13+                                                    | CMake 3.28+, Ninja, Python 3                                 | Clang 18 + libstdc++ 13 조합은 `std::expected`가 비활성화되어 구성 단계에서 거부됩니다 |
| macOS   | Apple Clang (Xcode 16+)                                                       | Xcode Command Line Tools, CMake, Ninja                       | Apple Silicon, macOS 13+                                                               |

구성 단계가 `std::expected`·`std::format`·`std::source_location`을 실제로 컴파일해 보고, 안 되면 원인 한 줄로 중단합니다 (`cmake/SbxCompilerSettings.cmake`).

Linux 의 Vulkan (Phase 13A): 빌드에는 아무것도 더 필요 없다 (헤더 · volk 벤더링, 로더는 실행 중에). 실행 · 시험에는
`libvulkan1` (로더) 과 드라이버 — GPU 가 없으면 `mesa-vulkan-drivers` 의 lavapipe, `--rhi-debug` 에는 `vulkan-validationlayers`.
내장 셰이더(그리기 시험 · 클라이언트 렌더러)는 `dxc` 가 PATH 에 있을 때만 (Vulkan SDK 또는 배포판 패키지 — 6장).

Linux 의 창 (Phase 13B): `libx11-dev` 가 필요하고 `libxi-dev` 가 있으면 커서 캡처에 XInput2 를 쓴다 (Debian/Ubuntu:
`sudo apt install libx11-dev libxi-dev`). 창 없이(서버 · 도구만) 빌드하려면 `-D SBX_ENABLE_X11=OFF`. 창 시험
(`platform_x11` · `client_x11_world_smoke`)은 `xvfb` 가 있으면 가상 화면에서 돈다. 한글 UI 글꼴은 `fonts-noto-cjk` 또는
`fonts-nanum` 을 찾는다.

Linux 의 Wayland 창 (Phase 13C): `libwayland-dev` · `wayland-protocols` · `libxkbcommon-dev` · `libdecor-0-dev` (Debian/Ubuntu:
`sudo apt install libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev`, 창 틀 플러그인 `libdecor-0-plugin-1-cairo`
또는 `-gtk` — GNOME 은 서버가 틀을 그려 주지 않는다). 확장 프로토콜 C 코드는 빌드 때 `wayland-scanner` 로 만든다 (C 컴파일러
필요). Wayland 없이 빌드하려면 `-D SBX_ENABLE_WAYLAND=OFF` (X11 만). 둘 다 켜면 실행 중에 고른다 (7장 `--platform`).
창 시험(`platform_wayland` · `client_wayland_world_smoke`)은 `weston` 이 있으면 화면 없는 컴포지터(`tools/wayland/run_headless.sh`)에서 돈다.

macOS 의 창 · Metal (Phase 14B · 14C, **구현 완료 · macOS 검증 대기**): 더 필요한 것 없음 — AppKit · QuartzCore · CoreGraphics ·
Metal 은 SDK 에 있다 (Xcode Command Line Tools). Objective-C++ (`platform/macos/CocoaWindow.mm`, `render/metal/*.mm`, ARC).
배포 목표 macOS 13.4 (`CMAKE_OSX_DEPLOYMENT_TARGET`, 루트 CMakeLists 가 project() 앞에서 정한다 — 13.0 이면 Apple libc++ 의
std::format 이 쓰는 to_chars(double) 이 "unavailable" 로 컴파일 오류, 14C 에서 올렸다). GPU 는 Metal 3 (Apple Silicon · 최근
Intel/AMD Mac — 아니면 렌더러 없이 창만). 내장 셰이더 · MSL 은 PATH 의 `dxc` (Vulkan SDK) 가 있을 때만 — 없으면
Metal 은 지우기만 하고 월드를 그리지 않는다 (렌더 시험도 그리기 케이스가 빠진다). 이 코드는 개발 환경에 macOS 가 없어 빌드해
보지 않았다 — 오류가 나면 로그를 보고해 주십시오.

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

| configure 프리셋   | 대상                                                                             |
|--------------------|----------------------------------------------------------------------------------|
| `windows-msvc`     | Windows · MSVC (Developer 환경 필요)                                             |
| `windows-clangcl`  | Windows · clang-cl                                                               |
| `linux-clang`      | Linux · `clang++` (19 이상이어야 함 — 아니면 `-D CMAKE_CXX_COMPILER=clang++-19`) |
| `linux-gcc`        | Linux · GCC (보조)                                                               |
| `linux-clang-asan` | Linux · Clang + AddressSanitizer + UBSan                                         |
| `linux-clang-tsan` | Linux · Clang + ThreadSanitizer                                                  |
| `macos`            | macOS · Apple Clang                                                              |

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

| 옵션                     | 기본        | 의미                                                                               | 상태                  |
|--------------------------|-------------|------------------------------------------------------------------------------------|-----------------------|
| `SBX_BUILD_SERVER`       | ON          | SandboxServer                                                                      | 구현                  |
| `SBX_BUILD_TESTS`        | ON          | SandboxTests + CTest 등록                                                          | 구현                  |
| `SBX_BUILD_TOOLS`        | ON          | 개발 도구 `sbx_sim_check` · `sbx_net_probe` (9) · `sbx` (v0.2)                     | 구현 (Phase 3)        |
| `SBX_WARNINGS_AS_ERRORS` | OFF (CI ON) | 경고 = 에러                                                                        | 구현                  |
| `SBX_BOUNDARY_SELFTEST`  | OFF         | 내부: 경계 검사 실패 경로 시험 (테스트만 켬)                                       | 구현                  |
| `SBX_BUILD_CLIENT`       | ON          | SandboxClient (+ CTest `client_*`, 스위트 client). Platform 은 항상 빌드           | 구현 (Phase 6)        |
| `SBX_BUILD_BENCH`        | OFF         | sbx_bench + CTest `bench_quick` (EnTT 기준선은 `[계획]`)                           | 구현                  |
| `SBX_ENABLE_VULKAN`      | ON(Linux)   | Vulkan 백엔드 (render/vulkan). Windows 는 켜면 D3D12 와 함께 — `--rhi vulkan`      | 구현 (Phase 13A)      |
| `SBX_ENABLE_METAL`       | ON(macOS)   | Metal 백엔드 (render/metal, Objective-C++). macOS 만 — 다른 OS 는 늘 OFF           | 구현 (14C, 검증 대기) |
| `SBX_ENABLE_X11`         | ON(Linux)   | Linux X11 창 (platform/linux — libx11-dev, +libxi-dev). 둘 다 OFF = 창 없음        | 구현 (Phase 13B)      |
| `SBX_ENABLE_WAYLAND`     | ON(Linux)   | Linux Wayland 창 (libwayland · libdecor · xkbcommon). X11 과 함께 = 실행 중 선택   | 구현 (Phase 13C)      |
| `SBX_ENABLE_TRACY`       | OFF         | Tracy 계측                                                                         | `[계획]`              |
| `SBX_BUILD_SHADERS`      | ON(Windows) | HLSL 을 DXC 로 빌드해 SandboxRender 에 내장, 정의 `SBX_HAS_SHADERS`. Python 3 필요 | 구현 (Phase 7B)       |
| `SBX_DXC`                | (빔)        | dxc 경로. 비면 Windows 는 고정 버전을 `.cache/dxc/` 로, Linux 는 PATH 의 dxc       | 구현 (Phase 7B · 13A) |
| `SBX_SHADER_MSL`         | ON(macOS)   | 셰이더를 MSL 로도 내장 (호스트 도구 sbx_msl, 정의 `SBX_HAS_MSL`). 교차 빌드 불가   | 구현 (Phase 14A)      |
| `SBX_SHADER_HOT_RELOAD`  | ON(Debug)   | 런타임 DXC 호출 허용                                                               | `[계획]` Phase 8 이후 |

옵션은 기능이 실제로 생기는 Phase 에 추가합니다. 쓰이지 않는 옵션을 미리 만들지 않습니다.

## 4. 타깃 구조

현재 (2026-10-26 — Phase 7B 의 표에 뒤 Phase · 버전의 타깃을 더해 왔다):

```text
sbx_warnings              INTERFACE  경고 수준. MSVC: /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
sbx_strict_conversions    INTERFACE  -Wconversion -Wsign-conversion (Foundation·Core 만)
sbx_simulation_flags      INTERFACE  /fp:precise · -ffp-contract=off -fno-fast-math  (04-DETERMINISM 4.6)
SandboxFoundation         STATIC     include 루트 = 저장소 루트 + build/<preset>/generated. PUBLIC Threads::Threads (JobSystem, 5B)
sbx_nlohmann_json         INTERFACE  external/nlohmann_json (SYSTEM include, JSON_USE_IMPLICIT_CONVERSIONS=0)
sbx_doctest               INTERFACE  external/doctest (SYSTEM include), DOCTEST_CONFIG_USE_STD_HEADERS (5A, MSVC)
SandboxScript             STATIC     PUBLIC Foundation, PUBLIC sbx_simulation_flags (v0.3 — 컴파일러 · 검증기 · VM · Host 표 · SDK 생성).
                                     Core · Network · Render · Platform · Editor 를 모른다 (sbx_forbid_link · check_includes.py — ADR-0056 9)
SandboxCore               STATIC     PUBLIC Foundation, PUBLIC Script (v0.3), PUBLIC sbx_nlohmann_json, PUBLIC sbx_simulation_flags
SandboxPlatform           STATIC     PUBLIC Foundation, PRIVATE sbx_nlohmann_json (ActionMap). 항상 빌드 (입력·오디오 단위 테스트)
                                     WIN32: platform/windows/* + user32 imm32, UNICODE, _WIN32_WINNT=0x0A00
                                     SBX_ENABLE_X11 (Linux, 13B): platform/linux/* + X11::X11 (+ X11::Xi → SBX_HAS_XINPUT2),
                                     PUBLIC SBX_HAS_X11. SBX_ENABLE_WAYLAND (Linux, 13C): WaylandWindow · WaylandInput +
                                     PkgConfig::SBX_WAYLAND + 생성한 프로토콜 C (cmake/SbxWayland.cmake), PUBLIC SBX_HAS_WAYLAND.
                                     APPLE (14B): OBJCXX + platform/macos (CocoaWindow.mm -fobjc-arc) + AppKit · QuartzCore ·
                                     CoreGraphics, PUBLIC SBX_HAS_COCOA. 그 밖(Linux 에서 둘 다 끔): platform/stub
                                     platform/linux/X11KeyMap 은 순수 표라 모든 OS 에서 컴파일한다 (Win32KeyMap 과 같다)
SandboxRender             STATIC     PUBLIC Platform, PRIVATE sbx_stb. 항상 빌드 (RHI 순수 로직·이미지 비교 단위 테스트). Core 를 모른다
                                     WIN32: render/dx12/* + d3d12 dxgi dxguid, PUBLIC SBX_HAS_D3D12. SBX_ENABLE_VULKAN: render/vulkan/* +
                                     PRIVATE sbx_volk, PUBLIC SBX_HAS_VULKAN (13A). 백엔드는 rhi/RenderDeviceFactory 가 고른다 (stub 은 지웠다)
                                     SBX_ENABLE_METAL (14C): OBJCXX + render/metal/*.mm (-fobjc-arc) + Metal · QuartzCore · Foundation,
                                     PUBLIC SBX_HAS_METAL
                                     SBX_BUILD_SHADERS: sbx_add_shader 가 만든 generated/render/generated/*Shader.cpp 를 소스에 더하고
                                     PUBLIC SBX_HAS_SHADERS=1 (7B, cmake/SbxShaders.cmake). SBX_SHADER_MSL: + PUBLIC SBX_HAS_MSL=1 (14A)
sbx_stb_impl              OBJECT     render/asset/StbImpl.cpp — stb 구현 TU (sbx_warnings 밖)
sbx_vulkan_headers        INTERFACE  external/vulkan-headers (SYSTEM), VK_NO_PROTOTYPES, WIN32: VK_USE_PLATFORM_WIN32_KHR (13A)
sbx_volk                  STATIC     external/volk/volk.c (C, 경고 끔) + ${CMAKE_DL_LIBS}. SBX_ENABLE_VULKAN 일 때만 (13A)
sbx_spirv_cross           STATIC     external/spirv-cross (코어 · GLSL · MSL, 경고 끔). SBX_SHADER_MSL 일 때만 (14A)
sbx_msl                   EXECUTABLE tools/shader/sbx_msl.cpp — 빌드 때 SPIR-V → MSL 호스트 도구. 셰이더 생성이 의존한다 (14A)
SandboxNetwork            STATIC     PUBLIC Core, PRIVATE sbx_enet (Phase 9). Transport · Protocol · ServerHost · ClientSession
SandboxEditor             STATIC     PUBLIC Core Render, PRIVATE sbx_imgui (Phase 12A). 항상 빌드 (툴 단위 테스트). Network 를 모른다
sbx_enet                  STATIC     external/enet (C — 루트가 C 언어를 켠다). WIN32: ws2_32 winmm. 그 밖: 업스트림과 같은 configure 검사
SandboxServer             EXE        Network Core Crash (v0.5 M1-T1 — 충돌 처리기)
SandboxClient             EXE        Editor Network Render Platform Core Crash (SBX_BUILD_CLIENT=ON — 10B: 로컬 · 원격 서버의 복제본). WIN32: GUI 서브시스템 +
                                     /ENTRY:mainCRTStartup (MSVC), SandboxClient.manifest (Per-Monitor DPI v2). 정의 SBX_DEFAULT_CONTENT_DIR ·
                                     SBX_DEFAULT_ASSETS_DIR (저장소의 content/ · assets/). Editor 는 Phase 12
SandboxTests              EXE        Foundation Script Core Network Platform Render Editor (+ ServerOptions.cpp · SimCheckOptions.cpp · NetProbeOptions.cpp · SbxOptions.cpp · apps/client 의 Application ·
                                     ClientOptions · DefaultInput 직접 컴파일), sbx_doctest
sbx_sim_check             EXE        Core   (SBX_BUILD_TOOLS=ON)
sbx_net_probe             EXE        Network Core   (SBX_BUILD_TOOLS=ON, Phase 9 — 서버 접속 확인)
sbx                       EXE        Core   (SBX_BUILD_TOOLS=ON, v0.2 — 게임 프로젝트 도구 new · check · build, v0.3 sdk, ADR-0043). 정의 SBX_DEFAULT_CONTENT_DIR
sbx_bench                 EXE        Core Network Render   (SBX_BUILD_BENCH=ON 일 때만 — 기본 OFF. Network 은 net.* 벤치, Phase 11)
sbx_render_tests          EXE        Render, sbx_doctest — SBX_BUILD_TESTS 와 (WIN32 · SBX_ENABLE_VULKAN · SBX_ENABLE_METAL) (tests/render).
                                     CTest render_tests_warp (Windows, --rhi dx12) · render_tests_lavapipe (Linux, --rhi vulkan — 13A) ·
                                     render_tests_metal (macOS, --rhi metal --skip-unsupported — 14C, Metal 3 가 없으면 건너뜀)

생성 헤더 build/<preset>/generated/foundation/BuildInfo.hpp
  kVersion*, kGitCommit, kCompilerId, kSystemName,
  kToolchainKey = "<CMAKE_SYSTEM_NAME>-<processor>-<CompilerId>-<major>"  (골든 해시 키, ADR-0012)
    processor 는 소문자, AMD64 → x86_64. MSVC 는 major.minor (19.44). 교차 컴파일에서 비어 있으면 호스트 값.
```

`sbx_simulation_flags`는 Core 가 **PUBLIC**으로 전파합니다. ECS·시스템 코드 상당수가 헤더 템플릿이라 Core 를 쓰는
모든 번역 단위(Network, Server, Tests, Client)에 같은 규칙이 걸려야 하기 때문입니다. Render 는 Core 를 링크하지 않으므로 자유롭습니다.

전역 설정: C++23, 확장 끔, `CMAKE_CXX_SCAN_FOR_MODULES OFF`(모듈 미사용 — 켜 두면 clang-scan-deps 가 없는 환경에서 전부 실패),
`compile_commands.json` 생성.

계획 (Phase 6 이후):

```cmake
add_library(SandboxEditor   STATIC …)   # PUBLIC Core Render, PRIVATE imgui  — Phase 12A 에 생겼다 (위 목록)
add_executable(SandboxClient …)         # Editor Network Render Platform Core
if(WIN32) platform/windows + render/dx12   elseif(APPLE) OBJCXX + platform/macos + render/metal   elseif(UNIX) platform/linux + render/vulkan
```

경계 규칙은 아직 없는 타깃까지 루트 `CMakeLists.txt`에 미리 선언되어 있어, 타깃이 생기는 순간부터 검사됩니다
([01-ARCHITECTURE](01-ARCHITECTURE.md) 2.2).

## 5. 외부 의존성 (버전 고정)

| 라이브러리                             | 용도                 | 대상                        | 방식                                                                             |
|----------------------------------------|----------------------|-----------------------------|----------------------------------------------------------------------------------|
| nlohmann/json                          | 콘텐츠·세이브        | Core (PUBLIC — 헤더 템플릿) | vendored — **v3.12.0** (`external/nlohmann_json/`)                               |
| doctest                                | 테스트               | Tests                       | vendored — **v2.5.0** (`external/doctest/`)                                      |
| ENet                                   | Transport            | Network                     | vendored — **v1.3.18** (`external/enet/`, C, ADR-0024)                           |
| zstd                                   | 청크·스냅샷 압축     | Core/Network                | FetchContent (해시 고정)                                                         |
| Dear ImGui (docking)                   | Editor UI            | Editor/Render               | vendored, 백엔드 파일 미사용                                                     |
| stb_image, stb_truetype, stb_rect_pack | 디코드·폰트·아틀라스 | Render                      | vendored                                                                         |
| miniaudio                              | 오디오               | Platform                    | vendored                                                                         |
| D3D12 Memory Allocator                 | D3D12 메모리         | Render/dx12                 | vendored                                                                         |
| volk, Vulkan-Headers, VMA              | Vulkan               | Render/vulkan               | vendored — volk **1.4.304** · Vulkan-Headers **1.4.309** (13A). VMA `[계획]`     |
| DXC                                    | 셰이더 컴파일러      | 빌드 타임                   | NuGet Microsoft.Direct3D.DXC **1.9.2609.5** (SHA256 고정, 구성 때 `.cache/dxc/`) |
| SPIRV-Cross                            | MSL 변환 (Metal)     | 빌드 타임 (sbx_msl)         | vendored — **1.4.304.1** (`external/spirv-cross/`, 14A, ADR-0035)                |
| WinPixEventRuntime                     | PIX 마커             | Render/dx12                 | NuGet 패키지 버전 고정                                                           |
| Tracy                                  | 프로파일러           | 옵션                        | FetchContent                                                                     |
| EnTT                                   | 벤치 기준선만        | sbx_bench                   | FetchContent, `SBX_BUILD_BENCH`                                                  |

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

현재 구현 (Phase 1 + 3):

```text
SandboxServer --help | --version | --log-level <trace|debug|info|warn|error|off>
SandboxServer --scenario <random_walk_1k|random_walk_10k> [--ticks N] [--seed N] [--realtime]
    헤드리스로 시나리오를 돌리고 "tick N hash 0x… entities … avg tick … ms" 한 줄을 출력한다.
    --realtime: 30 Hz × speed 로 페이싱, 3틱 넘게 밀리면 기준점을 다시 잡고 overruns 에 센다 (03-SIMULATION 3장).
    네트워크 없이 시나리오만 — 결정론 · 성능 확인용으로 남긴다.
SandboxServer --world <시나리오|게임 프로젝트|세이브 폴더> [--port 7777] [--bind addr] [--max-clients 16] [--default-role editor]
              [--threads N] [--seed N] [--content-root d] [--snapshot-kbps 256] [--ticks N --exit]  (Phase 9 · 10A)
    네트워크 서버: ServerHost (Simulation 스레드 + Net IO 스레드) + ENet UDP. 시작하면 "listening udp *:<port> world …"
    (--port 0 = 빈 포트), 끝나면 "tick N hash 0x… entities … accepted … rejected … overruns … replication X ms"
    (replication = 스냅숏 만들기 평균, Phase 10A). 세이브 폴더는 world.json 의 팩을 읽는다. 스크립트 시스템이 있으면 그 아래
    시스템마다 "스크립트 <id>: 호출 · 트랩 · 예산 · 건너뜀 · 연료 합 · 최대 (· 꺼짐)" 한 줄 (v0.3 M4-T3 — sbx_sim_check 도 같다).
    (v0.2 M1-T2) 게임 프로젝트 폴더(sandbox.project.json): 프로젝트 검사(18 5.2 — 매니페스트 · 이 엔진 버전 · 팩 · 시작 월드)를
    통과해야 연다 (오류면 문제 목록과 종료 코드 2, 경고는 로그). 팩은 <프로젝트>/content (--content-root 는 쓰지 않는다),
    월드는 entryWorld 세이브 그대로 (틱 · 일시정지 포함), 월드 이름 = 매니페스트 name. 원격 클라이언트는
    --content <프로젝트>/content 로 같은 팩을 읽는다 (ADR-0041).
    --snapshot-kbps: 클라이언트당 복제 예산 KB/s (기본 256, 0 = 제한 없음 — 08 6.3, ADR-0025). 넘는 엔티티는 다음 스냅숏으로.
    0 은 측정용 — 10k 월드의 스냅숏이 1 MB 를 넘어 UDP 받기 버퍼에서 조각을 잃고 클라이언트가 거의 못 받는다 (10B 측정).
    역할: 일시정지 · 한 틱 · 속도는 admin 부터 (10-EDITOR 7장) — 혼자 시험할 때는 --default-role admin.
    (v0.5 M1-T1) 충돌하면 보고 묶음을 남긴다 — <보고 폴더>/<UTC 시각>-SandboxServer/ (report.json · log.txt · Windows crash.dmp),
    stderr 에 "충돌 보고: <폴더>". 보고 폴더 = SBX_REPORTS_DIR, 아니면 %LOCALAPPDATA%/sandbox/reports (Windows) ·
    $XDG_STATE_HOME/sandbox/reports. 소유자 열쇠 · 사용자 폴더 · 사용자 이름 · IP 주소는 거른다 (덤프는 거르지 않는다).
    [--crash-test] (시험용): 일부러 충돌 — --world 면 월드를 연 뒤 (묶음 문맥 game · package · packs, M1-T2), 클라이언트는
    처리기를 단 뒤 (20-CREATOR-ALPHA 7.1). 묶음은 sbx report 로 본다 (콜스택 · 마지막 로그).
    (12C) [--owner-key <k>]: 클라이언트가 같은 --owner-key 로 들어오면 owner — "접속자" 패널에서 다른 사람의 역할을 바꾼다
    (owner = 누구든, admin = admin 아래인 사람을 editor 이하로). 틀린 열쇠는 접속 거절. 평문이다 (LAN · 신뢰하는 환경).
    (12D) [--saves <dir>]: 세이브 · 리플레이 폴더 (기본 saves — 서버 작업 폴더 기준, 게임 프로젝트면 <프로젝트>/worlds — v0.2
    M1-T5, ADR-0044). 클라이언트 "월드" 패널의 저장 · 불러오기 ·
    기록 · 재생이 여기에 <이름>/ 으로 (admin 부터, 목록은 모두 — 09 4.1, ADR-0031). 기록 중에 Ctrl+C 로 끝내면 리플레이를 쓴다.
    (v0.2 M6-T1) --exit 는 --ticks 에 닿거나 게임이 끝나면 (Finished — 자동 재시작 모드 말고) 끝낸다 — Finished 는 틱을 멈춘다.
    끝날 때 게임 세션이 있으면 "게임 결과: 게임 <상태> · <결과> · team · rule · 점수 · tick · hash" 한 줄을 더 쓴다.
    (v0.2 M6-T1) [--script <파일>]: 자동 플레이 스크립트 (13 5.1 — 서버 러너가 정한 단계에 명령). 게임이 끝나거나 maxTicks 면
    결과 한 줄을 쓰고 끝낸다, expect 와 다르면 종료 코드 1. --start-game 과는 같이 쓰지 않는다. 클라이언트도 같은 옵션
    (SandboxClient --world <…> --script <파일> — 헤드리스면 끝날 때 같은 결과 줄을 쓰고 끝, 창이면 끝난 판을 본다).
    데모: SandboxServer --world samples/rabbit_guard --script samples/rabbit_guard/playthrough.json --port 0
    스크립트 데모 (v0.3 M6-T1): SandboxServer --world samples/mana_regen --script samples/mana_regen/playthrough.json --port 0
    패키지 데모 (v0.4 M6-T1): sbx install samples/stash 다음 SandboxServer --world samples/stash --script
    samples/stash/playthrough.json --port 0 (설치하지 않으면 P8 — 설치 폴더 .sandbox 는 저장소 밖)
    (v0.2 M2-T3) [--start-game]: 게임 모드가 있는 프로젝트(매니페스트 gameMode)는 Lobby 로 열려 틱이 오르지 않는다 — admin 이
    클라이언트 "게임" 패널에서 시작하거나, 이 옵션으로 서버가 첫 틱에 시작한다 (헤드리스 · CI: --start-game --ticks N --exit).
    게임 모드가 없는 월드에 주면 종료 코드 2. 재시작(WorldOp Restart · GameMode.restart auto)은 시작 월드를 다시 연다 (ADR-0046).
    Ctrl+C: 접속자에게 Disconnect{ServerShutdown} 을 보내고 끝낸다. 암호화 없음 — LAN · 신뢰하는 환경 전용 (R6).
    Windows 방화벽이 처음 실행 때 UDP 허용을 묻는다 (같은 PC 안 127.0.0.1 은 묻지 않아도 된다).
종료 코드: 0 정상 · 1 실행 오류 · 2 잘못된 인자 (모르는 월드 포함)

sbx new <폴더> --id <gameId> [--name 이름] [--from <시나리오>] [--mode <GameMode id>] [--content-root d] [--seed n]
                                                                                            (v0.2 M1-T4, ADR-0043)
sbx check <폴더 | 파일.sbpkg>  ·  sbx build <폴더> [--out 파일.sbpkg]  ·  sbx templates  ·  sbx sdk [<폴더>]  ·  sbx --version
  (sdk — SandboxScript SDK 선언 sandbox.d.ts 를 쓴다, v0.3 M2-T2. 저장소의 sdk/ 를 갱신: sbx sdk sdk — 19 14장)
  (v0.4 M5-T2, ADR-0077 — 라이선스 요약) build 는 "  팩 n — <라이선스> <수> · …" 한 줄 (라이선스가 없는 팩은 "라이선스 없음"),
  check <파일.sbpkg> 는 팩마다 "  팩 <id> <version> · <license> · <폴더> · digest <앞 16>…" (형식 1 패키지면 요약이 없다고)
sbx digest <팩 폴더> [--cache | --cache-dir <폴더>]   (v0.4 M2-T1, ADR-0071 — 09 10장)
  "팩 <id> <version> — 파일 n · m 바이트" · "digest <64 글자>" (링크 · 경로 · 크기 거절은 종료 코드 2). --cache 는 사용자 캐시에,
  --cache-dir 는 그 폴더에 넣고 "캐시 <폴더> (넣음 | 이미 있음)"
sbx resolve <프로젝트> [--cache-dir <폴더>]   (v0.4 M3-T1 — 09 11장)
  매니페스트 dependencies 에서 출발해 소스를 받아 그래프를 푼다: "의존 그래프: 팩 n · 오류 m" · 팩마다 "  <id> <version> · <license>
  · <소스> · digest <앞 16>… ← <요구자들>" · "  오류: …" (충돌 · 범위 · 순환 — 있으면 종료 코드 1)
sbx new --pack <폴더> --id <팩 id> [--license <SPDX>]   (v0.4 M4-T2, ADR-0076)
  패키지 (팩) 틀 — pack.json (0.1.0 · license — 없으면 LicenseRef-proprietary) · scripts/main.ts (컴포넌트 <팩>.counter · export
  function bump) · scripts/tick.ts (시스템 <팩>.tick) · prefabs/thing.json. 검사를 통과해야 제자리로 (비지 않은 폴더는 종료 2).
  게임에서: sbx add <게임> <팩> --path <폴더>, 스크립트는 import { Counter } from "@<팩>/main"
sbx test <프로젝트>   (v0.4 M4-T2, ADR-0076 — 13 5.1)
  검사 → playthrough.json · tests/*.json 을 같은 프로세스에서 끝까지 · expect. "통과 | 실패 <파일> — <결과>" · "시험: 통과 n ·
  실패 m". 검사 오류 · 실패가 하나라도 있으면 종료 1
cmake --build --preset <구성> --target sbx_release   (v0.5 M1-T4 — 배포 묶음, 20 7.7)
  <빌드>/release/<구성>/ 에 sandbox-<버전>-<플랫폼>.zip (실행 파일 · content · assets · samples · sdk · docs · BUILD.txt) ·
  -symbols.zip (PDB) · SHA256SUMS.txt (sha256sum -c 형식). 실행 파일은 옆의 content/ · assets/ 를 먼저 찾고, 없으면 빌드한
  저장소의 것 (개발 빌드). 서버 로그 "콘텐츠 루트 <폴더>" 가 고른 곳. CI windows 잡이 RelWithDebInfo 로 만들어 아티팩트로 올린다
sbx doctor <프로젝트 | 패키지.sbpkg>   (v0.5 M1-T2 — 20 7.3)
  진단 — "[영역] 수준 문장" 줄마다 "→ 고치는 법": 엔진 · 환경 (지원 매트릭스) · 패키지 (깨짐 · 형식 · 라이선스) · 매니페스트 ·
  엔진 범위 (새 엔진 · 옛 엔진) · 의존 · 설치 (→ sbx install) · 콘텐츠 · 시작 월드 · 도구. 끝 줄 "진단: 오류 n · 경고 m",
  오류가 있으면 종료 1
sbx report [<묶음 | 이름>] [--lines <n>] [--reports-dir <d>]   (v0.5 M1-T2 — 20 7.2)
  충돌 보고 묶음 — 없으면 보고 폴더의 목록 (새것부터), 있으면 요약 · 콜스택 · 마지막 로그 (기본 40 줄) · 파일 · 덤프 알림
sbx install <프로젝트> · update <프로젝트> [<팩>] · add <프로젝트> <팩> [--path <p> | --git <URL> --rev <r>] [--version <범위>]
    · remove <프로젝트> <팩>   (모두 [--cache-dir <폴더>] — v0.4 M3-T3, ADR-0074 · 09 12장)
  install 은 잠금 sandbox.lock 그대로 (캐시의 digest → 없으면 소스가 잠금과 같을 때만, 잠금이 없으면 해결해서 만든다), update 는
  다시 해결 (팩을 주면 그 팩만 바뀔 수 있다), add 는 매니페스트 dependencies 에 더하고 설치 (소스가 없으면 content/ 의 팩), remove 는
  빼고 다시 설치. 설치는 트랜잭션 — 실패하면 매니페스트 · 잠금 · .sandbox/packages 모두 그대로. 출력 "설치: .sandbox/packages 에
  팩 n · 잠금 sandbox.lock 팩 m [· 매니페스트 갱신] [· tsconfig.json paths 갱신]" 과 팩마다 "  <id> <version> · <license> ·
  <소스> [· digest <앞 16>…]". (v0.4 M4-T1, ADR-0075) 만든 그대로의 tsconfig.json 이면 스크립트 팩마다 "@<팩>/*" paths 를 맞춘다.
  실패는 종료 코드 2 (매니페스트 형식 오류는 1)
sbx fetch --git <URL> --rev <태그 · 커밋> [--id <팩>] [--cache-dir <폴더>]   (v0.4 M2-T2, ADR-0072)
  시스템 git 으로 저장소 (뿌리 = 팩) 를 받아 rev 를 꺼내 캐시에 — "팩 …" · "commit <40 글자>" · "digest …" · "캐시 …". git 이 없거나
  받기 · 꺼내기가 실패하면 종료 코드 2 (git 의 말을 붙인다)
    new: 빈 폴더에만 새 게임 프로젝트 (sandbox.project.json + worlds/main). --from 이 없으면 빈 월드(내장 콘텐츠 · 16 × 16 청크 ·
    일시정지), 있으면 그 시나리오의 팩을 content/ 로 복사하고 첫 틱의 월드로. 옆의 <폴더>.sbx-new 에 만들어 검사(오류 · 경고 0)를
    통과해야 제자리로 — 실패하면 아무것도 남기지 않는다. 엔진 범위는 ">=<이 엔진> <1.0.0". (v0.3 M5-T1, ADR-0065) 스크립트
    SDK sdk/sandbox.d.ts · tsconfig.json 도 만든다 (팩의 scripts/*.ts — IDE 자동 완성 · tsc 검사).
    check: 프로젝트 검사 (18 5.2 — 매니페스트 · 엔진 범위 · 팩 · 시작 월드 열기 · SDK 파일 P6). "프로젝트 검사: 오류 N, 경고 M (…)".
    (v0.4 M1-T2) 매니페스트 dependencies 가 있으면 "의존: n" 과 "  <팩> <범위> → <찾은 version> · <license> [· path … | · git …@…]"
    줄 끝에 읽은 폴더 "(content/<팩>)" 또는 "(.sandbox/packages/<팩>)" (v0.4 M3-T2). 범위 밖이면 P7, 설치가 잠금
    sandbox.lock 과 다르면 · 잠금이 매니페스트와 어긋나면 · path · git 의존을 설치하지 않았으면 P8 오류 (18 5.2 — sbx install).
    스크립트가 있으면 "스크립트: 팩 n · 컴포넌트 n · 시스템 n · SDK sdk/sandbox.d.ts 최신|다름|없음" 과 "  시스템 <id> — <파일> ·
    onTick (n 틱마다) · onEvent (…)" (v0.3 M5-T1). 스크립트 진단은 문제 목록 ("content/<팩>/scripts/a.ts:줄:열: 오류 S426 — …").
    (T1 의 sbx_sim_check --validate-project 를 옮겼다.) 종료 코드: 0 · 1 검사 오류 · 2 잘못된 인자 · 만들기 실패
    실행은 SandboxServer --world <폴더> · SandboxClient --world <폴더>.
    --mode (v0.2 M3-T3, ADR-0049): 매니페스트 schemaVersion 2 · gameMode — 템플릿 콘텐츠에 없는 모드면 종료 코드 2 와 있는 모드.
    build (v0.2 M5-T2, ADR-0054): 게임 패키지 하나 (09 9장) — 기본 <폴더>/build/<gameId>-<version>.sbpkg. 검사 오류면 만들지
    않는다. 같은 프로젝트의 두 빌드는 같은 바이트. check <파일.sbpkg> = 파일마다 SHA-256 · digest 확인 (고친 패키지면 종료 코드 1)
    → 풀어서 프로젝트 검사. SandboxServer · SandboxClient --world <파일.sbpkg> 도 같은 검증 뒤 연다 (세이브 = <이름>-saves).
    데모 "토끼 지키기":  sbx new Demo --id studio.demo --from ecosystem_small --mode eco.protect
                         SandboxClient --world Demo  →  "게임" 패널 시작  →  Q 먹이 주기 · E 덫 놓기 (커서 자리)

sbx_net_probe [--connect host:port] [--name n] [--content-root d] [--seconds s] [--timeout s]
              [--pause] [--resume] [--step n] [--speed x] [--create x,y]       (Phase 9 — 화면 없는 접속 확인 도구)
    접속 → "접속 client #1 role … world … tick …" → 명령 결과 "결과 #n <명령> 수락 tick … | 거절 <ErrorCode> — 사유" →
    "서버 tick … · 개체 … · 틱 … ms · TPS · 접속 …" → (Phase 10A) "복제 개체 N · 스냅숏 M (다시 맞춤 R) · 지형 청크 C · 받은 X KB"
    → "probe 끝: 명령 N 수락 a 거절 r". 복제 개체 수는 --seconds 동안 따라온 만큼 (예산이 걸리면 큰 월드는 늦게 다 온다).
    콘텐츠가 다르다고 거절되면 서버가 알려 준 팩을 --content-root 에서 읽어 한 번 다시 접속한다.
    종료 코드: 0 · 1 접속 실패/거절 · 2 잘못된 인자 · 3 거절된 명령 있음

sbx_sim_check --help      결정론 하네스. 옵션은 13-TESTING 4장

sbx_bench [--quick] [--out result.json] [--machine name] [--threads n] [--only <이름 접두사>]   (14-PERFORMANCE 2장)
    기본 구성에는 없다: -DSBX_BUILD_BENCH=ON 으로 구성해야 타깃이 생긴다 (CLion 은 프로필의 CMake options 에).
    예: --only net. → net.snapshot (50k, 클라이언트 1 · 4 · 16, 관심 = 월드 전체 · 화면) · net.late_join (Phase 11).
    --quick 은 10k · 클라이언트 4 까지. 숫자는 Release 로

SandboxClient (Phase 6 — 빈 창 + 앱 상태기계, --help 에 전체 목록)
    [--console] [--headless] [--frames N] [--fps HZ] [--width W] [--height H] [--input settings.json] [--log-input]
    [--log-level L] [--version]
    (Phase 7A) [--no-render] [--rhi-debug] [--rhi-gbv] [--rhi-warp] [--rhi-fl11] [--vsync on|off] [--frames-in-flight 2|3]
    렌더러가 있으면 화면을 천천히 색이 바뀌는 어두운 색으로 지우고, 제목 줄 끝에 "D3D12 60 fps VSync 켬" 이 붙는다.
    (Phase 13A) [--rhi auto|dx12|vulkan] — 빌드에 든 백엔드만 (Windows 는 -D SBX_ENABLE_VULKAN=ON 으로 구성하면 vulkan).
    (Phase 14C) [--rhi metal] — macOS ("window Cocoa, render Metal"). --rhi-warp 는 Metal 에서 경고 후 기본 GPU.
    --version 의 render 는 기본 백엔드 + 빌드에 든 다른 백엔드 ("render D3D12 (+Vulkan)").
    (Phase 13B) Linux: X11 창 + Vulkan ("window X11, render Vulkan"). DISPLAY 가 없으면 창을 만들지 못하고 종료 코드 1 —
    --headless 로.
    (Phase 13C) [--platform auto|x11|wayland] — Linux 창 시스템. auto = WAYLAND_DISPLAY 가 있으면 Wayland, 안 되면 X11
    (Xwayland). 강제한 쪽이 안 되면 종료 코드 1. 로그 "창 Wayland" = 실제로 고른 것, --version 은 빌드에 든 것
    ("window Wayland · X11"). Wayland 창은 libdecor 틀 · 정수 배율(wl_output.scale) · text-input-v3 입력기.
    (Phase 10B) [--world <시나리오|게임 프로젝트|세이브 폴더>] [--connect host:port] [--name <이름>] [--seed N] [--threads N]
                [--content <dir>] [--assets <dir>]       (8A 의 --direct-sim 은 지웠다 — ADR-0026)
    --world: 싱글플레이 — 같은 프로세스의 서버(LocalServerHost: ServerHost + Loopback, 역할 owner)로 시나리오 · 게임 프로젝트
    (v0.2, 서버와 같은 규칙) 또는 세이브를 돌리고 접속한다. 창에서는 서버의 Simulation · Net IO 스레드, --headless 는 프레임 안에서 (틱 수 고정).
    --threads 는 그 Worker 수 (기본 코어 수 - 2 를 1~4, 결과는 같다 — D5).
    --connect: SandboxServer 에 접속 (포트 기본 7777). 콘텐츠가 다르다고 거절되면 서버 팩을 --content 에서 읽어 다시.
    (v0.2 M1-T3) 게임 프로젝트 서버면 "월드" 패널 · --headless 끝 줄(game …)에 게임 이름 · gameId · 버전. 그 팩은
    --content <프로젝트>/content 로 (못 읽으면 실패 문구가 안내한다 — ADR-0042).
    일시정지 · 한 틱 · 속도는 서버 명령 — 원격 서버의 기본 역할 editor 는 거절된다 (서버 --default-role admin).
    (12C) --owner-key <k>: 서버의 --owner-key 와 같으면 owner 로 접속 (--connect 만 — 싱글플레이는 언제나 owner).
    (12D) --saves <dir>: 싱글플레이(--world)의 세이브 · 리플레이 폴더 (기본 saves — 작업 폴더 기준). --connect 는 서버의 --saves.
    (v0.2 M1-T5) 게임 프로젝트면 기본 <프로젝트>/worlds, "월드" 창 · Ctrl+S 의 처음 이름 = 시작 월드 (ADR-0044).
    접속하고 첫 스냅숏을 받을 때까지 Connecting, 거절 · 끊김이면 오류를 남기고 종료 코드 1.
    (Phase 11) 화면에 보이는 영역 + 1 청크만 받는다 (관심 영역 — 줌 아웃해 월드 전체가 보이면 전부). --connect 에서 네트워크가
    끊기면(시간 초과) 같은 세션 토큰으로 2 초마다 다시 접속하고 60 초 넘으면 끝낸다 — 그동안 마지막 화면이 남고 상태 줄에
    "다시 접속 중 N 초". 서버가 Ctrl+C 로 끝났거나 쫓아냈으면 바로 끝낸다.
    제목 줄에 "<월드> tick · 개체 · TPS · 틱 ms (· RTT)" 와 "fps · 월드 · 추출 · 렌더 ms". 끝날 때
    "world <이름> tick N 개체 M 스냅숏 S (다시 맞춤 R) · 받기 · 적용 X ms" + 프레임 평균 (+ 로컬 서버 틱 · 스냅숏 만들기).
    성능을 볼 때는 RelWithDebInfo/Release 로 (Debug 는 틱 · 적용이 5 ~ 10 배 느리다 — 14-PERFORMANCE 7.7 · 7.10).
    월드 조작: WASD/화살표 이동 · 휠 확대(커서 기준) · 가운데 끌기 · Home 맞춤 · Space 일시정지 · . 한 틱 · = / - 속도
    (8B) 왼쪽 클릭 선택 · 왼쪽 끌기 박스 선택 · Shift 더하기/빼기 · Esc 해제 · G 격자 · V 선택한 개체의 감지 반경 · 경로.
    (12A) 에디터 툴: 1 선택 · 2 이동 · 3 배치 · 4 지형 · 5 지우기 (왼쪽 = 툴, 지형은 오른쪽 = 바탕으로 지우기), Delete = 선택
    지우기. 패널 "편집"(오른쪽 아래): Prefab 목록 · 머티리얼 · 브러시 · 격자 맞춤. 제목 줄에 "툴 …" (10-EDITOR 3장, ADR-0028)
    (12B) Ctrl+Z 되돌리기 · Ctrl+Y 다시 하기 (편집 패널의 단추에 무엇을), F 선택으로 카메라. 패널 "인스펙터"(선택한 개체의 컴포넌트
    값 — 바꾸면 서버로, "(서버)" = 서버 전용 값) · "목록"(관심 영역 안의 개체, 거르기, 두 번 클릭 = 카메라) (10-EDITOR 4 · 6.2장)
    (12C) 패널 "접속자"(접속자 · 역할, owner · admin 은 역할 콤보) · "콘텐츠" 창("편집" 패널의 "콘텐츠 창" 단추 — 규칙 · 행동 ·
    Prefab 의 JSON 을 고쳐 "검사" · "적용", 모두에게 · 세이브에 남는다). 역할이 막는 툴은 숨는다 (10-EDITOR 7 · 8장, ADR-0030)
    (12D) 패널 "월드"(처음엔 접혀 있다 — 제목 줄 "월드 — 보통"을 눌러 펼친다): 이름 · 저장 · 기록 시작/끝 · 새로고침, 서버의
    세이브 · 리플레이 목록 → 불러오기 · 재생, 기록 · 재생 상태 (call · 해시 일치). Ctrl+S = 그 이름으로 저장. 불러오기 · 재생은 모든 접속자의 월드를 바꾸고 되돌리기 기록을 비운다. 재생
    중에는 편집이 거절되고 Space · . · = / - 가 재생을 멈추고 · 한 call · 배율 — 끝나면 그 월드에서 일시정지 (10-EDITOR 9장)
    제목 줄 끝에 "GPU … ms (지형 · 스프라이트 · 격자 · 선)" — 패스별 GPU 시간 (타임스탬프, ADR-0022)
    (Phase 8C) [--font <ttf|ttc>] [--no-ui]
    ImGui 패널 "시뮬레이션" · "통계" (F1 로 숨김). 폰트: --font → Windows 맑은 고딕 · Linux Noto Sans CJK · 나눔고딕 (13B)
    → 내장 영문 (ADR-0023).
    (10B) 패널 "네트워크": 서버 · 역할 · RTT · 스냅숏(epoch · 다시 맞춤) · 받은 KB/s · 적용 ms · 보간 지연 · 거절된 명령.
    (11) + "관심 영역" (월드 전체 | 청크 x0..x1 × y0..y1) · "다시 접속" (횟수 | 중…).
    Linux · Wine 시험에서 한글: --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc (Wine 은 Z:/usr/…)
    (v0.5 M1-T3) 사용자 설정 폴더 (SBX_CONFIG_DIR · %APPDATA%\sandbox · macOS ~/Library/Application Support/sandbox · XDG —
    07 8장) 의 client.json {"uiScale": 0.5~3, "updateCheck": bool, "updateFeed": "<파일>"} · input.json (키 바인딩 — 기본 위,
    --input 아래). 깨진 파일은 "<이름>.broken-<UTC>" 로 옮기고 "알림" 창 · 로그 "알림: …" 에 알린 뒤 기본값으로 시작한다.
    [--safe-mode]: 두 파일 · UI 배율 · 패키지 캐시 (임시 폴더 sbx-packages-safe — 매번 빈 것) 를 쓰지 않는다.
    [--update-feed <파일 | https://URL>]: 릴리스 피드 (GitHub Releases "List releases" JSON) — 이 클라이언트보다 새 버전이면
    알림만 (받기 · 설치는 직접). URL 은 시스템 curl 로 백그라운드에서 (https 만 · 5 초 — M1-T4, ADR-0084). 예:
    --update-feed https://api.github.com/repos/<소유자>/<저장소>/releases. 피드가 없거나 · 오프라인 · 깨졌으면 조용히 (20 7.5 · 7.6)

sbx_render_tests [--rhi auto|dx12|vulkan] [--warp] [--debug] [--gbv] [--fl11] [--update-references] [--references <dir>]
                 [--out <dir>] [doctest 옵션]
    기준 이미지 tests/render/references/*.png (백엔드 공통). 실패하면 --out 에 <name>.actual.png · <name>.diff.png
    (13A) --rhi: 빌드에 든 백엔드 (auto = 플랫폼 기본). Vulkan 의 --warp = CPU 장치(lavapipe), --gbv = 동기화 검증.
    GPU 가 있는 Linux 에서 lavapipe 로만: VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json (CTest 가 그렇게 한다)
    Windows 는 GUI 서브시스템 실행 파일이라 PowerShell 에서 실행해도 로그가 안 보인다 → --console (부모 콘솔에 붙는다).
    CLion 실행 창·CTest 처럼 출력이 이미 연결된 곳에서는 그대로 보인다.
    창이 없는 빌드(macOS · SBX_ENABLE_X11 · SBX_ENABLE_WAYLAND 둘 다 OFF)에서는 --headless 로만 돈다. 끝날 때 "SandboxClient 끝: frames N state Shutdown".
    (v0.3 M5-T3) 콘텐츠에 client 스크립트 시스템이 있으면 그 앞에 시스템마다 "클라이언트 스크립트 <id>: 호출 · 트랩 · 예산 · 건너뜀 ·
    연료 합 · 최대 · 시간 · 최대 ms" 와 "클라이언트 스크립트 마지막 프레임 N: 글자 n · 줄 m" · 그 줄들 (19 21 · 22장).
    (v0.3 M5-T4) SandboxServer 끝의 "스크립트 <id>: …" 줄에도 " · 시간 <합> ms · 최대 <호출 하나> ms" (서버의 진단 시계 —
    sbx_sim_check 의 끝 줄에는 없다). sbx_net_probe 는 1 초마다 받은 서버 스크립트 표를 "  스크립트 <id>: 창 … | 누적 …" 으로.
    창 안 단축키(수동 QA): F2 글자 입력 · F3 마우스 캡처 · F4 커서 모양 · Ctrl+C/V · Esc · Ctrl+Q (qa/MANUAL-QA Phase 6)
종료 코드: 0 정상 · 1 실행 오류(창 생성·설정 파일 · 서버 접속 실패 · 끊김) · 2 잘못된 인자 (없는 월드 포함)

(Phase 5A) 콘텐츠 루트: SandboxServer --content-root · sbx_sim_check --content-root. 생략하면 빌드 때 컴파일된
저장소의 content/ (SBX_DEFAULT_CONTENT_DIR — 개발용 기본값, 배포 경로 정책은 Phase 9)
```

계획:

```text
SandboxServer … [--autosave 300] [--record-replay] [--metrics-csv path]      (Phase 9 에서 나머지는 구현)

SandboxClient (--world · --connect · --name 은 Phase 10B 구현)
              [--rhi metal] (14) [--rhi-capture N]
```

## 8. CI

[13-TESTING](13-TESTING.md) 8장의 매트릭스. 워크플로: `.github/workflows/ci.yml` — Windows MSVC, Linux clang-19/gcc-13/asan, macOS.
Linux 잡은 (Phase 13C) Vulkan 로더 · lavapipe · Validation 레이어 + X11 · Wayland 개발 파일 + Xvfb · weston 을 설치해 창 시험
(`platform_x11` · `platform_wayland` · `client_*_world_smoke`)까지 화면 없이 돈다. 원격 저장소에 push 하면 실행됩니다.

```text
- Core·Server·Tests 는 Phase 1 부터 Windows/Linux/macOS 세 OS 에서 빌드.
- 아티팩트: 실패 시 sbx_sim_check 진단 로그, 렌더 diff 이미지.
- 캐시: FetchContent 다운로드, DXC 바이너리.
```

## 9. 알려진 함정

```text
- MinGW 로 구성된 기존 CLion 프로필(RTS)을 재사용하지 말 것. 새 프로필을 Visual Studio 툴체인으로.
  MinGW 로 구성하면 CMake 가 경고를 낸다 (막지는 않는다). Phase 2 까지의 코드는 MinGW GCC 13·15 에서도 빌드된다.
- IDE(CLion)는 Ninja 를 PATH 가 아니라 CMAKE_MAKE_PROGRAM 으로 넘긴다. CMake 를 중첩 호출하는 테스트
  (arch_link_boundary_selftest)는 CMAKE_MAKE_PROGRAM·CMAKE_TOOLCHAIN_FILE 을 그대로 전달해야 한다 (Phase 3 에서 수정).
  같은 이유로 CLion 은 vcvars 환경(LIB·INCLUDE·LIBPATH)을 CTest 에 주지 않아 중첩 구성의 링크가 LNK1104(kernel32.lib)로
  실패했다 → MSVC 면 구성 시점의 값을 테스트 환경으로 넘기고, 그래도 컴파일러를 못 쓰면 SKIP (Phase 5A).
- Windows 콘솔·CTest 의 기본 인코딩은 cp949 다. Python 도구는 출력 인코딩을 UTF-8 로 직접 고정한다 (check_includes.py,
  테스트는 -X utf8 도 붙인다). C++ 도구(sbx_sim_check · SandboxServer)는 시작할 때 콘솔 출력 코드 페이지를 UTF-8 로 바꾼다 (foundation/io/Console, 5B 후속). CTest 로그 파일은 여전히 UTF-8 바이트다.
- doctest 는 MSVC 에서 std 앞선언 때문에 std::string_view 를 문자열화하지 못한다 → sbx_doctest 가
  DOCTEST_CONFIG_USE_STD_HEADERS 를 켠다 (Phase 5A, 사용자 PC 에서 발견).
- Python 3 이 없으면 arch_include_lint 가 등록되지 않는다 (구성 경고). Windows 에서는 python.org 설치본을 PATH 에 두면 잡힌다.
- 골든 해시는 툴체인별이다. 새 컴파일러에서 det_*_golden 이 SKIP 이면 tests/golden/ 의 각 파일에 대해
  sbx_sim_check --record-golden tests/golden/<이름>.json 으로 기록해 커밋한다 (ADR-0012).
  simVersion 이 오른 커밋 뒤에는 기존 항목도 다시 기록해야 한다 (Phase 4 에서 1 → 2).
  지금 골든 4개: random_walk_1k · world_save_load · eco_lifecycle · ecosystem_small (kSimVersion 4).
  상대 경로는 현재 폴더 기준이다 — 저장소 루트에서 실행한다 (예: .\cmake-build-debug\bin\sbx_sim_check.exe --record-golden tests\golden\eco_lifecycle.json).
  골든 파일이 없으면 --record-golden 은 --scenario 없이 실행을 거절한다 (기본 시나리오를 엉뚱한 파일에 기록하지 않게).
- tests/data/saves/ 의 청크 파일(*.chunk)은 바이너리다 (.gitattributes). 샘플 세이브는 고치지 않는다 (09 3.5).
- GCC 13 -O2 의 -Warray-bounds 는 인라인 버퍼(SmallVector)에 대한 memmove 를 오진한다. 우회는 SmallVector::relocate 의 명시적 루프.
- Windows 경로 길이: 저장소를 짧은 경로(D:\Game\Sandbox)에 둔다. FetchContent 깊은 경로 주의.
- sbx_simulation_flags 는 Core 가 PUBLIC 으로 전파한다(4장). Render 는 Core 를 모르므로 /fp:fast 허용.
- MSVC 는 /utf-8 이 필수다. 소스에 한국어 주석이 있어 빠지면 C4819 와 잘못된 컴파일이 난다 (sbx_warnings 가 넣는다).
- (Phase 5B) MinGW 교차 빌드는 std::thread 때문에 posix 스레드 모델 컴파일러(x86_64-w64-mingw32-g++-posix)를 쓴다.
- 메모리가 작은 머신(8 GB 이하)에서는 -j 를 코어 수 이하로. nlohmann/json 을 쓰는 번역 단위는 컴파일러 하나가 0.5 GB 안팎을
  쓴다 (클라우드 2코어 컨테이너에서 -j16 이 OOM 으로 실패한 적이 있다).
- Ubuntu 24.04 의 기본 clang++ 는 18 이다. linux-clang 프리셋에는 -D CMAKE_CXX_COMPILER=clang++-19 를 붙인다.
- 열거형 → 문자열 함수를 toString 이라 부르지 않는다 (doctest 가 ADL 로 잡아 컴파일 오류). 12-CODING-STANDARDS 2장.
- 셰이더 산출물을 저장소에 커밋하지 않는다.
- (Phase 7B) Windows 의 첫 구성은 DXC 패키지(약 53 MB)를 내려받는다 — 인터넷이 필요하다. 이후에는 `<저장소>/.cache/dxc/`
  를 프리셋끼리 공유한다. 오프라인이면 -D SBX_DXC=<dxc.exe> (Windows SDK 의 bin\<버전>\x64\dxc.exe 도 된다 — 단 같은 폴더에
  dxil.dll 이 있어야 한다. 없으면 DXIL 이 서명되지 않아 생성기가 빌드를 멈춘다. 서명 없는 DXIL 은 실제 D3D12 가 거부한다).
- (Phase 7B) SBX_BUILD_SHADERS=ON 이면 Python 3 이 필수다 (없으면 구성 오류). 셰이더 없이 빌드하려면 -D SBX_BUILD_SHADERS=OFF
  — 그러면 sbx_render_tests 의 그리기 케이스와 SandboxClient 의 삼각형이 빠진다.
- (Phase 7B) 클라우드에서 DXIL 그리기 시험: Ubuntu 24.04 의 wine 9.0(vkd3d 1.10)은 DXIL 을 컴파일하지 못한다
  ("vkd3d result -4"). WineHQ 의 wine-devel(11.19 에서 확인)을 dpkg -x 로 풀고 tools/wine/run.sh 에 WINE=<풀어 둔 곳>/opt/wine-devel/bin/wine
  을 준다. 새 Wine 은 접두사를 따로 둔다 (WINEPREFIX). 셰이더 컴파일(tools/wine/dxc.sh 로 Windows dxc.exe)은 wine 9 로도 된다.
- (Phase 6) SandboxClient 는 Windows GUI 서브시스템이다. PowerShell 에서 그냥 실행하면 로그가 보이지 않는다 → --console.
  MSVC 는 /ENTRY:mainCRTStartup 으로 main 을 진입점으로 쓴다. Windows CRT 는 파이프로 넘긴 stderr 를 버퍼링해 강제 종료 때
  마지막 로그를 잃었다 → 기본 로그 싱크가 줄마다 flush 한다.
- (Phase 7A) D3D12 Debug Layer(--rhi-debug, sbx_render_tests --debug)는 Windows 선택적 기능 "그래픽 도구" 가 있어야 켜진다.
  없으면 경고만 하고 Debug Layer 없이 돈다 (render_tests_warp 도 통과하지만 Debug Layer 검사는 빠진다).
- (Phase 7A) 클라우드의 D3D12 시험: MinGW 교차 빌드 → tools/wine/run.sh (Wine d3d12 = vkd3d → lavapipe). vkd3d 는 FL 11_1 까지라
  --fl11 / --rhi-fl11 이 필요하고, lavapipe 의 viewportSubPixelBits 를 올리는 시험용 Vulkan 레이어를 함께 쓴다 (ADR-0018).
  Wine 이 보고하는 어댑터 이름(예: "NVIDIA GeForce GTX 470")은 가짜다.
- (Phase 13A) Linux 에는 고정 버전 DXC 를 내려받지 않는다 — PATH 의 dxc(Vulkan SDK · 배포판)가 없으면 SBX_BUILD_SHADERS 가
  OFF 로 구성되고 (구성 메시지) render_tests_lavapipe 는 7A 케이스만 돈다. 다른 dxc 는 -D SBX_DXC=<dxc>. 클라우드에서는
  tools/wine/dxc.sh (Windows dxc.exe 를 Wine 으로) 를 SBX_DXC 로 준다. dxc 버전이 달라도 기준 이미지는 같아야 한다.
- (Phase 14A) MSL 생성 시험은 macOS 가 아니어도 된다: -D SBX_SHADER_MSL=ON (SBX_BUILD_SHADERS 필요) 이면 sbx_msl 을 빌드해
  셰이더마다 .metal 을 만들고 (<빌드>/shaders/<이름>/<이름>.<vs|ps>.metal) unit_render 가 번호를 확인한다. 처음 빌드에
  SPIRV-Cross 컴파일이 약 30 초 더 든다 (클라우드 측정). 교차 빌드(MinGW)에서는 켤 수 없다 — 호스트 도구를 실행한다.
- (Phase 14B) macOS 가 없는 개발 환경의 Objective-C++ 검사: GNUstep 헤더(libgnustep-gui-dev)로 clang -fsyntax-only
  -fobjc-arc -fobjc-runtime=gnustep-2.0 을 돌린다. GNUstep 에 없는 Apple API(CAMetalLayer · CoreGraphics 일부 · 10.12 이후
  이름)는 검사용 머리 파일에 적어 -include 한다 (저장소 밖 — 클라우드 /tmp). gcc 의 objc/runtime.h 는 ARC 에서 오류가 나 고친
  사본을 쓴다. 문법 · C++ 형 · ARC 규칙만 보고 Apple API 이름은 확인하지 못한다 — 실제 확인은 CI 의 macOS 잡 (ADR-0036).
- (Phase 14B) 저장소의 .clang-format 은 Objective-C 를 지원하지 않는다 ("Configuration file(s) do(es) not support
  Objective-C") — .mm 에 clang-format 을 돌리면 빈 출력이 나온다. .mm 은 손으로 같은 모양을 맞춘다. (14C) 같은 설정에
  `Language: ObjC` · `ObjCBlockIndentWidth: 4` 를 붙인 임시 파일을 --style=file:<경로> 로 주면 .mm 도 같은 모양으로 맞출 수 있다.
- (Phase 14C) Metal 백엔드의 문법 검사도 14B 와 같은 방법 — 검사용 머리 파일에 쓰는 Metal 선언(프로토콜 · 서술자 · 열거)을
  Apple SDK 의 모양대로 적고 -fblocks (완료 처리기 블록, GNUstep 의 objc/blocks_runtime.h 는 선언만 둔 사본)를 더한다.
  MTL_DEBUG_LAYER 는 오류에서 프로세스를 멈춘다 (기본 assert) — 로그만 원하면 실행 전에 MTL_DEBUG_LAYER_ERROR_MODE=nslog.
- (Phase 13A) Linux 파일 시스템은 대소문자를 가린다. 에셋 경로는 소문자로 정규화돼 열리므로 (AssetId) 에셋 파일 이름은
  소문자로 둔다.
- (Phase 13A) Vulkan Validation 레이어가 없으면 --rhi-debug · --debug 는 경고만 하고 계속한다 (Linux: vulkan-validationlayers,
  Windows: Vulkan SDK). Wine 의 winevulkan 은 Windows 쪽 레이어를 보지 않는다 — Validation 은 Linux 빌드에서 본다.
- (Phase 13A) 클라우드의 Windows Vulkan 시험: MinGW 를 -D SBX_ENABLE_VULKAN=ON 으로 구성하고 tools/wine/run.sh 로
  sbx_render_tests.exe --rhi vulkan · SandboxClient.exe --rhi vulkan (Wine 의 winevulkan → 호스트 lavapipe — vkd3d 가 없어도 된다).
- (Phase 13C) Wayland 표면은 버퍼를 붙여 커밋해야 지도에 오른다 — 그 전에는 창이 안 보이고 입력도 없다. 그래서
  --no-render 에서도 wl_shm 어두운 버퍼를 붙인다. libdecor 장식(제목 줄 · 테두리)도 우리 wl_pointer 로 이벤트가 온다 —
  우리 표면의 것만 낸다 (안 그러면 닫기 단추를 누른 것이 게임 클릭으로 샌다).
- (Phase 13C) Wayland 는 다른 클라이언트에 입력을 보낼 수 없다 (XSendEvent · XTEST 같은 것이 없다). 클라우드의 사람 손 시험 =
  Xvfb 위에 weston --backend=x11-backend.so 를 띄우고 xdotool 로 weston 창에 입력 (weston 이 Wayland 입력으로 옮긴다) ·
  wl-copy · wl-paste (wl-clipboard). wl-copy 는 잠깐 포커스를 가져가므로 창을 누르기 전에 한다.
- (Phase 13C) 클립보드 쓰기(set_selection)는 마지막 입력의 serial 이 필요하다 — 입력이 한 번도 없는 창은 서버가 거절한다.
- (Phase 13C) CMake 의 Wayland 블록은 C 언어를 켠다 (wayland-scanner 가 만든 .c). 프리셋은 C++ 컴파일러만 고르므로 C 는
  시스템 cc 다 — 경고 없이(-w) 컴파일하고 산출물은 C 링크라 섞여도 된다.
- (Phase 6) Win32 코드는 클라우드에서 MinGW 교차 빌드 + Wine(Xvfb) + xdotool 로 실행 시험을 한다. Wine 은 Per-Monitor DPI 를
  지원하지 않아 "DPI 를 켜지 못했습니다" 경고가 나온다 — 실제 Windows 에서는 나오지 않아야 한다.
```
